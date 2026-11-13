/**
 * @file ui_statusbar.c
 * @brief See ui_statusbar.h.
 */
#include "ui_statusbar.h"
#include "ui_theme.h"
#include "bled_core.h"
#include "bled_storage.h"
#include "crowpanel_bsp.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#define COL_DIM lv_color_hex(0x7F93A8)

typedef struct {
    lv_obj_t *dot;
    lv_obj_t *label;
    uint32_t  last_color;
} indicator_t;

static lv_obj_t *s_chip;
static lv_obj_t *s_chip_label;
static lv_obj_t *s_clock;
static lv_obj_t *s_sim;
static indicator_t s_unit, s_sd, s_wifi, s_pwr;
static uint32_t s_chip_color;

static void make_indicator(lv_obj_t *parent, indicator_t *ind, const char *text)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_column(box, 5, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ind->dot = lv_obj_create(box);
    lv_obj_set_size(ind->dot, 10, 10);
    lv_obj_set_style_radius(ind->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ind->dot, 0, 0);
    lv_obj_set_style_bg_color(ind->dot, COL_DIM, 0);
    lv_obj_remove_flag(ind->dot, LV_OBJ_FLAG_SCROLLABLE);
    ind->last_color = lv_color_to_u32(COL_DIM);

    ind->label = ui_label(box, text, UI_FONT_SM, lv_color_white());
}

static void set_indicator(indicator_t *ind, lv_color_t color, const char *text)
{
    uint32_t c = lv_color_to_u32(color);
    if (c != ind->last_color) {
        ind->last_color = c;
        lv_obj_set_style_bg_color(ind->dot, color, 0);
    }
    ui_set_text(ind->label, text);
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    const bled_system_t *sys = bled_sys();
    const bled_case_t *cs = bled_case();
    char buf[64];

    /* ---- case chip ---- */
    lv_color_t chip_col;
    if (cs->active) {
        int level = bled_ebl_level(cs->total_ebl_ml);
        chip_col = (level == 0 && cs->sd_warning) ? UI_COLOR_WARN : ui_level_color(level);
        int ml = (int)(cs->total_ebl_ml + 0.5f);
        if (ml < 0) {
            ml = 0;
        } else if (ml > 99999) {
            ml = 99999;
        }
        snprintf(buf, sizeof(buf), "CASE %s   %d mL%s", cs->id, ml, cs->sd_warning ? "   NOT SAVED" : "");
    } else {
        chip_col = lv_color_hex(0x27476B);
        snprintf(buf, sizeof(buf), "NO ACTIVE CASE");
    }
    ui_set_text(s_chip_label, buf);
    if (lv_color_to_u32(chip_col) != s_chip_color) {
        s_chip_color = lv_color_to_u32(chip_col);
        lv_obj_set_style_bg_color(s_chip, chip_col, 0);
    }
    ui_set_text_color(s_chip_label, cs->active ? lv_color_white() : lv_color_hex(0xB8C7D6));

    /* ---- indicators ---- */
    ui_set_hidden(s_sim, !sys->unit_simulated);
    set_indicator(&s_unit, sys->unit_online ? UI_COLOR_OK : UI_COLOR_ALERT, "UNIT");

    bled_sd_info_t sd;
    bled_storage_get_info(&sd);
    lv_color_t sd_col = (sd.state == BLED_SD_READY) ? UI_COLOR_OK : (sd.state == BLED_SD_MOUNTING ? UI_COLOR_WARN : UI_COLOR_ALERT);
    set_indicator(&s_sd, sd_col, "SD");

    set_indicator(&s_wifi, wifi_manager_is_connected() ? UI_COLOR_OK : COL_DIM, "WiFi");

    if (!sys->unit_online || sys->battery_pct < 0) {
        set_indicator(&s_pwr, COL_DIM, "PWR --");
    } else if (sys->on_battery) {
        snprintf(buf, sizeof(buf), "BAT %d%%", sys->battery_pct);
        set_indicator(&s_pwr, sys->battery_pct < 20 ? UI_COLOR_ALERT : UI_COLOR_WARN, buf);
    } else {
        set_indicator(&s_pwr, UI_COLOR_OK, "AC");
    }

    /* ---- clock ---- */
    pcf8563_time_t t;
    if (bsp_rtc_get_time(&t) == ESP_OK) {
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(t.hour % 24), (unsigned)(t.minute % 60));
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    ui_set_text(s_clock, buf);
}

lv_obj_t *ui_statusbar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, BSP_LCD_H_RES, UI_STATUSBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_NAVY, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = ui_label(bar, "BLED", UI_FONT_LG, lv_color_white());
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 12, 0);

    s_chip = lv_obj_create(bar);
    lv_obj_set_size(s_chip, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(s_chip, 11, 0);
    lv_obj_set_style_border_width(s_chip, 0, 0);
    lv_obj_set_style_pad_hor(s_chip, 12, 0);
    lv_obj_set_style_pad_ver(s_chip, 0, 0);
    lv_obj_set_style_shadow_width(s_chip, 0, 0);
    lv_obj_remove_flag(s_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_chip, LV_ALIGN_LEFT_MID, 74, 0);
    s_chip_label = ui_label(s_chip, "NO ACTIVE CASE", UI_FONT_SM, lv_color_hex(0xB8C7D6));
    lv_obj_center(s_chip_label);
    s_chip_color = 0xFFFFFFFFu;

    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_set_size(right, LV_SIZE_CONTENT, UI_STATUSBAR_H);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_column(right, 14, 0);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_sim = lv_obj_create(right);
    lv_obj_set_size(s_sim, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_radius(s_sim, 4, 0);
    lv_obj_set_style_bg_color(s_sim, UI_COLOR_WARN, 0);
    lv_obj_set_style_border_width(s_sim, 0, 0);
    lv_obj_set_style_pad_hor(s_sim, 7, 0);
    lv_obj_set_style_pad_ver(s_sim, 0, 0);
    lv_obj_remove_flag(s_sim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sim_label = ui_label(s_sim, "SIM", UI_FONT_SM, lv_color_black());
    lv_obj_center(sim_label);
    lv_obj_add_flag(s_sim, LV_OBJ_FLAG_HIDDEN);

    make_indicator(right, &s_unit, "UNIT");
    make_indicator(right, &s_sd, "SD");
    make_indicator(right, &s_wifi, "WiFi");
    make_indicator(right, &s_pwr, "PWR --");
    s_clock = ui_label(right, "--:--", UI_FONT_LG, lv_color_white());

    refresh(NULL);
    lv_timer_create(refresh, 500, NULL);
    return bar;
}
