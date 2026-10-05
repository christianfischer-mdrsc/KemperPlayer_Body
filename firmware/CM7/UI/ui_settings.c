/**
 * @file ui_settings.c
 * System: Ausbaustufe des Players, Footswitch-Modus, Display-Helligkeit,
 * Kemper-Verbindung, Datum/Uhrzeit und Info.
 */
#include <stdio.h>
#include "ui_common.h"
#include "ui_screens.h"
#include "ui_statusbar.h"
#include "kemper_link.h"

/* Zeilen im Abschnitt "Kemper-Verbindung" */
enum {
    KR_USB = 0, KR_RIG1, KR_RIG, KR_STACK, KR_TEMPO, KR_FIRMWARE,
    KR_SENSE, KR_STATS, KR_COUNT
};
static const char * const KR_NAME[KR_COUNT] = {
    "USB", "Rig 1 (Bank 1)", "Aktuelles Rig", "Amp / Cab", "Rig-Tempo", "Firmware",
    "Lebenszeichen", "Nachrichten"
};

#define COL_OK    0x3ddc84
#define COL_WAIT  0xf0b429
#define COL_ERR   0xe5483d

static struct {
    lv_obj_t * level[3];
    lv_obj_t * level_info;
    lv_obj_t * fsm[2];
    lv_obj_t * bright;
    lv_obj_t * bright_val;
    lv_obj_t * roller[5];          /* Tag, Monat, Jahr, Stunde, Minute */
    lv_obj_t * time_msg;
    lv_obj_t * k_dot;              /* Kemper-Verbindung */
    lv_obj_t * k_state;
    lv_obj_t * k_val[KR_COUNT];
    lv_timer_t * k_timer;
} w;

#define YEAR_FIRST 2024
#define YEAR_COUNT 27

/* Abschnitt "Kemper-Verbindung" aus dem Zustand des USB-Tasks fuellen */
static void update_link(void)
{
    static kl_info_t i;
    char buf[96];
    kemper_link_get_info(&i);

    const bool ok = i.kemper == KL_KEMPER_OK;
    uint32_t col = COL_ERR;
    if (ok) col = COL_OK;
    else if (i.usb == KL_USB_ENUM || (i.usb == KL_USB_READY && i.kemper != KL_KEMPER_LOST)) col = COL_WAIT;
    lv_obj_set_style_bg_color(w.k_dot, lv_color_hex(col), 0);

    if (ok) {
        uint32_t s = (i.now_ms - i.connected_since_ms) / 1000U;
        lv_snprintf(buf, sizeof(buf), "Verbunden seit %02lu:%02lu:%02lu",
                    (unsigned long)(s / 3600U), (unsigned long)(s / 60U % 60U),
                    (unsigned long)(s % 60U));
        lv_label_set_text(w.k_state, buf);
    } else {
        lv_label_set_text(w.k_state, kemper_link_state_text(&i));
    }

    /* USB-Seite */
    static const char * const USB_TXT[] = {
        "nicht verbunden", "wird vom Kemper eingerichtet", "vom Kemper eingerichtet (MIDI bereit)"
    };
    lv_label_set_text(w.k_val[KR_USB], USB_TXT[i.usb <= KL_USB_READY ? i.usb : 0]);

    /* Daten vom Kemper (SysEx) */
    if (i.identity_valid) {
        lv_snprintf(buf, sizeof(buf), "%u.%u.%u.%u", i.id_version[0], i.id_version[1],
                    i.id_version[2], i.id_version[3]);
        lv_label_set_text(w.k_val[KR_FIRMWARE], buf);
    } else {
        lv_label_set_text(w.k_val[KR_FIRMWARE], ok ? "vom Kemper nicht gemeldet" : "-");
    }
    lv_label_set_text(w.k_val[KR_RIG1], i.rig1_name[0] ? i.rig1_name : (ok ? "keine Antwort" : "-"));
    lv_label_set_text(w.k_val[KR_RIG], i.rig_name[0] ? i.rig_name : "-");
    if (i.amp_name[0] || i.cab_name[0]) {
        lv_snprintf(buf, sizeof(buf), "%s / %s", i.amp_name[0] ? i.amp_name : "-",
                    i.cab_name[0] ? i.cab_name : "-");
        lv_label_set_text(w.k_val[KR_STACK], buf);
    } else {
        lv_label_set_text(w.k_val[KR_STACK], "-");
    }
    if (i.tempo_valid) {
        lv_snprintf(buf, sizeof(buf), "%u BPM%s", i.tempo_bpm, i.tempo_on ? "" : " (Tempo aus)");
        lv_label_set_text(w.k_val[KR_TEMPO], buf);
    } else {
        lv_label_set_text(w.k_val[KR_TEMPO], "-");
    }

    /* Ueberwachung */
    if (ok) {
        uint32_t age = i.now_ms - i.last_sense_ms;
        lv_snprintf(buf, sizeof(buf), "vor %lu,%lu s", (unsigned long)(age / 1000U),
                    (unsigned long)(age % 1000U / 100U));
    } else if (i.kemper == KL_KEMPER_LOST) {
        lv_snprintf(buf, sizeof(buf), "seit %lu s keine Antwort",
                    (unsigned long)((i.now_ms - i.last_sense_ms) / 1000U));
    } else {
        lv_snprintf(buf, sizeof(buf), "-");
    }
    lv_label_set_text(w.k_val[KR_SENSE], buf);

    int n = lv_snprintf(buf, sizeof(buf), "%lu empfangen, %lu gesendet, %lu Unterbrechungen",
                        (unsigned long)i.rx_messages, (unsigned long)i.tx_messages,
                        (unsigned long)i.lost_count);
    if (i.usb_errors && n > 0 && (uint32_t)n < sizeof(buf))
        lv_snprintf(buf + n, sizeof(buf) - (uint32_t)n, ", %lu USB-Fehler", (unsigned long)i.usb_errors);
    lv_label_set_text(w.k_val[KR_STATS], buf);
}

