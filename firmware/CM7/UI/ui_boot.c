/**
 * @file ui_boot.c
 * Startbildschirm des Kemper-Displays (1024 x 600), LVGL v9.
 * Benoetigte Fonts: Montserrat 12, 14, 16, 18, 28
 */
#include "ui_boot.h"
#include <stdio.h>

/* Farben passend zur restlichen Oberflaeche */
#define C_BG_TOP     0x0b0c0e
#define C_BG_BOTTOM  0x17191c
#define C_TRACK      0x24272c
#define C_TEXT       0xeceef1
#define C_TEXT2      0xa3aab4
#define C_TEXT3      0x6f7680
#define C_BLUE       0x4a93e8
#define C_CYAN       0x3cc3d6
#define C_GREEN      0x46c26b

static const uint32_t fx_colors[9] = {
    0x3cc3d6, 0xf08a24, 0xe5483d, 0xe8c93a, 0xb8bec7,
    0xa58af0, 0x4a93e8, 0x46c26b, 0x2fb3a0
};

static const char * step_names[UI_BOOT_STEPS] = {
    "Display", "Touch", "Speicher", "USB-MIDI", "Kemper"
};
static const char * step_texts[UI_BOOT_STEPS] = {
    "Display wird gestartet",
    "Touch wird initialisiert",
    "Einstellungen werden geladen",
    "USB-MIDI wird gestartet",
    "Verbinde mit Kemper"
};

static lv_obj_t * s_scr;
static lv_obj_t * s_bar;
static lv_obj_t * s_status;
static lv_obj_t * s_percent;
static lv_obj_t * s_time;
static lv_obj_t * s_dots[UI_BOOT_STEPS];
static lv_obj_t * s_names[UI_BOOT_STEPS];
static lv_timer_t * s_timer;
static uint32_t   s_done_ms;

/* ------------------------------------------------------------------------- */

static lv_obj_t * box(lv_obj_t * parent)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_pad_gap(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t * text(lv_obj_t * parent, const char * t, uint32_t color,
                       const lv_font_t * font)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static void set_time_label(uint32_t ms)
{
    lv_label_set_text_fmt(s_time, "%lu,%02lu s",
                          (unsigned long)(ms / 1000U),
                          (unsigned long)((ms % 1000U) / 10U));
}

static void timer_cb(lv_timer_t * t)
{
    (void)t;
    if (s_done_ms == 0) set_time_label(lv_tick_get());
}

/* Equalizer-Balken im Logo leicht "atmen" lassen */
static void eq_anim_cb(void * obj, int32_t v)
{
    lv_obj_set_height((lv_obj_t *)obj, v);
}

static void set_step_state(uint8_t i, int state) /* 0 offen, 1 aktiv, 2 fertig */
{
    uint32_t dot  = state == 2 ? C_GREEN : state == 1 ? C_BLUE : C_TRACK;
    uint32_t txt  = state == 0 ? C_TEXT3 : C_TEXT2;
    lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(dot), 0);
    lv_obj_set_style_text_color(s_names[i], lv_color_hex(state == 1 ? C_TEXT : txt), 0);
}

/* ------------------------------------------------------------------------- */

