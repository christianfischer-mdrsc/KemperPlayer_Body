/**
 * @file kp_storage.c
 * Einstellungen auf der microSD-Karte des Riverdi-Boards (FatFs, FAT32).
 * Ersetzt die schwachen kp_store_*()-Funktionen in kemper_player.c.
 *
 * Dateien:
 *   KPD/SETUP.BIN  globale Einstellungen (kp_global_t)
 *   KPD/RIGS.BIN   ein Datensatz je Rig-Platz (Bank * 5 + Rig), fester Abstand
 *                  REC_SIZE; Datensatz ohne gueltige Kennung/Pruefsumme = leer
 *
 * SD-Zugriff: eigener FatFs-Treiber im Polling-Betrieb statt des von CubeMX
 * erzeugten DMA-Treibers (sd_diskio.c). Polling kopiert ueber die CPU, damit
 * gibt es keine Probleme mit dem D-Cache; Hardware-Flusssteuerung haelt den
 * SD-Takt an, wenn der FIFO voll/leer ist (keine Ueber-/Unterlaeufe, wenn ein
 * anderer Task dazwischenkommt). SD-Takt 240 MHz / (2 * SD_CLKDIV) = 20 MHz.
 *
 * Alles laeuft im LVGL-Task, erst nach dem Start des Schedulers (FatFs nutzt
 * RTOS-Semaphoren). Schreiben wird kurz verzoegert (WRITE_DELAY_MS), damit
 * z. B. der Helligkeitsregler nicht bei jeder Bewegung schreibt.
 */
#include <string.h>
#include "kemper_player.h"
#include "kp_storage.h"
#include "main.h"
#include "sdmmc.h"
#include "fatfs.h"
#include "ui_live.h"           /* LVGL (Include-Pfad wie in der Oberflaeche) */

#define SD_CLKDIV        6U        /* 240,5 MHz / 12 = 20 MHz */
#define SD_TIMEOUT_MS    1000U
#define WRITE_DELAY_MS   800U
#define CARD_POLL_MS     1000U     /* Karte gesteckt/gezogen erkennen */

#define DIR_NAME         "KPD"
#define FILE_RIGS        "KPD/RIGS.BIN"
#define FILE_SETUP       "KPD/SETUP.BIN"

#define REC_MAGIC        0x4BU     /* 'K' */
#define REC_VERSION      1U
#define REC_SIZE         48U
#define SETUP_MAGIC      0x4450U   /* "PD" */
#define SETUP_VERSION    1U

/* Datensatz in RIGS.BIN */
typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint8_t fs[KP_FOOTSWITCHES][2];       /* kind, idx */
    uint8_t fx_btn[KP_FX_BUTTONS];
    char    name[KP_NAME_LEN];            /* nur zur Information */
    uint8_t reserved[REC_SIZE - 2 - 2 * KP_FOOTSWITCHES - KP_FX_BUTTONS - KP_NAME_LEN - 2];
    uint8_t sum;                          /* Summe aller Bytes davor, negiert */
    uint8_t sum2;                         /* Summe ^ 0xA5 */
} rec_t;
_Static_assert(sizeof(rec_t) == REC_SIZE, "rec_t");

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  version;
    uint8_t  level;
    uint8_t  brightness;
    uint8_t  tuner_mute;
    uint16_t tuner_ref;
    uint8_t  fs_default[KP_FOOTSWITCHES][2];
    uint8_t  reserved[16];
    uint8_t  sum;
    uint8_t  sum2;
} setup_t;

extern SD_HandleTypeDef hsd1;
extern char SDPath[4];
extern FATFS SDFatFS;

static enum { ST_NONE = 0, ST_NO_CARD, ST_ERROR, ST_READY } s_state;
static bool        s_linked;      /* eigener Treiber bei FatFs angemeldet */
static lv_timer_t *s_write_timer;

/* Ausstehende Schreibvorgaenge */
static bool           s_rig_pending, s_setup_pending, s_erase_pending;
static uint16_t       s_rig_idx;
static rec_t          s_rig_rec;
static setup_t        s_setup;

/* ------------------------------------------------------------------------
 * FatFs-Treiber (Polling)
 * --------------------------------------------------------------------- */
static volatile DSTATUS s_dstat = STA_NOINIT;

