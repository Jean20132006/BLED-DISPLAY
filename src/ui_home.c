/**
 * @file ui_home.c
 * @brief Home tab with a sidebar and stacked Profile, Dashboard,
 *        Brightness, Sound and About pages.  The Dashboard shows the live
 *        state of the whole BLED system (measurement unit, case, SD card,
 *        power, clock, network).
 *
 * This version intentionally contains no LVGL animations.
 */
#include "ui_home.h"
#include "crowpanel_bsp.h"
#include "img_profile_avatar.h"
#include "bled_core.h"
#include "bled_storage.h"
#include "ui_theme.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include <stdint.h>
#include <stdbool.h>

#define MENU_BUTTON_COUNT 5
static lv_obj_t *s_menu_buttons[MENU_BUTTON_COUNT];
static int s_menu_button_count = 0;

static lv_obj_t *s_page_profile;
static lv_obj_t *s_page_dashboard;
static lv_obj_t *s_page_brightness;
static lv_obj_t *s_page_sound;
static lv_obj_t *s_page_about;

/* -------------------------------------------------------------------------- */
/* Shared helpers                                                             */
/* -------------------------------------------------------------------------- */

static void show_page(lv_obj_t *page_to_show)
{
    lv_obj_add_flag(s_page_profile, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_dashboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_brightness, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_sound, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_about, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_flag(page_to_show, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(page_to_show, 0, LV_ANIM_OFF);
}

static void menu_btn_cb(lv_event_t *e)
{
    lv_obj_t *page = (lv_obj_t *)lv_event_get_user_data(e);
    show_page(page);

    lv_obj_t *clicked_btn = lv_event_get_target_obj(e);
    for (int i = 0; i < s_menu_button_count; i++) {
        if (s_menu_buttons[i] == clicked_btn) {
            lv_obj_add_state(s_menu_buttons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_menu_buttons[i], LV_STATE_CHECKED);
        }
    }
}

static lv_obj_t *make_page(lv_obj_t *content_area)
{
    lv_obj_t *page = lv_obj_create(content_area);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(page, 14, 0);
    lv_obj_set_style_pad_bottom(page, 20, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);

    /* Each page can scroll vertically if its content is taller than the view. */
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);

    return page;
}

static lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *create_menu_button(lv_obj_t *sidebar,
                                    const char *symbol,
                                    const char *text,
                                    lv_obj_t *page)
{
    lv_obj_t *button = lv_button_create(sidebar);
    lv_obj_set_width(button, lv_pct(100));

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text_fmt(label, "%s  %s", symbol, text);
    lv_obj_center(label);

    /* Highlight style for whichever page is currently shown. */
    lv_obj_set_style_bg_color(button, UI_COLOR_TINT, 0);
    lv_obj_set_style_text_color(button, UI_COLOR_PRIMARY_D, 0);
    lv_obj_set_style_bg_color(button, UI_COLOR_PRIMARY, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_CHECKED);

    lv_obj_add_event_cb(button, menu_btn_cb, LV_EVENT_CLICKED, page);

    if (s_menu_button_count < MENU_BUTTON_COUNT) {
        s_menu_buttons[s_menu_button_count++] = button;
    }
    return button;
}

static lv_obj_t *create_value_row(lv_obj_t *parent,
                                  const char *caption,
                                  const char *value,
                                  lv_coord_t y)
{
    lv_obj_t *caption_label = lv_label_create(parent);
    lv_label_set_text(caption_label, caption);
    lv_obj_align(caption_label, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_set_style_text_opa(caption_label, LV_OPA_60, 0);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, 0, y);

    return value_label;
}

/* -------------------------------------------------------------------------- */
/* Hardware callbacks                                                         */
/* -------------------------------------------------------------------------- */

static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    int32_t val = lv_slider_get_value(slider); /* 0-10 */
    bsp_display_backlight_set((uint8_t)val);

    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text_fmt(label, "%d / 10", (int)val);
}

static void screen_off_btn_cb(lv_event_t *e)
{
    (void)e;
    bsp_display_backlight_off();
}

static void buzzer_btn_cb(lv_event_t *e)
{
    bool on = (bool)(intptr_t)lv_event_get_user_data(e);
    bsp_buzzer_set(on);
}

static void speaker_btn_cb(lv_event_t *e)
{
    bool on = (bool)(intptr_t)lv_event_get_user_data(e);
    bsp_speaker_set(on);
}

