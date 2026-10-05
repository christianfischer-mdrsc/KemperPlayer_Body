/**
 * @file kemper_link.h
 * Verbindung zum Kemper Player ueber USB-MIDI.
 *
 * Anschluss: Das Display ist ein USB-MIDI-Geraet (class compliant) an der
 * USB-A-Buchse des Players; der Player ist USB-Host.
 *
 * Aufbau:
 *   USB-Geraet (usbd_conf.c, usbd_desc.c) -> MIDI-Klasse (usbd_midi.c)
 *   -> kemper_link.c
 *
 * kemper_link.c laeuft in einem eigenen FreeRTOS-Task. Er
 *  - startet das USB-Geraet und holt die empfangenen MIDI-Daten ab,
 *  - baut mit dem Kemper das bidirektionale SysEx-Protokoll auf (Beacon),
 *  - ueberwacht die Lebenszeichen ("Sense", ca. alle 500 ms) und erkennt
 *    so eine unterbrochene Verbindung,
 *  - fragt Testdaten ab (Name von Rig 1, aktuelles Rig, Amp, Cab, Tempo,
 *    Firmware-Kennung), um zu sehen, dass die Uebertragung funktioniert.
 *
 * Die Oberflaeche bekommt die Daten nur ueber kemper_link_get_info()
 * (Kopie unter Sperre) bzw. ueber kemper_link_ui_poll() im LVGL-Task.
 * Kein LVGL- oder Modell-Aufruf aus dem USB-Task.
 */
#ifndef KEMPER_LINK_H
#define KEMPER_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define KL_STR_LEN   40      /* inkl. Nullbyte */

/* Zustand der USB-Seite */
typedef enum {
    KL_USB_NONE = 0,         /* kein Host / Kabel nicht gesteckt / Bus im Ruhezustand */
    KL_USB_ENUM,             /* Kemper richtet das Display gerade ein */
    KL_USB_READY,            /* vom Kemper konfiguriert, MIDI-Endpunkte offen */
} kl_usb_state_t;

/* Zustand der Kemper-Verbindung (SysEx-Protokoll) */
typedef enum {
    KL_KEMPER_NONE = 0,      /* kein MIDI-Geraet */
    KL_KEMPER_WAIT,          /* Beacon gesendet, noch keine Antwort */
    KL_KEMPER_OK,            /* Lebenszeichen kommen regelmaessig */
    KL_KEMPER_LOST,          /* war verbunden, Lebenszeichen ausgeblieben */
} kl_kemper_state_t;

typedef struct {
    kl_usb_state_t    usb;
    kl_kemper_state_t kemper;

    /* MIDI Identity Reply (nicht dokumentiert, nur wenn der Kemper antwortet) */
    bool      identity_valid;
    uint8_t   id_family[2], id_model[2], id_version[4];

    /* Testdaten vom Kemper */
    char      rig1_name[KL_STR_LEN];     /* Bank 1, Rig 1 */
    char      rig_name[KL_STR_LEN];      /* aktuell geladenes Rig */
    char      amp_name[KL_STR_LEN];
    char      cab_name[KL_STR_LEN];
    bool      tempo_valid;
    bool      tempo_on;
    uint16_t  tempo_bpm;

    /* Ueberwachung */
    uint32_t  now_ms;                /* Zeitpunkt der Kopie */
    uint32_t  connected_since_ms;    /* nur bei KL_KEMPER_OK gueltig */
    uint32_t  last_sense_ms;         /* letztes Lebenszeichen */
    uint32_t  rx_messages;           /* empfangene MIDI-Nachrichten */
    uint32_t  tx_messages;           /* gesendete SysEx-Nachrichten */
    uint32_t  lost_count;            /* erkannte Unterbrechungen */
    uint32_t  usb_errors;

    uint32_t  generation;            /* aendert sich bei jeder inhaltlichen Aenderung */
} kl_info_t;

/** Task anlegen; aus MX_FREERTOS_Init() aufrufen (vor dem Scheduler-Start) */
void kemper_link_start(void);

/** Konsistente Kopie des aktuellen Zustands (aus jedem Task) */
void kemper_link_get_info(kl_info_t * out);

/** Kurzer Text zum Zustand, z. B. "Verbunden" (ASCII, ohne Umlaute) */
const char * kemper_link_state_text(const kl_info_t * info);

/**
 * Im LVGL-Task zyklisch aufrufen (main.c legt dafuer einen lv_timer an):
 * meldet Verbindungswechsel und neue Daten an das Modell (kp_rx_*).
 */
void kemper_link_ui_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* KEMPER_LINK_H */
