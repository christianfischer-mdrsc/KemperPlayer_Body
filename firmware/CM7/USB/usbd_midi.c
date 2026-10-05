/**
 * @file usbd_midi.c
 * USB-MIDI-Geraeteklasse (siehe usbd_midi.h).
 *
 * Konfigurationsdeskriptor nach "Universal Serial Bus Device Class
 * Definition for MIDI Devices 1.0", Anhang B: Audio-Control-Interface,
 * MIDIStreaming-Interface mit je einer eingebetteten und externen
 * MIDI-IN- und MIDI-OUT-Buchse, zwei Bulk-Endpunkte.
 */
#include <string.h>
#include "usbd_midi.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
#include "usbd_conf.h"

#define CFG_DESC_LEN          101U
#define TX_TIMEOUT_MS         200U
#define RX_RING_SIZE          1024U          /* Bytes, Zweierpotenz */

static uint8_t midi_init(USBD_HandleTypeDef * pdev, uint8_t cfgidx);
static uint8_t midi_deinit(USBD_HandleTypeDef * pdev, uint8_t cfgidx);
static uint8_t midi_setup(USBD_HandleTypeDef * pdev, USBD_SetupReqTypedef * req);
static uint8_t midi_data_in(USBD_HandleTypeDef * pdev, uint8_t epnum);
static uint8_t midi_data_out(USBD_HandleTypeDef * pdev, uint8_t epnum);
static uint8_t * midi_cfg_desc(uint16_t * length);
static uint8_t * midi_qualifier_desc(uint16_t * length);

USBD_ClassTypeDef USBD_MIDI_Class = {
    midi_init,
    midi_deinit,
    midi_setup,
    NULL,                 /* EP0_TxSent */
    NULL,                 /* EP0_RxReady */
    midi_data_in,
    midi_data_out,
    NULL,                 /* SOF */
    NULL,
    NULL,
    midi_cfg_desc,        /* HS (wird nicht benutzt, Full Speed) */
    midi_cfg_desc,        /* FS */
    midi_cfg_desc,        /* Other Speed */
    midi_qualifier_desc,
};

static uint8_t s_cfg[CFG_DESC_LEN] __attribute__((aligned(4))) = {
    /* Konfiguration */
    0x09, USB_DESC_TYPE_CONFIGURATION, LOBYTE(CFG_DESC_LEN), HIBYTE(CFG_DESC_LEN),
    0x02,                 /* 2 Interfaces */
    0x01,                 /* bConfigurationValue */
    0x00,
    0xC0,                 /* selbstversorgt */
    0x32,                 /* 100 mA (nur Angabe, das Display hat ein eigenes Netzteil) */

    /* Interface 0: Audio Control */
    0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    /* CS-Header Audio Control: bcdADC 1.00, Laenge 9, 1 Streaming-Interface (Nr. 1) */
    0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,

    /* Interface 1: MIDIStreaming, 2 Endpunkte */
    0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
    /* CS-Header MIDIStreaming: bcdMSC 1.00, Gesamtlaenge 65 */
    0x07, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,
    /* MIDI IN Jack, eingebettet, ID 1 */
    0x06, 0x24, 0x02, 0x01, 0x01, 0x00,
    /* MIDI IN Jack, extern, ID 2 */
    0x06, 0x24, 0x02, 0x02, 0x02, 0x00,
    /* MIDI OUT Jack, eingebettet, ID 3, Quelle Jack 2 Pin 1 */
    0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,
    /* MIDI OUT Jack, extern, ID 4, Quelle Jack 1 Pin 1 */
    0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,

    /* Bulk OUT 0x01 (Kemper -> Display) */
    0x09, USB_DESC_TYPE_ENDPOINT, USBD_MIDI_EP_OUT, 0x02,
    LOBYTE(USBD_MIDI_PKT_MAX), HIBYTE(USBD_MIDI_PKT_MAX), 0x00, 0x00, 0x00,
    /* CS-Endpunkt: 1 eingebettete Buchse, Jack 1 */
    0x05, 0x25, 0x01, 0x01, 0x01,

    /* Bulk IN 0x81 (Display -> Kemper) */
    0x09, USB_DESC_TYPE_ENDPOINT, USBD_MIDI_EP_IN, 0x02,
    LOBYTE(USBD_MIDI_PKT_MAX), HIBYTE(USBD_MIDI_PKT_MAX), 0x00, 0x00, 0x00,
    /* CS-Endpunkt: 1 eingebettete Buchse, Jack 3 */
    0x05, 0x25, 0x01, 0x01, 0x03,
};

