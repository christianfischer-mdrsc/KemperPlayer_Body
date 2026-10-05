/**
 * @file ui_settings.c
 * System: Ausbaustufe des Players, Footswitch-Modus, Display-Helligkeit,
 * Datum/Uhrzeit und Info.
 */
#include "ui_common.h"
#include "ui_screens.h"
#include "ui_statusbar.h"

static struct {
    lv_obj_t * level[3];
    lv_obj_t * level_info;
    lv_obj_t * fsm[2];
    lv_obj_t * bright;
    lv_obj_t * bright_val;
    lv_obj_t * roller[5];          /* Tag, Monat, Jahr, Stunde, Minute */
    lv_obj_t * time_msg;
    lv_obj_t * conn;
} w;

#define YEAR_FIRST 2024
#define YEAR_COUNT 27

static void update(void)
{
    for (int i = 0; i < 3; i++) ui_button_set_selected(w.level[i], kp_level() == (kp_level_t)(i + 1));
    if (kp_level() == KP_LEVEL_3)
        lv_label_set_text(w.level_info, "8 Effektmodule (A-D, X, MOD, DLY, REV), 125 Banks = 625 Rigs");
    else
        lv_label_set_text(w.level_info, "4 Effektmodule (A, B, DLY, REV), 10 Banks = 50 Rigs");
    ui_button_set_selected(w.fsm[0], kp_fs_mode() == KP_FS_MODE_RIGS);
    ui_button_set_selected(w.fsm[1], kp_fs_mode() == KP_FS_MODE_FX);
    lv_label_set_text(w.conn, kp_connected() ? "verbunden" : "nicht verbunden");
}

static void refresh(uint32_t chg)
{
    if (chg & KP_CHG_SYSTEM) update();
}

static void level_cb(lv_event_t * e)
{
    kp_set_level((kp_level_t)(uintptr_t)lv_event_get_user_data(e));
    update();
}

static void fsm_cb(lv_event_t * e)
{
    kp_fs_set_mode((kp_fs_mode_t)(uintptr_t)lv_event_get_user_data(e));
}

static void bright_cb(lv_event_t * e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    kp_hw_set_brightness((uint8_t)v);
    lv_label_set_text_fmt(w.bright_val, "%d %%", v);
}

static void set_time_cb(lv_event_t * e)
{
    (void)e;
    ui_datetime_t dt;
    dt.day    = (uint8_t)(lv_roller_get_selected(w.roller[0]) + 1);
    dt.month  = (uint8_t)(lv_roller_get_selected(w.roller[1]) + 1);
    dt.year   = (uint16_t)(lv_roller_get_selected(w.roller[2]) + YEAR_FIRST);
    dt.hour   = (uint8_t)lv_roller_get_selected(w.roller[3]);
    dt.minute = (uint8_t)lv_roller_get_selected(w.roller[4]);

    static const uint8_t mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (dt.year % 4 == 0 && dt.year % 100 != 0) || dt.year % 400 == 0;
    uint8_t max = mdays[dt.month - 1];
    if (dt.month == 2 && !leap) max = 28;
    if (dt.day > max) {
        lv_label_set_text_fmt(w.time_msg, "Den %u. gibt es in diesem Monat nicht", dt.day);
        return;
    }
    if (ui_statusbar_set_time(&dt)) {
        lv_label_set_text(w.time_msg, "Uhr gestellt");
        ui_statusbar_refresh();
    } else {
        lv_label_set_text(w.time_msg, "Uhr konnte nicht gestellt werden");
    }
}

static lv_obj_t * section(lv_obj_t * parent, const char * title)
{
    lv_obj_t * p = ui_panel(parent);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p, 14, 0);
    lv_obj_set_style_pad_row(p, 10, 0);
    ui_label(p, title, UI_COL_TEXT, &lv_font_montserrat_20);
    return p;
}

static lv_obj_t * roller(lv_obj_t * parent, const char * opts, uint32_t sel, int32_t width)
{
    lv_obj_t * r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_roller_set_selected(r, sel, LV_ANIM_OFF);
    lv_obj_set_width(r, width);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_text_color(r, lv_color_hex(UI_COL_TEXT2), 0);
    lv_obj_set_style_border_color(r, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(UI_COL_SEL_BG), LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), LV_PART_SELECTED);
    return r;
}

/* Optionstext "a\nb\n..." fuer Zahlenbereiche */
static void num_opts(char * buf, uint32_t len, int from, int to, bool two_digits)
{
    uint32_t pos = 0;
    for (int v = from; v <= to && pos + 6 < len; v++) {
        pos += (uint32_t)lv_snprintf(buf + pos, len - pos, two_digits ? "%02d%s" : "%d%s",
                                     v, v < to ? "\n" : "");
    }
}

