/**
 * @file usbh_midi.h
 * USB-MIDI-Klasse (Audio Class, MIDIStreaming) fuer die ST USB Host Library.
 *
 * Sucht in der Konfiguration des angeschlossenen Geraets das
 * MIDIStreaming-Interface (Klasse 0x01, Subklasse 0x03) und betreibt dessen
 * IN- und OUT-Endpunkt. Daneben liest sie Hersteller, Produktname und
 * Seriennummer aus den String-Deskriptoren.
 *
 * Alle Funktionen nur aus dem Task aufrufen, der USBH_Process() ausfuehrt.
 */
#ifndef USBH_MIDI_H
#define USBH_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "usbh_core.h"

#define USBH_MIDI_STR_LEN   40          /* inkl. Nullbyte */
#define USBH_MIDI_PKT_MAX   64          /* ein Full-Speed-Paket = 16 USB-MIDI-Events */

extern USBH_ClassTypeDef USBH_MIDI_Class;
#define USBH_MIDI_CLASS     (&USBH_MIDI_Class)

typedef struct {
    char     manufacturer[USBH_MIDI_STR_LEN];
    char     product[USBH_MIDI_STR_LEN];
    char     serial[USBH_MIDI_STR_LEN];
    uint16_t vid, pid;
    uint16_t bcd_device;                 /* Geraeterevision aus dem Deskriptor */
} usbh_midi_devinfo_t;

/** true, sobald Endpunkte offen und Strings gelesen sind */
bool usbh_midi_ready(void);

/** Daten des angeschlossenen Geraets (gueltig, wenn usbh_midi_ready()) */
const usbh_midi_devinfo_t * usbh_midi_devinfo(void);

/** true, wenn ein weiteres Paket gesendet werden kann */
bool usbh_midi_tx_free(void);

/**
 * USB-MIDI-Events (je 4 Byte) senden. len: Vielfaches von 4, hoechstens
 * USBH_MIDI_PKT_MAX. Gibt false zurueck, wenn noch gesendet wird.
 */
bool usbh_midi_send(const uint8_t * events, uint16_t len);

/** Zaehler fuer Fehler auf dem Bus (Stall, Transaktionsfehler) */
uint32_t usbh_midi_errors(void);

/**
 * Empfangene USB-MIDI-Events (je 4 Byte). Wird aus USBH_Process()
 * heraus aufgerufen; schwach definiert, kemper_link.c ueberschreibt sie.
 */
void usbh_midi_receive(const uint8_t * events, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* USBH_MIDI_H */
