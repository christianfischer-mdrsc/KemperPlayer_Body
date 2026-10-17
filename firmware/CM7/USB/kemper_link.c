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
 *               04/00 Rig-Tempo (Wert / 64 = BPM), 04/02 Tempo an/aus
 *
 * Aktuelles Rig:
 *   Der Name (00/01) wird alle RIG_POLL_MS abgefragt; aendert er sich, hat der
 *   Kemper ein anderes Rig geladen, und Amp/Cab/Tempo werden sofort neu
 *   gelesen. Bank und Platz kommen aus Program Change (mit Bank Select LSB,
 *   CC 32): Rig-Nummer = CC32 * 128 + PC, Bank = Nummer / 5, Platz = Nummer % 5.
 *   Gehoert zum Platz nur der erste Name nach dem Positionswechsel (Antwort
 *   auf die danach gestellte Anfrage). Jeder spaetere neue Name bedeutet ein
 *   anderes Rig an unbekanntem Platz (der Kemper schickt beim Wechsel per
 *   Footswitch den Namen vor dem Program Change). Ein am Display gewaehltes Rig wird mit
 *   derselben Zuordnung (CC 32 + PC auf MIDI-Kanal 1) am Kemper geladen.
 *   Beim Verbinden kommt kein Program Change. Bleibt er aus, fragt das Display
 *   die Bank mit der Bank-Vorauswahl ab (CC 48 = eine Bank vor, CC 49 = wieder
 *   zurueck, laut MIDI-Dokumentation ohne Rig-Wechsel); der Kemper meldet die
 *   vorgewaehlte Bank mit CC 47 und, falls dazu gesendet, den Platz mit
 *   CC 50-54.
 *
 * Transpose: Rig-Parameter 04/04, 28..100 (64 = 0 Halbtoene).
 *
 * Effekte:
 *   Slots A, B, C, D, X, MOD, DLY, REV liegen auf den Seiten 32, 33, 34, 35,
 *   38, 3A, 3C, 3D (hex); Nr. 0 = Typ, Nr. 3 = Ein/Aus. Fixed Effects: Seite 5,
 *   Ein/Aus auf Nr. 1, 6, 11, 16, 21, 26, 36, 41. Im bidirektionalen Modus
 *   meldet der Kemper Typ und Ein/Aus von A-MOD selbst; zusaetzlich fragt
 *   das Display alles alle FX_POLL_MS ab (DLY/REV und Fixed meldet er nicht).
 *   Aenderungen vom Display: Single Parameter Change (Funktion 01).
 *   Werte als Text: Funktion 7C (Antwort 3C) liefert die Darstellung des
 *   Kemper, z. B. "3.5 dB" oder den Effektnamen zum Typ.
 *
 * Ausserdem wird die allgemeine MIDI-Geraeteabfrage (Identity Request,
 * F0 7E 7F 06 01 F7) gesendet. Kemper dokumentiert keine Antwort darauf;
 * kommt eine, wird die Firmware-Kennung angezeigt.
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

/* ------------------------------------------------------------------------
 * Zeiten und Konstanten
 * --------------------------------------------------------------------- */
#define BEACON_INIT_PERIOD_MS  2000U   /* Verbindungsversuch, solange keine Antwort */
#define LEASE_S                10U     /* Gueltigkeit des bidirektionalen Modus */
#define BEACON_REFRESH_MS      5000U   /* nach der halben Lease erneuern */
#define SENSE_TIMEOUT_MS       1500U   /* Player sendet ca. alle 500 ms */
#define POLL_PERIOD_MS         3000U   /* Amp/Cab/Tempo regelmaessig neu lesen */
#define RIG_POLL_MS            500U    /* Name des geladenen Rigs abfragen */
#define NAME_ANSWER_MS         1000U   /* danach gilt eine Namensanfrage als verloren */
#define FX_POLL_MS             600U    /* Effekt-Slots und Fixed Effects abfragen */
#define WATCH_POLL_MS          500U    /* Einstellungen des offenen Effekt-Dialogs */
#define PUBLISH_PERIOD_MS      100U

#define KEMPER_PRODUCT_PLAYER  0x02U
#define KEMPER_DEVICE_OMNI     0x7FU
#define KEMPER_PARAM_SET       0x02U   /* Parametersatz fuer automatische Meldungen */

#define FN_PARAM               0x01U
#define FN_STRING              0x03U
#define FN_REQ_PARAM           0x41U
#define FN_REQ_STRING          0x43U
#define FN_BIDIR               0x7EU
#define FN_REQ_RENDER          0x7CU   /* Wert als Text anfordern */
#define FN_RENDER              0x3CU   /* Antwort darauf */

