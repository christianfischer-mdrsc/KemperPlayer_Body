/**
 * @file ui_common.c
 * Gemeinsame Bausteine aller Ansichten (siehe ui_common.h).
 */
#include <string.h>
#include "ui_common.h"
#include "ui_statusbar.h"
#include "ui_screens.h"
#include "kemper_link.h"

#define COL_PRESSED   0x2a2d31
#define COL_DIM       0x5a5f66

/* ------------------------------------------------------------------------
 * Stile (einmal angelegt, von allen Objekten geteilt: spart LVGL-Speicher)
 * --------------------------------------------------------------------- */
static lv_style_t st_plain;      /* unsichtbarer Container */
static lv_style_t st_panel;      /* Kachel */
static lv_style_t st_btn;        /* Schaltflaeche */
static lv_style_t st_btn_sel;    /* ausgewaehlt */
static lv_style_t st_pressed;    /* gedrueckt */
static bool       s_theme_ok;

void ui_theme_init(void)
{
    if (s_theme_ok) return;
    s_theme_ok = true;

    lv_style_init(&st_plain);
    lv_style_set_bg_opa(&st_plain, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_plain, 0);
    lv_style_set_radius(&st_plain, 0);
    lv_style_set_pad_all(&st_plain, 0);
    lv_style_set_shadow_width(&st_plain, 0);

    lv_style_init(&st_panel);
    lv_style_set_bg_color(&st_panel, lv_color_hex(UI_COL_TILE));
    lv_style_set_bg_opa(&st_panel, LV_OPA_COVER);
    lv_style_set_border_color(&st_panel, lv_color_hex(UI_COL_LINE));
    lv_style_set_border_width(&st_panel, 1);
    lv_style_set_radius(&st_panel, 10);
    lv_style_set_pad_all(&st_panel, 10);
    lv_style_set_shadow_width(&st_panel, 0);

    lv_style_init(&st_btn);
    lv_style_set_bg_color(&st_btn, lv_color_hex(UI_COL_TILE));
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn, lv_color_hex(UI_COL_LINE));
    lv_style_set_border_width(&st_btn, 1);
    lv_style_set_radius(&st_btn, 10);
    lv_style_set_shadow_width(&st_btn, 0);
    lv_style_set_pad_hor(&st_btn, 14);
    lv_style_set_pad_ver(&st_btn, 6);
    lv_style_set_text_color(&st_btn, lv_color_hex(UI_COL_TEXT));

    lv_style_init(&st_btn_sel);
    lv_style_set_bg_color(&st_btn_sel, lv_color_hex(UI_COL_SEL_BG));
    lv_style_set_border_color(&st_btn_sel, lv_color_hex(UI_COL_SEL_LINE));

    lv_style_init(&st_pressed);
    lv_style_set_bg_color(&st_pressed, lv_color_hex(COL_PRESSED));
}

/* ------------------------------------------------------------------------
 * Navigation
 * --------------------------------------------------------------------- */
static ui_scr_t   s_cur = UI_SCR_HOME;
static ui_scr_t   s_prev = UI_SCR_HOME;
static lv_obj_t * s_refresh_owner;
static void     (*s_refresh)(uint32_t);
static void     (*s_dialog_refresh)(uint32_t);
static void     (*s_dialog_close)(void);

static void on_model(uint32_t chg)
{
    if (chg & KP_CHG_SYSTEM) ui_statusbar_set_usb(kp_connected());
    if (s_refresh) s_refresh(chg);
    if (s_dialog_refresh) s_dialog_refresh(chg);
}

static void scr_delete_cb(lv_event_t * e)
{
    lv_obj_t * scr = lv_event_get_target(e);
    if (scr == s_refresh_owner) {
        s_refresh = NULL;
        s_refresh_owner = NULL;
    }
}

void ui_nav_set_refresh(lv_obj_t * scr, void (*fn)(uint32_t))
{
    s_refresh_owner = scr;
    s_refresh = fn;
    lv_obj_add_event_cb(scr, scr_delete_cb, LV_EVENT_DELETE, NULL);
}

lv_obj_t * ui_nav_build(ui_scr_t scr)
{
    ui_theme_init();
    kp_set_listener(on_model);
    ui_dialog_close();
    if (scr != s_cur) s_prev = s_cur;
    s_cur = scr;
    switch (scr) {
    case UI_SCR_EDIT:     return ui_edit_build();
    case UI_SCR_BANKS:    return ui_banks_build();
    case UI_SCR_TUNER:    return ui_tuner_build();
    case UI_SCR_SETTINGS: return ui_settings_build();
    case UI_SCR_HOME:     return ui_home_build();
    case UI_SCR_LIVE:
    default:              return ui_live_build();
    }
}

void ui_nav_go(ui_scr_t scr)
{
    lv_obj_t * s = ui_nav_build(scr);
    lv_screen_load_anim(s, LV_SCREEN_LOAD_ANIM_FADE_IN, 150, 0, true);
}

ui_scr_t ui_nav_current(void)  { return s_cur; }
ui_scr_t ui_nav_previous(void) { return s_prev; }

/* ------------------------------------------------------------------------
 * Bausteine
 * --------------------------------------------------------------------- */
lv_obj_t * ui_label(lv_obj_t * parent, const char * txt, uint32_t color, const lv_font_t * font)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t * plain(lv_obj_t * parent)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &st_plain, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t * ui_row(lv_obj_t * parent, int32_t gap)
{
    lv_obj_t * o = plain(parent);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, gap, 0);
    return o;
}

lv_obj_t * ui_col(lv_obj_t * parent, int32_t gap)
{
    lv_obj_t * o = plain(parent);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

lv_obj_t * ui_spacer(lv_obj_t * parent, int32_t w, int32_t h, bool grow)
{
    lv_obj_t * o = plain(parent);
    lv_obj_set_size(o, w, h);
    if (grow) lv_obj_set_flex_grow(o, 1);
    return o;
}

lv_obj_t * ui_panel(lv_obj_t * parent)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &st_panel, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t * btn_base(lv_obj_t * parent, bool selected)
{
    lv_obj_t * b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_sel, LV_STATE_CHECKED);
    lv_obj_add_style(b, &st_pressed, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    if (selected) lv_obj_add_state(b, LV_STATE_CHECKED);
    return b;
}

lv_obj_t * ui_button(lv_obj_t * parent, const char * txt, const lv_font_t * font, bool selected)
{
    lv_obj_t * b = btn_base(parent, selected);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 48);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t * ui_icon_button(lv_obj_t * parent, const char * sym, bool selected)
{
    lv_obj_t * b = btn_base(parent, selected);
    lv_obj_set_size(b, 52, 48);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t * ui_slider(lv_obj_t * parent, int32_t min, int32_t max, int32_t value)
{
    lv_obj_t * sl = lv_slider_create(parent);
    lv_obj_set_height(sl, 10);
    lv_obj_set_width(sl, 1);
    lv_obj_set_flex_grow(sl, 1);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x33373d), 0);
    lv_obj_set_style_bg_color(sl, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(UI_COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 20);
    return sl;
}

void ui_button_set_selected(lv_obj_t * btn, bool selected)
{
    if (selected) lv_obj_add_state(btn, LV_STATE_CHECKED);
    else          lv_obj_remove_state(btn, LV_STATE_CHECKED);
}

lv_obj_t * ui_screen_base(lv_obj_t ** body)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 1024, 600);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_statusbar_create(scr);

    lv_obj_t * b = plain(scr);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(b, 8, 0);
    lv_obj_set_style_pad_row(b, 6, 0);
    *body = b;
    return scr;
}

