/**
 * @file usbd_desc.c
 * Geraete- und String-Deskriptoren des Displays als USB-MIDI-Geraet.
 *
 * VID/PID: 0x1209/0x0001 aus dem Bereich, den pid.codes fuer private Tests
 * freigibt. Fuer eine Weitergabe des Geraets braeuchte es eine eigene PID.
 * Der Kemper erkennt das Display als normales (class compliant) MIDI-Geraet;
 * VID/PID spielen fuer ihn keine Rolle.
 */
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

#define KPD_VID                0x1209U
#define KPD_PID                0x0001U
#define KPD_LANGID             0x0409U          /* Englisch (USA), ueblich */
#define KPD_MANUFACTURER       "Eigenbau"
#define KPD_PRODUCT            "Kemper Player Display"
#define KPD_CONFIGURATION      "MIDI"
#define KPD_INTERFACE          "MIDI"

static uint8_t * dev_desc(USBD_SpeedTypeDef speed, uint16_t * length);
static uint8_t * langid_desc(USBD_SpeedTypeDef speed, uint16_t * length);
static uint8_t * mfc_desc(USBD_SpeedTypeDef speed, uint16_t * length);
static uint8_t * product_desc(USBD_SpeedTypeDef speed, uint16_t * length);
static uint8_t * serial_desc(USBD_SpeedTypeDef speed, uint16_t * length);
static uint8_t * config_desc(USBD_SpeedTypeDef speed, uint16_t * length);
static uint8_t * itf_desc(USBD_SpeedTypeDef speed, uint16_t * length);

USBD_DescriptorsTypeDef KPD_Desc = {
    dev_desc, langid_desc, mfc_desc, product_desc, serial_desc, config_desc, itf_desc,
};

static uint8_t s_dev[USB_LEN_DEV_DESC] __attribute__((aligned(4))) = {
    0x12,                       /* bLength */
    USB_DESC_TYPE_DEVICE,
    0x00, 0x02,                 /* bcdUSB 2.00 */
    0x00, 0x00, 0x00,           /* Klasse wird im Interface angegeben */
    USB_MAX_EP0_SIZE,
    LOBYTE(KPD_VID), HIBYTE(KPD_VID),
    LOBYTE(KPD_PID), HIBYTE(KPD_PID),
    0x00, 0x01,                 /* bcdDevice 1.00 */
    USBD_IDX_MFC_STR,
    USBD_IDX_PRODUCT_STR,
    USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

static uint8_t s_langid[USB_LEN_LANGID_STR_DESC] __attribute__((aligned(4))) = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, LOBYTE(KPD_LANGID), HIBYTE(KPD_LANGID),
};

static uint8_t s_str[USBD_MAX_STR_DESC_SIZ] __attribute__((aligned(4)));

static uint8_t * dev_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    *length = sizeof(s_dev);
    return s_dev;
}

static uint8_t * langid_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    *length = sizeof(s_langid);
    return s_langid;
}

static uint8_t * str_desc(const char * txt, uint16_t * length)
{
    USBD_GetString((uint8_t *)txt, s_str, length);
    return s_str;
}

static uint8_t * mfc_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    return str_desc(KPD_MANUFACTURER, length);
}

static uint8_t * product_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    return str_desc(KPD_PRODUCT, length);
}

/* Seriennummer aus der eindeutigen ID des STM32 (12 Hex-Zeichen) */
static uint8_t * serial_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    static const char HEX[] = "0123456789ABCDEF";
    char sn[13];
    uint32_t a = *(volatile uint32_t *)(UID_BASE) + *(volatile uint32_t *)(UID_BASE + 8U);
    uint32_t b = *(volatile uint32_t *)(UID_BASE + 4U);
    for (int i = 0; i < 8; i++) sn[i] = HEX[(a >> (28 - 4 * i)) & 0xFU];
    for (int i = 0; i < 4; i++) sn[8 + i] = HEX[(b >> (28 - 4 * i)) & 0xFU];
    sn[12] = '\0';
    return str_desc(sn, length);
}

static uint8_t * config_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    return str_desc(KPD_CONFIGURATION, length);
}

static uint8_t * itf_desc(USBD_SpeedTypeDef speed, uint16_t * length)
{
    (void)speed;
    return str_desc(KPD_INTERFACE, length);
}