static void link_timer_cb(lv_timer_t * t)
{
    (void)t;
    update_link();
}

static void scr_delete_cb(lv_event_t * e)
{
    /* Timer genau dieses Screens loeschen (beim Ueberblenden kann schon
     * ein neuer Screen samt Timer existieren) */
    lv_timer_t * t = lv_event_get_user_data(e);
    if (t) lv_timer_delete(t);
    if (w.k_timer == t) w.k_timer = NULL;
}

static void update(void)
{
    for (int i = 0; i < 3; i++) ui_button_set_selected(w.level[i], kp_level() == (kp_level_t)(i + 1));
    if (kp_level() == KP_LEVEL_3)
        lv_label_set_text(w.level_info, "8 Effektmodule (A-D, X, MOD, DLY, REV), 125 Banks = 625 Rigs");
    else
        lv_label_set_text(w.level_info, "4 Effektmodule (A, B, DLY, REV), 10 Banks = 50 Rigs");
    ui_button_set_selected(w.fsm[0], kp_fs_mode() == KP_FS_MODE_RIGS);
    ui_button_set_selected(w.fsm[1], kp_fs_mode() == KP_FS_MODE_FX);
    update_link();
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
    /* Rechte Spalte ist hoeher als der Bildschirm: senkrecht scrollen */
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_scrollable(right, true);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(right, LV_SCROLLBAR_MODE_AUTO);

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

    /* Kemper-Verbindung */
    p = section(right, "Kemper-Verbindung");
    r = ui_row(p, 10);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    w.k_dot = lv_obj_create(r);
    lv_obj_remove_style_all(w.k_dot);
    lv_obj_set_size(w.k_dot, 14, 14);
    lv_obj_set_style_radius(w.k_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(w.k_dot, LV_OPA_COVER, 0);
    w.k_state = ui_label(r, "", UI_COL_TEXT, &lv_font_montserrat_18);
    for (int k = 0; k < KR_COUNT; k++) {
        r = ui_row(p, 8);
        lv_obj_set_width(r, LV_PCT(100));
        lv_obj_t * key = ui_label(r, KR_NAME[k], UI_COL_TEXT3, &lv_font_montserrat_16);
        lv_obj_set_width(key, 150);
        w.k_val[k] = ui_label(r, "-", UI_COL_TEXT, &lv_font_montserrat_16);
        lv_obj_set_width(w.k_val[k], 1);
        lv_obj_set_flex_grow(w.k_val[k], 1);
        lv_label_set_long_mode(w.k_val[k], LV_LABEL_LONG_DOT);
    }

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
    ui_label(r, "Display-Firmware:", UI_COL_TEXT3, &lv_font_montserrat_16);
    ui_label(r, __DATE__ "  " __TIME__, UI_COL_TEXT, &lv_font_montserrat_16);
    r = ui_row(p, 8);
    ui_label(r, "LVGL:", UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_obj_t * lv = ui_label(r, "", UI_COL_TEXT, &lv_font_montserrat_16);
    lv_label_set_text_fmt(lv, "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    update();
    ui_nav_set_refresh(scr, refresh);
    /* Lebenszeichen und Verbindungsdauer laufend aktualisieren */
    w.k_timer = lv_timer_create(link_timer_cb, 500, NULL);
    lv_obj_add_event_cb(scr, scr_delete_cb, LV_EVENT_DELETE, w.k_timer);
    return scr;
}