#define PAGE_STRINGS           0x00U
#define STR_RIG_NAME           0x01U
#define STR_AMP_NAME           0x10U
#define STR_CAB_NAME           0x20U
#define PAGE_RIG               0x04U
#define RIG_TEMPO              0x00U
#define RIG_TEMPO_ENABLE       0x02U
#define RIG_TRANSPOSE          0x04U
#define TRANSPOSE_ZERO         64U
#define PAGE_SENSE             0x7FU
#define FX_TYPE                0x00U   /* Nummern auf den Seiten der Effekt-Slots */
#define FX_ON                  0x03U

/* Seiten der Effekt-Slots, Index = kp_mod_t */
static const uint8_t SLOT_PAGE[KL_SLOTS] = { 0x32, 0x33, 0x34, 0x35, 0x38, 0x3A, 0x3C, 0x3D };

#define MIDI_CH                0x00U   /* Kanal 1 fuer Program Change an den Kemper */
#define CC_BANK_LSB            32U
#define CC_BANK_PRESELECT      47U     /* Antwort: vorgewaehlte Bank */
#define CC_BANK_UP             48U
#define CC_BANK_DOWN           49U
#define CC_SLOT_FIRST          50U     /* CC 50-54: Platz 1-5 */
#define POS_PROBE_DELAY_MS     1000U   /* so lange auf einen Program Change warten */
#define LOAD_NONE              0xFFFFU


#define TASK_STACK_WORDS       1024U

/* ------------------------------------------------------------------------
 * Zustand (nur im Link-Task veraendert)
 * --------------------------------------------------------------------- */
static USBD_HandleTypeDef hUsbDevice;
static kl_info_t w;            /* Arbeitskopie des Tasks */
static kl_info_t pub;          /* veroeffentlichte Kopie (unter Sperre) */
static uint32_t  pub_time;
static uint32_t  pub_gen = 0xFFFFFFFFU;

static uint32_t  t_last_init, t_last_beacon, t_last_poll, t_last_rig_poll;
/* Offene Anfragen nach dem Rig-Namen. Der Kemper antwortet der Reihe nach;
 * Antworten auf Anfragen von vor einem Positionswechsel werden verworfen,
 * sie koennten noch das alte Rig beschreiben. */
static uint8_t   name_pending, name_skip;
static uint32_t  t_name_req;
static uint8_t   bank_lsb;           /* zuletzt empfangenes CC 32 */
static bool      pos_probed;         /* Bank-Abfrage nach dem Verbinden gesendet */

/* Rig-Wahl am Display (LVGL-Task -> Link-Task): Rig-Nummer oder LOAD_NONE */
static volatile uint16_t load_req = LOAD_NONE;

/* Parameter-Aenderungen vom Display (LVGL-Task schreibt set_head, Link-Task
 * liest set_tail; ein Schreiber, ein Leser) */
#define SETQ_N   16U
static struct { uint8_t page, nr; uint16_t value; } setq[SETQ_N];
static volatile uint8_t set_head, set_tail;

/* Effekt-Dialog (LVGL-Task -> Link-Task): (Art << 8) | Index */
static volatile uint16_t watch_req;
static uint16_t          watch_cur;
static uint16_t          watch_for_type;     /* Typ, fuer den die Liste gilt */
static bool              watch_typed;        /* Liste mit bekanntem Typ aufgebaut */
static uint8_t           watch_page;
static uint8_t           watch_nr[KL_WATCH_MAX];
static uint32_t          t_last_fx_poll, t_last_watch;

/* Sendewarteschlange fuer SysEx-Nachrichten */
#define TXQ_N    64U
#define TXQ_LEN  24U
static struct { uint8_t len; uint8_t d[TXQ_LEN]; } txq[TXQ_N];
static uint8_t txq_head, txq_tail;

/* Empfang einer SysEx-Nachricht aus mehreren USB-MIDI-Events */
static uint8_t  sx[256];
static uint16_t sx_len;
static bool     sx_on;

static uint32_t       task_stack[TASK_STACK_WORDS];
static osStaticThreadDef_t task_tcb;

/* ------------------------------------------------------------------------
 * Hilfen
 * --------------------------------------------------------------------- */
static void changed(void) { w.generation++; }

/* true, wenn sich der Text geaendert hat */
static bool set_str(char * dst, const char * src)
{
    char tmp[KL_STR_LEN];
    strncpy(tmp, src ? src : "", KL_STR_LEN - 1U);
    tmp[KL_STR_LEN - 1U] = '\0';
    if (strcmp(dst, tmp) == 0) return false;
    strcpy(dst, tmp);
    changed();
    return true;
}

