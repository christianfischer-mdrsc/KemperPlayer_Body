/**
 * @file ui_common.c
 * Gemeinsame Bausteine aller Ansichten (siehe ui_common.h).
 */
#include "ui_common.h"
#include "ui_statusbar.h"
#include "ui_screens.h"

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
static ui_scr_t   s_cur = UI_SCR_LIVE;
static ui_scr_t   s_prev = UI_SCR_LIVE;
static lv_obj_t * s_refresh_owner;
static void     (*s_refresh)(uint32_t);
static void     (*s_dialog_refresh)(uint32_t);

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
    lv_label_set_text_fmt(label, "Bank %u", (unsigned)(kp_bank() + 1));
    lv_obj_set_style_bg_color(dot, lv_color_hex(kp_bank_color(kp_bank())), 0);
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
void ui_chain_create(ui_chain_t * c, lv_obj_t * parent, bool compact, lv_event_cb_t cb)
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
        if (cb) lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

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
            const kp_effect_t * fx = kp_effect(m->type);
            uint32_t cc = kp_cat_color(fx->cat);
            lv_label_set_text(c->slot[i], kp_module_name((kp_mod_t)c->id[i]));
            if (!m->type) {
                name = "leer";
                state = "";
                ncol = COL_DIM;
            } else {
                name = fx->name;
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
 * Footswitch-Leiste
 * --------------------------------------------------------------------- */
static void fs_cb(lv_event_t * e)
{
    kp_footswitch((uint8_t)(uintptr_t)lv_event_get_user_data(e), lv_tick_get());
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
        lv_obj_add_event_cb(b, fs_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

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
