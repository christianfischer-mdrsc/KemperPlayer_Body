/**
 * @file ui_live.h
 * Oberflaeche des Kemper-Player-Displays (1024 x 600): Farben und Einstieg.
 * Aufbau: kemper_player.c (Daten) + ui_common.c (Bausteine, Navigation)
 * + je Ansicht eine Datei (ui_live, ui_edit, ui_banks, ui_tuner, ui_settings).
 */
#ifndef UI_LIVE_H
#define UI_LIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__has_include)
  #if __has_include("lvgl.h")
    #include "lvgl.h"
  #else
    #include "lvgl/lvgl.h"      /* Struktur im Riverdi-Projekt */
  #endif
#else
  #include "lvgl/lvgl.h"
#endif

/* Farben (Nachtmodus) */
#define UI_COL_BG        0x141517
#define UI_COL_TILE      0x1d1f22
#define UI_COL_LINE      0x2c2f34
#define UI_COL_TEXT      0xeceef1
#define UI_COL_TEXT2     0xa3aab4
#define UI_COL_TEXT3     0x8b929c
#define UI_COL_ACCENT    0x7fb4f5
#define UI_COL_SEL_BG    0x1b4a86
#define UI_COL_SEL_LINE  0x4a93e8

/* Effektfarben */
#define UI_COL_COMP      0x3cc3d6
#define UI_COL_WAH       0xf08a24
#define UI_COL_DIST      0xe5483d
#define UI_COL_EQ        0xe8c93a
#define UI_COL_STACK     0xb8bec7
#define UI_COL_PITCH     0xa58af0
#define UI_COL_MOD       0x4a93e8
#define UI_COL_DLY       0x46c26b
#define UI_COL_REV       0x2fb3a0

/**
 * Startet die Oberflaeche: Datenmodell anlegen, Live-Ansicht erzeugen und
 * anzeigen. Laeuft gerade der Startbildschirm (ui_boot_create()), wird
 * dieser abgeschlossen und danach zur Live-Ansicht uebergeblendet.
 */
void ui_start(void);

/**
 * Fuer die echten Footswitches: wirkt wie ein Tipp auf das Feld FS1..FS6
 * (idx 0..5). Nur aus dem LVGL-Task bzw. mit dessen Sperre aufrufen.
 */
void ui_footswitch(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* UI_LIVE_H */
