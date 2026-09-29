/**
 * @file ui_boot.h
 * Startbildschirm des Kemper-Displays mit Fortschritt und Bootzeit.
 *
 * Ablauf:
 *   ui_boot_create();                  // direkt nach lv_init() + Display-Init
 *   ui_boot_step(0, NULL);             // "Display"
 *   ... Initialisierung ...
 *   ui_boot_step(1, NULL);             // "Touch"
 *   ...
 *   ui_boot_finish(ui_live_create());  // zeigt 100 %, friert die Zeit ein,
 *                                      // blendet danach zur Live-Ansicht ueber
 *
 * Die angezeigte Zeit ist lv_tick_get(), also die Millisekunden seit Reset
 * (im Riverdi-Projekt ist lv_tick an HAL_GetTick() gekoppelt).
 */
#ifndef UI_BOOT_H
#define UI_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__has_include)
  #if __has_include("lvgl.h")
    #include "lvgl.h"
  #else
    #include "lvgl/lvgl.h"
  #endif
#else
  #include "lvgl/lvgl.h"
#endif

/** Anzahl der Boot-Schritte */
#define UI_BOOT_STEPS 5

/** Erzeugt den Startbildschirm und zeigt ihn sofort an. */
lv_obj_t * ui_boot_create(void);

/**
 * Setzt den aktuellen Schritt (0 .. UI_BOOT_STEPS-1).
 * Alle vorherigen Schritte werden als erledigt markiert.
 * @param text  Statustext, NULL fuer den Standardtext des Schritts
 */
void ui_boot_step(uint8_t index, const char * text);

/**
 * Schliesst den Startvorgang ab: 100 %, alle Schritte erledigt, Bootzeit
 * wird eingefroren. Nach kurzer Pause wird zu 'next' ueberblendet.
 * @param next  Screen, der danach angezeigt wird (NULL = bleibt stehen)
 */
void ui_boot_finish(lv_obj_t * next);

/** Gemessene Bootzeit in ms (0, solange nicht abgeschlossen). */
uint32_t ui_boot_time_ms(void);

/** true, solange der Startbildschirm laeuft und noch nicht abgeschlossen ist. */
bool ui_boot_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BOOT_H */
