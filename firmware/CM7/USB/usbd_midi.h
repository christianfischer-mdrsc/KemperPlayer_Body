/**
 * @file usbd_midi.h
 * USB-MIDI-Geraeteklasse (USB MIDI 1.0, "class compliant") fuer die
 * ST USB Device Library. Ein MIDI-Port in jede Richtung, Bulk-Endpunkte
 * 0x01 (vom Kemper) und 0x81 (zum Kemper), je 64 Byte.
 *
 * Empfang: Der USB-Interrupt legt die USB-MIDI-Events (je 4 Byte) in einen
 * Ringpuffer; der Link-Task holt sie mit usbd_midi_read() ab.
 * Senden: usbd_midi_send() aus dem Link-Task.
 */
#ifndef USBD_MIDI_H
#define USBD_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "usbd_def.h"

#define USBD_MIDI_EP_OUT       0x01U
#define USBD_MIDI_EP_IN        0x81U
#define USBD_MIDI_PKT_MAX      64U

extern USBD_ClassTypeDef USBD_MIDI_Class;
#define USBD_MIDI_CLASS        (&USBD_MIDI_Class)

/** true, wenn der Kemper (Host) das Display konfiguriert hat und der Bus aktiv ist */
bool     usbd_midi_ready(void);
/** true, wenn ein weiteres Paket gesendet werden kann */
bool     usbd_midi_tx_free(void);
/** USB-MIDI-Events senden (len: Vielfaches von 4, max. 64). false = belegt */
bool     usbd_midi_send(const uint8_t * events, uint16_t len);
/** Empfangene Events abholen; gibt die Anzahl Bytes zurueck (Vielfaches von 4) */
uint16_t usbd_midi_read(uint8_t * buf, uint16_t max);
/** Zyklisch aus dem Task: haengende Sendungen nach einer Weile verwerfen */
void     usbd_midi_poll(uint32_t now_ms);
/** Fehlerzaehler (verworfene Sendungen, Pufferueberlauf) */
uint32_t usbd_midi_errors(void);

#ifdef __cplusplus
}
#endif

#endif /* USBD_MIDI_H */