static uint8_t s_qualifier[USB_LEN_DEV_QUALIFIER_DESC] __attribute__((aligned(4))) = {
    USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

static struct {
    USBD_HandleTypeDef * pdev;
    volatile bool        open;        /* Endpunkte offen (konfiguriert) */
    volatile bool        tx_busy;
    uint32_t             tx_start;
    volatile uint32_t    errors;
    uint8_t              rx_pkt[USBD_MIDI_PKT_MAX] __attribute__((aligned(4)));
    uint8_t              tx_buf[USBD_MIDI_PKT_MAX] __attribute__((aligned(4)));
    uint8_t              ring[RX_RING_SIZE];
    volatile uint32_t    head;        /* schreibt nur der Interrupt */
    volatile uint32_t    tail;        /* schreibt nur der Task */
} m;

/* ------------------------------------------------------------------------
 * Klassen-Callbacks (Interrupt-Kontext)
 * --------------------------------------------------------------------- */
static uint8_t midi_init(USBD_HandleTypeDef * pdev, uint8_t cfgidx)
{
    (void)cfgidx;
    m.pdev = pdev;
    (void)USBD_LL_OpenEP(pdev, USBD_MIDI_EP_IN, USBD_EP_TYPE_BULK, USBD_MIDI_PKT_MAX);
    pdev->ep_in[USBD_MIDI_EP_IN & 0x0FU].is_used = 1U;
    (void)USBD_LL_OpenEP(pdev, USBD_MIDI_EP_OUT, USBD_EP_TYPE_BULK, USBD_MIDI_PKT_MAX);
    pdev->ep_out[USBD_MIDI_EP_OUT & 0x0FU].is_used = 1U;
    (void)USBD_LL_PrepareReceive(pdev, USBD_MIDI_EP_OUT, m.rx_pkt, USBD_MIDI_PKT_MAX);
    m.tx_busy = false;
    m.open = true;
    return (uint8_t)USBD_OK;
}

static uint8_t midi_deinit(USBD_HandleTypeDef * pdev, uint8_t cfgidx)
{
    (void)cfgidx;
    m.open = false;
    (void)USBD_LL_CloseEP(pdev, USBD_MIDI_EP_IN);
    pdev->ep_in[USBD_MIDI_EP_IN & 0x0FU].is_used = 0U;
    (void)USBD_LL_CloseEP(pdev, USBD_MIDI_EP_OUT);
    pdev->ep_out[USBD_MIDI_EP_OUT & 0x0FU].is_used = 0U;
    m.tx_busy = false;
    return (uint8_t)USBD_OK;
}

static uint8_t midi_setup(USBD_HandleTypeDef * pdev, USBD_SetupReqTypedef * req)
{
    static uint8_t alt = 0;
    static uint8_t status[2] = { 0, 0 };

    if ((req->bmRequest & USB_REQ_TYPE_MASK) != USB_REQ_TYPE_STANDARD) {
        /* MIDI 1.0 kennt keine Klassen-Anfragen, die wir brauchen */
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;
    }
    switch (req->bRequest) {
    case USB_REQ_GET_STATUS:
        (void)USBD_CtlSendData(pdev, status, 2U);
        break;
    case USB_REQ_GET_INTERFACE:
        (void)USBD_CtlSendData(pdev, &alt, 1U);
        break;
    case USB_REQ_SET_INTERFACE:
        if ((uint8_t)req->wValue != 0U) {
            USBD_CtlError(pdev, req);
            return (uint8_t)USBD_FAIL;
        }
        break;
    case USB_REQ_CLEAR_FEATURE:
        break;
    default:
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;
    }
    return (uint8_t)USBD_OK;
}

static uint8_t midi_data_in(USBD_HandleTypeDef * pdev, uint8_t epnum)
{
    (void)pdev;
    if ((epnum | 0x80U) == USBD_MIDI_EP_IN) m.tx_busy = false;
    return (uint8_t)USBD_OK;
}

static uint8_t midi_data_out(USBD_HandleTypeDef * pdev, uint8_t epnum)
{
    if (epnum != (USBD_MIDI_EP_OUT & 0x0FU)) return (uint8_t)USBD_OK;
    uint32_t n = USBD_LL_GetRxDataSize(pdev, epnum) & ~3UL;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t next = (m.head + 1U) & (RX_RING_SIZE - 1U);
        if (next == m.tail) {                 /* voll: Rest verwerfen */
            m.errors++;
            break;
        }
        m.ring[m.head] = m.rx_pkt[i];
        __DMB();
        m.head = next;
    }
    (void)USBD_LL_PrepareReceive(pdev, USBD_MIDI_EP_OUT, m.rx_pkt, USBD_MIDI_PKT_MAX);
    return (uint8_t)USBD_OK;
}

static uint8_t * midi_cfg_desc(uint16_t * length)
{
    *length = sizeof(s_cfg);
    return s_cfg;
}

static uint8_t * midi_qualifier_desc(uint16_t * length)
{
    *length = sizeof(s_qualifier);
    return s_qualifier;
}

/* ------------------------------------------------------------------------
 * Funktionen fuer den Link-Task
 * --------------------------------------------------------------------- */
bool usbd_midi_ready(void)
{
    return m.open && m.pdev && m.pdev->dev_state == USBD_STATE_CONFIGURED;
}

bool usbd_midi_tx_free(void)
{
    return usbd_midi_ready() && !m.tx_busy;
}

bool usbd_midi_send(const uint8_t * events, uint16_t len)
{
    if (!usbd_midi_tx_free() || len == 0U || (len & 3U) || len > USBD_MIDI_PKT_MAX) return false;
    memcpy(m.tx_buf, events, len);
    m.tx_busy = true;
    m.tx_start = HAL_GetTick();
    if (USBD_LL_Transmit(m.pdev, USBD_MIDI_EP_IN, m.tx_buf, len) != USBD_OK) {
        m.tx_busy = false;
        m.errors++;
        return false;
    }
    return true;
}

uint16_t usbd_midi_read(uint8_t * buf, uint16_t max)
{
    uint16_t n = 0;
    max &= (uint16_t)~3U;
    while (n < max) {
        uint32_t t = m.tail;
        if (t == m.head) break;
        buf[n++] = m.ring[t];
        __DMB();
        m.tail = (t + 1U) & (RX_RING_SIZE - 1U);
    }
    return n;
}

void usbd_midi_poll(uint32_t now_ms)
{
    /* Holt der Kemper ein Paket nicht ab (z. B. Kabel gezogen, ohne dass
     * der Bus in Suspend ging), nicht ewig blockieren */
    if (m.tx_busy && now_ms - m.tx_start > TX_TIMEOUT_MS) {
        if (m.pdev) (void)USBD_LL_FlushEP(m.pdev, USBD_MIDI_EP_IN);
        m.tx_busy = false;
        m.errors++;
    }
}

uint32_t usbd_midi_errors(void) { return m.errors; }
