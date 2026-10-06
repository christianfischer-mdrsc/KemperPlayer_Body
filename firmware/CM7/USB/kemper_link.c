/**
 * @file kemper_link.c
 * Verbindung zum Kemper Player ueber USB-MIDI (siehe kemper_link.h).
 *
 * Protokoll (Kemper "Profiler MIDI Parameter Documentation" und
 * das bidirektionale Protokoll, wie es auch PySwitch/MIDI Captain nutzen):
 *
 *   Anfrage an den Player:  F0 00 20 33 02 7F <Funktion> 00 ... F7
 *                           (02 = Produkttyp Player, 7F = Omni)
 *   Antwort vom Player:     F0 00 20 33 00 00 <Funktion> 00 ... F7
 *
 *   Beacon      7E 00 40 <Set> <Flags> <Lease>   bidirektionalen Modus an
 *               Flags: Bit0 = Init, Bit1 = SysEx, Bit5 = Tuner-Daten
 *               Lease: Dauer in 2-s-Schritten, danach endet der Modus
 *   Sense       7E 00 7F ...  kommt danach ca. alle 500 ms vom Player
 *   String      43 00 <Seite> <Nr>  ->  03 00 <Seite> <Nr> <ASCII> 00
 *               00/01 Rig-Name, 00/10 Amp-Name, 00/20 Cab-Name
 *   Parameter   41 00 <Seite> <Nr>  ->  01 00 <Seite> <Nr> <MSB> <LSB>
 *               (Wert 14 Bit, 0..16383, Mitte 8192)
 *
 * Verwendete Parameter (Seite/Nr):
 *   04/00 Rig-Tempo (Wert / 64 = BPM)    04/02 Tempo an/aus
 *   04/01 Rig Volume                     04/04 Rig Transpose
 *   09/03 Noise Gate                     09/04 Clean Sens
 *   0A/04 Gain                           0A/06 Definition
 *   0B/04..07 Bass, Middle, Treble, Presence
 *   Effekt-Slots: Seite 32 A, 33 B, 34 C, 35 D, 38 X, 3A MOD, 3C DLY, 3D REV;
 *               Nr 00 = Typ (Kemper-Typnummer), Nr 03 = an/aus
 *
 * Rig-Wechsel:
 *   Der Player meldet jedes geladene Rig mit CC 32 (Index / 128) und
 *   Program Change (Index % 128); Index = Bank * 5 + Rig, 0-basiert.
 *   Laden geht genauso herum (Program Change 1-50 im Handbuch = 0-49 auf
 *   dem Draht). Den MIDI-Kanal uebernimmt das Display aus diesen Meldungen.
 *
 * Einlesen aller Rigs ("Scan"):
 *   Fuer jedes Rig: laden -> auf die Meldung des Players warten -> kurz
 *   warten -> alle Daten abfragen -> Ergebnis an das Modell. Danach wird das
 *   Ausgangs-Rig wieder geladen. Bleibt die Meldung aus, wird trotzdem
 *   gelesen; kommt dabei zweimal hintereinander derselbe Rig-Name wie beim
 *   vorigen Rig, hat der Player den Wechsel offenbar nicht ausgefuehrt und der
 *   Scan bricht ab (sonst haette jedes Rig die Daten des gerade geladenen).
 *
 * Verbindungsueberwachung:
 *   - USB abgezogen: sofort (USB-Host meldet Disconnect)
 *   - Kemper antwortet nicht mehr: kein Sense fuer SENSE_TIMEOUT_MS
 *   - danach alle BEACON_INIT_PERIOD_MS ein neuer Verbindungsversuch
 */
#include <string.h>
#include <stdio.h>
#include "kemper_link.h"
#include "usbd_core.h"
#include "usbd_conf.h"
#include "usbd_desc.h"
#include "usbd_midi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "kemper_player.h"
#include "rig_store.h"

/* ------------------------------------------------------------------------
 * Zeiten und Konstanten
 * --------------------------------------------------------------------- */
#define BEACON_INIT_PERIOD_MS  2000U   /* Verbindungsversuch, solange keine Antwort */
#define LEASE_S                10U     /* Gueltigkeit des bidirektionalen Modus */
#define BEACON_REFRESH_MS      5000U   /* nach der halben Lease erneuern */
#define SENSE_TIMEOUT_MS       1500U   /* Player sendet ca. alle 500 ms */
#define POLL_PERIOD_MS         3000U   /* geladenes Rig regelmaessig neu lesen */
#define PUBLISH_PERIOD_MS      100U
#define FETCH_TIMEOUT_MS       1500U   /* so lange auf alle Antworten warten */
#define RIG_SETTLE_MS          200U    /* nach dem Rig-Wechsel, vor den Anfragen */
#define SELECT_TIMEOUT_MS      1000U   /* auf die Meldung des Players warten */
#define SELECT_MISS_MAX        2U      /* so oft hintereinander -> Scan abbrechen */

#define KEMPER_PRODUCT_PLAYER  0x02U
#define KEMPER_DEVICE_OMNI     0x7FU
#define KEMPER_PARAM_SET       0x02U   /* Parametersatz fuer automatische Meldungen */

#define FN_PARAM               0x01U
#define FN_STRING              0x03U
#define FN_REQ_PARAM           0x41U
#define FN_REQ_STRING          0x43U
#define FN_BIDIR               0x7EU

