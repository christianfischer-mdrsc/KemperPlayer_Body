/**
 * @file ui_banks.c
 * Bank-Uebersicht: links die Banks (seitenweise je 10, damit bei Level III
 * mit 125 Banks nicht 125 Schaltflaechen gleichzeitig existieren), rechts
 * die 5 Rigs der gewaehlten Bank. Antippen eines Rigs laedt es.
 */
#include "ui_common.h"
#include "ui_screens.h"

#define BANKS_PER_PAGE 10

static struct {
    ui_scr_t   back;
    uint8_t    browse;                       /* angezeigte Bank */
    lv_obj_t * page_lbl;
    lv_obj_t * bank_btn[BANKS_PER_PAGE];
    lv_obj_t * bank_lbl[BANKS_PER_PAGE];
    lv_obj_t * bank_dot[BANKS_PER_PAGE];
    lv_obj_t * head_lbl;
    lv_obj_t * head_dot;
    lv_obj_t * rig_btn[KP_RIGS_PER_BANK];
    lv_obj_t * rig_name[KP_RIGS_PER_BANK];
    lv_obj_t * rig_sub[KP_RIGS_PER_BANK];
    lv_obj_t * info;
} w;

static uint8_t page_first(void)
{
    return (uint8_t)((w.browse / BANKS_PER_PAGE) * BANKS_PER_PAGE);
}

static void update(void)
{
    uint8_t first = page_first();
    uint8_t count = kp_bank_count();
    uint8_t last = first + BANKS_PER_PAGE;
    if (last > count) last = count;

    lv_label_set_text_fmt(w.page_lbl, "%u-%u von %u", (unsigned)(first + 1), (unsigned)last, (unsigned)count);

    for (uint8_t i = 0; i < BANKS_PER_PAGE; i++) {
        uint8_t b = first + i;
        if (b >= count) { lv_obj_set_hidden(w.bank_btn[i], true); continue; }
        lv_obj_set_hidden(w.bank_btn[i], false);
        lv_label_set_text_fmt(w.bank_lbl[i], "Bank %u%s", (unsigned)(b + 1),
                              b == kp_bank() ? "  " LV_SYMBOL_PLAY : "");
        lv_obj_set_style_bg_color(w.bank_dot[i], lv_color_hex(kp_bank_color(b)), 0);
        ui_button_set_selected(w.bank_btn[i], b == w.browse);
    }

    lv_label_set_text_fmt(w.head_lbl, "Bank %u  (%s)", (unsigned)(w.browse + 1),
                          kp_bank_color_name(w.browse));
    lv_obj_set_style_bg_color(w.head_dot, lv_color_hex(kp_bank_color(w.browse)), 0);

    for (uint8_t s = 0; s < KP_RIGS_PER_BANK; s++) {
        const char * n = kp_rig_name_at(w.browse, s);
        bool cur = w.browse == kp_bank() && s == kp_slot();
        if (cur && kp_rig()->name[0]) n = kp_rig()->name;
        lv_label_set_text(w.rig_name[s], n[0] ? n : "Leer");
        lv_obj_set_style_text_color(w.rig_name[s],
            lv_color_hex(n[0] ? UI_COL_TEXT : UI_COL_TEXT3), 0);
        lv_label_set_text_fmt(w.rig_sub[s], "Rig %u%s", (unsigned)(s + 1), cur ? "  -  geladen" : "");
        ui_button_set_selected(w.rig_btn[s], cur);
    }
}

static void refresh(uint32_t chg)
{
    (void)chg;
    if (w.browse >= kp_bank_count()) w.browse = kp_bank();
    update();
}

static void bank_cb(lv_event_t * e)
{
    w.browse = (uint8_t)(page_first() + (uintptr_t)lv_event_get_user_data(e));
    update();
}

static void page_cb(lv_event_t * e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int pages = (kp_bank_count() + BANKS_PER_PAGE - 1) / BANKS_PER_PAGE;
    int p = w.browse / BANKS_PER_PAGE + d;
    if (p < 0) p = pages - 1;
    if (p >= pages) p = 0;
    w.browse = (uint8_t)(p * BANKS_PER_PAGE);
    update();
}