lv_obj_t * ui_bank_chip(lv_obj_t * parent, lv_obj_t ** label_out, lv_obj_t ** dot_out)
{
    lv_obj_t * chip = btn_base(parent, false);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 48);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chip, 10, 0);

    lv_obj_t * dot = lv_obj_create(chip);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * l = ui_label(chip, "", UI_COL_TEXT, &lv_font_montserrat_24);
    *label_out = l;
    *dot_out = dot;
    ui_bank_chip_update(l, dot);
    return chip;
}

void ui_bank_chip_update(lv_obj_t * label, lv_obj_t * dot)
{
    if (kp_bank_known()) {
        lv_label_set_text_fmt(label, "Bank %u", (unsigned)(kp_bank() + 1));
        lv_obj_set_style_bg_color(dot, lv_color_hex(kp_bank_color(kp_bank())), 0);
    } else {
        lv_label_set_text(label, "Bank ?");     /* Kemper hat sie noch nicht gemeldet */
        lv_obj_set_style_bg_color(dot, lv_color_hex(COL_DIM), 0);
    }
}

static void back_cb(lv_event_t * e)
{
    ui_nav_go((ui_scr_t)(uintptr_t)lv_event_get_user_data(e));
}

lv_obj_t * ui_title_bar(lv_obj_t * body, const char * title, ui_scr_t back_to)
{
    lv_obj_t * row = ui_row(body, 14);
    lv_obj_t * back = ui_icon_button(row, LV_SYMBOL_LEFT, false);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)back_to);
    ui_label(row, title, UI_COL_TEXT, &lv_font_montserrat_28);
    return row;
}

/* ------------------------------------------------------------------------
 * Effektkette
 * --------------------------------------------------------------------- */