#define PAGE_STRINGS           0x00U
#define STR_RIG_NAME           0x01U
#define STR_AMP_NAME           0x10U
#define STR_CAB_NAME           0x20U
#define PAGE_RIG               0x04U
#define RIG_TEMPO              0x00U
#define RIG_TEMPO_ENABLE       0x02U
#define PAGE_SENSE             0x7FU

#define SLOT_TYPE              0x00U
#define SLOT_STATE             0x03U

#define CC_RIG_INDEX_HI        32U     /* Bank Select LSB: Rig-Index / 128 */

#define TASK_STACK_WORDS       1024U

/* Seiten der Effekt-Slots, Reihenfolge wie kp_mod_t */
static const uint8_t SLOT_PAGE[KP_MOD_COUNT] = { 0x32, 0x33, 0x34, 0x35, 0x38, 0x3A, 0x3C, 0x3D };

/* Rig-Parameter und Umrechnung in die Einheiten des Modells */
typedef enum { CV_TENTH, CV_BIPOLAR, CV_DB12, CV_TRANSPOSE, CV_VOLUME } conv_t;
static const struct {
    kp_param_t p;
    uint8_t    page, nr;
    conv_t     conv;
} PMAP[] = {
    { KP_P_RIG_VOLUME,    0x04, 0x01, CV_VOLUME    },
    { KP_P_RIG_TRANSPOSE, 0x04, 0x04, CV_TRANSPOSE },
    { KP_P_NOISE_GATE,    0x09, 0x03, CV_TENTH     },
    { KP_P_CLEAN_SENS,    0x09, 0x04, CV_DB12      },
    { KP_P_GAIN,          0x0A, 0x04, CV_TENTH     },
    { KP_P_DEFINITION,    0x0A, 0x06, CV_TENTH     },
    { KP_P_BASS,          0x0B, 0x04, CV_BIPOLAR   },
    { KP_P_MIDDLE,        0x0B, 0x05, CV_BIPOLAR   },
    { KP_P_TREBLE,        0x0B, 0x06, CV_BIPOLAR   },
    { KP_P_PRESENCE,      0x0B, 0x07, CV_BIPOLAR   },
};
#define PMAP_N  (sizeof(PMAP) / sizeof(PMAP[0]))

/* Bits der offenen Anfragen beim Lesen eines Rigs */
enum {
    RQ_NAME = 0, RQ_AMP, RQ_CAB, RQ_TEMPO, RQ_TEMPO_EN,
    RQ_MOD_TYPE,                         /* + Modul (8) */
    RQ_MOD_ON  = RQ_MOD_TYPE + KP_MOD_COUNT,
    RQ_PARAM   = RQ_MOD_ON + KP_MOD_COUNT,
    RQ_COUNT   = RQ_PARAM + PMAP_N
};
_Static_assert(RQ_COUNT <= 32, "zu viele Anfragen fuer die Bitmaske");

/* ------------------------------------------------------------------------
 * Zustand (nur im Link-Task veraendert)
 * --------------------------------------------------------------------- */
static USBD_HandleTypeDef hUsbDevice;
static kl_info_t w;            /* Arbeitskopie des Tasks */
static kl_info_t pub;          /* veroeffentlichte Kopie (unter Sperre) */
static uint32_t  pub_time;
static uint32_t  pub_gen = 0xFFFFFFFFU;

static uint32_t  t_last_init, t_last_beacon, t_last_poll;

/* Sendewarteschlange (SysEx oder Kanalnachricht) */
#define TXQ_N    48U
#define TXQ_LEN  24U
static struct { uint8_t len; uint8_t d[TXQ_LEN]; } txq[TXQ_N];
static uint8_t txq_head, txq_tail;

/* Empfang einer SysEx-Nachricht aus mehreren USB-MIDI-Events */
static uint8_t  sx[256];
static uint16_t sx_len;
static bool     sx_on;

/* Lesen eines Rigs */
static struct {
    bool          active;
    int16_t       index;       /* wohin das Ergebnis gehoert, -1 = unbekannt */
    uint32_t      pending;     /* RQ_*-Bits */
    uint32_t      t_start;
    kp_rig_data_t d;
} fetch;
static bool     fetch_due;     /* geladenes Rig neu lesen, sobald moeglich */
static uint32_t t_fetch_due;   /* fruehestens ab hier */

/* Rig-Meldungen des Players */
static uint8_t  rx_cc32;
static uint32_t rig_msg_count; /* zaehlt jede PC-Meldung */

/* Scan */
typedef enum { SC_OFF = 0, SC_SELECT, SC_SETTLE, SC_FETCH } sc_step_t;
static sc_step_t sc_step;
static uint16_t  sc_count;
static int16_t   sc_origin;
static uint32_t  sc_t;
static uint32_t  sc_msg_count;
static uint8_t   sc_miss;
static bool      sc_unconfirmed;
static char      sc_prev_name[KP_NAME_LEN];
static char      sc_origin_name[KP_NAME_LEN];   /* falls die Position unbekannt ist */
static int16_t   sc_origin_found;

/* Anfragen aus dem LVGL-Task (einfache Variablen, nur ein Schreiber) */
static volatile uint16_t req_scan_count;      /* > 0: Scan starten */
static volatile bool     req_scan_cancel;
static volatile int16_t  req_load = -1;       /* Rig laden (Index) */
static volatile uint8_t  mod_mask = 0xFFU;    /* vorhandene Module (vom Modell) */

