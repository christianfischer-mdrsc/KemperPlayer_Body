/**
 * @file ui_statusbar.h
 * Statusleiste oben (1024 x 40): USB-Verbindung links, Datum und Uhrzeit rechts.
 * Jede Ansicht bekommt ihre eigene Leiste, alle werden gemeinsam aktualisiert.
 */
#ifndef UI_STATUSBAR_H
#define UI_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ui_live.h"

#define UI_STATUSBAR_HEIGHT  40

typedef struct {
    uint16_t year;    /* z. B. 2026 */
    uint8_t  month;   /* 1..12 */
    uint8_t  day;     /* 1..31 */
    uint8_t  hour;    /* 0..23 */
    uint8_t  minute;  /* 0..59 */
} ui_datetime_t;

/**
 * Erzeugt eine Statusleiste als erstes Kind von 'parent'
 * (volle Breite, UI_STATUSBAR_HEIGHT hoch). Bis zu 4 Leisten gleichzeitig.
 */
lv_obj_t * ui_statusbar_create(lv_obj_t * parent);

/** Aus dem USB-Code aufrufen, wenn der Kemper verbunden/getrennt wird. */
void ui_statusbar_set_usb(bool connected);

/** Sofort neu zeichnen, z. B. nachdem die Uhr gestellt wurde. */
void ui_statusbar_refresh(void);

/**
 * Zeitquelle. Die schwache Standard-Implementierung liefert false
 * ("--:--"). Auf dem Board ueberschreibt rtc.c sie mit dem STM32-RTC.
 * @return false, wenn keine gueltige Zeit vorliegt
 */
bool ui_statusbar_get_time(ui_datetime_t * out);

/**
 * Uhr stellen. Schwache Standard-Implementierung liefert false;
 * auf dem Board ueberschreibt rtc.c sie.
 * @return true, wenn die Zeit gesetzt wurde
 */
bool ui_statusbar_set_time(const ui_datetime_t * in);

#ifdef __cplusplus
}
#endif

#endif /* UI_STATUSBAR_H */
