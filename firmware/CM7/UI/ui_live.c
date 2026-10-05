/**
 * @file ui_live.c
 * Live-Ansicht: grosse, reduzierte Anzeige zum Spielen.
 *
 *  - Bank (antippen = Bank-Uebersicht, Pfeile = Bank wechseln)
 *  - Rig-Name, Amp/Cab
 *  - Tempo (antippen = Tap Tempo)
 *  - Effektkette (antippen = Modul ein/aus)
 *  - Footswitch-Leiste FS1-FS6
 */
#include "ui_common.h"
#include "ui_screens.h"
#include "ui_boot.h"

static struct {
    lv_obj_t * bank_lbl;
    lv_obj_t * bank_dot;
    lv_obj_t * edited;
    lv_obj_t * bpm;
    lv_obj_t * bpm_led;
    lv_obj_t * slot;
    lv_obj_t * rig;
    lv_obj_t * stack;
    ui_chain_t chain;
    ui_fsbar_t fs;
} w;

static void update_tempo(void)
{
    const kp_rig_t * r = kp_rig();
    if (r->tempo_on) lv_label_set_text_fmt(w.bpm, "%u BPM", r->tempo_bpm);
    else             lv_label_set_text(w.bpm, "-- BPM");
    lv_obj_set_style_bg_color(w.bpm_led,
        lv_color_hex(r->tempo_on ? UI_COL_DIST : 0x3a3e44), 0);
}

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

    if (r->amp[0]) {
        lv_label_set_text_fmt(w.stack, "Amp: %s   /   Cab: %s", r->amp, r->cab[0] ? r->cab : "-");
    } else if (!kp_connected()) {
        lv_label_set_text(w.stack, "Rig-Daten kommen vom Kemper Player, sobald er verbunden ist");
    } else {
        lv_label_set_text(w.stack, "");
    }

    if (r->edited) lv_obj_set_hidden(w.edited, false);
    else           lv_obj_set_hidden(w.edited, true);
}

static void refresh(uint32_t chg)
{
    if (chg & (KP_CHG_RIG | KP_CHG_SYSTEM | KP_CHG_NAMES)) update_rig();
    if (chg & (KP_CHG_TEMPO | KP_CHG_RIG))  update_tempo();
    if (chg & (KP_CHG_MODULES | KP_CHG_RIG)) ui_chain_update(&w.chain);
    ui_fsbar_update(&w.fs);
}

/* ---- Ereignisse ------------------------------------------------------- */
static void bank_cb(lv_event_t * e)    { (void)e; ui_nav_go(UI_SCR_BANKS); }
static void bank_prev_cb(lv_event_t * e) { (void)e; kp_bank_step(-1); }
static void bank_next_cb(lv_event_t * e) { (void)e; kp_bank_step(+1); }
static void edit_cb(lv_event_t * e)    { (void)e; ui_nav_go(UI_SCR_EDIT); }
static void tap_cb(lv_event_t * e)     { (void)e; kp_tap(lv_tick_get()); }

static void tile_cb(lv_event_t * e)
{
    uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (w.chain.id[i] != KP_CHAIN_STACK) kp_module_toggle((kp_mod_t)w.chain.id[i]);
}

/* ---- Aufbau ----------------------------------------------------------- */
lv_obj_t * ui_live_build(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = ui_screen_base(&body);

    /* Kopfzeile */
    lv_obj_t * head = ui_row(body, 10);
    lv_obj_t * prev = ui_icon_button(head, LV_SYMBOL_LEFT, false);
    lv_obj_add_event_cb(prev, bank_prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * chip = ui_bank_chip(head, &w.bank_lbl, &w.bank_dot);
    lv_obj_add_event_cb(chip, bank_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * next = ui_icon_button(head, LV_SYMBOL_RIGHT, false);
    lv_obj_add_event_cb(next, bank_next_cb, LV_EVENT_CLICKED, NULL);

    ui_spacer(head, 1, 1, true);

    w.edited = ui_label(head, "bearbeitet", 0xe8c93a, &lv_font_montserrat_16);

    lv_obj_t * bpm = ui_button(head, "", &lv_font_montserrat_24, false);
    lv_obj_set_flex_flow(bpm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bpm, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bpm, 10, 0);
    lv_obj_add_event_cb(bpm, tap_cb, LV_EVENT_PRESSED, NULL);   /* sofort, fuer genaues Tap */
    w.bpm = lv_obj_get_child(bpm, 0);
    w.bpm_led = lv_obj_create(bpm);
    lv_obj_remove_style_all(w.bpm_led);
    lv_obj_set_size(w.bpm_led, 14, 14);
    lv_obj_set_style_radius(w.bpm_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(w.bpm_led, LV_OPA_COVER, 0);
    lv_obj_move_to_index(w.bpm_led, 0);
    lv_obj_remove_flag(w.bpm_led, LV_OBJ_FLAG_CLICKABLE);
    ui_label(bpm, "TAP", UI_COL_TEXT3, &lv_font_montserrat_14);

    lv_obj_t * edit = ui_button(head, "Bearbeiten", &lv_font_montserrat_18, false);
    lv_obj_set_style_text_color(edit, lv_color_hex(UI_COL_TEXT2), 0);
    lv_obj_add_event_cb(edit, edit_cb, LV_EVENT_CLICKED, NULL);

    /* Rig */
    w.slot = ui_label(body, "", UI_COL_TEXT3, &lv_font_montserrat_20);
    lv_obj_set_style_pad_left(w.slot, 12, 0);
    lv_obj_set_style_pad_top(w.slot, 4, 0);
    w.rig = ui_label(body, "", UI_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_set_size(w.rig, LV_PCT(100), 60);
    lv_obj_set_style_pad_left(w.rig, 12, 0);
    lv_label_set_long_mode(w.rig, LV_LABEL_LONG_DOT);
    w.stack = ui_label(body, "", UI_COL_TEXT2, &lv_font_montserrat_18);
    lv_obj_set_style_pad_left(w.stack, 12, 0);

    ui_spacer(body, LV_PCT(100), 1, true);

    ui_chain_create(&w.chain, body, false, tile_cb);
    ui_spacer(body, LV_PCT(100), 22, false);
    ui_fsbar_create(&w.fs, body);

    update_rig();
    update_tempo();
    ui_nav_set_refresh(scr, refresh);
    return scr;
}

/* ---- Einstieg --------------------------------------------------------- */
void ui_start(void)
{
    kp_init();
    lv_obj_t * scr = ui_nav_build(UI_SCR_LIVE);

    /* Startbildschirm abschliessen (Bootzeit stoppen, danach Ueberblendung).
     * Ohne vorherigen ui_boot_create() wird die Live-Ansicht direkt geladen. */
    if (ui_boot_is_active()) ui_boot_finish(scr);
    else                     lv_screen_load(scr);
}

void ui_footswitch(uint8_t idx)
{
    kp_footswitch(idx, lv_tick_get());
}