/* Ergebnisse an den LVGL-Task */
#define RES_N  4U
static struct { int16_t index; kp_rig_data_t d; } res[RES_N];
static volatile uint8_t res_head, res_tail;

static uint32_t       task_stack[TASK_STACK_WORDS];
static osStaticThreadDef_t task_tcb;

/* ------------------------------------------------------------------------
 * Hilfen
 * --------------------------------------------------------------------- */
static void changed(void) { w.generation++; }

static void set_str(char * dst, const char * src)
{
    char tmp[KL_STR_LEN];
    strncpy(tmp, src ? src : "", KL_STR_LEN - 1U);
    tmp[KL_STR_LEN - 1U] = '\0';
    if (strcmp(dst, tmp) != 0) {
        strcpy(dst, tmp);
        changed();
    }
}

/* ASCII-Text aus einer SysEx-Nachricht (endet mit 00 oder F7) */
static void sysex_str(char * dst, uint32_t size, const uint8_t * p, const uint8_t * end)
{
    uint32_t i = 0;
    while (p < end && *p != 0x00U && *p != 0xF7U && i < size - 1U) {
        uint8_t c = *p++;
        dst[i++] = (c >= 0x20U && c < 0x7FU) ? (char)c : '?';
    }
    dst[i] = '\0';
}

static void clear_kemper_data(void)
{
    w.rig_name[0] = w.amp_name[0] = w.cab_name[0] = '\0';
    w.tempo_valid = false;
    w.identity_valid = false;
    w.rig_index = -1;
    changed();
}

/* Kemper-Wert (0..16383) in die Einheit des Modells */
static int16_t convert(conv_t c, uint16_t v)
{
    int32_t x;
    switch (c) {
    case CV_TENTH:     return (int16_t)(((int32_t)v * 100 + 8191) / 16383);
    case CV_BIPOLAR:
        x = ((int32_t)v - 8192) * 50;
        return (int16_t)((x + (x >= 0 ? 4096 : -4096)) / 8192);
    case CV_DB12:
        x = ((int32_t)v - 8192) * 12;
        return (int16_t)((x + (x >= 0 ? 4096 : -4096)) / 8192);
    case CV_TRANSPOSE: return (int16_t)((int32_t)(v >> 7) - 64);
    case CV_VOLUME: {
        /* Naeherung aus PySwitch (Rig Volume in dB, 7-Bit-Wert) */
        float f = (float)(v >> 7), db;
        if (f >= 30.0f) db = f * 0.24f - 24.0f;
        else            db = -166.6667f / (f + 2.0f) - 11.6f;
        return (int16_t)(db >= 0.0f ? db + 0.5f : db - 0.5f);
    }
    default:           return 0;
    }
}

/* ------------------------------------------------------------------------
 * Senden
 * --------------------------------------------------------------------- */
static void txq_reset(void) { txq_head = txq_tail = 0; }

static bool txq_push_raw(const uint8_t * d, uint8_t len)
{
    uint8_t next = (uint8_t)((txq_head + 1U) % TXQ_N);
    if (next == txq_tail || len == 0U || len > TXQ_LEN) return false;
    memcpy(txq[txq_head].d, d, len);
    txq[txq_head].len = len;
    txq_head = next;
    return true;
}

/* Kemper-SysEx: Kopf + body + F7 */
static bool send_kemper(const uint8_t * body, uint8_t n)
{
    uint8_t buf[TXQ_LEN];
    if ((uint32_t)n + 7U > TXQ_LEN) return false;
    buf[0] = 0xF0; buf[1] = 0x00; buf[2] = 0x20; buf[3] = 0x33;
    buf[4] = KEMPER_PRODUCT_PLAYER;
    buf[5] = KEMPER_DEVICE_OMNI;
    memcpy(&buf[6], body, n);
    buf[6U + n] = 0xF7;
    return txq_push_raw(buf, (uint8_t)(n + 7U));
}

static void request_string(uint8_t page, uint8_t nr)
{
    const uint8_t b[] = { FN_REQ_STRING, 0x00, page, nr };
    (void)send_kemper(b, sizeof(b));
}

static void request_param(uint8_t page, uint8_t nr)
{
    const uint8_t b[] = { FN_REQ_PARAM, 0x00, page, nr };
    (void)send_kemper(b, sizeof(b));
}

static void send_beacon(bool init)
{
    const uint8_t flags = (uint8_t)(0x02U | (init ? 0x01U : 0x00U));   /* SysEx (+ Init) */
    const uint8_t b[] = { FN_BIDIR, 0x00, 0x40, KEMPER_PARAM_SET, flags, (uint8_t)(LEASE_S / 2U) };
    (void)send_kemper(b, sizeof(b));
    t_last_beacon = HAL_GetTick();
}

