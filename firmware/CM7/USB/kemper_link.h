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
 *  - fragt Testdaten ab (aktuelles Rig, Amp, Cab, Tempo, Firmware-Kennung),
 *    um zu sehen, dass die Uebertragung funktioniert,
 *  - verfolgt das am Kemper geladene Rig (Name laufend abfragen, Bank/Platz
 *    aus Program Change) und laedt am Display gewaehlte Rigs am Kemper,
 *  - liest Typ, Name und Ein/Aus der Effekt-Slots und den Zustand der Fixed
 *    Effects (laufend, Aenderungen am Kemper erscheinen sofort) und schickt
 *    Aenderungen vom Display an den Kemper,
 *  - liest auf Wunsch die Einstellungen eines Effekts, als Text so, wie der
 *    Kemper sie anzeigt (kemper_link_watch()).
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
#define KL_NAME_LEN  24      /* Effektname, inkl. Nullbyte */
#define KL_SLOTS     8       /* Effekt-Slots A, B, C, D, X, MOD, DLY, REV (= kp_mod_t) */
#define KL_FIXED     8       /* Fixed Effects (= kp_fixed_t) */
#define KL_WATCH_MAX 12      /* Einstellungen im Effekt-Dialog */
#define KL_VAL_LEN   16      /* Wert als Text, inkl. Nullbyte */

/* Was der Effekt-Dialog beobachtet */
typedef enum {
    KL_WATCH_NONE = 0,
    KL_WATCH_SLOT,           /* idx = kp_mod_t */
    KL_WATCH_FIXED,          /* idx = kp_fixed_t */
} kl_watch_t;

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
    char      rig_name[KL_STR_LEN];      /* aktuell geladenes Rig, "" = noch keine Antwort */
    /* Bank/Platz des geladenen Rigs: aus Program Change vom Kemper oder der
     * eigenen Rig-Wahl. Ungueltig, wenn der Kemper ein anderes Rig meldet,
     * ohne dass bekannt ist, wo es liegt. */
    bool      rig_pos_valid;
    uint8_t   rig_bank, rig_slot;
    uint32_t  rig_seq;                   /* zaehlt bei jedem Rig-Wechsel hoch */

    /* Effekt-Slots (Bit/Index = kp_mod_t) */
    uint8_t   slot_type_valid;           /* Bitmaske: Typ bekannt */
    uint8_t   slot_on_valid;             /* Bitmaske: Ein/Aus bekannt */
    uint8_t   slot_on;                   /* Bitmaske */
    uint16_t  slot_type[KL_SLOTS];       /* Kemper-Typnummer */
    char      slot_name[KL_SLOTS][KL_NAME_LEN];   /* Name vom Kemper, "" = noch keiner */

    /* Fixed Effects (Bit = kp_fixed_t) */
    uint8_t   fixed_valid;
    uint8_t   fixed_on;

    /* Einstellungen des beobachteten Effekts */
    uint8_t   watch_kind;                /* kl_watch_t */
    uint8_t   watch_idx;
    uint8_t   watch_n;                   /* Zahl der Eintraege */
    uint16_t  watch_valid;               /* Bitmaske: Text vorhanden */
    const char * watch_name[KL_WATCH_MAX];
    char      watch_txt[KL_WATCH_MAX][KL_VAL_LEN];
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

/**
 * Einstellungen eines Effekts laufend lesen (aus dem LVGL-Task, z. B. solange
 * ein Dialog offen ist). KL_WATCH_NONE beendet das Lesen. Die Werte stehen
 * dann in kl_info_t (watch_*), Aenderungen melden KP_CHG_SYSTEM.
 */
void kemper_link_watch(kl_watch_t kind, uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* KEMPER_LINK_H */