lv_obj_t * ui_boot_create(void)
{
    s_done_ms = 0;

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(C_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);

    /* Logo: Spinner mit Equalizer in Effektfarben ------------------------ */
    lv_obj_t * spin = lv_spinner_create(s_scr);
    lv_obj_set_size(spin, 170, 170);
    lv_obj_align(spin, LV_ALIGN_TOP_MID, 0, 70);
    lv_spinner_set_anim_params(spin, 1400, 80);
    lv_obj_set_style_arc_width(spin, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spin, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, lv_color_hex(C_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(spin, true, LV_PART_INDICATOR);

    lv_obj_t * eq = box(spin);
    lv_obj_set_size(eq, 9 * 8 + 8 * 4, 70);
    lv_obj_center(eq);
    lv_obj_set_flex_flow(eq, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eq, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(eq, 4, 0);

    static const int32_t h[9] = { 26, 44, 60, 38, 52, 30, 56, 40, 22 };
    for (int i = 0; i < 9; i++) {
        lv_obj_t * b = box(eq);
        lv_obj_set_size(b, 8, h[i]);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(fx_colors[i]), 0);
        lv_obj_set_style_radius(b, 4, 0);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, b);
        lv_anim_set_exec_cb(&a, eq_anim_cb);
        lv_anim_set_values(&a, h[i], h[i] > 40 ? h[i] - 24 : h[i] + 22);
        lv_anim_set_duration(&a, 520 + i * 70);
        lv_anim_set_playback_duration(&a, 520 + i * 70);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    /* Titel ------------------------------------------------------------- */
    lv_obj_t * title = text(s_scr, "KEMPER PLAYER DISPLAY", C_TEXT, &lv_font_montserrat_28);
    lv_obj_set_style_text_letter_space(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 272);

    lv_obj_t * sub = text(s_scr, "", C_TEXT3, &lv_font_montserrat_14);
    lv_label_set_text_fmt(sub, "Riverdi 7\"   /   STM32H757   /   LVGL %d.%d",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
    lv_obj_set_style_text_letter_space(sub, 1, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 316);

    /* Fortschritt ------------------------------------------------------- */
    lv_obj_t * prog = box(s_scr);
    lv_obj_set_size(prog, 600, LV_SIZE_CONTENT);
    lv_obj_align(prog, LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_set_flex_flow(prog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(prog, 12, 0);

    lv_obj_t * row = box(prog);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    s_status  = text(row, step_texts[0], C_TEXT2, &lv_font_montserrat_18);
    lv_obj_align(s_status, LV_ALIGN_LEFT_MID, 0, 0);
    s_percent = text(row, "0 %", C_TEXT, &lv_font_montserrat_18);
    lv_obj_align(s_percent, LV_ALIGN_RIGHT_MID, 0, 0);

    s_bar = lv_bar_create(prog);
    lv_obj_set_size(s_bar, LV_PCT(100), 8);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(C_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_bar, lv_color_hex(C_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(s_bar, 250, 0);

    lv_obj_t * steps = box(prog);
    lv_obj_set_size(steps, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(steps, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(steps, 4, 0);
    for (int i = 0; i < UI_BOOT_STEPS; i++) {
        lv_obj_t * st = box(steps);
        lv_obj_set_size(st, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(st, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(st, 8, 0);
        s_dots[i] = box(st);
        lv_obj_set_size(s_dots[i], 10, 10);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        s_names[i] = text(st, step_names[i], C_TEXT3, &lv_font_montserrat_14);
        set_step_state((uint8_t)i, 0);
    }

    /* Bootzeit unten links, Build unten rechts ------------------------- */
    lv_obj_t * tl = text(s_scr, "BOOTZEIT", C_TEXT3, &lv_font_montserrat_12);
    lv_obj_set_style_text_letter_space(tl, 2, 0);
    lv_obj_align(tl, LV_ALIGN_BOTTOM_LEFT, 32, -64);
    s_time = text(s_scr, "0,00 s", C_TEXT, &lv_font_montserrat_28);
    lv_obj_align(s_time, LV_ALIGN_BOTTOM_LEFT, 32, -24);

    lv_obj_t * bl = text(s_scr, "FIRMWARE", C_TEXT3, &lv_font_montserrat_12);
    lv_obj_set_style_text_letter_space(bl, 2, 0);
    lv_obj_align(bl, LV_ALIGN_BOTTOM_RIGHT, -32, -64);
    lv_obj_t * bv = text(s_scr, "v0.1  " __DATE__, C_TEXT2, &lv_font_montserrat_16);
    lv_obj_align(bv, LV_ALIGN_BOTTOM_RIGHT, -32, -28);

    /* Farbleiste in den Effektfarben ganz unten ------------------------- */
    lv_obj_t * stripe = box(s_scr);
    lv_obj_set_size(stripe, LV_PCT(100), 4);
    lv_obj_align(stripe, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(stripe, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stripe, 0, 0);
    for (int i = 0; i < 9; i++) {
        lv_obj_t * seg = box(stripe);
        lv_obj_set_size(seg, 1, LV_PCT(100));
        lv_obj_set_flex_grow(seg, 1);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(fx_colors[i]), 0);
    }

    set_time_label(lv_tick_get());
    s_timer = lv_timer_create(timer_cb, 33, NULL);

    lv_screen_load(s_scr);
    return s_scr;
}

void ui_boot_step(uint8_t index, const char * t)
{
    if (!s_scr || index >= UI_BOOT_STEPS) return;
    for (uint8_t i = 0; i < UI_BOOT_STEPS; i++)
        set_step_state(i, i < index ? 2 : i == index ? 1 : 0);

    char buf[48];
    snprintf(buf, sizeof(buf), "%s ...", t ? t : step_texts[index]);
    lv_label_set_text(s_status, buf);

    int32_t pct = (int32_t)index * 100 / UI_BOOT_STEPS;
    lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);   /* ohne Animation: auch vor dem RTOS-Start korrekt */
    lv_label_set_text_fmt(s_percent, "%d %%", (int)pct);
    set_time_label(lv_tick_get());
}

void ui_boot_finish(lv_obj_t * next)
{
    if (!s_scr) return;
    s_done_ms = lv_tick_get();
    if (s_done_ms == 0) s_done_ms = 1;

    for (uint8_t i = 0; i < UI_BOOT_STEPS; i++) set_step_state(i, 2);
    lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
    lv_label_set_text(s_percent, "100 %");
    lv_label_set_text(s_status, "Bereit");
    lv_obj_set_style_text_color(s_status, lv_color_hex(C_GREEN), 0);
    set_time_label(s_done_ms);

    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (next) {
        /* 0,8 s stehen lassen, dann ueberblenden; Boot-Screen wird geloescht */
        lv_screen_load_anim(next, LV_SCREEN_LOAD_ANIM_FADE_IN, 300, 800, true);
        s_scr = NULL;
    }
}

uint32_t ui_boot_time_ms(void)
{
    return s_done_ms;
}

bool ui_boot_is_active(void)
{
    return s_scr != NULL && s_done_ms == 0;
}
