/**
 * @file usbd_conf.h
 * Konfiguration der ST USB Device Library fuer das Kemper Player Display.
 *
 * Das Display ist ein USB-MIDI-Geraet (class compliant) und haengt an der
 * USB-A-Buchse des Kemper Players; der Player ist der USB-Host.
 * Hardware: USB_OTG_HS mit internem Full-Speed-PHY an PB14/PB15,
 * Molex-Buchse "USB" (P10), ID-Pin offen (= Device-Modus laut Riverdi).
 */
#ifndef USBD_CONF_H
#define USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#define USBD_MAX_NUM_INTERFACES          2U   /* Audio Control + MIDIStreaming */
#define USBD_MAX_NUM_CONFIGURATION       1U
#define USBD_MAX_STR_DESC_SIZ            0x100U
#define USBD_SELF_POWERED                1U   /* eigenes Netzteil, VBUS nur zur Erkennung */
#define USBD_DEBUG_LEVEL                 0U
#define USBD_SUPPORT_USER_STRING_DESC    0U
#define USBD_CLASS_USER_STRING_DESC      0U
#define USBD_CLASS_BOS_ENABLED           0U
#define USBD_LPM_ENABLED                 0U

#define DEVICE_FS                        0U   /* Instanz-ID im Handle */

#define USBD_malloc               malloc
#define USBD_free                 free
#define USBD_memset               memset
#define USBD_memcpy               memcpy
#define USBD_Delay                HAL_Delay

#if (USBD_DEBUG_LEVEL > 0U)
#define USBD_UsrLog(...)   do { printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_UsrLog(...)   do {} while (0)
#endif
#if (USBD_DEBUG_LEVEL > 1U)
#define USBD_ErrLog(...)   do { printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_ErrLog(...)   do {} while (0)
#endif
#if (USBD_DEBUG_LEVEL > 2U)
#define USBD_DbgLog(...)   do { printf("DEBUG : "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBD_DbgLog(...)   do {} while (0)
#endif

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

#ifdef __cplusplus
}
#endif

#endif /* USBD_CONF_H */
