/**
 * @file ui_common.h
 * Gemeinsame Bausteine aller Ansichten: Stile, Navigation, Effektkette,
 * Footswitch-Leiste und Dialoge.
 *
 * Es existiert immer nur der gerade angezeigte Screen. Beim Wechsel wird der
 * neue Screen erzeugt und der alte geloescht; alle Werte stehen im Modell
 * (kemper_player.c). Das haelt den LVGL-Speicher klein.
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ui_live.h"
#include "kemper_player.h"

typedef enum {
    UI_SCR_LIVE = 0,
    UI_SCR_EDIT,
    UI_SCR_BANKS,
    UI_SCR_TUNER,
    UI_SCR_SETTINGS,
    UI_SCR_COUNT
} ui_scr_t;

/* ---------------------------------------------------------------------
 * Navigation
 * ------------------------------------------------------------------ */
/* Erzeugt den Screen, ohne ihn zu laden (fuer den Startbildschirm) */
lv_obj_t * ui_nav_build(ui_scr_t scr);
/* Wechselt mit kurzer Ueberblendung zum Screen */
void       ui_nav_go(ui_scr_t scr);
ui_scr_t   ui_nav_current(void);
ui_scr_t   ui_nav_previous(void);   /* Ansicht vor der aktuellen */
/* Vom Screen-Builder aufzurufen: Funktion, die bei Modell-Aenderungen
 * die Anzeige aktualisiert. Wird beim Loeschen des Screens entfernt. */
void       ui_nav_set_refresh(lv_obj_t * scr, void (*fn)(uint32_t changes));

/* ---------------------------------------------------------------------
 * Bausteine
 * ------------------------------------------------------------------ */
void       ui_theme_init(void);
lv_obj_t * ui_screen_base(lv_obj_t ** body);
lv_obj_t * ui_label(lv_obj_t * parent, const char * txt, uint32_t color, const lv_font_t * font);
lv_obj_t * ui_row(lv_obj_t * parent, int32_t gap);
lv_obj_t * ui_col(lv_obj_t * parent, int32_t gap);
lv_obj_t * ui_spacer(lv_obj_t * parent, int32_t w, int32_t h, bool grow);
lv_obj_t * ui_panel(lv_obj_t * parent);                 /* Kachel-Hintergrund */
lv_obj_t * ui_button(lv_obj_t * parent, const char * txt, const lv_font_t * font, bool selected);
lv_obj_t * ui_icon_button(lv_obj_t * parent, const char * sym, bool selected);
void       ui_button_set_selected(lv_obj_t * btn, bool selected);
lv_obj_t * ui_slider(lv_obj_t * parent, int32_t min, int32_t max, int32_t value);
lv_obj_t * ui_bank_chip(lv_obj_t * parent, lv_obj_t ** label_out, lv_obj_t ** dot_out);
void       ui_bank_chip_update(lv_obj_t * label, lv_obj_t * dot);
lv_obj_t * ui_title_bar(lv_obj_t * body, const char * title, ui_scr_t back_to);

/* ---------------------------------------------------------------------
 * Effektkette (Kacheln A, B, ... STACK ... DLY, REV)
 * ------------------------------------------------------------------ */
typedef struct {
    uint8_t    n;
    uint8_t    id[KP_CHAIN_MAX];       /* kp_mod_t oder KP_CHAIN_STACK */
    lv_obj_t * tile[KP_CHAIN_MAX];
    lv_obj_t * slot[KP_CHAIN_MAX];
    lv_obj_t * name[KP_CHAIN_MAX];
    lv_obj_t * state[KP_CHAIN_MAX];
    bool       compact;
} ui_chain_t;

/* cb wird beim Antippen einer Kachel aufgerufen, user_data = Kachel-Index */
void ui_chain_create(ui_chain_t * c, lv_obj_t * parent, bool compact, lv_event_cb_t cb);
void ui_chain_update(ui_chain_t * c);

/* ---------------------------------------------------------------------
 * Footswitch-Leiste (FS1-FS6, tippen = Taster druecken)
 * ------------------------------------------------------------------ */
typedef struct {
    lv_obj_t * btn[KP_FOOTSWITCHES];
    lv_obj_t * top[KP_FOOTSWITCHES];
    lv_obj_t * main[KP_FOOTSWITCHES];
    lv_obj_t * sub[KP_FOOTSWITCHES];
    lv_obj_t * led[KP_FOOTSWITCHES];
} ui_fsbar_t;

void ui_fsbar_create(ui_fsbar_t * f, lv_obj_t * parent);
void ui_fsbar_update(ui_fsbar_t * f);

/* ---------------------------------------------------------------------
 * Dialoge (modal auf der obersten Ebene)
 * ------------------------------------------------------------------ */
lv_obj_t * ui_dialog_open(const char * title, int32_t w, int32_t h, lv_obj_t ** content);
void       ui_dialog_close(void);
bool       ui_dialog_is_open(void);
/* Optionale Aktualisierung des offenen Dialogs bei Modell-Aenderungen */
void       ui_dialog_set_refresh(void (*fn)(uint32_t changes));
/* Dialog mit Schiebereglern fuer alle Parameter eines Bereichs */
void       ui_param_dialog(kp_section_t sec);

#ifdef __cplusplus
}
#endif

#endif /* UI_COMMON_H */
