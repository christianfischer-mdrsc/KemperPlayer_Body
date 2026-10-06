/**
 * @file rig_store.c
 * Eingelesene Rigs auf der microSD-Karte (siehe rig_store.h).
 *
 * Dateiaufbau RIGS.BIN (alle Werte little endian, wie im Speicher):
 *   "KPRS"            4 Byte Kennung
 *   Version           2 Byte (FILE_VERSION)
 *   Rig-Groesse       2 Byte (sizeof(kp_rig_t))
 *   Anzahl            2 Byte (KP_DETAIL_RIGS)
 *   reserviert        2 Byte
 *   je Rig: 1 Byte gueltig (0/1) + kp_rig_t
 * Passen Version oder Groesse nicht (andere Firmware), wird die Datei
 * ignoriert; dann einfach neu einlesen.
 */
#include <string.h>
#include <stdio.h>
#include "rig_store.h"
#include "kemper_player.h"
#include "main.h"
#include "sdmmc.h"
#include "fatfs.h"
#include "ff_gen_drv.h"
#include "bsp_driver_sd.h"
#include "cmsis_os.h"

#define FILE_NAME      "RIGS.BIN"
#define FILE_TMP       "RIGS.TMP"
#define FILE_VERSION   1U

#define SD_TIMEOUT_MS  1000U
#define SD_CLOCK_DIV   8U       /* Kartentakt = SDMMC-Takt / (2 * DIV), max. 25 MHz */

/* Wie die Rig-Namen im SRAM4 (D3): die CPU kopiert selbst, kein DMA */
#if defined(__arm__)
  #define RS_D3_BSS __attribute__((section(".d3_bss"), aligned(4)))
#else
  #define RS_D3_BSS
#endif

static FATFS    s_fs  RS_D3_BSS;
static FIL      s_fil RS_D3_BSS;
static kp_rig_t s_tmp RS_D3_BSS;

static bool     s_linked;
static bool     s_card_ok;
static volatile DSTATUS s_stat = STA_NOINIT;
static rig_store_result_t s_last_load = RS_NO_FILE;

/* ------------------------------------------------------------------------
 * SD-Karte im Polling-Betrieb
 * --------------------------------------------------------------------- */
static bool card_present(void)
{
    return BSP_SD_IsDetected() == SD_PRESENT;
}

static bool card_init(void)
{
    if (s_card_ok) return true;
    /* Nach einem Fehler neu einrichten (beim ersten Mal ist hsd1 noch leer:
     * MX_SDMMC1_SD_Init() wird bewusst nicht aufgerufen, weil es ohne
     * Karte in Error_Handler() endet) */
    if (hsd1.Instance != NULL && hsd1.State != HAL_SD_STATE_RESET) (void)HAL_SD_DeInit(&hsd1);
    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    /* Flusssteuerung haelt den Takt an, wenn der FIFO voll/leer ist: so
     * kann ein Taskwechsel mitten im Block keinen Over-/Underrun ausloesen */
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd1.Init.ClockDiv            = SD_CLOCK_DIV;
    if (HAL_SD_Init(&hsd1) != HAL_OK) return false;
    (void)HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B);
    s_card_ok = true;
    return true;
}

static bool card_wait_ready(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > SD_TIMEOUT_MS) return false;
        osDelay(1);
    }
    return true;
}

static DSTATUS sdp_initialize(BYTE lun)
{
    (void)lun;
    s_stat = (card_present() && card_init()) ? 0 : STA_NOINIT;
    return s_stat;
}

static DSTATUS sdp_status(BYTE lun)
{
    (void)lun;
    if (!card_present()) s_stat = STA_NOINIT | STA_NODISK;
    return s_stat;
}

static DRESULT sdp_read(BYTE lun, BYTE * buf, DWORD sector, UINT count)
{
    (void)lun;
    if (s_stat & STA_NOINIT) return RES_NOTRDY;
    if (!card_wait_ready()) return RES_ERROR;
    if (HAL_SD_ReadBlocks(&hsd1, buf, sector, count, SD_TIMEOUT_MS) != HAL_OK) {
        s_card_ok = false;
        return RES_ERROR;
    }
    return card_wait_ready() ? RES_OK : RES_ERROR;
}

#if _USE_WRITE == 1
static DRESULT sdp_write(BYTE lun, const BYTE * buf, DWORD sector, UINT count)
{
    (void)lun;
    if (s_stat & STA_NOINIT) return RES_NOTRDY;
    if (!card_wait_ready()) return RES_ERROR;
    if (HAL_SD_WriteBlocks(&hsd1, buf, sector, count, SD_TIMEOUT_MS) != HAL_OK) {
        s_card_ok = false;
        return RES_ERROR;
    }
    return card_wait_ready() ? RES_OK : RES_ERROR;
}
#endif