/* -------------------------------------------------------------------------- */
/* Profile page                                                               */
/* -------------------------------------------------------------------------- */

static void build_profile_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Profile");
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, "Personal information and workplace preferences");
    lv_obj_set_style_text_opa(subtitle, LV_OPA_60, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 32);

    lv_obj_t *identity_card = create_card(page, lv_pct(100), 116);
    lv_obj_align(identity_card, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t *avatar = lv_obj_create(identity_card);
    lv_obj_set_size(avatar, 72, 72);
    lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_clip_corner(avatar, true, 0); /* clips the image to the circle */
    lv_obj_t *avatar_img = lv_image_create(avatar);
    lv_image_set_src(avatar_img, &img_profile_avatar);
    lv_obj_center(avatar_img);

    lv_obj_t *name = lv_label_create(identity_card);
    lv_label_set_text(name, "Jean Eric Tsanga");
#if LV_FONT_MONTSERRAT_20
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
#endif
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 92, 8);

    lv_obj_t *role = lv_label_create(identity_card);
    lv_label_set_text(role, "Administrator  |  Embedded Systems");
    lv_obj_set_style_text_opa(role, LV_OPA_60, 0);
    lv_obj_align(role, LV_ALIGN_TOP_LEFT, 92, 39);

    lv_obj_t *status = lv_label_create(identity_card);
    lv_label_set_text(status, LV_SYMBOL_OK "  Profile active");
    lv_obj_set_style_text_color(status, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 92, 66);

    lv_obj_t *details_card = create_card(page, 290, 212);
    lv_obj_align(details_card, LV_ALIGN_TOP_LEFT, 0, 194);

    lv_obj_t *details_title = lv_label_create(details_card);
    lv_label_set_text(details_title, "Contact details");
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(details_title, &lv_font_montserrat_16, 0);
#endif
    lv_obj_align(details_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /*create_value_row(details_card, "Username", "jean", 42);
    create_value_row(details_card, "Email", "jeanerictsanga8@gmail.com", 76);
    create_value_row(details_card, "Phone", "+1 301 923 5717", 110);
    create_value_row(details_card, "Birthday", "August 13", 144); */

    create_value_row(details_card, LV_SYMBOL_EDIT      " Username",  "jean", 42);
    create_value_row(details_card, LV_SYMBOL_ENVELOPE  " Email",     "jean@gmail.com", 76);
    create_value_row(details_card, LV_SYMBOL_CALL      " Phone",     "+1 301 923 5717", 110);
    create_value_row(details_card, LV_SYMBOL_BELL      " Birthday",  "August 13", 144);

    lv_obj_t *preferences_card = create_card(page, 290, 212);
    lv_obj_align(preferences_card, LV_ALIGN_TOP_RIGHT, 0, 194);

    lv_obj_t *pref_title = lv_label_create(preferences_card);
    lv_label_set_text(pref_title, "Work preferences");
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(pref_title, &lv_font_montserrat_16, 0);
#endif
    lv_obj_align(pref_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *hard_label = lv_label_create(preferences_card);
    lv_label_set_text(hard_label, "Hard-working");
    lv_obj_align(hard_label, LV_ALIGN_TOP_LEFT, 0, 45);

    lv_obj_t *hard_switch = lv_switch_create(preferences_card);
    lv_obj_align(hard_switch, LV_ALIGN_TOP_RIGHT, 0, 36);
    lv_obj_add_state(hard_switch, LV_STATE_CHECKED);

    lv_obj_t *team_label = lv_label_create(preferences_card);
    lv_label_set_text(team_label, "Team player");
    lv_obj_align(team_label, LV_ALIGN_TOP_LEFT, 0, 94);

    lv_obj_t *team_switch = lv_switch_create(preferences_card);
    lv_obj_align(team_switch, LV_ALIGN_TOP_RIGHT, 0, 85);
    lv_obj_add_state(team_switch, LV_STATE_CHECKED);

    lv_obj_t *experience = lv_label_create(preferences_card);
    lv_label_set_text(experience, "Experience level");
    lv_obj_align(experience, LV_ALIGN_TOP_LEFT, 0, 142);

    lv_obj_t *exp_slider = lv_slider_create(preferences_card);
    lv_obj_set_width(exp_slider, 135);
    lv_slider_set_range(exp_slider, 0, 10);
    lv_slider_set_value(exp_slider, 8, LV_ANIM_OFF);
    lv_obj_align(exp_slider, LV_ALIGN_TOP_RIGHT, 0, 148);
}

/* -------------------------------------------------------------------------- */
/* Dashboard page                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    lv_obj_t *l1;   /* status line (coloured)  */
    lv_obj_t *l2;
    lv_obj_t *l3;
} dash_card_t;

