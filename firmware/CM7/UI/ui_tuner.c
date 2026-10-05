/**
 * @file ui_tuner.c
 * Tuner: schaltet beim Oeffnen den Tuner-Modus des Kemper ein und beim
 * Verlassen wieder aus. Ton und Abweichung misst der Kemper und meldet sie
 * ueber kp_rx_tuner(); ohne Kemper zeigt die Anzeige "Kein Signal".
 * Referenzton (424-456 Hz) und Stummschaltung sind einstellbar.
 */
#include "ui_common.h"
#include "ui_screens.h"

#define SCALE_W   820
#define COL_OK    0x3ddc84
#define COL_NEAR  0xe8c93a
#define COL_FAR   0xe5483d

static struct {
    lv_obj_t * note;
    lv_obj_t * cents;
    lv_obj_t * needle;
    lv_obj_t * status;
    lv_obj_t * ref;
    lv_obj_t * mute;
} w;

static void update(void)
{
    bool sig = kp_tuner_signal();
    int c = kp_tuner_cents();
    int a = c < 0 ? -c : c;
    uint32_t col = !sig ? UI_COL_TEXT3 : (a <= 3 ? COL_OK : (a <= 15 ? COL_NEAR : COL_FAR));

    lv_label_set_text(w.note, sig ? kp_tuner_note() : "-");
    lv_obj_set_style_text_color(w.note, lv_color_hex(col), 0);
    if (sig) lv_label_set_text_fmt(w.cents, "%+d Cent", c);
    else     lv_label_set_text(w.cents, "");

    lv_obj_set_x(w.needle, (SCALE_W / 2) - 3 + (sig ? c * (SCALE_W / 2) / 50 : 0));
    lv_obj_set_style_bg_color(w.needle, lv_color_hex(col), 0);
    if (sig) lv_obj_set_hidden(w.needle, false);
    else     lv_obj_set_hidden(w.needle, true);

    if (!kp_connected())
        lv_label_set_text(w.status, "Kein Kemper verbunden. Die Stimmanzeige kommt vom Kemper Player.");
    else if (!sig)
        lv_label_set_text(w.status, "Kein Signal");
    else
        lv_label_set_text(w.status, a <= 3 ? "Gestimmt" : (c < 0 ? "Zu tief" : "Zu hoch"));

    lv_label_set_text_fmt(w.ref, "%u Hz", kp_tuner_reference());
    if (kp_tuner_mute()) lv_obj_add_state(w.mute, LV_STATE_CHECKED);
    else                 lv_obj_remove_state(w.mute, LV_STATE_CHECKED);
}

static void refresh(uint32_t chg)
{
    if (chg & (KP_CHG_TUNER | KP_CHG_SYSTEM)) update();
}

static void scr_delete_cb(lv_event_t * e)
{
    (void)e;
    kp_tuner_enable(false);
}

static void ref_cb(lv_event_t * e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    kp_tuner_set_reference((uint16_t)(kp_tuner_reference() + d));
}

static void mute_cb(lv_event_t * e)
{
    kp_tuner_set_mute(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

lv_obj_t * ui_tuner_build(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = ui_screen_base(&body);
    ui_scr_t back = ui_nav_previous() == UI_SCR_EDIT ? UI_SCR_EDIT : UI_SCR_LIVE;
    ui_title_bar(body, "Tuner", back);

    lv_obj_t * p = ui_panel(body);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 14, 0);

    w.note = ui_label(p, "-", UI_COL_TEXT3, &lv_font_montserrat_48);
    w.cents = ui_label(p, "", UI_COL_TEXT2, &lv_font_montserrat_20);

    /* Skala -50 .. +50 Cent */
    lv_obj_t * sc = lv_obj_create(p);
    lv_obj_remove_style_all(sc);
    lv_obj_set_size(sc, SCALE_W, 70);
    lv_obj_remove_flag(sc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i <= 10; i++) {
        lv_obj_t * t = lv_obj_create(sc);
        lv_obj_remove_style_all(t);
        bool mid = i == 5;
        lv_obj_set_size(t, mid ? 4 : 2, mid ? 50 : (i % 5 == 0 ? 34 : 22));
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(mid ? COL_OK : 0x4a4f56), 0);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, i * (SCALE_W - 4) / 10, 0);
    }
    w.needle = lv_obj_create(sc);
    lv_obj_remove_style_all(w.needle);
    lv_obj_set_size(w.needle, 6, 66);
    lv_obj_set_style_radius(w.needle, 3, 0);
    lv_obj_set_style_bg_opa(w.needle, LV_OPA_COVER, 0);
    lv_obj_set_y(w.needle, 2);

    lv_obj_t * lbls = ui_row(p, 0);
    lv_obj_set_width(lbls, SCALE_W);
    lv_obj_set_flex_align(lbls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(lbls, "-50", UI_COL_TEXT3, &lv_font_montserrat_14);
    ui_label(lbls, "0", UI_COL_TEXT3, &lv_font_montserrat_14);
    ui_label(lbls, "+50", UI_COL_TEXT3, &lv_font_montserrat_14);

    w.status = ui_label(p, "", UI_COL_TEXT2, &lv_font_montserrat_18);

    /* Einstellungen */
    lv_obj_t * row = ui_row(body, 12);
    lv_obj_set_height(row, 60);
    lv_obj_set_style_pad_hor(row, 8, 0);
    ui_label(row, "Referenz", UI_COL_TEXT3, &lv_font_montserrat_18);
    lv_obj_t * m = ui_icon_button(row, LV_SYMBOL_MINUS, false);
    lv_obj_add_event_cb(m, ref_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    w.ref = ui_label(row, "", UI_COL_TEXT, &lv_font_montserrat_24);
    lv_obj_set_width(w.ref, 100);
    lv_obj_set_style_text_align(w.ref, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t * pl = ui_icon_button(row, LV_SYMBOL_PLUS, false);
    lv_obj_add_event_cb(pl, ref_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    ui_spacer(row, 1, 1, true);
    ui_label(row, "Ausgang stumm beim Stimmen", UI_COL_TEXT3, &lv_font_montserrat_18);
    w.mute = lv_switch_create(row);
    lv_obj_set_size(w.mute, 70, 36);
    lv_obj_set_style_bg_color(w.mute, lv_color_hex(UI_COL_SEL_LINE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(w.mute, mute_cb, LV_EVENT_VALUE_CHANGED, NULL);

    kp_tuner_enable(true);
    lv_obj_add_event_cb(scr, scr_delete_cb, LV_EVENT_DELETE, NULL);
    update();
    ui_nav_set_refresh(scr, refresh);
    return scr;
}
