/**
 * @file ui_live.c
 * Live-Ansicht des Kemper-Displays (1024 x 600), nur Darstellung.
 *
 * Benoetigte Fonts in lv_conf.h:
 *   LV_FONT_MONTSERRAT_12, _14, _16, _18, _20, _22, _24, _28, _48
 * Statusleiste (USB, Datum, Uhrzeit) oben: siehe ui_statusbar.c
 */
#include "ui_live.h"
#include "ui_boot.h"
#include "ui_statusbar.h"

/* Buttons zum Umschalten zwischen Live- und Bearbeiten-Ansicht */
static lv_obj_t * s_btn_to_edit;
static lv_obj_t * s_btn_to_live;
static lv_obj_t * s_scr_live;
static lv_obj_t * s_scr_edit;

/* -------------------------------------------------------------------------
 * Hilfsfunktionen
 * ---------------------------------------------------------------------- */

/* Unsichtbarer Container ohne Rahmen, Hintergrund und Scrollen */
static lv_obj_t * plain_row(lv_obj_t * parent, int32_t gap)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t * label(lv_obj_t * parent, const char * txt,
                        uint32_t color, const lv_font_t * font)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

/* Entspricht der Komponente effect_tile.xml */
static lv_obj_t * effect_tile(lv_obj_t * parent, const char * slot,
                              const char * name, bool on, uint32_t color)
{
    uint32_t c = on ? color : UI_COL_LINE;

    lv_obj_t * t = lv_obj_create(parent);
    lv_obj_set_height(t, 176);
    lv_obj_set_width(t, 1);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(t, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_color(t, lv_color_hex(c), 0);
    lv_obj_set_style_border_width(t, 2, 0);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    lv_obj_set_style_pad_row(t, 6, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    uint32_t sc = on ? color : UI_COL_TEXT3;          /* Schriftfarbe Slot/Status */
    label(t, slot, sc, &lv_font_montserrat_22);

    lv_obj_t * n = label(t, name, on ? UI_COL_TEXT : UI_COL_TEXT3,
                         &lv_font_montserrat_16);
    lv_obj_set_width(n, LV_PCT(100));
    lv_obj_set_flex_grow(n, 1);
    lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);

    label(t, on ? "ON" : "OFF", sc, &lv_font_montserrat_20);
    return t;
}

/* Entspricht der Komponente fs_button.xml */
static lv_obj_t * fs_button(lv_obj_t * parent, const char * fs,
                            const char * text, const char * sub, bool active)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_height(b, 74);
    lv_obj_set_width(b, 1);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(b, lv_color_hex(active ? UI_COL_SEL_BG : UI_COL_TILE), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(active ? UI_COL_SEL_LINE : UI_COL_LINE), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_left(b, 12, 0);
    lv_obj_set_style_pad_right(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 8, 0);
    lv_obj_set_style_pad_row(b, 2, 0);

    label(b, fs, UI_COL_TEXT3, &lv_font_montserrat_14);
    lv_obj_t * l = label(b, text, UI_COL_TEXT, &lv_font_montserrat_20);
    lv_obj_set_size(l, LV_PCT(100), 24);              /* eine Zeile, sonst "..." */
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    if (sub && sub[0]) label(b, sub, UI_COL_TEXT3, &lv_font_montserrat_14);
    return b;
}

/* -------------------------------------------------------------------------
 * Screen
 * ---------------------------------------------------------------------- */

/* Screen mit Statusleiste oben. Liefert den Inhaltsbereich darunter
 * (Spalte, 8 px Rand, 6 px Zeilenabstand), in den die Ansicht gebaut wird. */
static lv_obj_t * screen_with_statusbar(lv_obj_t ** body)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 1024, 600);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_statusbar_create(scr);

    lv_obj_t * b = lv_obj_create(scr);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_pad_all(b, 8, 0);
    lv_obj_set_style_pad_row(b, 6, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    *body = b;
    return scr;
}

