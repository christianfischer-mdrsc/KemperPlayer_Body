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
#include "usbh_core.h"
#include "usbh_conf.h"
#include "usbh_midi.h"
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
#define PUBLISH_PERIOD_MS      100U

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

#define TASK_STACK_WORDS       1024U

/* ------------------------------------------------------------------------
 * Zustand (nur im Link-Task veraendert)
 * --------------------------------------------------------------------- */
static USBH_HandleTypeDef hUsbHost;
static kl_info_t w;            /* Arbeitskopie des Tasks */
static kl_info_t pub;          /* veroeffentlichte Kopie (unter Sperre) */
static uint32_t  pub_time;
static uint32_t  pub_gen = 0xFFFFFFFFU;

static uint32_t  t_last_init, t_last_beacon, t_last_poll;

/* Sendewarteschlange fuer SysEx-Nachrichten */
#define TXQ_N    12U
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
    changed();
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

static void request_poll(void)
{
    request_string(PAGE_STRINGS, STR_RIG_NAME);
    request_string(PAGE_STRINGS, STR_AMP_NAME);
    request_string(PAGE_STRINGS, STR_CAB_NAME);
    request_param(PAGE_RIG, RIG_TEMPO);
    request_param(PAGE_RIG, RIG_TEMPO_ENABLE);
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
    if (txq_head == txq_tail || !usbh_midi_tx_free()) return;
    uint8_t ev[USBH_MIDI_PKT_MAX];
    uint16_t n = sysex_to_events(txq[txq_tail].d, txq[txq_tail].len, ev);
    if (usbh_midi_send(ev, n)) {
        txq_tail = (uint8_t)((txq_tail + 1U) % TXQ_N);
        w.tx_messages++;
    }
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
        sysex_str(tmp, &d[10], d + n);
        if      (nr == STR_RIG_NAME) set_str(w.rig_name, tmp);
        else if (nr == STR_AMP_NAME) set_str(w.amp_name, tmp);
        else if (nr == STR_CAB_NAME) set_str(w.cab_name, tmp);
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

/* Aufruf aus usbh_midi.c (im Link-Task) */
void usbh_midi_receive(const uint8_t * ev, uint16_t len)
{
    for (uint16_t i = 0; i + 3U < len; i += 4U) {
        const uint8_t cin = ev[i] & 0x0FU;
        switch (cin) {
        case 0x4: sx_add(ev[i + 1U]); sx_add(ev[i + 2U]); sx_add(ev[i + 3U]); break;
        case 0x5: sx_add(ev[i + 1U]); break;
        case 0x6: sx_add(ev[i + 1U]); sx_add(ev[i + 2U]); break;
        case 0x7: sx_add(ev[i + 1U]); sx_add(ev[i + 2U]); sx_add(ev[i + 3U]); break;
        case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE:
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
static void usb_user_cb(USBH_HandleTypeDef * phost, uint8_t id)
{
    (void)phost;
    (void)id;   /* Zustand wird in usb_state() direkt aus dem Host gelesen */
}

static kl_usb_state_t usb_state(void)
{
    if (usbh_midi_ready()) return KL_USB_READY;
    switch (hUsbHost.gState) {
    case HOST_IDLE:
    case HOST_DEV_WAIT_FOR_ATTACHMENT:
    case HOST_DEV_DISCONNECTED:
        return hUsbHost.device.is_connected ? KL_USB_ENUM : KL_USB_NONE;
    case HOST_ABORT_STATE:
        return KL_USB_NO_MIDI;
    default:
        return KL_USB_ENUM;
    }
}

static void link_step(uint32_t now)
{
    kl_usb_state_t us = usb_state();
    if (us != w.usb) {
        w.usb = us;
        changed();
        if (us == KL_USB_READY) {
            const usbh_midi_devinfo_t * di = usbh_midi_devinfo();
            set_str(w.manufacturer, di->manufacturer);
            set_str(w.product, di->product);
            set_str(w.serial, di->serial);
            w.vid = di->vid;
            w.pid = di->pid;
            w.bcd_device = di->bcd_device;
        } else {
            if (w.kemper == KL_KEMPER_OK) w.lost_count++;
            w.kemper = KL_KEMPER_NONE;
            if (us == KL_USB_NONE) {
                w.manufacturer[0] = w.product[0] = w.serial[0] = '\0';
                w.vid = w.pid = w.bcd_device = 0;
            }
            clear_kemper_data();
            txq_reset();
            sx_on = false;
        }
    }

    if (us != KL_USB_READY) return;

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
            if (now - t_last_poll >= POLL_PERIOD_MS) {
                t_last_poll = now;
                request_poll();
            }
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

    USBH_Init(&hUsbHost, usb_user_cb, HOST_HS);
    USBH_RegisterClass(&hUsbHost, USBH_MIDI_CLASS);
    USBH_Start(&hUsbHost);                         /* schaltet auch VBUS ein */
    w.vbus_on = true;
    publish(HAL_GetTick(), true);

    for (;;) {
        USBH_Process(&hUsbHost);
        uint32_t now = HAL_GetTick();
        link_step(now);
        bool oc = USBH_LL_OverCurrent() != 0U;
        if (oc != w.overcurrent) {
            w.overcurrent = oc;
            changed();
        }
        w.usb_errors = usbh_midi_errors();
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
    case KL_USB_ENUM:    return "USB-Verbindung wird aufgebaut";
    case KL_USB_NO_MIDI: return "USB-Geraet ohne MIDI gefunden";
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
void kemper_link_ui_poll(void)
{
    static kl_info_t i;
    static bool      last_conn;
    static uint32_t  last_gen = 0xFFFFFFFFU;
    static char      amp[KL_STR_LEN], cab[KL_STR_LEN];
    static bool      tempo_on;
    static uint16_t  tempo_bpm;

    kemper_link_get_info(&i);
    bool conn = i.kemper == KL_KEMPER_OK;
    if (conn != last_conn) {
        last_conn = conn;
        kp_rx_connected(conn);
    }
    if (i.generation == last_gen) return;
    last_gen = i.generation;

    if (conn && (strcmp(amp, i.amp_name) != 0 || strcmp(cab, i.cab_name) != 0)) {
        strcpy(amp, i.amp_name);
        strcpy(cab, i.cab_name);
        kp_rx_stack(amp, cab);
    }
    if (conn && i.tempo_valid && (i.tempo_bpm != tempo_bpm || i.tempo_on != tempo_on)) {
        tempo_bpm = i.tempo_bpm;
        tempo_on = i.tempo_on;
        kp_rx_tempo(tempo_on, tempo_bpm);
    }
    kp_rx_link_info();
}
