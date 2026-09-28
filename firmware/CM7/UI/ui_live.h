/**
 * @file ui_live.h
 * Live-Ansicht des Kemper-Displays (1024 x 600), nur Darstellung.
 * Entspricht live_screen.xml, aber als reiner C-Code fuer LVGL v9.
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
 * Erzeugt die Live-Ansicht als eigenen Screen.
 * @return Zeiger auf den Screen, laden mit lv_screen_load().
 */
lv_obj_t * ui_live_create(void);

/**
 * Erzeugt die Bearbeiten-Ansicht als eigenen Screen.
 * @return Zeiger auf den Screen, laden mit lv_screen_load().
 */
lv_obj_t * ui_edit_create(void);

/**
 * Erzeugt Live- und Bearbeiten-Ansicht, verbindet die Buttons
 * "Bearbeiten" und "Play" zum Umschalten und zeigt die Live-Ansicht.
 * Nach lv_init() und der Display-Initialisierung aufrufen.
 */
void ui_start(void);

#if LV_USE_XML
/**
 * Nur fuer die XML-Variante: meldet die Montserrat-Fonts beim
 * XML-Parser an, damit style_text_font="lv_font_montserrat_20" usw.
 * in den .xml-Dateien gefunden wird. Vor dem Laden der XML aufrufen.
 */
void ui_xml_register_fonts(void);

/**
 * Nur fuer die XML-Variante: laedt globals.xml, die Komponenten und
 * die Screens aus dem Ordner 'path' (z. B. "A:ui/") und erzeugt den
 * gewuenschten Screen. Die Dateien werden nur beim ersten Aufruf geladen.
 */
lv_obj_t * ui_live_create_from_xml(const char * path);
lv_obj_t * ui_edit_create_from_xml(const char * path);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UI_LIVE_H */