#if _USE_IOCTL == 1
static DRESULT sdp_ioctl(BYTE lun, BYTE cmd, void * buf)
{
    (void)lun;
    HAL_SD_CardInfoTypeDef info;
    if (s_stat & STA_NOINIT) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return card_wait_ready() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(DWORD *)buf = info.LogBlockNbr;
        return RES_OK;
    case GET_SECTOR_SIZE:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(WORD *)buf = (WORD)info.LogBlockSize;
        return RES_OK;
    case GET_BLOCK_SIZE:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(DWORD *)buf = info.LogBlockSize / 512U;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
#endif

static const Diskio_drvTypeDef SDP_Driver = {
    sdp_initialize,
    sdp_status,
    sdp_read,
#if _USE_WRITE == 1
    sdp_write,
#endif
#if _USE_IOCTL == 1
    sdp_ioctl,
#endif
};

/* ------------------------------------------------------------------------
 * Dateisystem
 * --------------------------------------------------------------------- */
static rig_store_result_t map(FRESULT fr)
{
    switch (fr) {
    case FR_OK:            return RS_OK;
    case FR_NO_FILE:
    case FR_NO_PATH:       return RS_NO_FILE;
    case FR_NO_FILESYSTEM: return RS_NO_FS;
    case FR_NOT_READY:
    case FR_DISK_ERR:      return RS_CARD_ERROR;
    default:               return RS_IO_ERROR;
    }
}

static rig_store_result_t mount(void)
{
    if (!s_linked) {
        /* MX_FATFS_Init() hat den DMA-Treiber von ST eingetragen: ersetzen */
        (void)FATFS_UnLinkDriver(SDPath);
        if (FATFS_LinkDriver(&SDP_Driver, SDPath) != 0U) return RS_IO_ERROR;
        s_linked = true;
    }
    if (!card_present()) {
        s_card_ok = false;
        s_stat = STA_NOINIT | STA_NODISK;
        return RS_NO_CARD;
    }
    FRESULT fr = f_mount(&s_fs, SDPath, 1);
    if (fr != FR_OK) s_card_ok = false;        /* beim naechsten Mal neu einrichten */
    return map(fr);
}

static void path(char * buf, uint32_t len, const char * name)
{
    snprintf(buf, len, "%s%s", SDPath, name);
}

static bool write_all(const void * d, UINT n)
{
    UINT bw = 0;
    return f_write(&s_fil, d, n, &bw) == FR_OK && bw == n;
}

static bool read_all(void * d, UINT n)
{
    UINT br = 0;
    return f_read(&s_fil, d, n, &br) == FR_OK && br == n;
}

rig_store_result_t rig_store_save(void)
{
    rig_store_result_t r = mount();
    if (r != RS_OK) return r;

    char tmp[16], fin[16];
    path(tmp, sizeof(tmp), FILE_TMP);
    path(fin, sizeof(fin), FILE_NAME);

    FRESULT fr = f_open(&s_fil, tmp, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return map(fr);

    const uint16_t hdr[4] = { FILE_VERSION, (uint16_t)sizeof(kp_rig_t), KP_DETAIL_RIGS, 0 };
    bool ok = write_all("KPRS", 4) && write_all(hdr, sizeof(hdr));
    for (uint16_t i = 0; ok && i < KP_DETAIL_RIGS; i++) {
        const kp_rig_t * rig = kp_store_rig(i);
        const uint8_t valid = rig ? 1U : 0U;
        if (!rig) {
            memset(&s_tmp, 0, sizeof(s_tmp));
            rig = &s_tmp;
        }
        ok = write_all(&valid, 1) && write_all(rig, sizeof(kp_rig_t));
    }
    fr = f_close(&s_fil);
    if (!ok || fr != FR_OK) {
        (void)f_unlink(tmp);
        return RS_IO_ERROR;
    }
    fr = f_unlink(fin);
    if (fr != FR_OK && fr != FR_NO_FILE) return map(fr);
    return map(f_rename(tmp, fin));
}

rig_store_result_t rig_store_load(void)
{
    rig_store_result_t r = mount();
    if (r != RS_OK) return s_last_load = r;

    char fin[16];
    path(fin, sizeof(fin), FILE_NAME);
    FRESULT fr = f_open(&s_fil, fin, FA_READ);
    if (fr != FR_OK) return s_last_load = map(fr);

    char magic[4];
    uint16_t hdr[4];
    r = RS_OK;
    if (!read_all(magic, 4) || !read_all(hdr, sizeof(hdr))) {
        r = RS_IO_ERROR;
    } else if (memcmp(magic, "KPRS", 4) != 0 || hdr[0] != FILE_VERSION ||
               hdr[1] != sizeof(kp_rig_t) || hdr[2] == 0U) {
        r = RS_BAD_FILE;
    } else {
        const uint16_t n = hdr[2] < KP_DETAIL_RIGS ? hdr[2] : KP_DETAIL_RIGS;
        for (uint16_t i = 0; i < n; i++) {
            uint8_t valid;
            if (!read_all(&valid, 1) || !read_all(&s_tmp, sizeof(s_tmp))) {
                r = RS_IO_ERROR;
                break;
            }
            kp_store_put(i, valid ? &s_tmp : NULL);
        }
        kp_store_loaded();
    }
    (void)f_close(&s_fil);
    return s_last_load = r;
}

rig_store_result_t rig_store_last_load(void)
{
    return s_last_load;
}

const char * rig_store_result_text(rig_store_result_t r)
{
    switch (r) {
    case RS_OK:         return "auf SD-Karte gespeichert";
    case RS_NO_CARD:    return "keine SD-Karte eingelegt";
    case RS_CARD_ERROR: return "SD-Karte reagiert nicht";
    case RS_NO_FS:      return "SD-Karte nicht FAT32-formatiert";
    case RS_NO_FILE:    return "noch nichts gespeichert";
    case RS_BAD_FILE:   return "Datei von anderer Firmware, bitte neu einlesen";
    case RS_IO_ERROR:
    default:            return "Fehler beim Lesen/Schreiben der SD-Karte";
    }
}