void ui_chain_create(ui_chain_t * c, lv_obj_t * parent, bool compact,
                     lv_event_cb_t cb, lv_event_cb_t long_cb)
{
    lv_obj_t * row = ui_row(parent, 6);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    c->compact = compact;
    c->n = kp_chain(c->id);

    for (uint8_t i = 0; i < c->n; i++) {
        lv_obj_t * t = btn_base(row, false);
        lv_obj_set_height(t, compact ? 132 : 176);
        lv_obj_set_width(t, 1);
        lv_obj_set_flex_grow(t, 1);
        lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(t, 2, 0);
        lv_obj_set_style_pad_all(t, 8, 0);
        lv_obj_set_style_pad_row(t, compact ? 4 : 6, 0);
        /* SHORT_CLICKED kommt nur, wenn nicht lange gedrueckt wurde */
        if (cb) lv_obj_add_event_cb(t, cb, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
        if (long_cb) lv_obj_add_event_cb(t, long_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);

        c->tile[i]  = t;
        c->slot[i]  = ui_label(t, "", UI_COL_TEXT3,
                               compact ? &lv_font_montserrat_18 : &lv_font_montserrat_22);
        c->name[i]  = ui_label(t, "", UI_COL_TEXT,
                               compact ? &lv_font_montserrat_14 : &lv_font_montserrat_16);
        lv_obj_set_width(c->name[i], LV_PCT(100));
        lv_obj_set_flex_grow(c->name[i], 1);
        lv_label_set_long_mode(c->name[i], LV_LABEL_LONG_WRAP);
        c->state[i] = ui_label(t, "", UI_COL_TEXT3,
                               compact ? &lv_font_montserrat_16 : &lv_font_montserrat_20);
    }
    ui_chain_update(c);
}

void ui_chain_update(ui_chain_t * c)
{
    const kp_rig_t * r = kp_rig();
    for (uint8_t i = 0; i < c->n; i++) {
        uint32_t border = UI_COL_LINE, scol = UI_COL_TEXT3, ncol = UI_COL_TEXT;
        const char * name;
        const char * state;

        if (c->id[i] == KP_CHAIN_STACK) {
            lv_label_set_text(c->slot[i], "STACK");
            name  = r->amp[0] ? r->amp : "Amp";
            state = r->cab[0] ? r->cab : "Cab";
            border = UI_COL_STACK;
            scol = UI_COL_STACK;
            if (!r->amp[0]) ncol = COL_DIM;
            lv_label_set_long_mode(c->state[i], LV_LABEL_LONG_DOT);
            lv_obj_set_width(c->state[i], LV_PCT(100));
        } else {
            const kp_module_t * m = &r->mod[c->id[i]];
            uint32_t cc = kp_cat_color(kp_effect_cat(m->type));
            lv_label_set_text(c->slot[i], kp_module_name((kp_mod_t)c->id[i]));
            if (!m->type) {
                name = "leer";
                state = "";
                ncol = COL_DIM;
            } else {
                name = kp_module_fx_name((kp_mod_t)c->id[i]);
                state = m->on ? "ON" : "OFF";
                if (m->on) { border = cc; scol = cc; }
                else       { ncol = UI_COL_TEXT3; }
            }
        }
        lv_label_set_text(c->name[i], name);
        lv_label_set_text(c->state[i], state);
        lv_obj_set_style_border_color(c->tile[i], lv_color_hex(border), 0);
        lv_obj_set_style_text_color(c->slot[i], lv_color_hex(scol), 0);
        lv_obj_set_style_text_color(c->state[i], lv_color_hex(scol), 0);
        lv_obj_set_style_text_color(c->name[i], lv_color_hex(ncol), 0);
    }
}

/* ------------------------------------------------------------------------
 * Signalpfad (Live-Ansicht)
 * --------------------------------------------------------------------- */
typedef enum { PE_FIXED = 0, PE_MOD, PE_STACK } ui_path_kind_t;

#define PATH_W        1008      /* Breite des Inhalts (Screen minus Rand) */
#define PATH_GAP      16        /* Abstand der Kacheln, darin der Pfeil */
#define PATH_ROW_GAP  30        /* Abstand der Zeilen, darin die Rueckfuehrung */
#define PATH_TR_X     22        /* Transpose-Chip */
#define PATH_TR_W     74
#define PATH_TR_H     58
#define PATH_RIGHT    12        /* Platz rechts fuer die Rueckfuehrung */
#define COL_PATH      0x4a5058

static const char * const FIX_TAG[KP_FIX_COUNT] = {
    "TRANSP", "GATE", "COMP", "BOOST", "WAH", "CHORUS", "AIR", "DOUBLE"
};
/* Namen fuer schmale Kacheln (Level III): von Hand getrennt, sonst bricht
 * LVGL lange Woerter mitten im Wort um */
static const char * const FIX_NARROW[KP_FIX_COUNT] = {
    "Transpose", "Noise\nGate", "Compres-\nsor", "Pure\nBooster", "Wah Wah",
    "Vintage\nChorus", "Air\nChorus", "Double\nTracker"
};

static void path_tap_cb(lv_event_t * e)
{
    const ui_path_t * p = lv_event_get_user_data(e);
    lv_obj_t * t = lv_event_get_current_target(e);
    for (uint8_t i = 0; i < p->n; i++) {
        if (p->tile[i] != t) continue;
        if (p->kind[i] == PE_FIXED)    kp_fixed_toggle((kp_fixed_t)p->id[i]);
        else if (p->kind[i] == PE_MOD) kp_module_toggle((kp_mod_t)p->id[i]);
    }
}

static void path_long_cb(lv_event_t * e)
{
    const ui_path_t * p = lv_event_get_user_data(e);
    lv_obj_t * t = lv_event_get_current_target(e);
    for (uint8_t i = 0; i < p->n; i++) {
        if (p->tile[i] != t) continue;
        if (p->kind[i] == PE_FIXED)    ui_fixed_dialog((kp_fixed_t)p->id[i]);
        else if (p->kind[i] == PE_MOD) ui_fx_dialog((kp_mod_t)p->id[i]);
        else                           ui_param_dialog(KP_SEC_AMP);
    }
}

static void tr_tap_cb(lv_event_t * e)  { (void)e; kp_fixed_toggle(KP_FIX_TRANSPOSE); }
static void tr_long_cb(lv_event_t * e) { (void)e; ui_transpose_dialog(); }

static void path_arrow(lv_obj_t * box, const char * sym, int32_t cx, int32_t cy)
{
    lv_obj_t * a = ui_label(box, sym, 0x6a7079, &lv_font_montserrat_12);
    lv_obj_update_layout(a);
    lv_obj_set_pos(a, cx - lv_obj_get_width(a) / 2, cy - lv_obj_get_height(a) / 2);
}

static void path_add(ui_path_t * p, uint8_t kind, uint8_t id)
{
    p->kind[p->n] = kind;
    p->id[p->n] = id;
    p->n++;
}

void ui_path_create(ui_path_t * p, lv_obj_t * parent, int32_t th)
{
    uint8_t mods[KP_CHAIN_MAX];
    const uint8_t nm = kp_chain(mods);
    uint8_t n1, stack_at = 0;

    /* Reihenfolge = Signalfluss */
    p->n = 0;
    for (uint8_t f = KP_FIX_GATE; f < KP_FIX_INPUT_COUNT; f++) path_add(p, PE_FIXED, f);
    for (uint8_t i = 0; i < nm; i++) {
        if (mods[i] == KP_CHAIN_STACK) { stack_at = p->n; path_add(p, PE_STACK, 0); }
        else                           path_add(p, PE_MOD, mods[i]);
    }
    for (uint8_t f = KP_FIX_INPUT_COUNT; f < KP_FIX_COUNT; f++) path_add(p, PE_FIXED, f);
    n1 = stack_at;                                  /* Zeile 2 beginnt mit dem Stack */

    const uint8_t n2 = (uint8_t)(p->n - n1);
    const uint8_t ncol = n1 > n2 ? n1 : n2;
    const int32_t x0 = PATH_TR_X + PATH_TR_W + PATH_GAP;
    const int32_t xr = PATH_W - PATH_RIGHT / 2;
    const int32_t tw = (PATH_W - PATH_RIGHT - x0 - (ncol - 1) * PATH_GAP) / ncol;
    p->narrow = tw < 100;
    const int32_t y1 = th / 2, y2 = th + PATH_ROW_GAP + th / 2, ym = th + PATH_ROW_GAP / 2;
    const int32_t xl = PATH_TR_X + PATH_TR_W / 2;
    const int32_t xe = x0 + n2 * (tw + PATH_GAP);   /* hinter der letzten Kachel */

    lv_obj_t * box = plain(parent);
    lv_obj_set_size(box, PATH_W, 2 * th + PATH_ROW_GAP);

    /* Linie: IN -> Zeile 1 -> runter -> zurueck -> Zeile 2 -> OUT */
    p->pts[0] = (lv_point_precise_t){ 0,  y1 };
    p->pts[1] = (lv_point_precise_t){ xr, y1 };
    p->pts[2] = (lv_point_precise_t){ xr, ym };
    p->pts[3] = (lv_point_precise_t){ xl, ym };
    p->pts[4] = (lv_point_precise_t){ xl, y2 };
    p->pts[5] = (lv_point_precise_t){ xe + 6, y2 };
    lv_obj_t * line = lv_line_create(box);
    lv_line_set_points(line, p->pts, 6);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(COL_PATH), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);

    ui_label(box, "IN", UI_COL_TEXT3, &lv_font_montserrat_12);
    lv_obj_t * out = ui_label(box, "OUT", UI_COL_TEXT3, &lv_font_montserrat_12);
    lv_obj_set_pos(out, xe + 12, y2 - 8);
    path_arrow(box, LV_SYMBOL_DOWN, xr, (y1 + ym) / 2 + 4);
    path_arrow(box, LV_SYMBOL_LEFT, (xl + xr) / 2, ym);
    path_arrow(box, LV_SYMBOL_RIGHT, PATH_TR_X / 2 + 1, y1);

    /* Transpose: klein, wie bisher; Wert in Halbtoenen */
    lv_obj_t * tr = btn_base(box, false);
    lv_obj_set_size(tr, PATH_TR_W, PATH_TR_H);
    lv_obj_set_pos(tr, PATH_TR_X, y1 - PATH_TR_H / 2);
    lv_obj_set_style_radius(tr, 8, 0);
    lv_obj_set_style_border_width(tr, 2, 0);
    lv_obj_set_style_pad_all(tr, 4, 0);
    lv_obj_add_event_cb(tr, tr_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(tr, tr_long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t * trt = ui_label(tr, "Transp.", UI_COL_TEXT3, &lv_font_montserrat_12);
    lv_obj_align(trt, LV_ALIGN_TOP_MID, 0, 0);
    p->tr_val = ui_label(tr, "", UI_COL_TEXT, &lv_font_montserrat_20);
    lv_obj_align(p->tr_val, LV_ALIGN_BOTTOM_MID, 0, 0);
    p->tr_led = lv_obj_create(tr);
    lv_obj_remove_style_all(p->tr_led);
    lv_obj_set_size(p->tr_led, 8, 8);
    lv_obj_set_style_radius(p->tr_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(p->tr_led, LV_OPA_COVER, 0);
    lv_obj_align(p->tr_led, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_obj_remove_flag(p->tr_led, LV_OBJ_FLAG_CLICKABLE);
    p->tr = tr;

    for (uint8_t i = 0; i < p->n; i++) {
        const bool row2 = i >= n1;
        const uint8_t col = row2 ? (uint8_t)(i - n1) : i;
        const int32_t x = x0 + col * (tw + PATH_GAP);

        lv_obj_t * t = btn_base(box, false);
        lv_obj_set_size(t, tw, th);
        lv_obj_set_pos(t, x, row2 ? th + PATH_ROW_GAP : 0);
        lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(t, 2, 0);
        lv_obj_set_style_radius(t, 8, 0);
        lv_obj_set_style_pad_all(t, 6, 0);
        lv_obj_set_style_pad_row(t, 2, 0);
        if (p->kind[i] == PE_FIXED) lv_obj_set_style_bg_color(t, lv_color_hex(0x191b1e), 0);
        lv_obj_add_event_cb(t, path_tap_cb, LV_EVENT_SHORT_CLICKED, p);
        lv_obj_add_event_cb(t, path_long_cb, LV_EVENT_LONG_PRESSED, p);

        p->tile[i] = t;
        p->slot[i] = ui_label(t, "", UI_COL_TEXT3, &lv_font_montserrat_14);
        lv_obj_set_width(p->slot[i], LV_PCT(100));
        lv_label_set_long_mode(p->slot[i], LV_LABEL_LONG_CLIP);
        /* Name: hoechstens zwei Zeilen, bei schmalen Kacheln kleiner */
        const lv_font_t * nf = p->narrow ? &lv_font_montserrat_12 : &lv_font_montserrat_14;
        p->name[i] = ui_label(t, "", UI_COL_TEXT, nf);
        lv_obj_set_size(p->name[i], LV_PCT(100), 2 * lv_font_get_line_height(nf) + 2);
        lv_label_set_long_mode(p->name[i], LV_LABEL_LONG_DOT);
        ui_spacer(t, 1, 1, true);
        p->state[i] = ui_label(t, "", UI_COL_TEXT3, &lv_font_montserrat_14);
        lv_obj_set_size(p->state[i], LV_PCT(100), lv_font_get_line_height(&lv_font_montserrat_14));
        lv_label_set_long_mode(p->state[i], LV_LABEL_LONG_DOT);

        /* Pfeil im Abstand vor der Kachel (nicht vor der ersten einer Zeile) */
        if (col > 0) path_arrow(box, LV_SYMBOL_RIGHT, x - PATH_GAP / 2, row2 ? y2 : y1);
    }
    ui_path_update(p);
}

void ui_path_update(ui_path_t * p)
{
    const kp_rig_t * r = kp_rig();
    for (uint8_t i = 0; i < p->n; i++) {
        uint32_t border = UI_COL_LINE, scol = UI_COL_TEXT3, ncol = UI_COL_TEXT;
        const char * slot;
        const char * name;
        const char * state;

        if (p->kind[i] == PE_STACK) {
            slot  = "STACK";
            name  = r->amp[0] ? r->amp : "Amp";
            state = r->cab[0] ? r->cab : "Cab";
            border = scol = UI_COL_STACK;
            if (!r->amp[0]) ncol = COL_DIM;
        } else if (p->kind[i] == PE_FIXED) {
            const kp_fixed_t f = (kp_fixed_t)p->id[i];
            const int8_t st = kp_fixed_state(f);
            const uint32_t cc = kp_cat_color(kp_fixed_cat(f));
            slot  = FIX_TAG[f];
            name  = p->narrow ? FIX_NARROW[f] : kp_fixed_name(f);
            state = st == 1 ? "ON" : (st == 0 ? "OFF" : "--");
            if (st == 1) { border = cc; scol = cc; }
            else         { ncol = UI_COL_TEXT3; }
        } else {
            const kp_mod_t m = (kp_mod_t)p->id[i];
            const kp_module_t * md = &r->mod[m];
            const uint32_t cc = kp_cat_color(kp_effect_cat(md->type));
            slot = kp_module_name(m);
            if (!md->type) {
                name = "leer";
                state = "";
                ncol = COL_DIM;
            } else {
                name = kp_module_fx_name(m);
                state = md->on ? "ON" : "OFF";
                if (md->on) { border = cc; scol = cc; }
                else        { ncol = UI_COL_TEXT3; }
            }
        }
        lv_label_set_text(p->slot[i], slot);
        lv_label_set_text(p->name[i], name);
        lv_label_set_text(p->state[i], state);
        lv_obj_set_style_border_color(p->tile[i], lv_color_hex(border), 0);
        lv_obj_set_style_text_color(p->slot[i], lv_color_hex(scol), 0);
        lv_obj_set_style_text_color(p->state[i], lv_color_hex(scol), 0);
        lv_obj_set_style_text_color(p->name[i], lv_color_hex(ncol), 0);
    }

    /* Transpose */
    const int8_t st = kp_fixed_state(KP_FIX_TRANSPOSE);
    const uint32_t cc = kp_cat_color(kp_fixed_cat(KP_FIX_TRANSPOSE));
    const int16_t semi = kp_param(KP_P_RIG_TRANSPOSE);
    if (semi) lv_label_set_text_fmt(p->tr_val, "%+d", semi);
    else      lv_label_set_text(p->tr_val, "0");
    lv_obj_set_style_text_color(p->tr_val, lv_color_hex(st == 1 ? UI_COL_TEXT : UI_COL_TEXT3), 0);
    lv_obj_set_style_border_color(p->tr, lv_color_hex(st == 1 ? cc : UI_COL_LINE), 0);
    lv_obj_set_style_bg_color(p->tr_led, lv_color_hex(st == 1 ? cc : (st == 0 ? 0x3a3e44 : UI_COL_TILE)), 0);
}

/* ------------------------------------------------------------------------
 * Fixed Effects
 * --------------------------------------------------------------------- */
static const char * const FIX_SHORT[KP_FIX_COUNT] = {
    "Transpose", "Gate", "Comp", "Boost", "Wah", "Chorus", "Air Chorus", "Doubler"
};

static void fixed_cb(lv_event_t * e)
{
    kp_fixed_toggle((kp_fixed_t)(uintptr_t)lv_event_get_user_data(e));
}

static void fixed_long_cb(lv_event_t * e)
{
    ui_fixed_dialog((kp_fixed_t)(uintptr_t)lv_event_get_user_data(e));
}

static void fixed_chip(ui_fixed_row_t * f, lv_obj_t * row, kp_fixed_t i)
{
    lv_obj_t * b = btn_base(row, false);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_column(b, 7, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_add_event_cb(b, fixed_cb, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
    lv_obj_add_event_cb(b, fixed_long_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);

    lv_obj_t * led = lv_obj_create(b);
    lv_obj_remove_style_all(led);
    lv_obj_set_size(led, 9, 9);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    lv_obj_remove_flag(led, LV_OBJ_FLAG_CLICKABLE);

    f->chip[i] = b;
    f->led[i] = led;
    f->txt[i] = ui_label(b, FIX_SHORT[i], UI_COL_TEXT3, &lv_font_montserrat_14);
}

void ui_fixed_create(ui_fixed_row_t * f, lv_obj_t * parent)
{
    lv_obj_t * row = ui_row(parent, 6);
    ui_label(row, "IN " LV_SYMBOL_RIGHT, UI_COL_TEXT3, &lv_font_montserrat_12);
    for (int i = 0; i < KP_FIX_INPUT_COUNT; i++) fixed_chip(f, row, (kp_fixed_t)i);
    ui_spacer(row, 1, 1, true);
    for (int i = KP_FIX_INPUT_COUNT; i < KP_FIX_COUNT; i++) fixed_chip(f, row, (kp_fixed_t)i);
    ui_label(row, LV_SYMBOL_RIGHT " OUT", UI_COL_TEXT3, &lv_font_montserrat_12);
    ui_fixed_update(f);
}

void ui_fixed_update(ui_fixed_row_t * f)
{
    for (int i = 0; i < KP_FIX_COUNT; i++) {
        const int8_t st = kp_fixed_state((kp_fixed_t)i);
        const uint32_t cc = kp_cat_color(kp_fixed_cat((kp_fixed_t)i));
        lv_obj_set_style_border_color(f->chip[i], lv_color_hex(st == 1 ? cc : UI_COL_LINE), 0);
        lv_obj_set_style_bg_color(f->led[i], lv_color_hex(st == 1 ? cc : (st == 0 ? 0x3a3e44 : UI_COL_TILE)), 0);
        lv_obj_set_style_border_width(f->led[i], st < 0 ? 1 : 0, 0);
        lv_obj_set_style_border_color(f->led[i], lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_color(f->txt[i], lv_color_hex(st == 1 ? UI_COL_TEXT : UI_COL_TEXT3), 0);
    }
}

/* ------------------------------------------------------------------------
 * Footswitch-Leiste
 * --------------------------------------------------------------------- */
static void fs_cb(lv_event_t * e)
{
    kp_footswitch((uint8_t)(uintptr_t)lv_event_get_user_data(e), lv_tick_get());
}

static void fs_long_cb(lv_event_t * e)
{
    ui_fs_assign_dialog((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

void ui_fsbar_create(ui_fsbar_t * f, lv_obj_t * parent)
{
    lv_obj_t * row = ui_row(parent, 6);
    for (uint8_t i = 0; i < KP_FOOTSWITCHES; i++) {
        lv_obj_t * b = btn_base(row, false);
        lv_obj_set_height(b, 78);
        lv_obj_set_width(b, 1);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_pad_left(b, 12, 0);
        lv_obj_set_style_pad_right(b, 10, 0);
        lv_obj_set_style_pad_ver(b, 6, 0);
        lv_obj_set_style_pad_row(b, 1, 0);
        /* SHORT_CLICKED kommt nur, wenn nicht lange gedrueckt wurde */
        lv_obj_add_event_cb(b, fs_cb, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(b, fs_long_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);

        f->btn[i] = b;
        f->top[i] = ui_label(b, "", UI_COL_TEXT3, &lv_font_montserrat_14);
        f->main[i] = ui_label(b, "", UI_COL_TEXT, &lv_font_montserrat_20);
        lv_obj_set_size(f->main[i], LV_PCT(100), 24);
        lv_label_set_long_mode(f->main[i], LV_LABEL_LONG_DOT);
        f->sub[i] = ui_label(b, "", UI_COL_TEXT3, &lv_font_montserrat_14);

        f->led[i] = lv_obj_create(b);
        lv_obj_remove_style_all(f->led[i]);
        lv_obj_set_size(f->led[i], 12, 12);
        lv_obj_set_style_radius(f->led[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(f->led[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(f->led[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(f->led[i], LV_ALIGN_TOP_RIGHT, 0, 2);
        lv_obj_remove_flag(f->led[i], LV_OBJ_FLAG_CLICKABLE);
    }
    ui_fsbar_update(f);
}

void ui_fsbar_update(ui_fsbar_t * f)
{
    kp_fs_info_t in;
    for (uint8_t i = 0; i < KP_FOOTSWITCHES; i++) {
        kp_fs_info(i, &in);
        lv_label_set_text(f->top[i], in.top);
        lv_label_set_text(f->main[i], in.main);
        lv_label_set_text(f->sub[i], in.sub);
        lv_obj_set_style_text_color(f->main[i],
            lv_color_hex(in.empty ? UI_COL_TEXT3 : UI_COL_TEXT), 0);
        ui_button_set_selected(f->btn[i], in.active);
        if (in.led) {
            lv_obj_set_hidden(f->led[i], false);
            lv_obj_set_style_bg_color(f->led[i], lv_color_hex(in.led), 0);
        } else {
            lv_obj_set_hidden(f->led[i], true);
        }
    }
}

/* ------------------------------------------------------------------------
 * Dialoge
 * --------------------------------------------------------------------- */
static lv_obj_t * s_dialog;

static void dialog_close_cb(lv_event_t * e)
{
    (void)e;
    ui_dialog_close();
}

static void dialog_bg_cb(lv_event_t * e)
{
    /* Tippen neben das Fenster schliesst den Dialog */
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) ui_dialog_close();
}

lv_obj_t * ui_dialog_open(const char * title, int32_t w, int32_t h, lv_obj_t ** content)
{
    ui_theme_init();
    ui_dialog_close();

    lv_obj_t * bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_60, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, dialog_bg_cb, LV_EVENT_CLICKED, NULL);
    s_dialog = bg;

    lv_obj_t * win = ui_panel(bg);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICKABLE);       /* Klicks nicht zum Hintergrund */
    lv_obj_set_size(win, w, h);
    lv_obj_center(win);
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(win, 16, 0);
    lv_obj_set_style_pad_row(win, 10, 0);
    lv_obj_set_style_border_color(win, lv_color_hex(0x454b53), 0);

    lv_obj_t * head = ui_row(win, 10);
    ui_label(head, title, UI_COL_TEXT, &lv_font_montserrat_24);
    ui_spacer(head, 1, 1, true);
    lv_obj_t * x = ui_icon_button(head, LV_SYMBOL_CLOSE, false);
    lv_obj_add_event_cb(x, dialog_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * c = lv_obj_create(win);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &st_plain, 0);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    *content = c;
    return win;
}

void ui_dialog_close(void)
{
    s_dialog_refresh = NULL;
    if (s_dialog_close) {
        void (*fn)(void) = s_dialog_close;
        s_dialog_close = NULL;
        fn();
    }
    if (s_dialog) {
        lv_obj_t * d = s_dialog;
        s_dialog = NULL;
        lv_obj_delete_async(d);
    }
}

bool ui_dialog_is_open(void) { return s_dialog != NULL; }

/* Wird von Dialogen gesetzt, die bei Modell-Aenderungen mitlaufen */
void ui_dialog_set_refresh(void (*fn)(uint32_t))
{
    s_dialog_refresh = fn;
}

void ui_dialog_set_close(void (*fn)(void))
{
    s_dialog_close = fn;
}

/* ---- Parameter-Dialog ------------------------------------------------- */
static lv_obj_t * s_pval[KP_P_COUNT];

static void param_slider_cb(lv_event_t * e)
{
    lv_obj_t * sl = lv_event_get_target(e);
    kp_param_t p = (kp_param_t)(uintptr_t)lv_event_get_user_data(e);
    char buf[16];
    kp_set_param(p, (int16_t)lv_slider_get_value(sl));
    kp_param_format(p, kp_param(p), buf, sizeof(buf));
    if (s_pval[p]) lv_label_set_text(s_pval[p], buf);
}

static void param_reset_cb(lv_event_t * e)
{
    kp_section_t sec = (kp_section_t)(uintptr_t)lv_event_get_user_data(e);
    for (int p = 0; p < KP_P_COUNT; p++) {
        if (kp_param_info((kp_param_t)p)->sec == sec)
            kp_set_param((kp_param_t)p, kp_param_info((kp_param_t)p)->def);
    }
    ui_param_dialog(sec);   /* neu aufbauen */
}

void ui_param_dialog(kp_section_t sec)
{
    lv_obj_t * content;
    int rows = 0;
    for (int p = 0; p < KP_P_COUNT; p++) if (kp_param_info((kp_param_t)p)->sec == sec) rows++;

    ui_dialog_open(kp_section_name(sec), 760, 150 + rows * 66, &content);
    for (int p = 0; p < KP_P_COUNT; p++) s_pval[p] = NULL;

    for (int p = 0; p < KP_P_COUNT; p++) {
        const kp_param_info_t * pi = kp_param_info((kp_param_t)p);
        if (pi->sec != sec) continue;
        char buf[16];

        lv_obj_t * row = ui_row(content, 16);
        lv_obj_set_height(row, 56);
        lv_obj_t * n = ui_label(row, pi->name, UI_COL_TEXT2, &lv_font_montserrat_18);
        lv_obj_set_width(n, 170);

        lv_obj_t * sl = ui_slider(row, pi->min, pi->max, kp_param((kp_param_t)p));
        if (pi->min < 0) {   /* bipolar: Balken ab der Mitte */
            lv_slider_set_mode(sl, LV_SLIDER_MODE_SYMMETRICAL);
        }
        lv_obj_add_event_cb(sl, param_slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)p);

        kp_param_format((kp_param_t)p, kp_param((kp_param_t)p), buf, sizeof(buf));
        lv_obj_t * v = ui_label(row, buf, UI_COL_TEXT, &lv_font_montserrat_20);
        lv_obj_set_width(v, 90);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
        s_pval[p] = v;
    }

    lv_obj_t * foot = ui_row(content, 10);
    ui_label(foot, sec == KP_SEC_OUTPUT ? "Global, nicht im Rig gespeichert"
                                        : "Teil des Rigs", UI_COL_TEXT3, &lv_font_montserrat_14);
    ui_spacer(foot, 1, 1, true);
    lv_obj_t * rst = ui_button(foot, "Standardwerte", &lv_font_montserrat_16, false);
    lv_obj_add_event_cb(rst, param_reset_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)sec);
}

/* ---- Effekt-Einstellungen (live vom Kemper) --------------------------- */
#define FD_STEP       164U      /* +/- bei stufenlosen Werten: etwa 1 % */
#define FD_SEND_MS    80U       /* beim Ziehen hoechstens so oft senden */

static struct {
    kl_watch_t kind;
    uint8_t    idx;
    bool       edit;                     /* Werte einstellbar (Fixed Effects) */
    const kp_fx_param_t * list;
    uint8_t    n;
    lv_obj_t * name;
    lv_obj_t * state;
    lv_obj_t * sw;
    lv_obj_t * item[KL_WATCH_MAX];
    lv_obj_t * key[KL_WATCH_MAX];
    lv_obj_t * val[KL_WATCH_MAX];
    lv_obj_t * slider[KL_WATCH_MAX];
    uint16_t   raw[KL_WATCH_MAX];        /* zuletzt bekannter bzw. gesendeter Wert */
    uint16_t   raw_ok;                   /* Bitmaske: Wert vom Kemper bekannt */
    uint32_t   sent_at[KL_WATCH_MAX];
    lv_obj_t * msg;
} fd;

static void fx_dialog_close(void)
{
    kemper_link_watch(KL_WATCH_NONE, 0);
    fd.kind = KL_WATCH_NONE;
}

static uint16_t fd_max(uint8_t k)
{
    return fd.list[k].steps ? (uint16_t)(fd.list[k].steps - 1U) : KP_FX_RAW_MAX;
}

static void fd_send(uint8_t k, uint16_t v)
{
    fd.raw[k] = v;
    fd.raw_ok |= (uint16_t)(1U << k);
    fd.sent_at[k] = lv_tick_get();
    kemper_link_set_param(KP_FIXED_PAGE, fd.list[k].nr, v);
}

static void fd_slider_cb(lv_event_t * e)
{
    const uint8_t k = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const uint16_t v = (uint16_t)lv_slider_get_value(lv_event_get_target(e));
    if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_tick_elaps(fd.sent_at[k]) >= FD_SEND_MS)
        fd_send(k, v);
}

static void fd_step_cb(lv_event_t * e)
{
    const int32_t code = (int32_t)(intptr_t)lv_event_get_user_data(e);
    const uint8_t k = (uint8_t)(code >> 1);
    if (!(fd.raw_ok & (1U << k))) return;          /* Ausgangswert noch unbekannt */
    const int32_t step = fd.list[k].steps ? 1 : (int32_t)FD_STEP;
    int32_t v = (int32_t)fd.raw[k] + ((code & 1) ? step : -step);
    if (v < 0) v = 0;
    if (v > fd_max(k)) v = fd_max(k);
    if (fd.slider[k]) lv_slider_set_value(fd.slider[k], v, LV_ANIM_OFF);
    fd_send(k, (uint16_t)v);
}

static void fd_switch_cb(lv_event_t * e)
{
    const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (fd.kind == KL_WATCH_SLOT) {
        kp_module_set_on((kp_mod_t)fd.idx, on);
    } else if ((kp_fixed_state((kp_fixed_t)fd.idx) == 1) != on) {
        kp_fixed_toggle((kp_fixed_t)fd.idx);
    }
}

static void fx_dialog_refresh(uint32_t chg)
{
    (void)chg;
    static kl_info_t i;
    kemper_link_get_info(&i);

    /* Kopf: Effekt und Zustand */
    const char * nm;
    uint32_t col;
    int8_t st;
    bool empty = false;
    if (fd.kind == KL_WATCH_SLOT) {
        const kp_module_t * m = &kp_rig()->mod[fd.idx];
        empty = m->type == 0;
        nm = empty ? "leer" : kp_module_fx_name((kp_mod_t)fd.idx);
        col = empty ? UI_COL_TEXT3 : kp_cat_color(kp_effect_cat(m->type));
        st = empty ? -1 : (m->on ? 1 : 0);
    } else {
        nm = kp_fixed_name((kp_fixed_t)fd.idx);
        col = kp_cat_color(kp_fixed_cat((kp_fixed_t)fd.idx));
        st = kp_fixed_state((kp_fixed_t)fd.idx);
    }
    lv_label_set_text(fd.name, nm);
    lv_obj_set_style_text_color(fd.name, lv_color_hex(col), 0);
    lv_label_set_text(fd.state, st == 1 ? "EIN" : (st == 0 ? "AUS" : ""));
    lv_obj_set_style_text_color(fd.state, lv_color_hex(st == 1 ? col : UI_COL_TEXT3), 0);
    if (st == 1) lv_obj_add_state(fd.sw, LV_STATE_CHECKED);
    else         lv_obj_remove_state(fd.sw, LV_STATE_CHECKED);
    if (st < 0)  lv_obj_add_state(fd.sw, LV_STATE_DISABLED);
    else         lv_obj_remove_state(fd.sw, LV_STATE_DISABLED);

    /* Werte */
    const bool mine = i.watch_kind == (uint8_t)fd.kind && i.watch_idx == fd.idx;
    const uint8_t n = mine ? i.watch_n : 0;
    for (uint8_t k = 0; k < KL_WATCH_MAX; k++) {
        const bool has = k < n && (i.watch_valid & (1U << k));
        lv_obj_set_hidden(fd.item[k], k >= n);
        if (k >= n) continue;
        lv_label_set_text(fd.key[k], i.watch_name[k] ? i.watch_name[k] : "");
        lv_label_set_text(fd.val[k], has ? i.watch_txt[k] : "...");
        /* Rohwert uebernehmen, ausser waehrend gezogen wird oder kurz nach dem Senden */
        if (fd.edit && k < fd.n && (i.watch_raw_valid & (1U << k)) &&
            lv_tick_elaps(fd.sent_at[k]) > 400U &&
            !(fd.slider[k] && lv_obj_has_state(fd.slider[k], LV_STATE_PRESSED))) {
            fd.raw[k] = i.watch_raw[k];
            fd.raw_ok |= (uint16_t)(1U << k);
            if (fd.slider[k] && lv_slider_get_value(fd.slider[k]) != fd.raw[k])
                lv_slider_set_value(fd.slider[k], fd.raw[k], LV_ANIM_OFF);
        }
    }

    if (i.kemper != KL_KEMPER_OK)
        lv_label_set_text(fd.msg, "Kemper nicht verbunden - die Werte kommen vom Kemper.");
    else if (empty)
        lv_label_set_text(fd.msg, "In diesem Slot ist kein Effekt.");
    else if (fd.edit)
        lv_label_set_text(fd.msg, "Regler ziehen oder - / + tippen; der Kemper zeigt den Wert an.");
    else
        lv_label_set_text(fd.msg, "Werte wie am Kemper angezeigt, laufend aktualisiert.");
}

static lv_obj_t * fd_small_btn(lv_obj_t * parent, const char * sym, uint8_t k, bool plus)
{
    lv_obj_t * b = ui_icon_button(parent, sym, false);
    lv_obj_set_size(b, 48, 44);
    lv_obj_add_event_cb(b, fd_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)((k << 1) | (plus ? 1 : 0)));
    return b;
}

static void fx_dialog_open(const char * title, kl_watch_t kind, uint8_t idx)
{
    lv_obj_t * c;
    memset(&fd, 0, sizeof(fd));
    if (kind == KL_WATCH_FIXED) {
        fd.edit = true;
        fd.n = kp_fixed_params((kp_fixed_t)idx, &fd.list);
        if (fd.n > KL_WATCH_MAX) fd.n = KL_WATCH_MAX;
    }
    ui_dialog_open(title, 880, fd.edit ? 200 + fd.n * 72 : 520, &c);
    fd.kind = kind;
    fd.idx = idx;

    lv_obj_t * head = ui_row(c, 16);
    fd.name = ui_label(head, "", UI_COL_TEXT, &lv_font_montserrat_28);
    fd.state = ui_label(head, "", UI_COL_TEXT3, &lv_font_montserrat_20);
    ui_spacer(head, 1, 1, true);
    ui_label(head, "Ein / Aus", UI_COL_TEXT3, &lv_font_montserrat_16);
    fd.sw = lv_switch_create(head);
    lv_obj_set_size(fd.sw, 70, 36);
    lv_obj_set_style_bg_color(fd.sw, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(fd.sw, fd_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * grid = ui_row(c, 12);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 8, 0);
    for (uint8_t k = 0; k < KL_WATCH_MAX; k++) {
        const bool ed = fd.edit && k < fd.n;
        lv_obj_t * it = ui_panel(grid);
        lv_obj_set_size(it, ed ? LV_PCT(100) : 404, ed ? 64 : 46);
        lv_obj_set_flex_flow(it, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(it, 0, 0);
        lv_obj_set_style_pad_hor(it, 12, 0);
        lv_obj_set_style_pad_column(it, ed ? 18 : 10, 0);
        lv_obj_set_style_bg_color(it, lv_color_hex(UI_COL_BG), 0);
        fd.item[k] = it;
        fd.key[k] = ui_label(it, "", UI_COL_TEXT3, &lv_font_montserrat_16);
        lv_label_set_long_mode(fd.key[k], LV_LABEL_LONG_DOT);
        if (ed) {
            /* einstellbar: Name | - | Regler | + | Wert */
            lv_obj_set_width(fd.key[k], 150);
            fd_small_btn(it, LV_SYMBOL_MINUS, k, false);
            if (!fd.list[k].steps) {
                fd.slider[k] = ui_slider(it, 0, KP_FX_RAW_MAX, 0);
                lv_obj_add_event_cb(fd.slider[k], fd_slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)k);
                lv_obj_add_event_cb(fd.slider[k], fd_slider_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)k);
            }
            fd.val[k] = ui_label(it, "", UI_COL_TEXT, &lv_font_montserrat_20);
            lv_obj_set_style_text_align(fd.val[k], LV_TEXT_ALIGN_CENTER, 0);
            if (fd.list[k].steps) {
                lv_obj_set_width(fd.val[k], 1);
                lv_obj_set_flex_grow(fd.val[k], 1);
            } else {
                lv_obj_set_width(fd.val[k], 120);
            }
            fd_small_btn(it, LV_SYMBOL_PLUS, k, true);
            if (!fd.list[k].steps) lv_obj_move_to_index(fd.val[k], -1);   /* Wert ganz rechts */
        } else {
            lv_obj_set_width(fd.key[k], 1);
            lv_obj_set_flex_grow(fd.key[k], 1);
            fd.val[k] = ui_label(it, "", UI_COL_TEXT, &lv_font_montserrat_20);
        }
        lv_obj_set_hidden(it, true);
    }
    fd.msg = ui_label(c, "", UI_COL_TEXT3, &lv_font_montserrat_14);
    lv_obj_set_width(fd.msg, LV_PCT(100));
    lv_label_set_long_mode(fd.msg, LV_LABEL_LONG_WRAP);

    kemper_link_watch(kind, idx);
    fx_dialog_refresh(KP_CHG_ALL);
    ui_dialog_set_refresh(fx_dialog_refresh);
    ui_dialog_set_close(fx_dialog_close);
}

void ui_fx_dialog(kp_mod_t m)
{
    char title[32];
    if (m >= KP_MOD_COUNT) return;
    lv_snprintf(title, sizeof(title), "Modul %s - Einstellungen", kp_module_name(m));
    fx_dialog_open(title, KL_WATCH_SLOT, (uint8_t)m);
}

void ui_fixed_dialog(kp_fixed_t f)
{
    if (f >= KP_FIX_COUNT) return;
    if (f == KP_FIX_TRANSPOSE) { ui_transpose_dialog(); return; }
    fx_dialog_open(f < KP_FIX_INPUT_COUNT ? "Fixed Effect (Input)" : "Fixed Effect (Output)",
                   KL_WATCH_FIXED, (uint8_t)f);
}

/* ---- Transpose -------------------------------------------------------- */
static struct {
    lv_obj_t * val;
    lv_obj_t * sw;
} td;

static void td_refresh(uint32_t chg)
{
    (void)chg;
    const int16_t t = kp_param(KP_P_RIG_TRANSPOSE);
    const int8_t st = kp_fixed_state(KP_FIX_TRANSPOSE);
    if (t) lv_label_set_text_fmt(td.val, "%+d", t);
    else   lv_label_set_text(td.val, "0");
    lv_obj_set_style_text_color(td.val, lv_color_hex(st == 0 ? UI_COL_TEXT3 : UI_COL_TEXT), 0);
    if (st == 1) lv_obj_add_state(td.sw, LV_STATE_CHECKED);
    else         lv_obj_remove_state(td.sw, LV_STATE_CHECKED);
    if (st < 0)  lv_obj_add_state(td.sw, LV_STATE_DISABLED);
    else         lv_obj_remove_state(td.sw, LV_STATE_DISABLED);
}

static void td_step_cb(lv_event_t * e)
{
    const int d = (int)(intptr_t)lv_event_get_user_data(e);
    kp_set_param(KP_P_RIG_TRANSPOSE, d ? (int16_t)(kp_param(KP_P_RIG_TRANSPOSE) + d) : 0);
}

static void td_switch_cb(lv_event_t * e)
{
    const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if ((kp_fixed_state(KP_FIX_TRANSPOSE) == 1) != on) kp_fixed_toggle(KP_FIX_TRANSPOSE);
}

static void td_button(lv_obj_t * row, const char * txt, int d)
{
    lv_obj_t * b = ui_button(row, txt, &lv_font_montserrat_22, false);
    lv_obj_set_size(b, 96, 64);
    lv_obj_add_event_cb(b, td_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)d);
}

void ui_transpose_dialog(void)
{
    lv_obj_t * c;
    ui_dialog_open("Transpose", 760, 380, &c);

    lv_obj_t * head = ui_row(c, 16);
    ui_label(head, "Tonhoehe in Halbtoenen", UI_COL_TEXT3, &lv_font_montserrat_18);
    ui_spacer(head, 1, 1, true);
    ui_label(head, "Ein / Aus", UI_COL_TEXT3, &lv_font_montserrat_16);
    td.sw = lv_switch_create(head);
    lv_obj_set_size(td.sw, 70, 36);
    lv_obj_set_style_bg_color(td.sw, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(td.sw, td_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * row = ui_row(c, 14);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 16, 0);
    td_button(row, "-12", -12);
    td_button(row, LV_SYMBOL_MINUS, -1);
    td.val = ui_label(row, "", UI_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_set_width(td.val, 150);
    lv_obj_set_style_text_align(td.val, LV_TEXT_ALIGN_CENTER, 0);
    td_button(row, LV_SYMBOL_PLUS, 1);
    td_button(row, "+12", 12);

    lv_obj_t * foot = ui_row(c, 10);
    ui_label(foot, kp_connected() ? "-36 bis +36, Teil des Rigs" : "Kemper nicht verbunden",
             UI_COL_TEXT3, &lv_font_montserrat_14);
    ui_spacer(foot, 1, 1, true);
    lv_obj_t * z = ui_button(foot, "Auf 0", &lv_font_montserrat_16, false);
    lv_obj_add_event_cb(z, td_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    td_refresh(KP_CHG_ALL);
    ui_dialog_set_refresh(td_refresh);
}

/* ---- Footswitch belegen ----------------------------------------------- */
static struct {
    uint8_t    fs;
    uint8_t    n;
    lv_obj_t * btn[4 + KP_FX_BUTTONS + KP_MOD_COUNT + KP_FIX_COUNT];
    kp_fs_assign_t a[4 + KP_FX_BUTTONS + KP_MOD_COUNT + KP_FIX_COUNT];
} fa;

static void fa_pick_cb(lv_event_t * e)
{
    const uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    kp_fs_set_assign(fa.fs, fa.a[i]);
    ui_dialog_close();
}

static lv_obj_t * fa_group(lv_obj_t * c, const char * caption)
{
    ui_label(c, caption, UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_obj_t * g = ui_row(c, 8);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(g, 8, 0);
    return g;
}

static void fa_option(lv_obj_t * g, uint8_t kind, uint8_t idx, const char * txt, uint32_t color)
{
    const kp_fs_assign_t a = { kind, idx };
    const kp_fs_assign_t cur = kp_fs_assign(fa.fs);
    lv_obj_t * b = ui_button(g, txt, &lv_font_montserrat_16, cur.kind == kind && (kind == KP_FSA_NONE ||
                             kind == KP_FSA_TAP || cur.idx == idx));
    lv_obj_set_size(b, 196, 46);
    lv_label_set_long_mode(lv_obj_get_child(b, 0), LV_LABEL_LONG_DOT);
    lv_obj_set_width(lv_obj_get_child(b, 0), 170);
    lv_obj_set_style_text_align(lv_obj_get_child(b, 0), LV_TEXT_ALIGN_CENTER, 0);
    if (color) {
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(b, 5, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    }
    lv_obj_add_event_cb(b, fa_pick_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)fa.n);
    fa.a[fa.n] = a;
    fa.btn[fa.n++] = b;
}

void ui_fs_assign_dialog(uint8_t fs)
{
    static const char * const BTN[KP_FX_BUTTONS] = { "Effect Btn I", "Effect Btn II", "Effect Btn III", "Effect Btn IIII" };
    char title[24];
    lv_obj_t * c;
    if (fs >= KP_FOOTSWITCHES) return;
    lv_snprintf(title, sizeof(title), "FS%u belegen", (unsigned)(fs + 1));
    ui_dialog_open(title, 900, 560, &c);
    fa.fs = fs;
    fa.n = 0;

    lv_obj_t * g = fa_group(c, "Allgemein");
    fa_option(g, KP_FSA_NONE, 0, "nicht belegt", 0);
    fa_option(g, KP_FSA_TAP, 0, "Tap Tempo", 0);

    g = fa_group(c, "Effect Buttons des Players");
    for (uint8_t b = 0; b < KP_FX_BUTTONS; b++) fa_option(g, KP_FSA_FX_BUTTON, b, BTN[b], 0);

    g = fa_group(c, "Effekt-Module des Rigs");
    for (uint8_t m = 0; m < KP_MOD_COUNT; m++) {
        char txt[40];
        if (!kp_module_available((kp_mod_t)m)) continue;
        const uint16_t type = kp_rig()->mod[m].type;
        lv_snprintf(txt, sizeof(txt), "%s: %s", kp_module_name((kp_mod_t)m),
                    type ? kp_module_fx_name((kp_mod_t)m) : "leer");
        fa_option(g, KP_FSA_MODULE, m, txt, kp_cat_color(kp_effect_cat(type)));
    }

    g = fa_group(c, "Fixed Effects");
    for (uint8_t f = 0; f < KP_FIX_COUNT; f++)
        fa_option(g, KP_FSA_FIXED, f, kp_fixed_name((kp_fixed_t)f), kp_cat_color(kp_fixed_cat((kp_fixed_t)f)));
}
