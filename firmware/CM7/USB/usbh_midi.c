/**
 * @file usbh_midi.c
 * USB-MIDI-Klasse fuer die ST USB Host Library (siehe usbh_midi.h).
 *
 * Empfang: Der IN-Endpunkt ist laut Standard ein Bulk-Endpunkt. Ohne DMA
 * startet der HAL-Treiber einen Bulk-IN-Kanal nach jedem NAK sofort neu.
 * Ein MIDI-Geraet, das meistens nichts zu senden hat, erzeugt so zehntausende
 * Interrupts pro Sekunde. Deshalb wird der Kanal als Interrupt-Kanal
 * betrieben: Bei Full Speed sieht ein IN-Token auf dem Bus fuer Bulk und
 * Interrupt gleich aus, der Treiber haelt den Kanal nach einem NAK aber an.
 * Die Klasse fragt dann einmal pro Millisekunde (USB-Frame) neu an.
 */
#include <string.h>
#include "usbh_midi.h"
#include "usbh_ioreq.h"
#include "usbh_pipes.h"
#include "usbh_ctlreq.h"

#define AUDIO_CLASS          0x01U
#define MIDISTREAMING_SUB    0x03U

typedef enum { RX_IDLE = 0, RX_WAIT } rx_state_t;
typedef enum { TX_IDLE = 0, TX_SEND, TX_WAIT } tx_state_t;
typedef enum { REQ_MFC = 0, REQ_PRODUCT, REQ_SERIAL, REQ_DONE } req_state_t;

static struct {
    bool        ready;
    uint8_t     in_pipe, out_pipe;
    uint8_t     in_ep, out_ep;
    uint16_t    in_mps, out_mps;
    uint8_t     in_interval;
    rx_state_t  rx;
    uint32_t    rx_frame;               /* Frame der letzten Anfrage */
    tx_state_t  tx;
    uint16_t    tx_len;
    req_state_t req;
    uint32_t    errors;
    uint8_t     rx_buf[USBH_MIDI_PKT_MAX] __attribute__((aligned(4)));
    uint8_t     tx_buf[USBH_MIDI_PKT_MAX] __attribute__((aligned(4)));
    uint8_t     str_buf[130];
    usbh_midi_devinfo_t info;
} m;

