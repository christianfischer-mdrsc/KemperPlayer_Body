/**
 * @file usbh_conf.h
 * Konfiguration der ST USB Host Library fuer das Kemper Player Display.
 *
 * USB_OTG_HS laeuft mit dem internen Full-Speed-PHY an PB14/PB15
 * (Molex-Buchse "USB", P10). Das Display ist USB-Host, der Kemper Player
 * haengt mit seiner USB-B-Buchse daran.
 *
 * Kein OS-Modus: USBH_Process() wird zyklisch aus dem Kemper-Link-Task
 * aufgerufen (kemper_link.c). So braucht die Library keine eigenen
 * FreeRTOS-Objekte und keinen Heap.
 */
#ifndef USBH_CONF_H
#define USBH_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

/* Der Kemper Player ist ein zusammengesetztes Geraet (USB-Audio, MIDI,
 * Rig Manager). Die Konfiguration ist deshalb gross und hat viele
 * Interfaces (Alternate Settings zaehlen einzeln). Zu kleine Werte fuehren
 * dazu, dass die Enumeration mit "not supported" abbricht oder das
 * MIDI-Interface nicht gefunden wird. */
#define USBH_MAX_NUM_ENDPOINTS                8U
#define USBH_MAX_NUM_INTERFACES               24U
#define USBH_MAX_NUM_CONFIGURATION            1U
#define USBH_KEEP_CFG_DESCRIPTOR              1U
#define USBH_MAX_NUM_SUPPORTED_CLASS          1U
#define USBH_MAX_SIZE_CONFIGURATION           4096U
#define USBH_MAX_DATA_BUFFER                  512U
#define USBH_DEBUG_LEVEL                      0U
#define USBH_USE_OS                           0U
#define USBH_IN_NAK_PROCESS                   0U

#define HOST_HS                               0   /* Instanz-ID im Handle */

/* Speicher: die Library selbst nutzt kein malloc (Klassen-Daten statisch) */
#define USBH_malloc               malloc
#define USBH_free                 free
#define USBH_memset               memset
#define USBH_memcpy               memcpy

#if (USBH_DEBUG_LEVEL > 0U)
#define USBH_UsrLog(...)   do { printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBH_UsrLog(...)   do {} while (0)
#endif

#if (USBH_DEBUG_LEVEL > 1U)
#define USBH_ErrLog(...)   do { printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBH_ErrLog(...)   do {} while (0)
#endif

#if (USBH_DEBUG_LEVEL > 2U)
#define USBH_DbgLog(...)   do { printf("DEBUG : "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBH_DbgLog(...)   do {} while (0)
#endif

/* ---------------------------------------------------------------------
 * VBUS-Schalter (USB1_EN, PF10) und Ueberstrom-Meldung (USB1_OVERCURRENT,
 * PC15). Laut Riverdi-Datenblatt liefert die Buchse im Host-Modus 5 V,
 * max. 500 mA. Der Schalter wird hier als "aktiv high" angenommen
 * (gpio.c setzt PF10 beim Start auf low = aus). Falls am Board im
 * Host-Modus keine 5 V an Pin 1 anliegen, hier tauschen.
 * ------------------------------------------------------------------ */
#define USBH_VBUS_ON_LEVEL        GPIO_PIN_SET
#define USBH_VBUS_OFF_LEVEL       GPIO_PIN_RESET
/* Ueberstrom-Eingang: aktiv low (typisch fuer Lastschalter mit FLT-Ausgang) */
#define USBH_OVERCURRENT_ACTIVE   GPIO_PIN_RESET

/* Wird von der Library und von kemper_link.c benutzt */
extern HCD_HandleTypeDef hhcd_USB_OTG_HS;

/** true, wenn der VBUS-Lastschalter Ueberstrom meldet */
uint8_t USBH_LL_OverCurrent(void);

#ifdef __cplusplus
}
#endif

#endif /* USBH_CONF_H */
