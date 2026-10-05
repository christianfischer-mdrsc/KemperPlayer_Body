/**
 * @file ui_edit.c
 * Bearbeiten-Ansicht im Stil des Rig Managers:
 *
 *  - Bereiche RIG, INPUT, AMP, EQ, CAB, OUTPUT (Parameter-Dialoge),
 *    SYSTEM und TUNER (eigene Ansichten)
 *  - Effektkette: Antippen oeffnet das Modul (Effekttyp, Ein/Aus,
 *    Zuweisung zu den Effect Buttons I-IIII)
 *  - Rig umbenennen, Morph, Tempo
 */
#include "ui_common.h"
#include "ui_screens.h"
#include <string.h>

static struct {
    lv_obj_t * bank_lbl;
    lv_obj_t * bank_dot;
    lv_obj_t * slot;
    lv_obj_t * rig;
    lv_obj_t * stack;
    lv_obj_t * edited;
    lv_obj_t * morph;
    lv_obj_t * morph_val;
    lv_obj_t * bpm;
    lv_obj_t * tempo_sw;
    ui_chain_t chain;
    ui_fsbar_t fs;
} w;

/* ======================================================================
 * Modul-Dialog
 * ==================================================================== */
static struct {
    kp_mod_t   mod;
    lv_obj_t * cur;
    lv_obj_t * sw;
    lv_obj_t * fxb[KP_FX_BUTTONS];
    lv_obj_t * types[40];
    uint8_t    ntypes;
} md;

static const char * const FXB_TXT[KP_FX_BUTTONS] = { "I", "II", "III", "IIII" };

static void module_dialog_refresh(uint32_t chg)
{
    (void)chg;
    const kp_module_t * m = &kp_rig()->mod[md.mod];
    const kp_effect_t * fx = kp_effect(m->type);

    lv_label_set_text(md.cur, m->type ? fx->name : "leer");
    lv_obj_set_style_text_color(md.cur,
        lv_color_hex(m->type ? kp_cat_color(fx->cat) : UI_COL_TEXT3), 0);

    if (m->on) lv_obj_add_state(md.sw, LV_STATE_CHECKED);
    else       lv_obj_remove_state(md.sw, LV_STATE_CHECKED);
    if (m->type) lv_obj_remove_state(md.sw, LV_STATE_DISABLED);
    else         lv_obj_add_state(md.sw, LV_STATE_DISABLED);

    for (uint8_t b = 0; b < KP_FX_BUTTONS; b++) {
        ui_button_set_selected(md.fxb[b], (kp_rig()->fx_btn[b] >> md.mod) & 1u);
        if (m->type) lv_obj_remove_state(md.fxb[b], LV_STATE_DISABLED);
        else         lv_obj_add_state(md.fxb[b], LV_STATE_DISABLED);
    }
    for (uint8_t t = 0; t < md.ntypes; t++) ui_button_set_selected(md.types[t], t == m->type);
}