static void request_identity(void)
{
    static const uint8_t idreq[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    (void)txq_push_raw(idreq, sizeof(idreq));
}

/* Rig laden: CC 32 (Index / 128) + Program Change (Index % 128) */
static void send_rig_select(uint16_t index)
{
    const uint8_t ch = w.midi_channel & 0x0FU;
    const uint8_t cc[] = { (uint8_t)(0xB0U | ch), CC_RIG_INDEX_HI, (uint8_t)((index >> 7) & 0x7FU) };
    const uint8_t pc[] = { (uint8_t)(0xC0U | ch), (uint8_t)(index & 0x7FU) };
    (void)txq_push_raw(cc, sizeof(cc));
    (void)txq_push_raw(pc, sizeof(pc));
}

/* SysEx in USB-MIDI-Events (Kabel 0) zerlegen */
static uint16_t sysex_to_events(const uint8_t * d, uint8_t n, uint8_t * ev)
{
    uint16_t o = 0;
    uint8_t i = 0;
    while ((uint8_t)(n - i) > 3U) {
        ev[o++] = 0x04; ev[o++] = d[i]; ev[o++] = d[i + 1U]; ev[o++] = d[i + 2U];
        i = (uint8_t)(i + 3U);
    }
    uint8_t r = (uint8_t)(n - i);                        /* 1..3 Bytes, letztes ist F7 */
    ev[o++] = (uint8_t)(0x04U + r);                      /* CIN 5, 6 oder 7 */
    ev[o++] = d[i];
    ev[o++] = r > 1U ? d[i + 1U] : 0x00U;
    ev[o++] = r > 2U ? d[i + 2U] : 0x00U;
    return o;
}

/* Kanalnachricht (Status + 1-2 Datenbytes) als ein USB-MIDI-Event */
static uint16_t channel_to_event(const uint8_t * d, uint8_t n, uint8_t * ev)
{
    ev[0] = (uint8_t)(d[0] >> 4);                        /* CIN = oberes Nibble */
    ev[1] = d[0];
    ev[2] = n > 1U ? d[1] : 0x00U;
    ev[3] = n > 2U ? d[2] : 0x00U;
    return 4U;
}

static void pump_tx(void)
{
    if (txq_head == txq_tail || !usbd_midi_tx_free()) return;
    uint8_t ev[USBD_MIDI_PKT_MAX];
    const uint8_t * d = txq[txq_tail].d;
    const uint8_t   n = txq[txq_tail].len;
    uint16_t len = d[0] == 0xF0U ? sysex_to_events(d, n, ev) : channel_to_event(d, n, ev);
    if (usbd_midi_send(ev, len)) {
        txq_tail = (uint8_t)((txq_tail + 1U) % TXQ_N);
        w.tx_messages++;
    }
}

/* ------------------------------------------------------------------------
 * Ein Rig lesen
 * --------------------------------------------------------------------- */
static void fetch_start(int16_t index, uint32_t now)
{
    const uint8_t mm = mod_mask;
    memset(&fetch.d, 0, sizeof(fetch.d));
    fetch.active = true;
    fetch.index = index;
    fetch.t_start = now;
    fetch.pending = 0;

    fetch.pending |= 1UL << RQ_NAME;    request_string(PAGE_STRINGS, STR_RIG_NAME);
    fetch.pending |= 1UL << RQ_AMP;     request_string(PAGE_STRINGS, STR_AMP_NAME);
    fetch.pending |= 1UL << RQ_CAB;     request_string(PAGE_STRINGS, STR_CAB_NAME);
    fetch.pending |= 1UL << RQ_TEMPO;   request_param(PAGE_RIG, RIG_TEMPO);
    fetch.pending |= 1UL << RQ_TEMPO_EN; request_param(PAGE_RIG, RIG_TEMPO_ENABLE);
    for (uint32_t m = 0; m < KP_MOD_COUNT; m++) {
        if (!(mm & (1U << m))) continue;
        fetch.pending |= 1UL << (RQ_MOD_TYPE + m);
        fetch.pending |= 1UL << (RQ_MOD_ON + m);
        request_param(SLOT_PAGE[m], SLOT_TYPE);
        request_param(SLOT_PAGE[m], SLOT_STATE);
    }
    for (uint32_t i = 0; i < PMAP_N; i++) {
        fetch.pending |= 1UL << (RQ_PARAM + i);
        request_param(PMAP[i].page, PMAP[i].nr);
    }
}

/* Ergebnis an den LVGL-Task; false, wenn die Ablage voll ist */
static bool result_push(int16_t index, const kp_rig_data_t * d)
{
    uint8_t next = (uint8_t)((res_head + 1U) % RES_N);
    if (next == res_tail) return false;
    res[res_head].index = index;
    res[res_head].d = *d;
    taskENTER_CRITICAL();
    res_head = next;
    taskEXIT_CRITICAL();
    return true;
}

/* Antworten in die laufende Abfrage eintragen */
static void fetch_string(uint8_t nr, const char * txt)
{
    if (!fetch.active) return;
    if (nr == STR_RIG_NAME && (fetch.pending & (1UL << RQ_NAME))) {
        strncpy(fetch.d.name, txt, KP_NAME_LEN - 1U);
        fetch.pending &= ~(1UL << RQ_NAME);
    } else if (nr == STR_AMP_NAME && (fetch.pending & (1UL << RQ_AMP))) {
        strncpy(fetch.d.amp, txt, KP_NAME_LEN - 1U);
        fetch.pending &= ~(1UL << RQ_AMP);
    } else if (nr == STR_CAB_NAME && (fetch.pending & (1UL << RQ_CAB))) {
        strncpy(fetch.d.cab, txt, KP_NAME_LEN - 1U);
        fetch.pending &= ~(1UL << RQ_CAB);
    }
}

static void fetch_param(uint8_t page, uint8_t nr, uint16_t v)
{
    if (!fetch.active) return;
    if (page == PAGE_RIG && nr == RIG_TEMPO && (fetch.pending & (1UL << RQ_TEMPO))) {
        fetch.d.tempo_bpm = (uint16_t)((v + 32U) / 64U);
        fetch.d.tempo_valid = true;
        fetch.pending &= ~(1UL << RQ_TEMPO);
        return;
    }
    if (page == PAGE_RIG && nr == RIG_TEMPO_ENABLE && (fetch.pending & (1UL << RQ_TEMPO_EN))) {
        fetch.d.tempo_on = v != 0U;
        fetch.pending &= ~(1UL << RQ_TEMPO_EN);
        return;
    }
    for (uint32_t m = 0; m < KP_MOD_COUNT; m++) {
        if (page != SLOT_PAGE[m]) continue;
        if (nr == SLOT_TYPE && (fetch.pending & (1UL << (RQ_MOD_TYPE + m)))) {
            fetch.d.mod_kid[m] = v;
            fetch.d.mod_valid |= (uint8_t)(1U << m);
            fetch.pending &= ~(1UL << (RQ_MOD_TYPE + m));
        } else if (nr == SLOT_STATE && (fetch.pending & (1UL << (RQ_MOD_ON + m)))) {
            if (v) fetch.d.mod_on |= (uint8_t)(1U << m);
            fetch.pending &= ~(1UL << (RQ_MOD_ON + m));
        }
        return;
    }
    for (uint32_t i = 0; i < PMAP_N; i++) {
        if (PMAP[i].page == page && PMAP[i].nr == nr && (fetch.pending & (1UL << (RQ_PARAM + i)))) {
            fetch.d.param[PMAP[i].p] = convert(PMAP[i].conv, v);
            fetch.d.param_valid |= 1UL << PMAP[i].p;
            fetch.pending &= ~(1UL << (RQ_PARAM + i));
            return;
        }
    }
}

/* Fertig (alles da oder Zeit abgelaufen)? Dann Ergebnis abliefern. */
static bool fetch_finish(uint32_t now)
{
    if (!fetch.active) return true;
    if (fetch.pending && now - fetch.t_start < FETCH_TIMEOUT_MS) return false;
    if (!result_push(fetch.index, &fetch.d)) return false;        /* spaeter nochmal */
    fetch.active = false;

    /* Anzeige im System-Menue: Daten des geladenen Rigs */
    if (sc_step == SC_OFF) {
        set_str(w.rig_name, fetch.d.name);
        set_str(w.amp_name, fetch.d.amp);
        set_str(w.cab_name, fetch.d.cab);
        if (fetch.d.tempo_valid &&
            (!w.tempo_valid || w.tempo_bpm != fetch.d.tempo_bpm || w.tempo_on != fetch.d.tempo_on)) {
            w.tempo_valid = true;
            w.tempo_bpm = fetch.d.tempo_bpm;
            w.tempo_on = fetch.d.tempo_on;
            changed();
        }
    }
    return true;
}

static bool fetch_got_answer(void)
{
    /* Mindestens eine Antwort (Name kommt immer, auch bei leerem Rig "") */
    return (fetch.pending & (1UL << RQ_NAME)) == 0U || fetch.d.mod_valid != 0U;
}

/* ------------------------------------------------------------------------
 * Empfangen
 * --------------------------------------------------------------------- */
static void on_sense(void)
{
    uint32_t now = HAL_GetTick();
    w.last_sense_ms = now;
    if (w.kemper != KL_KEMPER_OK) {
        w.kemper = KL_KEMPER_OK;
        w.connected_since_ms = now;
        changed();
        request_identity();
        fetch_due = true;                /* geladenes Rig lesen */
        t_fetch_due = now;
        t_last_poll = now;
    }
}

static void handle_identity(const uint8_t * d, uint16_t n)
{
    /* F0 7E <dev> 06 02 <Hersteller 1 oder 3 Byte> <Familie 2> <Modell 2> <Version 4> F7 */
    uint16_t p = 5;
    bool kemper;
    if (n < 6U) return;
    if (d[5] == 0x00U) {
        if (n < 8U) return;
        kemper = d[6] == 0x20U && d[7] == 0x33U;
        p = 8;
    } else {
        kemper = false;
        p = 6;
    }
    if (!kemper || n < (uint16_t)(p + 9U)) return;
    memcpy(w.id_family,  &d[p],      2);
    memcpy(w.id_model,   &d[p + 2U], 2);
    memcpy(w.id_version, &d[p + 4U], 4);
    w.identity_valid = true;
    changed();
}

static void handle_sysex(const uint8_t * d, uint16_t n)
{
    w.rx_messages++;
    if (n >= 6U && d[1] == 0x7EU && d[3] == 0x06U && d[4] == 0x02U) {
        handle_identity(d, n);
        return;
    }
    /* Kemper: F0 00 20 33 <Typ> <Geraet> <Fn> <Inst> <Seite> <Nr> ... */
    if (n < 10U || d[1] != 0x00U || d[2] != 0x20U || d[3] != 0x33U) return;
    const uint8_t fn = d[6], page = d[8], nr = d[9];

    if (fn == FN_BIDIR && page == PAGE_SENSE) {
        on_sense();
    } else if (fn == FN_STRING && page == PAGE_STRINGS && n >= 11U) {
        char tmp[KL_STR_LEN];
        sysex_str(tmp, sizeof(tmp), &d[10], d + n);
        fetch_string(nr, tmp);
        /* Name des geladenen Rigs kommt im bidirektionalen Modus auch von selbst */
        if (nr == STR_RIG_NAME && sc_step == SC_OFF) set_str(w.rig_name, tmp);
    } else if (fn == FN_PARAM && n >= 13U) {
        uint16_t v = (uint16_t)(((uint16_t)d[10] << 7) | d[11]);
        fetch_param(page, nr, v);
    }
}

/* Rig-Meldung: CC 32 + Program Change */
static void handle_channel(uint8_t status, uint8_t d1, uint8_t d2)
{
    const uint8_t type = status & 0xF0U;
    w.rx_messages++;
    if (type == 0xB0U && d1 == CC_RIG_INDEX_HI) {
        rx_cc32 = d2;
    } else if (type == 0xC0U) {
        int16_t idx = (int16_t)((uint16_t)rx_cc32 * 128U + d1);
        if (w.midi_channel != (status & 0x0FU)) {
            w.midi_channel = status & 0x0FU;
            changed();
        }
        rig_msg_count++;
        if (idx != w.rig_index) {
            w.rig_index = idx;
            changed();
        }
        if (sc_step == SC_OFF) {         /* Rig am Player gewechselt: neu lesen */
            fetch_due = true;
            t_fetch_due = HAL_GetTick() + RIG_SETTLE_MS;
        }
    }
}

static void sx_add(uint8_t b)
{
    if (b == 0xF0U) {
        sx_len = 0;
        sx_on = true;
    }
    if (!sx_on) return;
    if (sx_len >= sizeof(sx)) {          /* zu lang: verwerfen */
        sx_on = false;
        return;
    }
    sx[sx_len++] = b;
    if (b == 0xF7U) {
        sx_on = false;
        handle_sysex(sx, sx_len);
    }
}

/* Empfangene USB-MIDI-Events auswerten (im Link-Task) */
static void midi_receive(const uint8_t * ev, uint16_t len)
{
    for (uint16_t i = 0; i + 3U < len; i += 4U) {
        const uint8_t cin = ev[i] & 0x0FU;
        switch (cin) {
        case 0x4: sx_add(ev[i + 1U]); sx_add(ev[i + 2U]); sx_add(ev[i + 3U]); break;
        case 0x5: sx_add(ev[i + 1U]); break;
        case 0x6: sx_add(ev[i + 1U]); sx_add(ev[i + 2U]); break;
        case 0x7: sx_add(ev[i + 1U]); sx_add(ev[i + 2U]); sx_add(ev[i + 3U]); break;
        case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE:
            handle_channel(ev[i + 1U], ev[i + 2U], ev[i + 3U]);
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------------
 * Scan
 * --------------------------------------------------------------------- */
static void scan_end(kl_scan_state_t st)
{
    if (sc_step == SC_OFF) return;
    sc_step = SC_OFF;
    fetch.active = false;
    w.scan_state = st;
    w.scan_generation++;
    changed();
}

/* Rig, das vor dem Scan geladen war (Position gemeldet oder am Namen erkannt) */
static int16_t scan_origin(void)
{
    return sc_origin >= 0 ? sc_origin : sc_origin_found;
}

static void scan_select(uint32_t now)
{
    sc_step = SC_SELECT;
    sc_t = now;
    sc_msg_count = rig_msg_count;
    send_rig_select(w.scan_pos);
}

static void scan_begin(uint16_t count, uint32_t now)
{
    if (count > KP_DETAIL_RIGS) count = KP_DETAIL_RIGS;
    sc_count = count;
    sc_origin = w.rig_index;
    sc_miss = 0;
    strncpy(sc_prev_name, w.rig_name, KP_NAME_LEN - 1U);
    sc_prev_name[KP_NAME_LEN - 1U] = '\0';
    memcpy(sc_origin_name, sc_prev_name, KP_NAME_LEN);
    sc_origin_found = -1;
    fetch.active = false;
    fetch_due = false;
    w.scan_state = KL_SCAN_RUNNING;
    w.scan_pos = 0;
    w.scan_total = count;
    w.scan_ok = 0;
    changed();
    scan_select(now);
}

static void scan_step(uint32_t now)
{
    switch (sc_step) {
    case SC_SELECT: {
        bool confirmed = rig_msg_count != sc_msg_count && w.rig_index == (int16_t)w.scan_pos;
        if (!confirmed && now - sc_t < SELECT_TIMEOUT_MS) break;
        sc_unconfirmed = !confirmed;     /* ohne Meldung: am Namen pruefen */
        sc_step = SC_SETTLE;
        sc_t = now;
        break;
    }
    case SC_SETTLE:
        if (now - sc_t >= RIG_SETTLE_MS) {
            fetch_start((int16_t)w.scan_pos, now);
            sc_step = SC_FETCH;
        }
        break;
    case SC_FETCH:
        if (!fetch_finish(now)) break;
        if (sc_unconfirmed && fetch.d.name[0] && strcmp(fetch.d.name, sc_prev_name) == 0) {
            if (++sc_miss >= SELECT_MISS_MAX) {
                /* Player fuehrt den Program Change nicht aus (MIDI-Kanal, Einstellung?) */
                scan_end(KL_SCAN_NO_RESPONSE);
                if (scan_origin() >= 0) send_rig_select((uint16_t)scan_origin());
                break;
            }
        } else {
            sc_miss = 0;
        }
        memcpy(sc_prev_name, fetch.d.name, KP_NAME_LEN);
        /* Ausgangs-Rig am Namen wiedererkennen (erster Treffer) */
        if (sc_origin < 0 && sc_origin_found < 0 && sc_origin_name[0] &&
            strcmp(fetch.d.name, sc_origin_name) == 0) {
            sc_origin_found = (int16_t)w.scan_pos;
        }
        if (fetch_got_answer()) w.scan_ok++;
        w.scan_pos++;
        changed();
        if (w.scan_pos < sc_count) {
            scan_select(now);
        } else {
            /* Ausgangs-Rig wieder laden; dessen Daten danach neu lesen */
            int16_t o = scan_origin();
            if (o >= 0) {
                send_rig_select((uint16_t)o);
                if (w.rig_index != o) {      /* falls der Player den Wechsel nicht meldet */
                    w.rig_index = o;
                    changed();
                }
            }
            scan_end(KL_SCAN_DONE);
            fetch_due = true;
            t_fetch_due = now + RIG_SETTLE_MS + 300U;
        }
        break;
    case SC_OFF:
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * Zustandsmaschine
 * --------------------------------------------------------------------- */
static kl_usb_state_t usb_state(void)
{
    if (usbd_midi_ready()) return KL_USB_READY;
    switch (hUsbDevice.dev_state) {
    case USBD_STATE_DEFAULT:
    case USBD_STATE_ADDRESSED:
        return KL_USB_ENUM;
    default:                      /* nicht verbunden oder Suspend */
        return KL_USB_NONE;
    }
}

static void connection_lost(void)
{
    scan_end(KL_SCAN_CANCELLED);
    fetch.active = false;
    fetch_due = false;
}

static void link_step(uint32_t now)
{
    kl_usb_state_t us = usb_state();
    if (us != w.usb) {
        w.usb = us;
        changed();
        if (us != KL_USB_READY) {
            if (w.kemper == KL_KEMPER_OK) w.lost_count++;
            w.kemper = KL_KEMPER_NONE;
            clear_kemper_data();
            connection_lost();
            txq_reset();
            sx_on = false;
        }
    }

    /* Anfragen aus dem LVGL-Task */
    if (req_scan_cancel) {
        req_scan_cancel = false;
        if (sc_step != SC_OFF) {
            scan_end(KL_SCAN_CANCELLED);
            if (scan_origin() >= 0) send_rig_select((uint16_t)scan_origin());
        }
    }

    if (us != KL_USB_READY) {
        req_scan_count = 0;
        req_load = -1;
        return;
    }

    switch (w.kemper) {
    case KL_KEMPER_NONE:
        w.kemper = KL_KEMPER_WAIT;
        t_last_init = now - BEACON_INIT_PERIOD_MS;      /* sofort versuchen */
        changed();
        /* fall through */
    case KL_KEMPER_WAIT:
    case KL_KEMPER_LOST:
        req_scan_count = 0;
        if (now - t_last_init >= BEACON_INIT_PERIOD_MS) {
            t_last_init = now;
            send_beacon(true);
        }
        break;
    case KL_KEMPER_OK:
        if (now - w.last_sense_ms > SENSE_TIMEOUT_MS) {
            w.kemper = KL_KEMPER_LOST;
            w.lost_count++;
            t_last_init = now - BEACON_INIT_PERIOD_MS;
            connection_lost();
            changed();
            break;
        }
        if (now - t_last_beacon >= BEACON_REFRESH_MS) send_beacon(false);

        if (req_scan_count && sc_step == SC_OFF) {
            uint16_t n = req_scan_count;
            req_scan_count = 0;
            scan_begin(n, now);
        }
        if (sc_step != SC_OFF) {
            scan_step(now);
            break;
        }

        /* Normalbetrieb: Rig laden, geladenes Rig lesen */
        if (req_load >= 0) {
            send_rig_select((uint16_t)req_load);
            req_load = -1;
        }
        if (fetch.active) {
            (void)fetch_finish(now);
        } else if (fetch_due && (int32_t)(now - t_fetch_due) >= 0) {
            fetch_due = false;
            t_last_poll = now;
            fetch_start(w.rig_index, now);
        } else if (now - t_last_poll >= POLL_PERIOD_MS) {
            t_last_poll = now;           /* z. B. Effekt am Player geschaltet */
            fetch_start(w.rig_index, now);
        }
        break;
    }
    pump_tx();
}

static void publish(uint32_t now, bool force)
{
    if (!force && w.generation == pub_gen && now - pub_time < PUBLISH_PERIOD_MS) return;
    w.now_ms = now;
    taskENTER_CRITICAL();
    pub = w;
    taskEXIT_CRITICAL();
    pub_gen = w.generation;
    pub_time = now;
}

static void link_task(void const * arg)
{
    (void)arg;
    memset(&w, 0, sizeof(w));
    w.rig_index = -1;

    /* USB-Geraet starten: ab hier kann der Kemper das Display einrichten */
    if (USBD_Init(&hUsbDevice, &KPD_Desc, DEVICE_FS) != USBD_OK ||
        USBD_RegisterClass(&hUsbDevice, USBD_MIDI_CLASS) != USBD_OK ||
        USBD_Start(&hUsbDevice) != USBD_OK) {
        Error_Handler();
    }
    publish(HAL_GetTick(), true);

    static uint8_t ev[USBD_MIDI_PKT_MAX];
    for (;;) {
        uint16_t n;
        while ((n = usbd_midi_read(ev, sizeof(ev))) > 0U) midi_receive(ev, n);
        uint32_t now = HAL_GetTick();
        usbd_midi_poll(now);
        link_step(now);
        w.usb_errors = usbd_midi_errors();
        publish(now, false);
        vTaskDelay(1);
    }
}

/* ------------------------------------------------------------------------
 * Oeffentliche Funktionen
 * --------------------------------------------------------------------- */
void kemper_link_start(void)
{
    osThreadStaticDef(kemper_link, link_task, osPriorityAboveNormal, 0,
                      TASK_STACK_WORDS, task_stack, &task_tcb);
    (void)osThreadCreate(osThread(kemper_link), NULL);
}

void kemper_link_get_info(kl_info_t * out)
{
    taskENTER_CRITICAL();
    *out = pub;
    taskEXIT_CRITICAL();
    out->now_ms = HAL_GetTick();
}

const char * kemper_link_state_text(const kl_info_t * i)
{
    switch (i->usb) {
    case KL_USB_NONE:    return "Kein Kemper an USB";
    case KL_USB_ENUM:    return "Kemper richtet USB ein";
    case KL_USB_READY:
    default:
        break;
    }
    switch (i->kemper) {
    case KL_KEMPER_OK:   return "Verbunden";
    case KL_KEMPER_LOST: return "Verbindung unterbrochen, neuer Versuch";
    case KL_KEMPER_WAIT:
    case KL_KEMPER_NONE:
    default:             return "Warte auf Antwort vom Kemper";
    }
}

bool kemper_link_scan_start(uint16_t count)
{
    kl_info_t i;
    kemper_link_get_info(&i);
    if (i.kemper != KL_KEMPER_OK || i.scan_state == KL_SCAN_RUNNING || count == 0U) return false;
    req_scan_cancel = false;
    req_scan_count = count;
    return true;
}

void kemper_link_scan_cancel(void)
{
    req_scan_cancel = true;
}

/* Vom Modell (LVGL-Task): Rig am Display gewaehlt -> am Player laden */
void kp_link_load_rig(uint8_t bank, uint8_t slot)
{
    req_load = (int16_t)(bank * KP_RIGS_PER_BANK + slot);
}

/* ------------------------------------------------------------------------
 * Anbindung an das Modell (nur im LVGL-Task)
 * --------------------------------------------------------------------- */
static char save_text[64] = "";

const char * kemper_link_scan_save_text(void)
{
    return save_text;
}

void kemper_link_ui_poll(void)
{
    static kl_info_t i;
    static bool      last_conn;
    static int16_t   last_index = -1;
    static uint32_t  last_scan_gen;
    static uint32_t  last_gen = 0xFFFFFFFFU;
    static bool      loaded;

    /* Beim ersten Aufruf (Scheduler laeuft) die gespeicherten Rigs laden */
    if (!loaded) {
        loaded = true;
        (void)rig_store_load();
    }

    /* Vorhandene Module fuer die Abfragen (aendert sich mit dem Level) */
    uint8_t mm = 0;
    for (int m = 0; m < KP_MOD_COUNT; m++)
        if (kp_module_available((kp_mod_t)m)) mm |= (uint8_t)(1U << m);
    mod_mask = mm;

    kemper_link_get_info(&i);
    bool conn = i.kemper == KL_KEMPER_OK;
    if (conn != last_conn) {
        last_conn = conn;
        kp_rx_connected(conn);
    }

    /* Rig am Player gewechselt (nicht waehrend des Scans) */
    if (conn && i.scan_state != KL_SCAN_RUNNING && i.rig_index >= 0 && i.rig_index != last_index) {
        last_index = i.rig_index;
        kp_rx_rig_loaded((uint8_t)(i.rig_index / KP_RIGS_PER_BANK),
                         (uint8_t)(i.rig_index % KP_RIGS_PER_BANK));
    }
    if (!conn) last_index = -1;

    /* Eingelesene Rigs ans Modell */
    while (res_tail != res_head) {
        static kp_rig_data_t d;
        int16_t idx;
        taskENTER_CRITICAL();
        idx = res[res_tail].index;
        d = res[res_tail].d;
        res_tail = (uint8_t)((res_tail + 1U) % RES_N);
        taskEXIT_CRITICAL();
        kp_rx_rig_details(idx, &d);
    }

    /* Scan beendet: auf die SD-Karte */
    if (i.scan_generation != last_scan_gen) {
        last_scan_gen = i.scan_generation;
        if (i.scan_state == KL_SCAN_DONE || (i.scan_state == KL_SCAN_CANCELLED && i.scan_ok > 0U)) {
            rig_store_result_t r = rig_store_save();
            snprintf(save_text, sizeof(save_text), "%s", rig_store_result_text(r));
        } else {
            save_text[0] = '\0';
        }
        kp_rx_link_info();
    }

    if (i.generation == last_gen) return;
    last_gen = i.generation;
    kp_rx_link_info();
}