static lv_obj_t *s_banner;
static lv_obj_t *s_banner_icon;
static lv_obj_t *s_banner_title;
static lv_obj_t *s_banner_text;
static dash_card_t s_dc_unit, s_dc_case, s_dc_sd, s_dc_power, s_dc_clock, s_dc_net;

static lv_obj_t *dash_line(lv_obj_t *card, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *l = ui_label(card, "--", UI_FONT_SM, color);
    ui_label_oneline(l, 173);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
    return l;
}

static dash_card_t dash_make_card(lv_obj_t *page, const char *title, lv_align_t align, lv_coord_t y)
{
    lv_obj_t *card = create_card(page, 193, 104);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_align(card, align, 0, y);
    lv_obj_t *t = ui_label(card, title, UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    dash_card_t c;
    c.l1 = dash_line(card, 26, UI_COLOR_TEXT);
    c.l2 = dash_line(card, 46, UI_COLOR_MUTED);
    c.l3 = dash_line(card, 66, UI_COLOR_MUTED);
    return c;
}

/* append "text" to the banner message, separated by "  |  " */
static void issue_add(char *buf, size_t size, const char *text)
{
    size_t used = strlen(buf);
    if (used > 0 && used + 5 < size) {
        memcpy(buf + used, "  |  ", 6);
        used += 5;
    }
    size_t len = strlen(text);
    if (used + len + 1 > size) {
        len = size > used + 1 ? size - used - 1 : 0;
    }
    memcpy(buf + used, text, len);
    buf[used + len] = '\0';
}

static void dashboard_refresh(lv_timer_t *timer)
{
    (void)timer;
    const bled_system_t *sys = bled_sys();
    const bled_case_t *cs = bled_case();
    bled_sd_info_t sd;
    bled_storage_get_info(&sd);
    char buf[96];
    char a[16];

    /* ---- Measurement unit ---- */
    if (sys->unit_online) {
        ui_set_text(s_dc_unit.l1, sys->unit_simulated ? LV_SYMBOL_OK " Online (SIM)" : LV_SYMBOL_OK " Online");
        ui_set_text_color(s_dc_unit.l1, sys->unit_simulated ? UI_COLOR_WARN : UI_COLOR_OK);
        snprintf(buf, sizeof(buf), "State: %s", bled_unit_state_text(sys->unit_state));
        ui_set_text(s_dc_unit.l2, buf);
        ui_set_text(s_dc_unit.l3, sys->cartridge ? "Cartridge: loaded" : "Cartridge: none");
    } else {
        ui_set_text(s_dc_unit.l1, LV_SYMBOL_CLOSE " Offline");
        ui_set_text_color(s_dc_unit.l1, UI_COLOR_ALERT);
        ui_set_text(s_dc_unit.l2, "No data from unit");
        ui_set_text(s_dc_unit.l3, "Check UART0 cable");
    }

    /* ---- Active case ---- */
    if (cs->active) {
        snprintf(buf, sizeof(buf), "ID: %s", cs->id);
        ui_set_text(s_dc_case.l1, buf);
        ui_set_text_color(s_dc_case.l1, UI_COLOR_TEXT);
        snprintf(buf, sizeof(buf), "Pads: %d", cs->pad_count);
        ui_set_text(s_dc_case.l2, buf);
        ui_format_ml(a, sizeof(a), cs->total_ebl_ml);
        snprintf(buf, sizeof(buf), "EBL: %s mL", a);
        ui_set_text(s_dc_case.l3, buf);
        ui_set_text_color(s_dc_case.l3, ui_level_color(bled_ebl_level(cs->total_ebl_ml)));
    } else {
        ui_set_text(s_dc_case.l1, "No active case");
        ui_set_text_color(s_dc_case.l1, UI_COLOR_MUTED);
        ui_set_text(s_dc_case.l2, "Start one in New Case");
        ui_set_text(s_dc_case.l3, "");
    }

    /* ---- SD card ---- */
    if (sd.state == BLED_SD_READY) {
        ui_set_text(s_dc_sd.l1, LV_SYMBOL_OK " Mounted");
        ui_set_text_color(s_dc_sd.l1, UI_COLOR_OK);
        unsigned tenths = (unsigned)(sd.capacity_bytes / 100000000ULL);       /* tenths of a GB */
        if (tenths > 99999u) {
            tenths = 99999u;
        }
        snprintf(buf, sizeof(buf), "Capacity: %u.%u GB", tenths / 10u, tenths % 10u);
        ui_set_text(s_dc_sd.l2, buf);
        if (sd.records >= 0) {
            snprintf(buf, sizeof(buf), "Records: %d", sd.records);
        } else {
            snprintf(buf, sizeof(buf), "Records: --");
        }
        ui_set_text(s_dc_sd.l3, buf);
    } else if (sd.state == BLED_SD_MOUNTING) {
        ui_set_text(s_dc_sd.l1, LV_SYMBOL_REFRESH " Mounting...");
        ui_set_text_color(s_dc_sd.l1, UI_COLOR_WARN);
        ui_set_text(s_dc_sd.l2, "");
        ui_set_text(s_dc_sd.l3, "");
    } else {
        ui_set_text(s_dc_sd.l1, LV_SYMBOL_CLOSE " Not detected");
        ui_set_text_color(s_dc_sd.l1, UI_COLOR_ALERT);
        ui_set_text(s_dc_sd.l2, "Insert FAT32 card");
        ui_set_text(s_dc_sd.l3, "DIP S1/S0 = 1/1");
    }

    /* ---- Power (reported by the measurement unit) ---- */
    if (!sys->unit_online || sys->battery_pct < 0) {
        ui_set_text(s_dc_power.l1, "Unknown");
        ui_set_text_color(s_dc_power.l1, UI_COLOR_MUTED);
        ui_set_text(s_dc_power.l2, sys->unit_online ? "" : "Unit offline");
        ui_set_text(s_dc_power.l3, "");
    } else {
        bool low = sys->on_battery && sys->battery_pct < 20;
        ui_set_text(s_dc_power.l1, sys->on_battery ? LV_SYMBOL_WARNING " On battery" : LV_SYMBOL_CHARGE " AC power");
        ui_set_text_color(s_dc_power.l1, low ? UI_COLOR_ALERT : (sys->on_battery ? UI_COLOR_WARN : UI_COLOR_OK));
        snprintf(buf, sizeof(buf), "Battery: %d%%", sys->battery_pct);
        ui_set_text(s_dc_power.l2, buf);
        ui_set_text(s_dc_power.l3, low ? "LOW - connect power" : "");
        ui_set_text_color(s_dc_power.l3, UI_COLOR_ALERT);
    }

    /* ---- Clock / RTC ---- */
    pcf8563_time_t t;
    if (bsp_rtc_get_time(&t) == ESP_OK) {
        ui_set_text(s_dc_clock.l1, LV_SYMBOL_OK " RTC running");
        ui_set_text_color(s_dc_clock.l1, UI_COLOR_OK);
        snprintf(buf, sizeof(buf), "%02u/%02u/20%02u", (unsigned)(t.day % 100), (unsigned)(t.month % 100), (unsigned)(t.year % 100));
        ui_set_text(s_dc_clock.l2, buf);
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(t.hour % 100), (unsigned)(t.minute % 100), (unsigned)(t.second % 100));
        ui_set_text(s_dc_clock.l3, buf);
    } else {
        ui_set_text(s_dc_clock.l1, LV_SYMBOL_CLOSE " RTC not responding");
        ui_set_text_color(s_dc_clock.l1, UI_COLOR_ALERT);
        ui_set_text(s_dc_clock.l2, "Times not recorded");
        ui_set_text(s_dc_clock.l3, "");
    }

    /* ---- Network ---- */
    bool wifi = wifi_manager_is_connected();
    ui_set_text(s_dc_net.l1, wifi ? LV_SYMBOL_WIFI " Wi-Fi connected" : LV_SYMBOL_WIFI " No Wi-Fi link");
    ui_set_text_color(s_dc_net.l1, wifi ? UI_COLOR_OK : UI_COLOR_MUTED);
    ui_set_text(s_dc_net.l2, "Optional for BLED");
    ui_set_text(s_dc_net.l3, "Unit link: wired UART");

    /* ---- Banner: the worst thing wins ---- */
    int level = 0;
    char issues[120];
    issues[0] = '\0';
    if (sys->unit_error != 0 || sys->unit_state == BLED_UNIT_ERROR) {
        level = 2;
        snprintf(buf, sizeof(buf), "Unit: %s", bled_error_text(sys->unit_error != 0 ? sys->unit_error : 0));
        issue_add(issues, sizeof(issues), sys->unit_error != 0 ? buf : "Unit reports an error");
    }
    if (cs->active && bled_ebl_level(cs->total_ebl_ml) == 2) {
        level = 2;
        issue_add(issues, sizeof(issues), "Blood loss above ALERT limit");
    }
    if (!sys->unit_online) {
        level = level < 1 ? 1 : level;
        issue_add(issues, sizeof(issues), "Measurement unit offline");
    }
    if (sd.state != BLED_SD_READY) {
        level = level < 1 ? 1 : level;
        issue_add(issues, sizeof(issues), "SD card not available");
    }
    if (cs->active && cs->sd_warning) {
        level = level < 1 ? 1 : level;
        issue_add(issues, sizeof(issues), "Case data NOT saved to SD");
    }
    if (cs->active && bled_ebl_level(cs->total_ebl_ml) == 1) {
        level = level < 1 ? 1 : level;
        issue_add(issues, sizeof(issues), "Blood loss above warning limit");
    }
    if (!sys->rtc_ok) {
        level = level < 1 ? 1 : level;
        issue_add(issues, sizeof(issues), "RTC not responding");
    }

    if (level == 0) {
        ui_set_bg_color(s_banner, UI_COLOR_OK_BG);
        ui_set_text(s_banner_icon, LV_SYMBOL_OK);
        ui_set_text_color(s_banner_icon, UI_COLOR_OK);
        ui_set_text(s_banner_title, "All systems ready");
        ui_set_text(s_banner_text, sys->unit_simulated ? "Measurement unit (simulated), SD card and clock OK"
                                                       : "Measurement unit, SD card and clock OK");
    } else {
        ui_set_bg_color(s_banner, level == 2 ? UI_COLOR_ALERT_BG : UI_COLOR_WARN_BG);
        ui_set_text(s_banner_icon, LV_SYMBOL_WARNING);
        ui_set_text_color(s_banner_icon, level == 2 ? UI_COLOR_ALERT : UI_COLOR_WARN);
        ui_set_text(s_banner_title, level == 2 ? "ALERT" : "Attention required");
        ui_set_text(s_banner_text, issues);
    }
}