lv_obj_t * ui_live_create(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = screen_with_statusbar(&body);

    /* Kopfzeile ------------------------------------------------------- */
    lv_obj_t * head = plain_row(body, 16);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    label(head, "012", UI_COL_ACCENT, &lv_font_montserrat_28);
    label(head, "Sunday Set", UI_COL_TEXT, &lv_font_montserrat_28);

    lv_obj_t * spacer = lv_obj_create(head);           /* Abstandhalter */
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * bpm = lv_obj_create(head);              /* Tempo-Anzeige */
    lv_obj_set_size(bpm, LV_SIZE_CONTENT, 48);
    lv_obj_set_flex_flow(bpm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bpm, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bpm, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_color(bpm, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_border_width(bpm, 1, 0);
    lv_obj_set_style_radius(bpm, 10, 0);
    lv_obj_set_style_pad_hor(bpm, 14, 0);
    lv_obj_set_style_pad_ver(bpm, 0, 0);
    lv_obj_set_style_pad_column(bpm, 10, 0);
    lv_obj_remove_flag(bpm, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * led = lv_obj_create(bpm);
    lv_obj_set_size(led, 14, 14);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(led, lv_color_hex(UI_COL_DIST), 0);
    lv_obj_set_style_border_width(led, 0, 0);
    label(bpm, "120 BPM", UI_COL_TEXT, &lv_font_montserrat_24);

    lv_obj_t * edit = lv_button_create(head);          /* Bearbeiten */
    s_btn_to_edit = edit;
    lv_obj_set_height(edit, 48);
    lv_obj_set_style_bg_color(edit, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_color(edit, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_border_width(edit, 1, 0);
    lv_obj_set_style_radius(edit, 10, 0);
    lv_obj_set_style_shadow_width(edit, 0, 0);
    lv_obj_t * el = label(edit, "Bearbeiten", UI_COL_TEXT2, &lv_font_montserrat_18);
    lv_obj_center(el);

    /* Slot und Rig-Name ----------------------------------------------- */
    lv_obj_t * slot = label(body, "Slot 3", UI_COL_TEXT3, &lv_font_montserrat_20);
    lv_obj_set_style_pad_left(slot, 12, 0);

    lv_obj_t * rig = label(body, "65 Deluxe 2.0 LQD N", UI_COL_TEXT,
                           &lv_font_montserrat_48);
    lv_obj_set_size(rig, LV_PCT(100), 60);
    lv_obj_set_style_pad_left(rig, 12, 0);
    lv_label_set_long_mode(rig, LV_LABEL_LONG_DOT);

    /* Freiraum --------------------------------------------------------- */
    lv_obj_t * grow = lv_obj_create(body);
    lv_obj_set_size(grow, LV_PCT(100), 1);
    lv_obj_set_flex_grow(grow, 1);
    lv_obj_set_style_bg_opa(grow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grow, 0, 0);
    lv_obj_remove_flag(grow, LV_OBJ_FLAG_SCROLLABLE);

    /* Effektkette ------------------------------------------------------ */
    lv_obj_t * chain = plain_row(body, 6);
    effect_tile(chain, "A",     "Compressor",     true,  UI_COL_COMP);
    effect_tile(chain, "B",     "Wah Wah",        false, UI_COL_WAH);
    effect_tile(chain, "C",     "Green Scream",   true,  UI_COL_DIST);
    effect_tile(chain, "D",     "Studio EQ",      false, UI_COL_EQ);
    effect_tile(chain, "STACK", "Amp",            true,  UI_COL_STACK);
    effect_tile(chain, "X",     "Pitch Shifter",  false, UI_COL_PITCH);
    effect_tile(chain, "MOD",   "Vintage Chorus", true,  UI_COL_MOD);
    effect_tile(chain, "DLY",   "Dual Delay",     true,  UI_COL_DLY);
    effect_tile(chain, "REV",   "Natural Hall",   true,  UI_COL_REV);

    /* Abstand zwischen Stomps und Footswitch-Feldern ------------------ */
    lv_obj_t * gap = lv_obj_create(body);
    lv_obj_set_size(gap, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(gap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gap, 0, 0);
    lv_obj_remove_flag(gap, LV_OBJ_FLAG_SCROLLABLE);

    /* Footswitch-Leiste ------------------------------------------------ */
    lv_obj_t * fs = plain_row(body, 6);
    fs_button(fs, "FS1", "Clean Twin",    "Slot 1", false);
    fs_button(fs, "FS2", "Plexi Crunch",  "Slot 2", false);
    fs_button(fs, "FS3", "65 Deluxe 2.0", "Slot 3", true);
    fs_button(fs, "FS4", "Lead Mars 800", "Slot 4", false);
    fs_button(fs, "FS5", "Ambient Swell", "Slot 5", false);
    fs_button(fs, "FS6", "Stomps",        "Modus",  false);

    return scr;
}

/* =========================================================================
 * Bearbeiten-Ansicht (entspricht edit_screen.xml)
 * ====================================================================== */

/* Entspricht section_button.xml */
static lv_obj_t * section_button(lv_obj_t * parent, const char * icon, const char * text)
{
    lv_obj_t * b = lv_obj_create(parent);
    lv_obj_set_size(b, 60, 52);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x454b53), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_pad_row(b, 3, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    label(b, icon, 0xcfd6df, &lv_font_montserrat_20);
    label(b, text, UI_COL_TEXT2, &lv_font_montserrat_12);
    return b;
}

/* Entspricht edit_tile.xml */
static lv_obj_t * edit_tile(lv_obj_t * parent, const char * slot,
                            const char * name, bool on, uint32_t color)
{
    lv_obj_t * t = lv_obj_create(parent);
    lv_obj_set_height(t, 140);
    lv_obj_set_width(t, 1);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(t, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_color(t, lv_color_hex(on ? color : UI_COL_LINE), 0);
    lv_obj_set_style_border_width(t, 2, 0);
    lv_obj_set_style_radius(t, 8, 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    lv_obj_set_style_pad_row(t, 4, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    label(t, slot, on ? color : UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_obj_t * n = label(t, name, on ? UI_COL_TEXT : UI_COL_TEXT3, &lv_font_montserrat_14);
    lv_obj_set_width(n, LV_PCT(100));
    lv_obj_set_flex_grow(n, 1);
    lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);

    lv_obj_t * pill = lv_obj_create(t);                /* ON/OFF-Feld */
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, lv_color_hex(on ? color : 0x2a2d31), 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, 4, 0);
    lv_obj_set_style_pad_hor(pill, 8, 0);
    lv_obj_set_style_pad_ver(pill, 3, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    label(pill, on ? "ON" : "OFF", on ? UI_COL_BG : 0x9aa1ab, &lv_font_montserrat_14);
    return t;
}

static lv_obj_t * icon_button(lv_obj_t * parent, const char * sym, bool selected)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_size(b, 44, 40);
    lv_obj_set_style_bg_color(b, lv_color_hex(selected ? UI_COL_SEL_BG : UI_COL_TILE), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(selected ? UI_COL_SEL_LINE : UI_COL_LINE), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t * l = label(b, sym, selected ? 0xffffff : 0xd6d9de, &lv_font_montserrat_18);
    lv_obj_center(l);
    return b;
}

static lv_obj_t * spacer(lv_obj_t * parent, int32_t w, int32_t h, bool grow)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    if (grow) lv_obj_set_flex_grow(o, 1);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t * ui_edit_create(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = screen_with_statusbar(&body);

    /* Kopfzeile ------------------------------------------------------- */
    lv_obj_t * head = plain_row(body, 6);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * badge = lv_obj_create(head);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x16345a), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_style_pad_hor(badge, 8, 0);
    lv_obj_set_style_pad_ver(badge, 4, 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    label(badge, "PERFORMANCE", 0xa6cbfa, &lv_font_montserrat_12);

    label(head, "012", UI_COL_TEXT, &lv_font_montserrat_22);
    lv_obj_t * pn = label(head, "Sunday Set", UI_COL_TEXT2, &lv_font_montserrat_18);
    lv_obj_set_width(pn, 110);
    lv_label_set_long_mode(pn, LV_LABEL_LONG_DOT);

    icon_button(head, LV_SYMBOL_DIRECTORY, false);    /* Bank-Uebersicht */
    s_btn_to_live = icon_button(head, LV_SYMBOL_PLAY, true);   /* Live-Ansicht */
    spacer(head, 1, 1, true);

    section_button(head, LV_SYMBOL_LIST,       "RIG");
    section_button(head, LV_SYMBOL_DOWNLOAD,   "INPUT");
    section_button(head, LV_SYMBOL_AUDIO,      "AMP");
    section_button(head, LV_SYMBOL_BARS,       "EQ");
    section_button(head, LV_SYMBOL_VOLUME_MAX, "CAB");
    section_button(head, LV_SYMBOL_UPLOAD,     "OUTPUT");
    section_button(head, LV_SYMBOL_SETTINGS,   "SYSTEM");
    section_button(head, LV_SYMBOL_BELL,       "TUNER");

    /* Slot, Rig-Name, Amp und Cab ------------------------------------- */
    lv_obj_t * sl = label(body, "Slot 3", UI_COL_ACCENT, &lv_font_montserrat_16);
    lv_obj_set_style_pad_left(sl, 12, 0);
    lv_obj_t * rig = label(body, "65 Deluxe 2.0 LQD N", UI_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_set_size(rig, LV_PCT(100), 56);
    lv_obj_set_style_pad_left(rig, 12, 0);
    lv_label_set_long_mode(rig, LV_LABEL_LONG_DOT);
    lv_obj_t * ac = label(body, "Amp: Deluxe Reverb   /   Cab: 1x12 Jensen", UI_COL_TEXT2,
                          &lv_font_montserrat_16);
    lv_obj_set_style_pad_left(ac, 12, 0);

    spacer(body, LV_PCT(100), 1, true);

    /* Effektkette ------------------------------------------------------ */
    lv_obj_t * chain = plain_row(body, 6);
    edit_tile(chain, "A",     "Compressor",     true,  UI_COL_COMP);
    edit_tile(chain, "B",     "Wah Wah",        false, UI_COL_WAH);
    edit_tile(chain, "C",     "Green Scream",   true,  UI_COL_DIST);
    edit_tile(chain, "D",     "Studio EQ",      false, UI_COL_EQ);
    edit_tile(chain, "STACK", "Deluxe Reverb",  true,  UI_COL_STACK);
    edit_tile(chain, "X",     "Pitch Shifter",  false, UI_COL_PITCH);
    edit_tile(chain, "MOD",   "Vintage Chorus", true,  UI_COL_MOD);
    edit_tile(chain, "DLY",   "Dual Delay",     true,  UI_COL_DLY);
    edit_tile(chain, "REV",   "Natural Hall",   true,  UI_COL_REV);

    /* Morph und Tempo ------------------------------------------------ */
    lv_obj_t * mr = plain_row(body, 14);
    lv_obj_set_height(mr, 44);
    lv_obj_set_style_pad_hor(mr, 12, 0);
    lv_obj_set_flex_align(mr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(mr, "Morph", UI_COL_TEXT3, &lv_font_montserrat_14);

    lv_obj_t * sld = lv_slider_create(mr);
    lv_obj_set_size(sld, 1, 10);
    lv_obj_set_flex_grow(sld, 1);
    lv_slider_set_value(sld, 38, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sld, lv_color_hex(0x2a2d31), 0);
    lv_obj_set_style_bg_color(sld, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld, lv_color_hex(UI_COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sld, 6, LV_PART_KNOB);

    label(mr, "38 %", UI_COL_TEXT2, &lv_font_montserrat_14);

    lv_obj_t * bpm = lv_obj_create(mr);
    lv_obj_set_size(bpm, LV_SIZE_CONTENT, 36);
    lv_obj_set_flex_flow(bpm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bpm, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bpm, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_color(bpm, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_border_width(bpm, 1, 0);
    lv_obj_set_style_radius(bpm, 8, 0);
    lv_obj_set_style_pad_hor(bpm, 12, 0);
    lv_obj_set_style_pad_ver(bpm, 0, 0);
    lv_obj_set_style_pad_column(bpm, 8, 0);
    lv_obj_remove_flag(bpm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * led = lv_obj_create(bpm);
    lv_obj_set_size(led, 10, 10);
    lv_obj_set_style_radius(led, 5, 0);
    lv_obj_set_style_bg_color(led, lv_color_hex(UI_COL_DIST), 0);
    lv_obj_set_style_border_width(led, 0, 0);
    label(bpm, "120 BPM", UI_COL_TEXT, &lv_font_montserrat_18);

    /* Abstand und Footswitch-Leiste ------------------------------------ */
    spacer(body, LV_PCT(100), 10, false);

    lv_obj_t * fs = plain_row(body, 6);
    fs_button(fs, "FS1", "Clean Twin",    "Slot 1", false);
    fs_button(fs, "FS2", "Plexi Crunch",  "Slot 2", false);
    fs_button(fs, "FS3", "65 Deluxe 2.0", "Slot 3", true);
    fs_button(fs, "FS4", "Lead Mars 800", "Slot 4", false);
    fs_button(fs, "FS5", "Ambient Swell", "Slot 5", false);
    fs_button(fs, "FS6", "Stomps",        "Modus",  false);

    return scr;
}

/* -------------------------------------------------------------------------
 * Start: beide Screens erzeugen, Live-Ansicht anzeigen.
 * Einzige "Funktion": Bearbeiten <-> Live per Touch umschalten.
 * ---------------------------------------------------------------------- */
static void to_edit_cb(lv_event_t * e)
{
    (void)e;
    lv_screen_load_anim(s_scr_edit, LV_SCREEN_LOAD_ANIM_FADE_IN, 150, 0, false);
}

static void to_live_cb(lv_event_t * e)
{
    (void)e;
    lv_screen_load_anim(s_scr_live, LV_SCREEN_LOAD_ANIM_FADE_IN, 150, 0, false);
}

void ui_start(void)
{
    s_scr_live = ui_live_create();
    s_scr_edit = ui_edit_create();
    lv_obj_add_event_cb(s_btn_to_edit, to_edit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_btn_to_live, to_live_cb, LV_EVENT_CLICKED, NULL);

    /* Startbildschirm abschliessen (Bootzeit stoppen, danach Ueberblendung).
     * Ohne vorherigen ui_boot_create() wird die Live-Ansicht direkt geladen. */
    if (ui_boot_is_active()) ui_boot_finish(s_scr_live);
    else                     lv_screen_load(s_scr_live);
}

/* -------------------------------------------------------------------------
 * Optional: dieselbe Ansicht aus den XML-Dateien laden (LV_USE_XML = 1)
 * ---------------------------------------------------------------------- */
#if LV_USE_XML
void ui_xml_register_fonts(void)
{
    lv_xml_register_font(NULL, "lv_font_montserrat_12", &lv_font_montserrat_12);
    lv_xml_register_font(NULL, "lv_font_montserrat_14", &lv_font_montserrat_14);
    lv_xml_register_font(NULL, "lv_font_montserrat_16", &lv_font_montserrat_16);
    lv_xml_register_font(NULL, "lv_font_montserrat_18", &lv_font_montserrat_18);
    lv_xml_register_font(NULL, "lv_font_montserrat_20", &lv_font_montserrat_20);
    lv_xml_register_font(NULL, "lv_font_montserrat_22", &lv_font_montserrat_22);
    lv_xml_register_font(NULL, "lv_font_montserrat_24", &lv_font_montserrat_24);
    lv_xml_register_font(NULL, "lv_font_montserrat_28", &lv_font_montserrat_28);
    lv_xml_register_font(NULL, "lv_font_montserrat_48", &lv_font_montserrat_48);
}

static void ui_xml_load(const char * path)
{
    static bool loaded = false;
    char file[128];
    if (loaded) return;

    ui_xml_register_fonts();

    /* globals.xml zuerst, damit die Konstanten (#bg, #tile ...) bekannt sind */
    lv_snprintf(file, sizeof(file), "%sglobals.xml", path);
    lv_xml_register_component_from_file(file);

    /* danach Komponenten und Screens */
    lv_xml_load_all_from_path(path);
    loaded = true;
}

lv_obj_t * ui_live_create_from_xml(const char * path)
{
    ui_xml_load(path);
    return lv_xml_create_screen("live_screen");
}

lv_obj_t * ui_edit_create_from_xml(const char * path)
{
    ui_xml_load(path);
    return lv_xml_create_screen("edit_screen");
}
#endif