/* ASCII-Text aus einer SysEx-Nachricht (endet mit 00 oder F7) */
static void sysex_str(char * dst, const uint8_t * p, const uint8_t * end)
{
    uint32_t i = 0;
    while (p < end && *p != 0x00U && *p != 0xF7U && i < KL_STR_LEN - 1U) {
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
    w.rig_pos_valid = w.rig_bank_valid = false;
    w.transpose_valid = false;
    pos_probed = false;
    bank_lsb = 0;
    name_pending = name_skip = 0;
    w.slot_type_valid = w.slot_on_valid = w.slot_on = 0;
    memset(w.slot_name, 0, sizeof(w.slot_name));
    w.fixed_valid = w.fixed_on = 0;
    w.watch_valid = 0;
    w.watch_raw_valid = 0;
    set_tail = set_head;                 /* offene Aenderungen verwerfen */
    changed();
}

/* ------------------------------------------------------------------------
 * Senden
 * --------------------------------------------------------------------- */
static void txq_reset(void) { txq_head = txq_tail = 0; }

static uint8_t txq_free(void)
{
    return (uint8_t)((txq_tail + TXQ_N - txq_head - 1U) % TXQ_N);
}

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

/* Parameter setzen (Funktion 01) */
static void send_param(uint8_t page, uint8_t nr, uint16_t v)
{
    const uint8_t b[] = { FN_PARAM, 0x00, page, nr, (uint8_t)((v >> 7) & 0x7FU), (uint8_t)(v & 0x7FU) };
    (void)send_kemper(b, sizeof(b));
}

/* Wert als Text anfordern (Funktion 7C, Antwort 3C) */
static void request_render(uint8_t page, uint8_t nr, uint16_t v)
{
    const uint8_t b[] = { FN_REQ_RENDER, 0x00, page, nr, (uint8_t)((v >> 7) & 0x7FU), (uint8_t)(v & 0x7FU) };
    (void)send_kemper(b, sizeof(b));
}

/* Typ und Ein/Aus aller Slots und den Zustand der Fixed Effects anfordern */
static void request_fx(void)
{
    if (txq_free() < 2U * KL_SLOTS + KL_FIXED + 4U) return;   /* naechstes Mal */
    for (uint8_t m = 0; m < KL_SLOTS; m++) {
        request_param(SLOT_PAGE[m], FX_TYPE);
        request_param(SLOT_PAGE[m], FX_ON);
    }
    for (uint8_t f = 0; f < KL_FIXED; f++) request_param(KP_FIXED_PAGE, kp_fixed_nr((kp_fixed_t)f));
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


static void request_rig_name(void)
{
    request_string(PAGE_STRINGS, STR_RIG_NAME);
    if (name_pending < 255U) name_pending++;
    t_name_req = HAL_GetTick();
}

static void request_rig_details(void)
{
    request_string(PAGE_STRINGS, STR_AMP_NAME);
    request_string(PAGE_STRINGS, STR_CAB_NAME);
    request_param(PAGE_RIG, RIG_TEMPO);
    request_param(PAGE_RIG, RIG_TEMPO_ENABLE);
    request_param(PAGE_RIG, RIG_TRANSPOSE);
}

/* Program Change (+ CC 32) an den Kemper: Rig an Bank/Platz laden */
static void send_load_rig(uint16_t idx)
{
    const uint8_t cc[] = { (uint8_t)(0xB0U | MIDI_CH), CC_BANK_LSB, (uint8_t)(idx / 128U) };
    const uint8_t pc[] = { (uint8_t)(0xC0U | MIDI_CH), (uint8_t)(idx % 128U) };
    (void)txq_push_raw(cc, sizeof(cc));
    (void)txq_push_raw(pc, sizeof(pc));
}

static void request_poll(void)
{
    request_rig_name();
    request_string(PAGE_STRINGS, STR_AMP_NAME);
    request_string(PAGE_STRINGS, STR_CAB_NAME);
    request_param(PAGE_RIG, RIG_TEMPO);
    request_param(PAGE_RIG, RIG_TEMPO_ENABLE);
    request_param(PAGE_RIG, RIG_TRANSPOSE);
}

/* Bank des geladenen Rigs erfragen (siehe oben): eine Bank vor und zurueck */
static void send_pos_probe(void)
{
    const uint8_t up[]   = { (uint8_t)(0xB0U | MIDI_CH), CC_BANK_UP, 0x00 };
    const uint8_t down[] = { (uint8_t)(0xB0U | MIDI_CH), CC_BANK_DOWN, 0x00 };
    (void)txq_push_raw(up, sizeof(up));
    (void)txq_push_raw(down, sizeof(down));
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

static void pump_tx(void)
{
    if (txq_head == txq_tail || !usbd_midi_tx_free()) return;
    uint8_t ev[USBD_MIDI_PKT_MAX];
    const uint8_t * d = txq[txq_tail].d;
    uint16_t n;
    if (d[0] == 0xF0U) {
        n = sysex_to_events(d, txq[txq_tail].len, ev);
    } else {                                             /* Kanalnachricht */
        ev[0] = (uint8_t)(d[0] >> 4);                    /* CIN = Statusnibble */
        ev[1] = d[0];
        ev[2] = d[1];
        ev[3] = txq[txq_tail].len > 2U ? d[2] : 0x00U;
        n = 4;
    }
    if (usbd_midi_send(ev, n)) {
        txq_tail = (uint8_t)((txq_tail + 1U) % TXQ_N);
        w.tx_messages++;
    }
}

/* ------------------------------------------------------------------------
 * Aktuelles Rig
 * --------------------------------------------------------------------- */
/* Anderes Rig geladen: alte Details verwerfen und neu anfordern */
static void rig_switched(void)
{
    w.amp_name[0] = w.cab_name[0] = '\0';
    w.tempo_valid = false;
    w.transpose_valid = false;
    w.slot_type_valid = w.slot_on_valid = 0;
    memset(w.slot_name, 0, sizeof(w.slot_name));
    w.watch_valid = 0;
    w.watch_raw_valid = 0;
    w.rig_seq++;
    changed();
    request_rig_details();
    request_fx();
    t_last_fx_poll = HAL_GetTick();
}

/* Rig an bekannter Position geladen (PC vom Kemper oder Rig-Wahl am Display) */
static void rig_moved(uint16_t idx, uint32_t now)
{
    if (idx >= KP_BANKS_MAX * KP_RIGS_PER_BANK) return;
    w.rig_bank = (uint8_t)(idx / KP_RIGS_PER_BANK);
    w.rig_slot = (uint8_t)(idx % KP_RIGS_PER_BANK);
    w.rig_pos_valid = w.rig_bank_valid = true;
    w.rig_name[0] = '\0';                /* Name gehoert zum alten Rig */
    name_skip = name_pending;            /* noch offene Antworten verwerfen */
    t_last_rig_poll = now;
    request_rig_name();
    rig_switched();
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
        /* Testdaten holen */
        request_identity();
        request_poll();
        request_fx();
        t_last_poll = t_last_rig_poll = t_last_fx_poll = now;
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

/* ------------------------------------------------------------------------
 * Effekte
 * --------------------------------------------------------------------- */
static int8_t slot_of_page(uint8_t page)
{
    for (uint8_t m = 0; m < KL_SLOTS; m++) if (SLOT_PAGE[m] == page) return (int8_t)m;
    return -1;
}

static int8_t watch_index(uint8_t page, uint8_t nr)
{
    if (!w.watch_kind || page != watch_page) return -1;
    for (uint8_t k = 0; k < w.watch_n; k++) if (watch_nr[k] == nr) return (int8_t)k;
    return -1;
}

/* Liste der beobachteten Einstellungen (neu) aufbauen */
static void watch_setup(void)
{
    const kp_fx_param_t * list = NULL;
    uint8_t n = 0;
    w.watch_kind = (uint8_t)(watch_cur >> 8);
    w.watch_idx = (uint8_t)(watch_cur & 0xFFU);
    if (w.watch_kind == KL_WATCH_SLOT && w.watch_idx < KL_SLOTS) {
        watch_page = SLOT_PAGE[w.watch_idx];
        watch_for_type = w.slot_type[w.watch_idx];
        watch_typed = (w.slot_type_valid & (1U << w.watch_idx)) != 0U;
        if (watch_typed) n = kp_fx_params(watch_for_type, &list);
    } else if (w.watch_kind == KL_WATCH_FIXED && w.watch_idx < KL_FIXED) {
        watch_page = KP_FIXED_PAGE;
        n = kp_fixed_params((kp_fixed_t)w.watch_idx, &list);
    } else {
        w.watch_kind = KL_WATCH_NONE;
    }
    if (n > KL_WATCH_MAX) n = KL_WATCH_MAX;
    for (uint8_t k = 0; k < n; k++) {
        watch_nr[k] = list[k].nr;
        w.watch_name[k] = list[k].name;
        w.watch_steps[k] = list[k].steps;
    }
    w.watch_n = n;
    w.watch_valid = 0;
    w.watch_raw_valid = 0;
    t_last_watch = HAL_GetTick() - WATCH_POLL_MS;      /* sofort lesen */
    changed();
}

static void watch_poll(void)
{
    if (txq_free() < w.watch_n + 4U) return;
    for (uint8_t k = 0; k < w.watch_n; k++) request_param(watch_page, watch_nr[k]);
}

static void on_value(uint8_t page, uint8_t nr, uint16_t v)
{
    const int8_t m = slot_of_page(page);
    if (m >= 0 && nr == FX_TYPE) {
        const uint8_t bit = (uint8_t)(1U << m);
        if (!(w.slot_type_valid & bit) || w.slot_type[m] != v) {
            w.slot_type[m] = v;
            w.slot_type_valid |= bit;
            w.slot_name[m][0] = '\0';
            changed();
            if (v) request_render(page, FX_TYPE, v);      /* Name des Effekts */
            if (w.watch_kind == KL_WATCH_SLOT && w.watch_idx == (uint8_t)m) watch_setup();
        } else if (v && !w.slot_name[m][0]) {
            request_render(page, FX_TYPE, v);              /* Name fehlt noch */
        }
    } else if (m >= 0 && nr == FX_ON) {
        const uint8_t bit = (uint8_t)(1U << m);
        const uint8_t on = v ? bit : 0U;
        if (!(w.slot_on_valid & bit) || (w.slot_on & bit) != on) {
            w.slot_on = (uint8_t)((w.slot_on & ~bit) | on);
            w.slot_on_valid |= bit;
            changed();
        }
    } else if (page == KP_FIXED_PAGE) {
        for (uint8_t f = 0; f < KL_FIXED; f++) {
            if (kp_fixed_nr((kp_fixed_t)f) != nr) continue;
            const uint8_t bit = (uint8_t)(1U << f);
            const uint8_t on = v ? bit : 0U;
            if (!(w.fixed_valid & bit) || (w.fixed_on & bit) != on) {
                w.fixed_on = (uint8_t)((w.fixed_on & ~bit) | on);
                w.fixed_valid |= bit;
                changed();
            }
        }
    }

    /* Einstellung im offenen Dialog: bei neuem Wert den Text holen */
    const int8_t k = watch_index(page, nr);
    if (k >= 0) {
        const uint16_t bit = (uint16_t)(1U << k);
        if (!(w.watch_raw_valid & bit) || w.watch_raw[k] != v || !(w.watch_valid & bit)) {
            w.watch_raw[k] = v;
            w.watch_raw_valid |= bit;
            changed();
            request_render(page, nr, v);
        }
    }
}

/* Antwort 3C: <Flags> <Seite> <Nr> <Wert MSB> <Wert LSB> <Text> 00 */
static void on_render(const uint8_t * d, uint16_t n)
{
    if (n < 14U) return;
    const uint8_t page = d[8], nr = d[9];
    const uint16_t v = (uint16_t)(((uint16_t)d[10] << 7) | d[11]);
    char tmp[KL_STR_LEN];
    sysex_str(tmp, &d[12], d + n);

    const int8_t m = slot_of_page(page);
    if (m >= 0 && nr == FX_TYPE && (w.slot_type_valid & (1U << m)) && w.slot_type[m] == v) {
        char nm[KL_NAME_LEN];
        strncpy(nm, tmp, KL_NAME_LEN - 1U);
        nm[KL_NAME_LEN - 1U] = '\0';
        if (strcmp(nm, w.slot_name[m]) != 0) {
            strcpy(w.slot_name[m], nm);
            changed();
        }
    }
    const int8_t k = watch_index(page, nr);
    if (k >= 0 && (w.watch_raw_valid & (1U << k)) && w.watch_raw[k] == v) {
        char * dst = w.watch_txt[k];
        if (!(w.watch_valid & (1U << k)) || strncmp(dst, tmp, KL_VAL_LEN - 1U) != 0) {
            strncpy(dst, tmp, KL_VAL_LEN - 1U);
            dst[KL_VAL_LEN - 1U] = '\0';
            w.watch_valid |= (uint16_t)(1U << k);
            changed();
        }
    }
}

/* Aenderungen vom Display an den Kemper; danach den Wert zur Bestaetigung
 * neu lesen (bis dahin gilt er als unbekannt) */
static void process_sets(void)
{
    while (set_tail != set_head && txq_free() >= 4U) {
        __DMB();
        const uint8_t page = setq[set_tail].page, nr = setq[set_tail].nr;
        const uint16_t v = setq[set_tail].value;
        set_tail = (uint8_t)((set_tail + 1U) % SETQ_N);

        send_param(page, nr, v);
        request_param(page, nr);
        const int8_t m = slot_of_page(page);
        if (m >= 0 && nr == FX_TYPE) w.slot_type_valid &= (uint8_t)~(1U << m);
        if (m >= 0 && nr == FX_ON)   w.slot_on_valid &= (uint8_t)~(1U << m);
        if (m >= 0 && nr == FX_TYPE) request_param(page, FX_ON);   /* Typwechsel kann Ein/Aus aendern */
        if (page == KP_FIXED_PAGE) {
            for (uint8_t f = 0; f < KL_FIXED; f++)
                if (kp_fixed_nr((kp_fixed_t)f) == nr) w.fixed_valid &= (uint8_t)~(1U << f);
        }
        if (page == PAGE_RIG && nr == RIG_TRANSPOSE) w.transpose_valid = false;
        changed();
    }
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
        sysex_str(tmp, &d[10], d + n);
        if (nr == STR_RIG_NAME) {
            if (name_pending) name_pending--;
            if (name_skip) {
                name_skip--;                 /* Antwort von vor dem Positionswechsel */
            } else {
                const bool was_empty = w.rig_name[0] == '\0';
                if (set_str(w.rig_name, tmp) && !was_empty) {
                    /* Anderes Rig am Kemper; wo es liegt, sagt erst der PC */
                    w.rig_pos_valid = w.rig_bank_valid = false;
                    rig_switched();
                }
            }
        }
        else if (nr == STR_AMP_NAME) set_str(w.amp_name, tmp);
        else if (nr == STR_CAB_NAME) set_str(w.cab_name, tmp);
    } else if (fn == FN_RENDER) {
        on_render(d, n);
    } else if (fn == FN_PARAM && page != PAGE_RIG && n >= 13U) {
        on_value(page, nr, (uint16_t)(((uint16_t)d[10] << 7) | d[11]));
    } else if (fn == FN_PARAM && page == PAGE_RIG && n >= 13U) {
        uint16_t v = (uint16_t)(((uint16_t)d[10] << 7) | d[11]);
        if (nr == RIG_TEMPO) {
            uint16_t bpm = (uint16_t)((v + 32U) / 64U);
            if (!w.tempo_valid || bpm != w.tempo_bpm) {
                w.tempo_bpm = bpm;
                w.tempo_valid = true;
                changed();
            }
        } else if (nr == RIG_TEMPO_ENABLE) {
            bool on = v != 0U;
            if (on != w.tempo_on) {
                w.tempo_on = on;
                changed();
            }
        } else if (nr == RIG_TRANSPOSE) {
            int8_t t = (int8_t)((int16_t)v - (int16_t)TRANSPOSE_ZERO);
            if (!w.transpose_valid || t != w.transpose) {
                w.transpose = t;
                w.transpose_valid = true;
                changed();
            }
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
        case 0xB:                        /* Control Change */
            w.rx_messages++;
            if (ev[i + 2U] == CC_BANK_LSB) {
                bank_lsb = ev[i + 3U] & 0x7FU;
            } else if (ev[i + 2U] == CC_BANK_PRESELECT && w.kemper == KL_KEMPER_OK) {
                /* Antwort auf die Bank-Abfrage: die letzte Meldung zaehlt */
                const uint8_t b = ev[i + 3U] & 0x7FU;
                if (!w.rig_bank_valid || w.rig_bank != b) {
                    w.rig_bank = b;
                    w.rig_bank_valid = true;
                    w.rig_pos_valid = false;     /* Platz kommt ggf. mit CC 50-54 */
                    changed();
                }
            } else if (ev[i + 2U] >= CC_SLOT_FIRST && ev[i + 2U] < CC_SLOT_FIRST + KP_RIGS_PER_BANK &&
                       w.rig_bank_valid && w.kemper == KL_KEMPER_OK) {
                w.rig_slot = (uint8_t)(ev[i + 2U] - CC_SLOT_FIRST);
                w.rig_pos_valid = true;
                changed();
            }
            break;
        case 0xC:                        /* Program Change: Kemper hat Rig gewechselt */
            w.rx_messages++;
            if (w.kemper == KL_KEMPER_OK)
                rig_moved((uint16_t)(bank_lsb * 128U + (ev[i + 2U] & 0x7FU)), HAL_GetTick());
            break;
        case 0x8: case 0x9: case 0xA: case 0xD: case 0xE:
            w.rx_messages++;             /* Kanalnachrichten: spaeter auswerten */
            break;
        default:
            break;
        }
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
            txq_reset();
            sx_on = false;
        }
    }

    if (us != KL_USB_READY) {
        load_req = LOAD_NONE;
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
            changed();
        } else {
            if (now - t_last_beacon >= BEACON_REFRESH_MS) send_beacon(false);
            uint16_t req = load_req;
            if (req != LOAD_NONE) {
                load_req = LOAD_NONE;
                send_load_rig(req);
                rig_moved(req, now);
            }
            if (!pos_probed && !w.rig_bank_valid && now - w.connected_since_ms >= POS_PROBE_DELAY_MS) {
                pos_probed = true;               /* einmal je Verbindung */
                send_pos_probe();
            }
            if (now - t_last_poll >= POLL_PERIOD_MS) {
                t_last_poll = now;
                t_last_rig_poll = now;
                request_poll();
            } else if (now - t_last_rig_poll >= RIG_POLL_MS) {
                t_last_rig_poll = now;
                request_rig_name();
            }
            /* Verlorene Antworten nicht ewig mitzaehlen */
            if (name_pending && now - t_name_req > NAME_ANSWER_MS) name_pending = name_skip = 0;

            /* Effekte */
            process_sets();
            if (now - t_last_fx_poll >= FX_POLL_MS) {
                t_last_fx_poll = now;
                request_fx();
            }
            const uint16_t wr = watch_req;
            if (wr != watch_cur) {
                watch_cur = wr;
                watch_setup();
            } else if (w.watch_kind == KL_WATCH_SLOT &&
                       ((w.slot_type_valid >> w.watch_idx) & 1U) &&
                       (!watch_typed || w.slot_type[w.watch_idx] != watch_for_type)) {
                watch_setup();               /* Typ jetzt bekannt oder geaendert */
            }
            if (w.watch_kind && now - t_last_watch >= WATCH_POLL_MS) {
                t_last_watch = now;
                watch_poll();
            }
        }
        break;
    }
    /* Rig-Wahl ohne Verbindung verfaellt (der Kemper meldet sein Rig selbst) */
    if (w.kemper != KL_KEMPER_OK) load_req = LOAD_NONE;
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

/* ------------------------------------------------------------------------
 * Anbindung an das Modell (nur im LVGL-Task)
 * --------------------------------------------------------------------- */
void kemper_link_watch(kl_watch_t kind, uint8_t idx)
{
    watch_req = kind == KL_WATCH_NONE ? 0U : (uint16_t)(((uint16_t)kind << 8) | idx);
}

/* Parameter-Aenderung in die Warteschlange zum Link-Task */
static void set_push(uint8_t page, uint8_t nr, uint16_t v)
{
    uint8_t next = (uint8_t)((set_head + 1U) % SETQ_N);
    if (next == set_tail) return;                      /* voll: verwerfen */
    setq[set_head].page = page;
    setq[set_head].nr = nr;
    setq[set_head].value = v;
    __DMB();
    set_head = next;
}

/* Ersetzen die schwachen Fassungen in kemper_player.c (LVGL-Task) */
void kp_link_module_on(kp_mod_t m, bool on)
{
    if (m < KL_SLOTS && kp_connected()) set_push(SLOT_PAGE[m], FX_ON, on ? 1U : 0U);
}

void kp_link_module_type(kp_mod_t m, uint16_t type)
{
    if (m < KL_SLOTS && kp_connected()) set_push(SLOT_PAGE[m], FX_TYPE, type);
}

void kp_link_fixed(kp_fixed_t f, bool on)
{
    if (f < KL_FIXED && kp_connected()) set_push(KP_FIXED_PAGE, kp_fixed_nr(f), on ? 1U : 0U);
}

void kemper_link_set_param(uint8_t page, uint8_t nr, uint16_t value)
{
    if (kp_connected()) set_push(page, nr, value > 16383U ? 16383U : value);
}

void kp_link_param(kp_param_t p, int16_t value)
{
    if (p == KP_P_RIG_TRANSPOSE && kp_connected())
        set_push(PAGE_RIG, RIG_TRANSPOSE, (uint16_t)(value + (int16_t)TRANSPOSE_ZERO));
}

/* Rig-Wahl am Display: der Link-Task schickt sie an den Kemper
 * (ersetzt die schwache Fassung in kemper_player.c) */
void kp_link_load_rig(uint8_t bank, uint8_t slot)
{
    load_req = (uint16_t)(bank * KP_RIGS_PER_BANK + slot);
}

void kemper_link_ui_poll(void)
{
    static kl_info_t i;
    static bool      last_conn;
    static uint32_t  last_gen = 0xFFFFFFFFU;
    static uint32_t  rig_seq;
    static char      rig[KL_STR_LEN];
    static char      amp[KL_STR_LEN], cab[KL_STR_LEN];
    static bool      tempo_valid, tempo_on;
    static uint16_t  tempo_bpm;
    static bool      transpose_valid;
    static int8_t    transpose;
    /* zuletzt ans Modell gemeldete Effektdaten */
    static uint8_t   fx_type_ok, fx_on_ok, fx_on, fix_ok, fix_on;
    static uint16_t  fx_type[KL_SLOTS];
    static char      fx_name[KL_SLOTS][KL_NAME_LEN];

    kemper_link_get_info(&i);
    bool conn = i.kemper == KL_KEMPER_OK;
    if (conn != last_conn) {
        last_conn = conn;
        kp_rx_connected(conn);
    }

    if (i.generation == last_gen) return;
    last_gen = i.generation;

    /* Ohne Verbindung (oder bei Rig-Wechsel) alles vergessen, damit die
     * Daten danach wieder vollstaendig ins Modell gehen */
    if (!conn || i.rig_seq != rig_seq) {
        rig_seq = i.rig_seq;
        rig[0] = amp[0] = cab[0] = '\0';
        tempo_valid = false;
        transpose_valid = false;
        fx_type_ok = fx_on_ok = fix_ok = 0;
        memset(fx_name, 0, sizeof(fx_name));
        /* Kemper hat an bekannter Stelle ein anderes Rig geladen */
        if (conn && i.rig_pos_valid && (!kp_slot_known() || i.rig_bank != kp_bank() || i.rig_slot != kp_slot()))
            kp_rx_rig_loaded(i.rig_bank, i.rig_slot);
    }
    if (!conn) {
        kp_rx_link_info();
        return;
    }

    /* Aktuell geladenes Rig */
    if (i.rig_name[0] && strcmp(rig, i.rig_name) != 0) {
        strcpy(rig, i.rig_name);
        if (i.rig_pos_valid && i.rig_bank == kp_bank() && i.rig_slot == kp_slot())
            kp_rx_rig_name_at(i.rig_bank, i.rig_slot, rig);
        else
            kp_rx_rig_changed(rig);      /* Platz unbekannt: nur das Rig selbst */
        amp[0] = cab[0] = '\0';         /* Modell hat die Details geleert */
        tempo_valid = false;
        transpose_valid = false;
    }

    /* Position des geladenen Rigs nachtraeglich bekannt (Bank-Abfrage) */
    if (i.rig_pos_valid) {
        if (!kp_slot_known() || i.rig_bank != kp_bank() || i.rig_slot != kp_slot())
            kp_rx_rig_position(i.rig_bank, i.rig_slot);
    } else if (i.rig_bank_valid && (!kp_bank_known() || i.rig_bank != kp_bank())) {
        kp_rx_bank(i.rig_bank);
    }

    if ((i.amp_name[0] || i.cab_name[0]) &&
        (strcmp(amp, i.amp_name) != 0 || strcmp(cab, i.cab_name) != 0)) {
        strcpy(amp, i.amp_name);
        strcpy(cab, i.cab_name);
        kp_rx_stack(amp, cab);
    }
    if (i.tempo_valid && (!tempo_valid || i.tempo_bpm != tempo_bpm || i.tempo_on != tempo_on)) {
        tempo_valid = true;
        tempo_bpm = i.tempo_bpm;
        tempo_on = i.tempo_on;
        kp_rx_tempo(tempo_on, tempo_bpm);
    }
    if (i.transpose_valid && (!transpose_valid || i.transpose != transpose)) {
        transpose_valid = true;
        transpose = i.transpose;
        kp_rx_param(KP_P_RIG_TRANSPOSE, transpose);
    }

    /* Effekt-Slots: nur gemeldete (gueltige) Werte uebernehmen. Ein ungueltiger
     * Wert (z. B. nach einer Aenderung am Display) wird beim naechsten
     * gueltigen Wert in jedem Fall neu gemeldet. */
    for (uint8_t m = 0; m < KL_SLOTS; m++) {
        const uint8_t bit = (uint8_t)(1U << m);
        if (!(i.slot_type_valid & bit)) {
            fx_type_ok &= (uint8_t)~bit;
        } else if (!(fx_type_ok & bit) || fx_type[m] != i.slot_type[m]) {
            fx_type_ok |= bit;
            fx_type[m] = i.slot_type[m];
            fx_name[m][0] = '\0';
            kp_rx_module_type((kp_mod_t)m, fx_type[m]);
        }
        if ((fx_type_ok & bit) && i.slot_name[m][0] && strcmp(fx_name[m], i.slot_name[m]) != 0) {
            strcpy(fx_name[m], i.slot_name[m]);
            kp_rx_module_name((kp_mod_t)m, fx_name[m]);
        }
        if (!(i.slot_on_valid & bit)) {
            fx_on_ok &= (uint8_t)~bit;
        } else if (!(fx_on_ok & bit) || (fx_on & bit) != (i.slot_on & bit)) {
            fx_on_ok |= bit;
            fx_on = (uint8_t)((fx_on & ~bit) | (i.slot_on & bit));
            kp_rx_module_on((kp_mod_t)m, (i.slot_on & bit) != 0U);
        }
    }

    /* Fixed Effects */
    for (uint8_t f = 0; f < KL_FIXED; f++) {
        const uint8_t bit = (uint8_t)(1U << f);
        if (!(i.fixed_valid & bit)) {
            fix_ok &= (uint8_t)~bit;
        } else if (!(fix_ok & bit) || (fix_on & bit) != (i.fixed_on & bit)) {
            fix_ok |= bit;
            fix_on = (uint8_t)((fix_on & ~bit) | (i.fixed_on & bit));
            kp_rx_fixed((kp_fixed_t)f, (i.fixed_on & bit) ? 1 : 0);
        }
    }
    kp_rx_link_info();
}