static void rig_cb(lv_event_t * e)
{
    kp_select_rig(w.browse, (uint8_t)(uintptr_t)lv_event_get_user_data(e));
    ui_nav_go(w.back);
}

lv_obj_t * ui_banks_build(void)
{
    lv_obj_t * body;
    lv_obj_t * scr = ui_screen_base(&body);

    /* Zurueck dorthin, wo man herkam */
    w.back = ui_nav_previous();
    if (w.back != UI_SCR_EDIT) w.back = UI_SCR_LIVE;
    w.browse = kp_bank();

    lv_obj_t * tb = ui_title_bar(body, "Banks und Rigs", w.back);
    ui_spacer(tb, 1, 1, true);
    w.info = ui_label(tb, "", UI_COL_TEXT3, &lv_font_montserrat_16);
    lv_label_set_text_fmt(w.info, "Level %s: %u Banks mit je 5 Rigs",
                          kp_level() == KP_LEVEL_3 ? "III" : (kp_level() == KP_LEVEL_2 ? "II" : "I"),
                          (unsigned)kp_bank_count());

    lv_obj_t * main = ui_row(body, 12);
    lv_obj_set_flex_grow(main, 1);
    lv_obj_set_flex_align(main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* links: Banks */
    lv_obj_t * left = ui_col(main, 6);
    lv_obj_set_width(left, 300);
    lv_obj_t * nav = ui_row(left, 6);
    lv_obj_t * pp = ui_icon_button(nav, LV_SYMBOL_UP, false);
    lv_obj_add_event_cb(pp, page_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    w.page_lbl = ui_label(nav, "", UI_COL_TEXT2, &lv_font_montserrat_16);
    lv_obj_set_flex_grow(w.page_lbl, 1);
    lv_obj_set_style_text_align(w.page_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t * pn = ui_icon_button(nav, LV_SYMBOL_DOWN, false);
    lv_obj_add_event_cb(pn, page_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    lv_obj_t * grid = ui_row(left, 6);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 6, 0);
    for (uint8_t i = 0; i < BANKS_PER_PAGE; i++) {
        lv_obj_t * b = ui_button(grid, "", &lv_font_montserrat_18, false);
        lv_obj_set_size(b, 147, 64);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 8, 0);
        lv_obj_add_event_cb(b, bank_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        w.bank_lbl[i] = lv_obj_get_child(b, 0);
        lv_obj_t * dot = lv_obj_create(b);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_to_index(dot, 0);
        w.bank_dot[i] = dot;
        w.bank_btn[i] = b;
    }

    /* rechts: Rigs der Bank */
    lv_obj_t * right = ui_col(main, 8);
    lv_obj_set_width(right, 1);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_t * h = ui_row(right, 10);
    lv_obj_set_height(h, 48);
    w.head_dot = lv_obj_create(h);
    lv_obj_remove_style_all(w.head_dot);
    lv_obj_set_size(w.head_dot, 18, 18);
    lv_obj_set_style_radius(w.head_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(w.head_dot, LV_OPA_COVER, 0);
    w.head_lbl = ui_label(h, "", UI_COL_TEXT, &lv_font_montserrat_24);

    for (uint8_t s = 0; s < KP_RIGS_PER_BANK; s++) {
        lv_obj_t * b = ui_button(right, "", &lv_font_montserrat_14, false);
        lv_obj_set_size(b, LV_PCT(100), 70);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_left(b, 18, 0);
        lv_obj_set_style_pad_row(b, 2, 0);
        lv_obj_add_event_cb(b, rig_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)s);
        w.rig_sub[s] = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_color(w.rig_sub[s], lv_color_hex(UI_COL_TEXT3), 0);
        w.rig_name[s] = ui_label(b, "", UI_COL_TEXT, &lv_font_montserrat_24);
        w.rig_btn[s] = b;
    }

    update();
    ui_nav_set_refresh(scr, refresh);
    return scr;
}