static USBH_StatusTypeDef midi_init(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef midi_deinit(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef midi_requests(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef midi_process(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef midi_sof(USBH_HandleTypeDef * phost);

USBH_ClassTypeDef USBH_MIDI_Class = {
    "MIDI",
    AUDIO_CLASS,
    midi_init,
    midi_deinit,
    midi_requests,
    midi_process,
    midi_sof,
    NULL,
};

__attribute__((weak)) void usbh_midi_receive(const uint8_t * events, uint16_t len)
{
    (void)events;
    (void)len;
}

/* ------------------------------------------------------------------------
 * Interface suchen und Endpunkte oeffnen
 * --------------------------------------------------------------------- */
static uint8_t find_midi_interface(USBH_HandleTypeDef * phost)
{
    for (uint8_t i = 0; i < USBH_MAX_NUM_INTERFACES; i++) {
        const USBH_InterfaceDescTypeDef * itf = &phost->device.CfgDesc.Itf_Desc[i];
        if (itf->bInterfaceClass == AUDIO_CLASS &&
            itf->bInterfaceSubClass == MIDISTREAMING_SUB &&
            itf->bNumEndpoints >= 2U) {
            return i;
        }
    }
    return 0xFFU;
}

static USBH_StatusTypeDef midi_init(USBH_HandleTypeDef * phost)
{
    memset(&m, 0, sizeof(m));
    m.in_pipe = m.out_pipe = 0xFFU;

    uint8_t idx = find_midi_interface(phost);
    if (idx == 0xFFU) {
        USBH_UsrLog("Kein MIDIStreaming-Interface gefunden");
        return USBH_FAIL;
    }
    if (USBH_SelectInterface(phost, idx) != USBH_OK) return USBH_FAIL;

    const USBH_InterfaceDescTypeDef * itf = &phost->device.CfgDesc.Itf_Desc[idx];
    uint8_t n = itf->bNumEndpoints < USBH_MAX_NUM_ENDPOINTS ? itf->bNumEndpoints
                                                            : USBH_MAX_NUM_ENDPOINTS;
    for (uint8_t e = 0; e < n; e++) {
        const USBH_EpDescTypeDef * ep = &itf->Ep_Desc[e];
        uint8_t type = ep->bmAttributes & 0x03U;
        if (type != 0x02U && type != 0x03U) continue;      /* nur Bulk/Interrupt */
        uint16_t mps = ep->wMaxPacketSize & 0x07FFU;
        if (mps > USBH_MIDI_PKT_MAX) mps = USBH_MIDI_PKT_MAX;
        if ((ep->bEndpointAddress & 0x80U) && !m.in_ep) {
            m.in_ep = ep->bEndpointAddress;
            m.in_mps = mps;
            m.in_interval = ep->bInterval ? ep->bInterval : 1U;
        } else if (!(ep->bEndpointAddress & 0x80U) && !m.out_ep) {
            m.out_ep = ep->bEndpointAddress;
            m.out_mps = mps;
        }
    }
    if (!m.in_ep || !m.out_ep || !m.in_mps || !m.out_mps) {
        USBH_UsrLog("MIDI-Interface ohne passende Endpunkte");
        return USBH_FAIL;
    }

    m.in_pipe  = USBH_AllocPipe(phost, m.in_ep);
    m.out_pipe = USBH_AllocPipe(phost, m.out_ep);
    if (m.in_pipe == 0xFFU || m.out_pipe == 0xFFU) return USBH_FAIL;

    /* IN als Interrupt-Kanal (siehe Kopfkommentar), OUT als Bulk */
    (void)USBH_OpenPipe(phost, m.in_pipe, m.in_ep, phost->device.address,
                        phost->device.speed, USB_EP_TYPE_INTR, m.in_mps);
    (void)USBH_OpenPipe(phost, m.out_pipe, m.out_ep, phost->device.address,
                        phost->device.speed, USB_EP_TYPE_BULK, m.out_mps);
    (void)USBH_LL_SetToggle(phost, m.in_pipe, 0U);
    (void)USBH_LL_SetToggle(phost, m.out_pipe, 0U);

    m.info.vid = phost->device.DevDesc.idVendor;
    m.info.pid = phost->device.DevDesc.idProduct;
    m.info.bcd_device = phost->device.DevDesc.bcdDevice;
    m.req = REQ_MFC;
    return USBH_OK;
}

static USBH_StatusTypeDef midi_deinit(USBH_HandleTypeDef * phost)
{
    if (m.in_pipe != 0xFFU) {
        (void)USBH_ClosePipe(phost, m.in_pipe);
        (void)USBH_FreePipe(phost, m.in_pipe);
    }
    if (m.out_pipe != 0xFFU) {
        (void)USBH_ClosePipe(phost, m.out_pipe);
        (void)USBH_FreePipe(phost, m.out_pipe);
    }
    memset(&m, 0, sizeof(m));
    m.in_pipe = m.out_pipe = 0xFFU;
    return USBH_OK;
}

/* ------------------------------------------------------------------------
 * Klassen-Anfragen: String-Deskriptoren lesen
 * --------------------------------------------------------------------- */
static void copy_str(char * dst, const uint8_t * src)
{
    uint32_t i = 0;
    /* Nur druckbares ASCII uebernehmen (die Schrift hat keine Umlaute) */
    for (; src[i] && i < USBH_MIDI_STR_LEN - 1U; i++) {
        uint8_t c = src[i];
        dst[i] = (c >= 0x20U && c < 0x7FU) ? (char)c : '?';
    }
    dst[i] = '\0';
    /* Leerzeichen am Ende entfernen */
    while (i > 0U && dst[i - 1U] == ' ') dst[--i] = '\0';
}

static USBH_StatusTypeDef midi_requests(USBH_HandleTypeDef * phost)
{
    while (m.req != REQ_DONE) {
        uint8_t idx;
        char * dst;
        switch (m.req) {
        case REQ_MFC:     idx = phost->device.DevDesc.iManufacturer; dst = m.info.manufacturer; break;
        case REQ_PRODUCT: idx = phost->device.DevDesc.iProduct;      dst = m.info.product;      break;
        case REQ_SERIAL:
        default:          idx = phost->device.DevDesc.iSerialNumber; dst = m.info.serial;       break;
        }
        if (idx == 0U) {                 /* Geraet hat diesen String nicht */
            m.req++;
            continue;
        }
        memset(m.str_buf, 0, sizeof(m.str_buf));
        USBH_StatusTypeDef st = USBH_Get_StringDesc(phost, idx, m.str_buf, 0xFFU);
        if (st == USBH_BUSY) return USBH_BUSY;
        if (st == USBH_OK) copy_str(dst, m.str_buf);
        m.req++;                         /* Fehler: String auslassen, weiter */
        return USBH_BUSY;                /* naechster String beim naechsten Aufruf */
    }
    m.ready = true;
    m.rx = RX_IDLE;
    m.tx = TX_IDLE;
    return USBH_OK;
}

/* ------------------------------------------------------------------------
 * Datenverkehr
 * --------------------------------------------------------------------- */
static void process_rx(USBH_HandleTypeDef * phost)
{
    switch (m.rx) {
    case RX_IDLE:
        /* hoechstens einmal pro Frame anfragen */
        if (phost->Timer == m.rx_frame) break;
        m.rx_frame = phost->Timer;
        (void)USBH_InterruptReceiveData(phost, m.rx_buf, (uint8_t)m.in_mps, m.in_pipe);
        m.rx = RX_WAIT;
        break;

    case RX_WAIT: {
        USBH_URBStateTypeDef urb = USBH_LL_GetURBState(phost, m.in_pipe);
        if (urb == USBH_URB_DONE) {
            uint32_t n = USBH_LL_GetLastXferSize(phost, m.in_pipe);
            if (n > m.in_mps) n = m.in_mps;
            n &= ~3UL;
            if (n) usbh_midi_receive(m.rx_buf, (uint16_t)n);
            m.rx = RX_IDLE;
            m.rx_frame = phost->Timer - 1U;     /* sofort weiterlesen */
        } else if (urb == USBH_URB_NOTREADY) {
            m.rx = RX_IDLE;                     /* NAK: im naechsten Frame wieder */
        } else if (urb == USBH_URB_STALL) {
            m.errors++;
            if (USBH_ClrFeature(phost, m.in_ep) != USBH_BUSY) m.rx = RX_IDLE;
        } else if (urb == USBH_URB_ERROR) {
            m.errors++;
            m.rx = RX_IDLE;
        }
        break;
    }
    }
}

static void process_tx(USBH_HandleTypeDef * phost)
{
    switch (m.tx) {
    case TX_IDLE:
        break;
    case TX_SEND:
        (void)USBH_BulkSendData(phost, m.tx_buf, m.tx_len, m.out_pipe, 0U);
        m.tx = TX_WAIT;
        break;
    case TX_WAIT: {
        USBH_URBStateTypeDef urb = USBH_LL_GetURBState(phost, m.out_pipe);
        if (urb == USBH_URB_DONE) {
            m.tx_len = 0;
            m.tx = TX_IDLE;
        } else if (urb == USBH_URB_NOTREADY) {
            m.tx = TX_SEND;                     /* NAK: nochmal */
        } else if (urb == USBH_URB_STALL || urb == USBH_URB_ERROR) {
            m.errors++;
            m.tx_len = 0;                       /* verwerfen */
            m.tx = TX_IDLE;
        }
        break;
    }
    }
}

static USBH_StatusTypeDef midi_process(USBH_HandleTypeDef * phost)
{
    if (!m.ready) return USBH_OK;
    process_tx(phost);
    process_rx(phost);
    return USBH_OK;
}

static USBH_StatusTypeDef midi_sof(USBH_HandleTypeDef * phost)
{
    (void)phost;
    return USBH_OK;
}

/* ------------------------------------------------------------------------
 * Oeffentliche Funktionen
 * --------------------------------------------------------------------- */
bool usbh_midi_ready(void)                       { return m.ready; }
const usbh_midi_devinfo_t * usbh_midi_devinfo(void) { return &m.info; }
bool usbh_midi_tx_free(void)                     { return m.ready && m.tx == TX_IDLE; }
uint32_t usbh_midi_errors(void)                  { return m.errors; }

bool usbh_midi_send(const uint8_t * events, uint16_t len)
{
    if (!usbh_midi_tx_free() || len == 0U || (len & 3U) || len > m.out_mps) return false;
    memcpy(m.tx_buf, events, len);
    m.tx_len = len;
    m.tx = TX_SEND;
    return true;
}