static bool card_present(void)
{
    return HAL_GPIO_ReadPin(SDIO_CD_GPIO_Port, SDIO_CD_Pin) == GPIO_PIN_RESET;
}

static bool wait_ready(void)
{
    uint32_t t = HAL_GetTick();
    while (HAL_GetTick() - t < SD_TIMEOUT_MS) {
        if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER) return true;
    }
    return false;
}

static DSTATUS drv_initialize(BYTE lun)
{
    (void)lun;
    s_dstat = STA_NOINIT;
    if (!card_present()) return STA_NOINIT | STA_NODISK;

    if (hsd1.State != HAL_SD_STATE_RESET) HAL_SD_DeInit(&hsd1);
    hsd1.Instance = SDMMC1;
    hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd1.Init.ClockDiv = SD_CLKDIV;
    if (HAL_SD_Init(&hsd1) != HAL_OK) return STA_NOINIT;
    if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK) return STA_NOINIT;
    /* Polling: der Interrupt wird nicht gebraucht */
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    s_dstat = 0;
    return s_dstat;
}

static DSTATUS drv_status(BYTE lun)
{
    (void)lun;
    if (!card_present()) return STA_NOINIT | STA_NODISK;
    return s_dstat;
}

static DRESULT drv_read(BYTE lun, BYTE * buff, DWORD sector, UINT count)
{
    (void)lun;
    if (s_dstat & STA_NOINIT) return RES_NOTRDY;
    if (!wait_ready()) return RES_ERROR;
    if (HAL_SD_ReadBlocks(&hsd1, buff, sector, count, SD_TIMEOUT_MS) != HAL_OK) return RES_ERROR;
    return wait_ready() ? RES_OK : RES_ERROR;
}

static DRESULT drv_write(BYTE lun, const BYTE * buff, DWORD sector, UINT count)
{
    (void)lun;
    if (s_dstat & STA_NOINIT) return RES_NOTRDY;
    if (!wait_ready()) return RES_ERROR;
    if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buff, sector, count, SD_TIMEOUT_MS) != HAL_OK) return RES_ERROR;
    return wait_ready() ? RES_OK : RES_ERROR;      /* Karte hat fertig programmiert */
}

static DRESULT drv_ioctl(BYTE lun, BYTE cmd, void * buff)
{
    (void)lun;
    HAL_SD_CardInfoTypeDef ci;
    if (s_dstat & STA_NOINIT) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:        return wait_ready() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: HAL_SD_GetCardInfo(&hsd1, &ci); *(DWORD *)buff = ci.LogBlockNbr; return RES_OK;
    case GET_SECTOR_SIZE:  HAL_SD_GetCardInfo(&hsd1, &ci); *(WORD *)buff = (WORD)ci.LogBlockSize; return RES_OK;
    case GET_BLOCK_SIZE:   HAL_SD_GetCardInfo(&hsd1, &ci); *(DWORD *)buff = ci.LogBlockSize / 512U; return RES_OK;
    default:               return RES_PARERR;
    }
}

static const Diskio_drvTypeDef POLL_DRIVER = {
    drv_initialize, drv_status, drv_read,
#if _USE_WRITE == 1
    drv_write,
#endif
#if _USE_IOCTL == 1
    drv_ioctl,
#endif
};

/* ------------------------------------------------------------------------
 * Einbinden
 * --------------------------------------------------------------------- */
static bool mount(void)
{
    if (s_state == ST_READY) return true;
    if (!card_present()) { s_state = ST_NO_CARD; return false; }

    /* Treiber neu verknuepfen (beim ersten Mal den DMA-Treiber aus
     * MX_FATFS_Init() ersetzen); setzt auch "initialisiert" zurueck */
    FATFS_UnLinkDriver(SDPath);
    s_linked = FATFS_LinkDriver(&POLL_DRIVER, SDPath) == 0;
    if (!s_linked || f_mount(&SDFatFS, SDPath, 1) != FR_OK) {
        s_state = ST_ERROR;
        return false;
    }
    FRESULT r = f_mkdir(DIR_NAME);
    if (r != FR_OK && r != FR_EXIST) { s_state = ST_ERROR; return false; }
    s_state = ST_READY;
    return true;
}

static void unmount(bool error)
{
    if (s_state == ST_READY) f_mount(NULL, SDPath, 0);
    s_state = error ? ST_ERROR : ST_NO_CARD;
}

