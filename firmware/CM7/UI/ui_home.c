/**
 * @file ui_home.c
 * Startseite: quadratische Kacheln in der Mitte zum Oeffnen von
 * Live-Ansicht, Banks, Tuner, Bearbeiten und System.
 */
#include "ui_common.h"
#include "ui_screens.h"

#define TILE_SIZE  172
#define TILE_GAP   22

static struct {
    lv_obj_t * state;
    lv_obj_t * rig;
} w;

static void update(void)
{
    lv_label_set_text(w.state, kp_connected() ? "Kemper verbunden" : "Kein Kemper verbunden");
    lv_obj_set_style_text_color(w.state,
        lv_color_hex(kp_connected() ? 0x3ddc84 : UI_COL_TEXT3), 0);
    const kp_rig_t * r = kp_rig();
    char bank[12] = "Bank ?", slot[12] = "Rig ?";
    if (kp_bank_known()) lv_snprintf(bank, sizeof(bank), "Bank %u", (unsigned)(kp_bank() + 1));
    if (kp_slot_known()) lv_snprintf(slot, sizeof(slot), "Rig %u", (unsigned)(kp_slot() + 1));
    lv_label_set_text_fmt(w.rig, "%s  -  %s:  %s", bank, slot, r->name[0] ? r->name : "Leeres Rig");
}

static void refresh(uint32_t chg)
{
    if (chg & (KP_CHG_RIG | KP_CHG_SYSTEM | KP_CHG_NAMES)) update();
}

static void tile_cb(lv_event_t * e)
{
    ui_nav_go((ui_scr_t)(uintptr_t)lv_event_get_user_data(e));
}

/* Kachel: Farbverlauf in der Akzentfarbe, grosses Symbol, Beschriftung */
static void tile(lv_obj_t * row, const char * sym, const char * txt, uint32_t accent, ui_scr_t target)
{
    const lv_color_t ac = lv_color_hex(accent);
    const lv_color_t base = lv_color_hex(UI_COL_TILE);

    lv_obj_t * b = lv_obj_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, TILE_SIZE, TILE_SIZE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 16, 0);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_mix(ac, base, 90), 0);
    lv_obj_set_style_bg_grad_color(b, base, 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, ac, 0);
    lv_obj_set_style_bg_color(b, lv_color_mix(ac, base, 160), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, tile_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)target);

    /* Symbol im farbigen Kreis */
    lv_obj_t * disc = lv_obj_create(b);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 84, 84);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(disc, ac, 0);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * ic = ui_label(disc, sym, UI_COL_BG, &lv_font_montserrat_48);
    lv_obj_center(ic);

    ui_label(b, txt, UI_COL_TEXT, &lv_font_montserrat_22);
}

lv_obj_t * ui_home_build(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = ui_screen_base(&body);

    lv_obj_t * head = ui_row(body, 14);
    ui_label(head, "Kemper Player", UI_COL_TEXT, &lv_font_montserrat_28);
    ui_spacer(head, 1, 1, true);
    w.state = ui_label(head, "", UI_COL_TEXT3, &lv_font_montserrat_18);
    w.rig = ui_label(body, "", UI_COL_TEXT2, &lv_font_montserrat_20);
    lv_obj_set_style_pad_left(w.rig, 4, 0);

    /* Kacheln quadratisch, waagrecht und senkrecht in der Mitte */
    ui_spacer(body, LV_PCT(100), 1, true);
    lv_obj_t * row = ui_row(body, TILE_GAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    tile(row, LV_SYMBOL_PLAY,     "Live",       UI_COL_DLY,       UI_SCR_LIVE);
    tile(row, LV_SYMBOL_LIST,     "Banks",      UI_COL_MOD,       UI_SCR_BANKS);
    tile(row, LV_SYMBOL_AUDIO,    "Tuner",      UI_COL_EQ,        UI_SCR_TUNER);
    tile(row, LV_SYMBOL_EDIT,     "Bearbeiten", UI_COL_WAH,       UI_SCR_EDIT);
    tile(row, LV_SYMBOL_SETTINGS, "System",     UI_COL_PITCH,     UI_SCR_SETTINGS);
    ui_spacer(body, LV_PCT(100), 1, true);
    /* gleicher Abstand oben und unten: Kopfzeilen oben ausgleichen */
    ui_spacer(body, LV_PCT(100), 60, false);

    update();
    ui_nav_set_refresh(scr, refresh);
    return scr;
}