lv_obj_t * ui_settings_build(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = ui_screen_base(&body);
    ui_title_bar(body, "System", UI_SCR_EDIT);

    lv_obj_t * cols = ui_row(body, 12);
    lv_obj_set_flex_grow(cols, 1);
    lv_obj_set_flex_align(cols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t * left = ui_col(cols, 12);
    lv_obj_set_width(left, 1);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_t * right = ui_col(cols, 12);
    lv_obj_set_width(right, 1);
    lv_obj_set_flex_grow(right, 1);

    /* Player-Level */
    lv_obj_t * p = section(left, "Kemper Player");
    lv_obj_t * r = ui_row(p, 8);
    static const char * const LV[3] = { "Level I", "Level II", "Level III" };
    for (int i = 0; i < 3; i++) {
        w.level[i] = ui_button(r, LV[i], &lv_font_montserrat_18, false);
        lv_obj_set_width(w.level[i], 140);
        lv_obj_add_event_cb(w.level[i], level_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
    }
    w.level_info = ui_label(p, "", UI_COL_TEXT2, &lv_font_montserrat_16);
    lv_obj_t * hint = ui_label(p, "Sobald der Kemper verbunden ist, liest das Display den Level selbst aus.",
                               UI_COL_TEXT3, &lv_font_montserrat_14);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    /* Footswitches */
    p = section(left, "Footswitches");
    r = ui_row(p, 8);
    w.fsm[0] = ui_button(r, "Rigs", &lv_font_montserrat_18, false);
    lv_obj_set_width(w.fsm[0], 140);
    lv_obj_add_event_cb(w.fsm[0], fsm_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)KP_FS_MODE_RIGS);
    w.fsm[1] = ui_button(r, "Effekte", &lv_font_montserrat_18, false);
    lv_obj_set_width(w.fsm[1], 140);
    lv_obj_add_event_cb(w.fsm[1], fsm_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)KP_FS_MODE_FX);
    ui_label(p, "Rigs: FS1-5 laden Rig 1-5 der Bank, FS6 wechselt zu Effekte.\n"
                "Effekte: FS1-4 = Effect Button I-IIII, FS5 = Tap, FS6 = Rigs.",
             UI_COL_TEXT3, &lv_font_montserrat_14);

    /* Display */
    p = section(left, "Display");
    r = ui_row(p, 14);
    ui_label(r, "Helligkeit", UI_COL_TEXT2, &lv_font_montserrat_16);
    w.bright = ui_slider(r, 5, 100, kp_hw_brightness());
    lv_obj_add_event_cb(w.bright, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    w.bright_val = ui_label(r, "", UI_COL_TEXT, &lv_font_montserrat_18);
    lv_obj_set_width(w.bright_val, 64);
    lv_label_set_text_fmt(w.bright_val, "%u %%", kp_hw_brightness());

    /* Datum und Uhrzeit */
    p = section(right, "Datum und Uhrzeit");
    ui_datetime_t now = { 2026, 1, 1, 12, 0 };
    ui_statusbar_get_time(&now);
    if (now.year < YEAR_FIRST || now.year >= YEAR_FIRST + YEAR_COUNT) now.year = YEAR_FIRST;

    static char opts[200];
    r = ui_row(p, 8);
    num_opts(opts, sizeof(opts), 1, 31, true);
    w.roller[0] = roller(r, opts, now.day - 1, 70);
    num_opts(opts, sizeof(opts), 1, 12, true);
    w.roller[1] = roller(r, opts, now.month - 1, 70);
    num_opts(opts, sizeof(opts), YEAR_FIRST, YEAR_FIRST + YEAR_COUNT - 1, false);
    w.roller[2] = roller(r, opts, now.year - YEAR_FIRST, 96);
    ui_spacer(r, 14, 1, false);
    num_opts(opts, sizeof(opts), 0, 23, true);
    w.roller[3] = roller(r, opts, now.hour, 70);
    ui_label(r, ":", UI_COL_TEXT2, &lv_font_montserrat_24);
    num_opts(opts, sizeof(opts), 0, 59, true);
    w.roller[4] = roller(r, opts, now.minute, 70);

    r = ui_row(p, 12);
    lv_obj_t * set = ui_button(r, "Uhr stellen", &lv_font_montserrat_18, true);
    lv_obj_add_event_cb(set, set_time_cb, LV_EVENT_CLICKED, NULL);
    w.time_msg = ui_label(r, "", UI_COL_TEXT3, &lv_font_montserrat_16);

    /* Info */
    p = section(right, "Info");
    r = ui_row(p, 8);
    ui_label(r, "Kemper:", UI_COL_TEXT3, &lv_font_montserrat_16);
    w.conn = ui_label(r, "", UI_COL_TEXT, &lv_font_montserrat_16);
    r = ui_row(p, 8);
    ui_label(r, "Firmware:", UI_COL_TEXT3, &lv_font_montserrat_16);
    ui_label(r, __DATE__ "  " __TIME__, UI_COL_TEXT, &lv_font_montserrat_16);
    r = ui_row(p, 8);
    ui_label(r, "LVGL:", UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_obj_t * lv = ui_label(r, "", UI_COL_TEXT, &lv_font_montserrat_16);
    lv_label_set_text_fmt(lv, "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    update();
    ui_nav_set_refresh(scr, refresh);
    return scr;
}
