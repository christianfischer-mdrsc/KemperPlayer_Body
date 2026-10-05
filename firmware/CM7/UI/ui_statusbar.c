/**
 * @file ui_statusbar.c
 * Statusleiste oben: USB-Verbindung links, Datum und Uhrzeit rechts.
 * Jeder Screen hat seine eigene Leiste; sie meldet sich beim Loeschen ab.
 *
 * Benoetigte Fonts in lv_conf.h: LV_FONT_MONTSERRAT_16, _22
 */
#include "ui_statusbar.h"

#define SB_MAX        4
#define SB_COL_BG     0x1a1c1f
#define SB_COL_USB_ON 0x3ddc84
#define SB_COL_USB_OFF 0x6a6e74

typedef struct {
    lv_obj_t * bar;
    lv_obj_t * usb_icon;
    lv_obj_t * usb_text;
    lv_obj_t * date;
    lv_obj_t * time;
} statusbar_t;

static statusbar_t s_bars[SB_MAX];
static uint8_t     s_count;
static bool        s_usb;
static int16_t     s_last_min = -1;
static lv_timer_t * s_timer;

static const char * const WEEKDAY[] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };

/* Wochentag nach Sakamoto, 0 = Sonntag */
static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

__attribute__((weak)) bool ui_statusbar_get_time(ui_datetime_t * out)
{
    (void)out;
    return false;
}

__attribute__((weak)) bool ui_statusbar_set_time(const ui_datetime_t * in)
{
    (void)in;
    return false;
}

/* Leiste wird mit ihrem Screen geloescht: aus der Liste nehmen */
static void bar_delete_cb(lv_event_t * e)
{
    lv_obj_t * bar = lv_event_get_target(e);
    for (uint8_t i = 0; i < s_count; i++) {
        if (s_bars[i].bar == bar) {
            for (uint8_t k = i; k + 1 < s_count; k++) s_bars[k] = s_bars[k + 1];
            s_count--;
            break;
        }
    }
}

static void update_clock(bool force)
{
    ui_datetime_t dt;
    char t[8], d[20];

    if (!ui_statusbar_get_time(&dt)) {
        if (!force && s_last_min == -2) return;
        s_last_min = -2;
        lv_snprintf(t, sizeof(t), "--:--");
        d[0] = '\0';
    } else {
        if (!force && dt.minute == s_last_min) return;   /* nur beim Minutenwechsel */
        s_last_min = dt.minute;
        lv_snprintf(t, sizeof(t), "%02u:%02u", dt.hour, dt.minute);
        lv_snprintf(d, sizeof(d), "%s, %02u.%02u.%04u",
                    WEEKDAY[weekday(dt.year, dt.month, dt.day)],
                    dt.day, dt.month, dt.year);
    }
    for (uint8_t i = 0; i < s_count; i++) {
        lv_label_set_text(s_bars[i].time, t);
        lv_label_set_text(s_bars[i].date, d);
    }
}

static void apply_usb(statusbar_t * b)
{
    lv_obj_set_style_text_color(b->usb_icon,
        lv_color_hex(s_usb ? SB_COL_USB_ON : SB_COL_USB_OFF), 0);
    lv_obj_set_style_text_color(b->usb_text,
        lv_color_hex(s_usb ? UI_COL_TEXT : UI_COL_TEXT3), 0);
    lv_label_set_text(b->usb_text, s_usb ? "Kemper verbunden" : "Kein Kemper");
}

static void timer_cb(lv_timer_t * t)
{
    (void)t;
    update_clock(false);
}

static lv_obj_t * sb_label(lv_obj_t * parent, uint32_t color, const lv_font_t * font)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

lv_obj_t * ui_statusbar_create(lv_obj_t * parent)
{
    if (s_count >= SB_MAX) return NULL;
    statusbar_t * b = &s_bars[s_count++];

    lv_obj_t * bar = lv_obj_create(parent);
    b->bar = bar;
    lv_obj_add_event_cb(bar, bar_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(bar, LV_PCT(100), UI_STATUSBAR_HEIGHT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bar, lv_color_hex(SB_COL_BG), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* links: USB */
    b->usb_icon = sb_label(bar, SB_COL_USB_OFF, &lv_font_montserrat_22);
    lv_label_set_text(b->usb_icon, LV_SYMBOL_USB);
    b->usb_text = sb_label(bar, UI_COL_TEXT3, &lv_font_montserrat_16);

    /* Abstandhalter */
    lv_obj_t * sp = lv_obj_create(bar);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, 1, 1);
    lv_obj_set_flex_grow(sp, 1);

    /* rechts: Datum | Uhrzeit */
    b->date = sb_label(bar, UI_COL_TEXT2, &lv_font_montserrat_16);

    lv_obj_t * sep = lv_obj_create(bar);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 1, 20);
    lv_obj_set_style_bg_color(sep, lv_color_hex(UI_COL_LINE), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_hor(sep, 6, 0);

    b->time = sb_label(bar, UI_COL_TEXT, &lv_font_montserrat_22);

    apply_usb(b);
    if (!s_timer) s_timer = lv_timer_create(timer_cb, 1000, NULL);
    update_clock(true);
    return bar;
}

void ui_statusbar_set_usb(bool connected)
{
    s_usb = connected;
    for (uint8_t i = 0; i < s_count; i++) apply_usb(&s_bars[i]);
}

void ui_statusbar_refresh(void)
{
    update_clock(true);
}