static void build_dashboard_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Dashboard");
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, "BLED system status and quick controls");
    lv_obj_set_style_text_opa(subtitle, LV_OPA_60, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 26);

    /* status banner: green = ready, amber = attention, red = alert */
    s_banner = create_card(page, lv_pct(100), 66);
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, 52);

    s_banner_icon = lv_label_create(s_banner);
    lv_label_set_text(s_banner_icon, LV_SYMBOL_OK);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(s_banner_icon, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(s_banner_icon, LV_ALIGN_LEFT_MID, 4, 0);

    s_banner_title = ui_label(s_banner, "All systems ready", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_align(s_banner_title, LV_ALIGN_TOP_LEFT, 52, 3);

    s_banner_text = ui_label(s_banner, "", UI_FONT_SM, UI_COLOR_MUTED);
    ui_label_oneline(s_banner_text, 520);
    lv_obj_align(s_banner_text, LV_ALIGN_TOP_LEFT, 52, 28);

    /* two rows of three status cards */
    s_dc_unit  = dash_make_card(page, LV_SYMBOL_USB "  Unit link",      LV_ALIGN_TOP_LEFT,  128);
    s_dc_case  = dash_make_card(page, LV_SYMBOL_EDIT "  Active case",   LV_ALIGN_TOP_MID,   128);
    s_dc_sd    = dash_make_card(page, LV_SYMBOL_SD_CARD "  SD card",    LV_ALIGN_TOP_RIGHT, 128);
    s_dc_power = dash_make_card(page, LV_SYMBOL_BATTERY_FULL "  Power", LV_ALIGN_TOP_LEFT,  240);
    s_dc_clock = dash_make_card(page, LV_SYMBOL_REFRESH "  Clock",      LV_ALIGN_TOP_MID,   240);
    s_dc_net   = dash_make_card(page, LV_SYMBOL_WIFI "  Network",       LV_ALIGN_TOP_RIGHT, 240);

    lv_obj_t *quick = create_card(page, lv_pct(100), 108);
    lv_obj_align(quick, LV_ALIGN_TOP_MID, 0, 358);

    lv_obj_t *quick_title = lv_label_create(quick);
    lv_label_set_text(quick_title, "Quick controls");
#if LV_FONT_MONTSERRAT_18
    lv_obj_set_style_text_font(quick_title, &lv_font_montserrat_18, 0);
#endif
    lv_obj_align(quick_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *screen_btn = lv_button_create(quick);
    lv_obj_set_size(screen_btn, 155, 44);
    lv_obj_align(screen_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *screen_lbl = lv_label_create(screen_btn);
    lv_label_set_text(screen_lbl, LV_SYMBOL_POWER "  Screen off");
    lv_obj_center(screen_lbl);
    lv_obj_add_event_cb(screen_btn, screen_off_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *buzz_btn = lv_button_create(quick);
    lv_obj_set_size(buzz_btn, 155, 44);
    lv_obj_align(buzz_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *buzz_lbl = lv_label_create(buzz_btn);
    lv_label_set_text(buzz_lbl, LV_SYMBOL_BELL "  Test buzzer");
    lv_obj_center(buzz_lbl);
    lv_obj_add_event_cb(buzz_btn, buzzer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);

    lv_obj_t *speaker_btn = lv_button_create(quick);
    lv_obj_set_size(speaker_btn, 155, 44);
    lv_obj_align(speaker_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *speaker_lbl = lv_label_create(speaker_btn);
    lv_label_set_text(speaker_lbl, LV_SYMBOL_AUDIO "  Speaker on");
    lv_obj_center(speaker_lbl);
    lv_obj_add_event_cb(speaker_btn, speaker_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);

    dashboard_refresh(NULL);
    lv_timer_create(dashboard_refresh, 500, NULL);
}

/* -------------------------------------------------------------------------- */
/* Existing pages                                                             */
/* -------------------------------------------------------------------------- */

static void build_brightness_page(lv_obj_t *page)
{
    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, "Backlight (via STC8H1K28)");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *value_label = lv_label_create(page);
    lv_label_set_text(value_label, "10 / 10");
    lv_obj_align(value_label, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *slider = lv_slider_create(page);
    lv_obj_set_width(slider, 400);
    lv_slider_set_range(slider, 0, 10);
    lv_slider_set_value(slider, 10, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, value_label);

    lv_obj_t *off_btn = lv_button_create(page);
    lv_obj_align(off_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *off_label = lv_label_create(off_btn);
    lv_label_set_text(off_label, LV_SYMBOL_POWER "  Screen Off");
    lv_obj_center(off_label);
    lv_obj_add_event_cb(off_btn, screen_off_btn_cb, LV_EVENT_CLICKED, NULL);
}

static void build_sound_page(lv_obj_t *page)
{
    lv_obj_t *buzzer_label = lv_label_create(page);
    lv_label_set_text(buzzer_label, "Buzzer");
    lv_obj_align(buzzer_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *buzzer_row = lv_obj_create(page);
    lv_obj_set_size(buzzer_row, lv_pct(100), 60);
    lv_obj_align(buzzer_row, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_border_width(buzzer_row, 0, 0);
    lv_obj_set_style_pad_all(buzzer_row, 0, 0);
    lv_obj_set_flex_flow(buzzer_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buzzer_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buzzer_row, 20, 0);

    lv_obj_t *buzzer_on = lv_button_create(buzzer_row);
    lv_obj_t *l1 = lv_label_create(buzzer_on);
    lv_label_set_text(l1, LV_SYMBOL_BELL "  Buzzer ON");
    lv_obj_center(l1);
    lv_obj_add_event_cb(buzzer_on, buzzer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);

    lv_obj_t *buzzer_off = lv_button_create(buzzer_row);
    lv_obj_t *l2 = lv_label_create(buzzer_off);
    lv_label_set_text(l2, LV_SYMBOL_MUTE "  Buzzer OFF");
    lv_obj_center(l2);
    lv_obj_add_event_cb(buzzer_off, buzzer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)false);

    lv_obj_t *speaker_label = lv_label_create(page);
    lv_label_set_text(speaker_label, "Speaker (NS4168 amp)");
    lv_obj_align(speaker_label, LV_ALIGN_TOP_LEFT, 0, 110);

    lv_obj_t *speaker_row = lv_obj_create(page);
    lv_obj_set_size(speaker_row, lv_pct(100), 60);
    lv_obj_align(speaker_row, LV_ALIGN_TOP_LEFT, 0, 140);
    lv_obj_set_style_border_width(speaker_row, 0, 0);
    lv_obj_set_style_pad_all(speaker_row, 0, 0);
    lv_obj_set_flex_flow(speaker_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speaker_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(speaker_row, 20, 0);

    lv_obj_t *speaker_on = lv_button_create(speaker_row);
    lv_obj_t *l3 = lv_label_create(speaker_on);
    lv_label_set_text(l3, LV_SYMBOL_AUDIO "  Speaker ON");
    lv_obj_center(l3);
    lv_obj_add_event_cb(speaker_on, speaker_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);

    lv_obj_t *speaker_off = lv_button_create(speaker_row);
    lv_obj_t *l4 = lv_label_create(speaker_off);
    lv_label_set_text(l4, LV_SYMBOL_MUTE "  Speaker OFF");
    lv_obj_center(l4);
    lv_obj_add_event_cb(speaker_off, speaker_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)false);
}

static void build_about_page(lv_obj_t *page)
{
    static const char *info =
        "Device:   BLED display unit - Blood Loss Estimation Device\n"
        "Firmware: " BLED_FW_VERSION "\n"
        "Display:  800 x 480 RGB565, parallel (esp_lcd_rgb_panel)\n"
        "Touch:    GT911, I2C addr 0x5D\n"
        "IO ext:   STC8H1K28, I2C addr 0x30 (hw v1.4 / fw v1.3 protocol)\n"
        "  0-244 brightness (0=max)  245 screen off\n"
        "  246/247 buzzer on/off     248/249 speaker on/off\n"
        "RTC:      PCF8563, I2C addr 0x51 (shared I2C bus)\n"
        "I2C bus:  SDA=IO15  SCL=IO16\n"
        "SD card:  TF slot, SPI mode: MOSI IO6, MISO IO4, SCK IO5\n"
        "Unit link: UART0-OUT  TX IO43, RX IO44, 115200 baud\n"
        "DIP switches S1/S0 = 1/1 (MIC + TF card) for SD";

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, LV_SYMBOL_FILE "  About this device");
#if LV_FONT_MONTSERRAT_20
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, info);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 42);
}

/* -------------------------------------------------------------------------- */
/* Public entry point                                                         */
/* -------------------------------------------------------------------------- */

void ui_home_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Sidebar: keep the dimensions and theme background from the original. */
    lv_obj_t *sidebar = lv_obj_create(parent);
    lv_obj_set_size(sidebar, 160, lv_pct(100));
    lv_obj_align(sidebar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(sidebar, 8, 0);
    lv_obj_set_style_pad_row(sidebar, 8, 0);

    /* Content area: original 16-pixel gap after the 160-pixel sidebar. */
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, BSP_LCD_H_RES - 176, lv_pct(100));
    lv_obj_align(content, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_page_profile = make_page(content);
    s_page_dashboard = make_page(content);
    s_page_brightness = make_page(content);
    s_page_sound = make_page(content);
    s_page_about = make_page(content);

    build_profile_page(s_page_profile);
    build_dashboard_page(s_page_dashboard);
    build_brightness_page(s_page_brightness);
    build_sound_page(s_page_sound);
    build_about_page(s_page_about);

    /* Sidebar buttons. No marquee or other animation is used. */
    create_menu_button(sidebar, LV_SYMBOL_HOME,     "Profile",    s_page_profile);
    create_menu_button(sidebar, LV_SYMBOL_LIST,     "Dashboard",  s_page_dashboard);
    create_menu_button(sidebar, LV_SYMBOL_SETTINGS, "Brightness", s_page_brightness);
    create_menu_button(sidebar, LV_SYMBOL_AUDIO,    "Sound",      s_page_sound);
    create_menu_button(sidebar, LV_SYMBOL_FILE,     "About",      s_page_about);

    /* Highlight the sidebar button matching the default view. */
    if (s_menu_button_count > 0) {
        lv_obj_add_state(s_menu_buttons[0], LV_STATE_CHECKED);
    }

    /* Default view */
    show_page(s_page_profile);
}