static void checksum(uint8_t * d, uint32_t n, uint8_t * sum, uint8_t * sum2)
{
    uint8_t c = 0;
    for (uint32_t i = 0; i < n; i++) c = (uint8_t)(c + d[i]);
    *sum = (uint8_t)~c;
    *sum2 = (uint8_t)(c ^ 0xA5U);
}

static bool checksum_ok(uint8_t * d, uint32_t n, uint8_t sum, uint8_t sum2)
{
    uint8_t a, b;
    checksum(d, n, &a, &b);
    return a == sum && b == sum2;
}

/* Datei an Position lesen/schreiben */
static bool file_rw(const char * path, uint32_t pos, void * data, uint32_t len, bool write)
{
    FIL f;
    UINT n = 0;
    if (!mount()) return false;
    FRESULT r = f_open(&f, path, write ? (FA_OPEN_ALWAYS | FA_WRITE | FA_READ) : FA_READ);
    if (r == FR_NO_FILE && !write) return false;
    if (r != FR_OK) { unmount(true); return false; }
    if (!write && f_size(&f) < pos + len) { f_close(&f); return false; }
    r = f_lseek(&f, pos);
    if (r == FR_OK) r = write ? f_write(&f, data, len, &n) : f_read(&f, data, len, &n);
    if (f_close(&f) != FR_OK) r = FR_DISK_ERR;
    if (r != FR_OK || n != len) {
        if (r != FR_OK) unmount(true);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------
 * Verzoegertes Schreiben
 * --------------------------------------------------------------------- */
static void flush(void)
{
    if (s_rig_pending || s_erase_pending) {
        if (s_erase_pending) memset(&s_rig_rec, 0, sizeof(s_rig_rec));
        if (file_rw(FILE_RIGS, (uint32_t)s_rig_idx * REC_SIZE, &s_rig_rec, REC_SIZE, true) || s_state != ST_READY)
            s_rig_pending = s_erase_pending = false;   /* ohne Karte verwerfen */
    }
    if (s_setup_pending) {
        if (file_rw(FILE_SETUP, 0, &s_setup, sizeof(s_setup), true) || s_state != ST_READY)
            s_setup_pending = false;
    }
}

static void write_timer_cb(lv_timer_t * t)
{
    (void)t;
    flush();
    lv_timer_pause(s_write_timer);
}

static void schedule_write(void)
{
    if (!s_write_timer) return;
    lv_timer_reset(s_write_timer);
    lv_timer_resume(s_write_timer);
}

/* Karte gesteckt / gezogen (erster Aufruf: Einstellungen laden) */
static void card_timer_cb(lv_timer_t * t)
{
    static uint8_t retry;
    (void)t;
    if (!card_present()) {
        if (s_state != ST_NO_CARD) {
            unmount(false);
            kp_rx_link_info();                     /* Statusanzeige aktualisieren */
        }
        return;
    }
    if (s_state == ST_READY) return;
    if (s_state == ST_ERROR && ++retry < 10) return;   /* nach Fehler alle 10 s */
    retry = 0;
    if (mount()) kp_storage_attached();            /* Einstellungen (neu) laden */
    kp_rx_link_info();
}

/* ------------------------------------------------------------------------
 * Oeffentlich
 * --------------------------------------------------------------------- */
void kp_storage_start(void)
{
    s_write_timer = lv_timer_create(write_timer_cb, WRITE_DELAY_MS, NULL);
    lv_timer_pause(s_write_timer);
    lv_timer_create(card_timer_cb, CARD_POLL_MS, NULL);
}

/* Ersetzen die schwachen Fassungen in kemper_player.c */
bool kp_store_ok(void) { return s_state == ST_READY; }

const char * kp_store_status(void)
{
    switch (s_state) {
    case ST_READY:   return "SD-Karte bereit, Einstellungen werden gespeichert";
    case ST_NO_CARD: return "Keine SD-Karte - Einstellungen nur bis zum Ausschalten";
    case ST_ERROR:   return "SD-Karte nicht lesbar (FAT32 noetig, max. 32 GB)";
    default:         return "SD-Karte wird gesucht";
    }
}

bool kp_store_rig_load(uint16_t idx, kp_rig_setup_t * out)
{
    rec_t r;
    if (idx >= KP_BANKS_MAX * KP_RIGS_PER_BANK) return false;
    if ((s_rig_pending || s_erase_pending) && s_rig_idx == idx) {
        if (s_erase_pending) return false;
        r = s_rig_rec;                                   /* noch nicht geschrieben */
    } else if (!file_rw(FILE_RIGS, (uint32_t)idx * REC_SIZE, &r, REC_SIZE, false)) {
        return false;
    }
    if (r.magic != REC_MAGIC || r.version != REC_VERSION ||
        !checksum_ok((uint8_t *)&r, REC_SIZE - 2, r.sum, r.sum2)) return false;
    for (int i = 0; i < KP_FOOTSWITCHES; i++) {
        out->fs[i].kind = r.fs[i][0];
        out->fs[i].idx = r.fs[i][1];
    }
    memcpy(out->fx_btn, r.fx_btn, sizeof(out->fx_btn));
    return true;
}

void kp_store_rig_save(uint16_t idx, const kp_rig_setup_t * in, const char * rig_name)
{
    if (idx >= KP_BANKS_MAX * KP_RIGS_PER_BANK) return;
    if ((s_rig_pending || s_erase_pending) && s_rig_idx != idx) flush();   /* anderer Platz: erst den alten */
    memset(&s_rig_rec, 0, sizeof(s_rig_rec));
    s_rig_rec.magic = REC_MAGIC;
    s_rig_rec.version = REC_VERSION;
    for (int i = 0; i < KP_FOOTSWITCHES; i++) {
        s_rig_rec.fs[i][0] = in->fs[i].kind;
        s_rig_rec.fs[i][1] = in->fs[i].idx;
    }
    memcpy(s_rig_rec.fx_btn, in->fx_btn, sizeof(s_rig_rec.fx_btn));
    strncpy(s_rig_rec.name, rig_name ? rig_name : "", KP_NAME_LEN - 1);
    checksum((uint8_t *)&s_rig_rec, REC_SIZE - 2, &s_rig_rec.sum, &s_rig_rec.sum2);
    s_rig_idx = idx;
    s_rig_pending = true;
    s_erase_pending = false;
    schedule_write();
}

void kp_store_rig_erase(uint16_t idx)
{
    if (idx >= KP_BANKS_MAX * KP_RIGS_PER_BANK) return;
    if ((s_rig_pending || s_erase_pending) && s_rig_idx != idx) flush();
    s_rig_idx = idx;
    s_rig_pending = false;
    s_erase_pending = true;
    schedule_write();
}

bool kp_store_global_load(kp_global_t * out)
{
    setup_t st;
    if (s_setup_pending) st = s_setup;
    else if (!file_rw(FILE_SETUP, 0, &st, sizeof(st), false)) return false;
    if (st.magic != SETUP_MAGIC || st.version != SETUP_VERSION ||
        !checksum_ok((uint8_t *)&st, sizeof(st) - 2, st.sum, st.sum2)) return false;
    memset(out, 0, sizeof(*out));
    out->level = st.level;
    out->brightness = st.brightness;
    out->tuner_ref = st.tuner_ref;
    out->tuner_mute = st.tuner_mute != 0;
    for (int i = 0; i < KP_FOOTSWITCHES; i++) {
        out->fs_default[i].kind = st.fs_default[i][0];
        out->fs_default[i].idx = st.fs_default[i][1];
    }
    return true;
}

void kp_store_global_save(const kp_global_t * in)
{
    memset(&s_setup, 0, sizeof(s_setup));
    s_setup.magic = SETUP_MAGIC;
    s_setup.version = SETUP_VERSION;
    s_setup.level = in->level;
    s_setup.brightness = in->brightness;
    s_setup.tuner_ref = in->tuner_ref;
    s_setup.tuner_mute = in->tuner_mute ? 1U : 0U;
    for (int i = 0; i < KP_FOOTSWITCHES; i++) {
        s_setup.fs_default[i][0] = in->fs_default[i].kind;
        s_setup.fs_default[i][1] = in->fs_default[i].idx;
    }
    checksum((uint8_t *)&s_setup, sizeof(s_setup) - 2, &s_setup.sum, &s_setup.sum2);
    s_setup_pending = true;
    schedule_write();
}
