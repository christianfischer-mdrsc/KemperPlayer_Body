/**
 * @file footswitch.h
 * Treiber fuer die Footswitches am Expansion-Header (P8) des Riverdi-Boards.
 *
 * Verdrahtung: Taster (Schliesser) zwischen Pin und GND. Der interne Pull-up
 * ist eingeschaltet, ein externer Pull-up (z. B. 4,7 kOhm auf 3,3 V) darf
 * zusaetzlich vorhanden sein. NIE 5 V an die Pins legen.
 *
 *   FS1  PD11  Header-Pin 10        FS4  PD13  Header-Pin 15
 *   FS2  PB10  Header-Pin 12        FS5  PB11  Header-Pin 34
 *   FS3  PD12  Header-Pin 13        FS6  PH4   Header-Pin 36
 *   GND: Header-Pins 6, 7, 17, 18, 28
 *
 * Die Pins waren in Riverdis CubeMX-Vorlage als LPTIM2, I2C4 und I2C2
 * konfiguriert; diese Peripherie nutzt das Projekt nicht. footswitch_init()
 * schaltet die Pins auf Eingang um.
 *
 * Zusaetzlich wirkt der Taster BTN1 auf dem Board (PC6) als FS1, damit man
 * ohne Verdrahtung testen kann (FOOTSWITCH_BOARD_BTN1).
 *
 * Abfrage alle 5 ms im LVGL-Task (lv_timer), dadurch kein Konflikt mit der
 * Oberflaeche. Ein Druck wird sofort beim ersten Flankenwechsel gemeldet
 * (wichtig fuer Tap Tempo), danach ignoriert der Treiber das Prellen.
 */
#ifndef FOOTSWITCH_H
#define FOOTSWITCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define FOOTSWITCH_COUNT        6
#define FOOTSWITCH_POLL_MS      5     /* Abfrageintervall */
#define FOOTSWITCH_DEBOUNCE_MS  30    /* Sperrzeit nach jeder Flanke */
#define FOOTSWITCH_LONG_MS      800   /* ab hier "lang gedrueckt" */

#ifndef FOOTSWITCH_BOARD_BTN1
#define FOOTSWITCH_BOARD_BTN1   1     /* BTN1 auf dem Board als FS1 nutzen */
#endif

/** Pins konfigurieren und Abfrage starten (nach lv_init() aufrufen). */
void footswitch_init(void);

/** Entprellter Zustand: true = gedrueckt. */
bool footswitch_is_pressed(uint8_t idx);

/** Anzahl erkannter Druecke seit dem Start (zur Fehlersuche). */
uint32_t footswitch_press_count(uint8_t idx);

/**
 * Wird aufgerufen, wenn ein Taster FOOTSWITCH_LONG_MS gehalten wird.
 * Schwach definiert (tut nichts); fuer Zusatzfunktionen ueberschreiben.
 */
void footswitch_long_press(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* FOOTSWITCH_H */