static void md_switch_cb(lv_event_t * e)
{
    kp_module_set_on(md.mod, lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void md_fxb_cb(lv_event_t * e)
{
    uint8_t b = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (!kp_rig()->mod[md.mod].type) return;
    bool now = (kp_rig()->fx_btn[b] >> md.mod) & 1u;
    kp_fx_button_assign(b, md.mod, !now);
}

static void md_type_cb(lv_event_t * e)
{
    kp_module_set_type(md.mod, (uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void module_dialog(kp_mod_t m)
{
    char title[24];
    lv_obj_t * c;
    lv_snprintf(title, sizeof(title), "Modul %s", kp_module_name(m));
    ui_dialog_open(title, 900, 540, &c);
    md.mod = m;

    /* aktueller Effekt + Ein/Aus */
    lv_obj_t * r1 = ui_row(c, 14);
    ui_label(r1, "Effekt", UI_COL_TEXT3, &lv_font_montserrat_16);
    md.cur = ui_label(r1, "", UI_COL_TEXT, &lv_font_montserrat_24);
    ui_spacer(r1, 1, 1, true);
    ui_label(r1, "Ein / Aus", UI_COL_TEXT3, &lv_font_montserrat_16);
    md.sw = lv_switch_create(r1);
    lv_obj_set_size(md.sw, 70, 36);
    lv_obj_set_style_bg_color(md.sw, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(md.sw, md_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Effect Buttons */
    lv_obj_t * r2 = ui_row(c, 10);
    lv_obj_t * l = ui_label(r2, "Effect Buttons", UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_obj_set_width(l, 140);
    for (uint8_t b = 0; b < KP_FX_BUTTONS; b++) {
        md.fxb[b] = ui_button(r2, FXB_TXT[b], &lv_font_montserrat_18, false);
        lv_obj_set_width(md.fxb[b], 76);
        lv_obj_add_event_cb(md.fxb[b], md_fxb_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)b);
    }
    ui_label(r2, "schaltet dieses Modul mit", UI_COL_TEXT3, &lv_font_montserrat_14);

    /* Effekttypen, nach Kategorie sortiert */
    ui_label(c, "Effekttyp", UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_obj_t * grid = ui_row(c, 8);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 8, 0);
    md.ntypes = kp_effect_count();
    if (md.ntypes > 40) md.ntypes = 40;
    for (uint8_t t = 0; t < md.ntypes; t++) {
        const kp_effect_t * fx = kp_effect(t);
        lv_obj_t * b = ui_button(grid, fx->name, &lv_font_montserrat_16, false);
        lv_obj_set_size(b, 202, 46);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(b, 5, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(kp_cat_color(fx->cat)), 0);
        lv_obj_add_event_cb(b, md_type_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)t);
        md.types[t] = b;
    }

    module_dialog_refresh(KP_CHG_ALL);
    ui_dialog_set_refresh(module_dialog_refresh);
}

/* ======================================================================
 * Umbenennen
 * ==================================================================== */
static lv_obj_t * s_ta;

static void rename_ok_cb(lv_event_t * e)
{
    (void)e;
    if (s_ta) kp_rig_rename(lv_textarea_get_text(s_ta));
    s_ta = NULL;
    ui_dialog_close();
}

static void kb_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) rename_ok_cb(e);
    else if (lv_event_get_code(e) == LV_EVENT_CANCEL) { s_ta = NULL; ui_dialog_close(); }
}

static void rename_dialog(void)
{
    lv_obj_t * c;
    ui_dialog_open("Rig umbenennen", 980, 540, &c);

    lv_obj_t * r = ui_row(c, 10);
    s_ta = lv_textarea_create(r);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, KP_NAME_LEN - 1);
    lv_textarea_set_text(s_ta, kp_rig()->name);
    lv_obj_set_width(s_ta, 1);
    lv_obj_set_flex_grow(s_ta, 1);
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(s_ta, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_border_color(s_ta, lv_color_hex(UI_COL_SEL_LINE), 0);
    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);

    lv_obj_t * ok = ui_button(r, "OK", &lv_font_montserrat_20, true);
    lv_obj_set_width(ok, 100);
    lv_obj_add_event_cb(ok, rename_ok_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * kb = lv_keyboard_create(c);
    lv_obj_set_size(kb, LV_PCT(100), 320);
    lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_TILE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x2a2d31), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_SEL_BG), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_hex(UI_COL_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(UI_COL_LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
}

/* ======================================================================
 * Ansicht
 * ==================================================================== */
static void update_rig(void)
{
    const kp_rig_t * r = kp_rig();
    ui_bank_chip_update(w.bank_lbl, w.bank_dot);
    lv_label_set_text_fmt(w.slot, "Rig %u", (unsigned)(kp_slot() + 1));
    if (r->name[0]) {
        lv_label_set_text(w.rig, r->name);
        lv_obj_set_style_text_color(w.rig, lv_color_hex(UI_COL_TEXT), 0);
    } else {
        lv_label_set_text(w.rig, "Leeres Rig");
        lv_obj_set_style_text_color(w.rig, lv_color_hex(UI_COL_TEXT3), 0);
    }
    lv_label_set_text_fmt(w.stack, "Amp: %s   /   Cab: %s",
                          r->amp[0] ? r->amp : "-", r->cab[0] ? r->cab : "-");
    if (r->edited) lv_obj_set_hidden(w.edited, false);
    else           lv_obj_set_hidden(w.edited, true);
}

static void update_params(void)
{
    const kp_rig_t * r = kp_rig();
    if (lv_slider_get_value(w.morph) != r->morph)
        lv_slider_set_value(w.morph, r->morph, LV_ANIM_OFF);
    lv_label_set_text_fmt(w.morph_val, "%u %%", r->morph);
}

static void update_tempo(void)
{
    const kp_rig_t * r = kp_rig();
    if (r->tempo_on) lv_label_set_text_fmt(w.bpm, "%u BPM", r->tempo_bpm);
    else             lv_label_set_text(w.bpm, "-- BPM");
    if (r->tempo_on) lv_obj_add_state(w.tempo_sw, LV_STATE_CHECKED);
    else             lv_obj_remove_state(w.tempo_sw, LV_STATE_CHECKED);
}

static void refresh(uint32_t chg)
{
    if (chg & (KP_CHG_RIG | KP_CHG_NAMES)) update_rig();
    if (chg & (KP_CHG_MODULES | KP_CHG_RIG)) ui_chain_update(&w.chain);
    if (chg & (KP_CHG_PARAMS | KP_CHG_RIG)) update_params();
    if (chg & (KP_CHG_TEMPO | KP_CHG_RIG)) update_tempo();
    ui_fsbar_update(&w.fs);
}

static void bank_cb(lv_event_t * e)   { (void)e; ui_nav_go(UI_SCR_BANKS); }
static void live_cb(lv_event_t * e)   { (void)e; ui_nav_go(UI_SCR_LIVE); }
static void rename_cb(lv_event_t * e) { (void)e; rename_dialog(); }
static void tap_cb(lv_event_t * e)    { (void)e; kp_tap(lv_tick_get()); }

static void bpm_step_cb(lv_event_t * e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    kp_set_tempo((uint16_t)(kp_rig()->tempo_bpm + d));
}

static void tempo_sw_cb(lv_event_t * e)
{
    kp_tempo_enable(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void morph_cb(lv_event_t * e)
{
    kp_set_morph((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

static void tile_cb(lv_event_t * e)
{
    uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (w.chain.id[i] == KP_CHAIN_STACK) ui_param_dialog(KP_SEC_AMP);
    else                                 module_dialog((kp_mod_t)w.chain.id[i]);
}

/* Bereiche oben rechts */
typedef struct { const char * icon; const char * text; int action; } section_t;
enum { ACT_SEC = 0, ACT_SYSTEM = 100, ACT_TUNER = 101 };
static const section_t SECTIONS[] = {
    { LV_SYMBOL_LIST,       "RIG",    ACT_SEC + KP_SEC_RIG    },
    { LV_SYMBOL_DOWNLOAD,   "INPUT",  ACT_SEC + KP_SEC_INPUT  },
    { LV_SYMBOL_AUDIO,      "AMP",    ACT_SEC + KP_SEC_AMP    },
    { LV_SYMBOL_BARS,       "EQ",     ACT_SEC + KP_SEC_EQ     },
    { LV_SYMBOL_VOLUME_MAX, "CAB",    ACT_SEC + KP_SEC_CAB    },
    { LV_SYMBOL_UPLOAD,     "OUTPUT", ACT_SEC + KP_SEC_OUTPUT },
    { LV_SYMBOL_SETTINGS,   "SYSTEM", ACT_SYSTEM },
    { LV_SYMBOL_BELL,       "TUNER",  ACT_TUNER  },
};

static void section_cb(lv_event_t * e)
{
    int a = (int)(intptr_t)lv_event_get_user_data(e);
    if (a == ACT_SYSTEM)     ui_nav_go(UI_SCR_SETTINGS);
    else if (a == ACT_TUNER) ui_nav_go(UI_SCR_TUNER);
    else                     ui_param_dialog((kp_section_t)(a - ACT_SEC));
}

static lv_obj_t * section_button(lv_obj_t * parent, const section_t * s)
{
    lv_obj_t * b = ui_button(parent, s->icon, &lv_font_montserrat_20, false);
    lv_obj_set_size(b, 62, 54);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 3, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(0xcfd6df), 0);
    ui_label(b, s->text, UI_COL_TEXT2, &lv_font_montserrat_12);
    lv_obj_add_event_cb(b, section_cb, LV_EVENT_CLICKED, (void *)(intptr_t)s->action);
    return b;
}

lv_obj_t * ui_edit_build(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = ui_screen_base(&body);

    /* Kopfzeile: Bank, Live, Bereiche */
    lv_obj_t * head = ui_row(body, 6);
    lv_obj_t * chip = ui_bank_chip(head, &w.bank_lbl, &w.bank_dot);
    lv_obj_add_event_cb(chip, bank_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * live = ui_icon_button(head, LV_SYMBOL_PLAY, true);
    lv_obj_add_event_cb(live, live_cb, LV_EVENT_CLICKED, NULL);
    ui_spacer(head, 1, 1, true);
    for (unsigned i = 0; i < sizeof(SECTIONS) / sizeof(SECTIONS[0]); i++)
        section_button(head, &SECTIONS[i]);

    /* Rig */
    lv_obj_t * r1 = ui_row(body, 12);
    lv_obj_set_style_pad_left(r1, 12, 0);
    lv_obj_set_style_pad_top(r1, 4, 0);
    w.slot = ui_label(r1, "", UI_COL_ACCENT, &lv_font_montserrat_16);
    w.edited = ui_label(r1, "bearbeitet", 0xe8c93a, &lv_font_montserrat_16);

    lv_obj_t * r2 = ui_row(body, 12);
    w.rig = ui_label(r2, "", UI_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_set_height(w.rig, 56);
    lv_obj_set_style_pad_left(w.rig, 12, 0);
    lv_obj_set_style_max_width(w.rig, 860, 0);
    lv_label_set_long_mode(w.rig, LV_LABEL_LONG_DOT);
    lv_obj_t * ren = ui_icon_button(r2, LV_SYMBOL_EDIT, false);
    lv_obj_add_event_cb(ren, rename_cb, LV_EVENT_CLICKED, NULL);

    w.stack = ui_label(body, "", UI_COL_TEXT2, &lv_font_montserrat_16);
    lv_obj_set_style_pad_left(w.stack, 12, 0);

    ui_spacer(body, LV_PCT(100), 1, true);
    ui_chain_create(&w.chain, body, true, tile_cb);

    /* Morph und Tempo */
    lv_obj_t * mr = ui_row(body, 12);
    lv_obj_set_height(mr, 52);
    lv_obj_set_style_pad_hor(mr, 12, 0);
    ui_label(mr, "Morph", UI_COL_TEXT3, &lv_font_montserrat_16);
    w.morph = ui_slider(mr, 0, 100, kp_rig()->morph);
    lv_obj_set_style_margin_hor(w.morph, 10, 0);
    lv_obj_add_event_cb(w.morph, morph_cb, LV_EVENT_VALUE_CHANGED, NULL);
    w.morph_val = ui_label(mr, "", UI_COL_TEXT2, &lv_font_montserrat_16);
    lv_obj_set_width(w.morph_val, 56);

    ui_spacer(mr, 16, 1, false);
    ui_label(mr, "Tempo", UI_COL_TEXT3, &lv_font_montserrat_16);
    w.tempo_sw = lv_switch_create(mr);
    lv_obj_set_size(w.tempo_sw, 56, 30);
    lv_obj_set_style_bg_color(w.tempo_sw, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.tempo_sw, tempo_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * minus = ui_icon_button(mr, LV_SYMBOL_MINUS, false);
    lv_obj_set_size(minus, 44, 42);
    lv_obj_add_event_cb(minus, bpm_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t * bpm = ui_button(mr, "", &lv_font_montserrat_18, false);
    lv_obj_set_size(bpm, 130, 42);
    lv_obj_add_event_cb(bpm, tap_cb, LV_EVENT_PRESSED, NULL);
    w.bpm = lv_obj_get_child(bpm, 0);
    lv_obj_t * plus = ui_icon_button(mr, LV_SYMBOL_PLUS, false);
    lv_obj_set_size(plus, 44, 42);
    lv_obj_add_event_cb(plus, bpm_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    ui_spacer(body, LV_PCT(100), 4, false);
    ui_fsbar_create(&w.fs, body);

    update_rig();
    update_params();
    update_tempo();
    ui_nav_set_refresh(scr, refresh);
    return scr;
}
