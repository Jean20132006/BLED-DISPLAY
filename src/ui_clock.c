/**
 * @file ui_clock.c
 * @brief Clock tab (formerly "Clock & Weather"). Time/date come from the onboard
 *        PCF8563 RTC. The three weather cards (temperature / humidity / air
 *        quality) were removed together with the sensor poller; in their place
 *        the tab now shows the live status of the active BLED case: elapsed
 *        time, pads measured and cumulative blood loss.
 *
 *        Date/time editing is unchanged: a set of plain in-tab pages (not
 *        lv_msgbox - that widget proved unreliable for this: dialog footer
 *        buttons didn't fire click events on this build). Main view -> "Set
 *        Time" opens a small menu -> Set Time / Set Date, each with its own
 *        roller page and working Save/Cancel buttons.
 */
#include "ui_clock.h"
#include "crowpanel_bsp.h"
#include "bled_core.h"
#include "ui_theme.h"
#include "pcf8563.h"
#include <stdio.h>
#include <string.h>

#define SET_TIME_YEAR_MIN 2024
#define SET_TIME_YEAR_MAX 2035

static lv_obj_t *s_page_main;
static lv_obj_t *s_page_menu;
static lv_obj_t *s_page_set_time;
static lv_obj_t *s_page_set_date;

static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
/* case status cards */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *value;
    lv_obj_t *sub;
} info_card_t;
static info_card_t s_card_elapsed, s_card_pads, s_card_ebl;

static lv_obj_t *s_roller_hour;
static lv_obj_t *s_roller_min;
static lv_obj_t *s_roller_sec;
static lv_obj_t *s_roller_year;
static lv_obj_t *s_roller_month;
static lv_obj_t *s_roller_day;

/* Working copy of the RTC time while the user is editing it across the
 * Set Time / Set Date pages (each page only touches its own fields). */
static pcf8563_time_t s_pending;

static const char *const WEEKDAY_NAMES[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

/* ---------------------------------------------------------------------- */
/* Page navigation                                                        */
/* ---------------------------------------------------------------------- */

static void show_page(lv_obj_t *page)
{
    lv_obj_add_flag(s_page_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_set_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_set_date, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(page, 16, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);
    return page;
}

/* ---------------------------------------------------------------------- */
/* Clock refresh (main page)                                              */
/* ---------------------------------------------------------------------- */

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    pcf8563_time_t t;
    if (bsp_rtc_get_time(&t) != ESP_OK) {
        ui_set_text(s_time_label, "--:--:--");
        ui_set_text(s_date_label, "RTC not responding");
        return;
    }

    /* Same texts as before, but a label is only touched when its text changed: the date label used to be
     * rewritten (and redrawn) every second although it only changes at midnight. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.minute, t.second);
    ui_set_text(s_time_label, buf);

    const char *weekday = (t.weekday <= 6) ? WEEKDAY_NAMES[t.weekday] : "?";
    snprintf(buf, sizeof(buf), "%s, %02d/%02d/20%02d", weekday, t.day, t.month, t.year);
    ui_set_text(s_date_label, buf);
}

/* ---------------------------------------------------------------------- */
/* Roller helpers                                                         */
/* ---------------------------------------------------------------------- */

/* Sakamoto's algorithm - returns 0 = Sunday, matching pcf8563_time_t.weekday. */
static int day_of_week(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/* Buffers are static so they stay valid for the program's lifetime,
 * regardless of whether lv_roller_set_options() copies the string or
 * just keeps the pointer. */
static void build_numeric_options(char *buf, size_t buf_size, int start, int end)
{
    buf[0] = '\0';
    for (int i = start; i <= end; i++) {
        char item[8];
        snprintf(item, sizeof(item), "%02d%s", i, (i < end) ? "\n" : "");
        strncat(buf, item, buf_size - strlen(buf) - 1);
    }
}

static void build_year_options(char *buf, size_t buf_size, int start, int end)
{
    buf[0] = '\0';
    for (int y = start; y <= end; y++) {
        char item[8];
        snprintf(item, sizeof(item), "%d%s", y, (y < end) ? "\n" : "");
        strncat(buf, item, buf_size - strlen(buf) - 1);
    }
}

static lv_obj_t *make_roller(lv_obj_t *parent, const char *options, int width)
{
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(roller, width);
    return roller;
}

static void commit_pending_time(void)
{
    if (bsp_rtc_set_time(&s_pending) == ESP_OK) {
        clock_timer_cb(NULL); /* refresh the on-screen time immediately */
    }
}

/* ---------------------------------------------------------------------- */
/* Menu page: Set Time / Set Date / Back                                  */
/* ---------------------------------------------------------------------- */

static void open_menu_page(lv_event_t *e)
{
    (void)e;

    /* Snapshot the current RTC value as our editing baseline. */
    if (bsp_rtc_get_time(&s_pending) != ESP_OK) {
        s_pending = (pcf8563_time_t){
            .year = 26, .month = 7, .day = 23, .weekday = 4, .hour = 12, .minute = 0, .second = 0
        };
    }
    show_page(s_page_menu);
}

static void menu_back_cb(lv_event_t *e)
{
    (void)e;
    show_page(s_page_main);
}

static void open_set_time_page(lv_event_t *e)
{
    (void)e;
    lv_roller_set_selected(s_roller_hour, s_pending.hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_min, s_pending.minute, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_sec, s_pending.second, LV_ANIM_OFF);
    show_page(s_page_set_time);
}

static void open_set_date_page(lv_event_t *e)
{
    (void)e;
    int year = 2000 + s_pending.year;
    if (year < SET_TIME_YEAR_MIN || year > SET_TIME_YEAR_MAX) {
        year = SET_TIME_YEAR_MIN;
    }
    int month = (s_pending.month >= 1 && s_pending.month <= 12) ? s_pending.month : 1;
    int day = (s_pending.day >= 1 && s_pending.day <= 31) ? s_pending.day : 1;

    lv_roller_set_selected(s_roller_year, year - SET_TIME_YEAR_MIN, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_month, month - 1, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_day, day - 1, LV_ANIM_OFF);
    show_page(s_page_set_date);
}

/* ---------------------------------------------------------------------- */
/* Set Time page                                                          */
/* ---------------------------------------------------------------------- */

static void set_time_save_cb(lv_event_t *e)
{
    (void)e;
    s_pending.hour = (uint8_t)lv_roller_get_selected(s_roller_hour);
    s_pending.minute = (uint8_t)lv_roller_get_selected(s_roller_min);
    s_pending.second = (uint8_t)lv_roller_get_selected(s_roller_sec);
    commit_pending_time();
    show_page(s_page_menu);
}

static void set_time_cancel_cb(lv_event_t *e)
{
    (void)e;
    show_page(s_page_menu);
}

/* ---------------------------------------------------------------------- */
/* Set Date page                                                          */
/* ---------------------------------------------------------------------- */

static void set_date_save_cb(lv_event_t *e)
{
    (void)e;
    int year = SET_TIME_YEAR_MIN + (int)lv_roller_get_selected(s_roller_year);
    int month = 1 + (int)lv_roller_get_selected(s_roller_month);
    int day = 1 + (int)lv_roller_get_selected(s_roller_day);

    s_pending.year = (uint8_t)(year - 2000);
    s_pending.month = (uint8_t)month;
    s_pending.day = (uint8_t)day;
    s_pending.weekday = (uint8_t)day_of_week(year, month, day);
    commit_pending_time();
    show_page(s_page_menu);
}

static void set_date_cancel_cb(lv_event_t *e)
{
    (void)e;
    show_page(s_page_menu);
}

/* ---------------------------------------------------------------------- */
/* Case status cards (main page)                                          */
/* ---------------------------------------------------------------------- */

static void make_info_card(lv_obj_t *parent, info_card_t *c, const char *title)
{
    c->card = ui_card(parent, 220, 130);

    lv_obj_t *title_label = ui_label(c->card, title, UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    c->value = ui_label(c->card, "--", UI_FONT_NUM, UI_COLOR_TEXT);
    lv_obj_align(c->value, LV_ALIGN_CENTER, 0, 2);

    c->sub = ui_label(c->card, "", UI_FONT_SM, UI_COLOR_MUTED);
    ui_label_oneline(c->sub, 196);
    lv_obj_set_style_text_align(c->sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(c->sub, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void cards_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const bled_case_t *cs = bled_case();
    char buf[64];
    char a[16], b[16];

    if (!cs->active) {
        ui_set_text(s_card_elapsed.value, "--:--:--");
        ui_set_text(s_card_elapsed.sub, "No active case");
        ui_set_text(s_card_pads.value, "--");
        ui_set_text(s_card_pads.sub, "Start one in the New Case tab");
        ui_set_text(s_card_ebl.value, "--");
        ui_set_text(s_card_ebl.sub, "mL cumulative");
        ui_set_text_color(s_card_ebl.value, UI_COLOR_MUTED);
        ui_set_border(s_card_ebl.card, UI_COLOR_BORDER, 1);
        return;
    }

    bled_format_duration(bled_case_elapsed_s(), a, sizeof(a));
    ui_set_text(s_card_elapsed.value, a);
    snprintf(buf, sizeof(buf), "Case %s", cs->id);
    ui_set_text(s_card_elapsed.sub, buf);

    snprintf(buf, sizeof(buf), "%d", cs->pad_count);
    ui_set_text(s_card_pads.value, buf);
    if (cs->pad_count > 0) {
        ui_format_ml(a, sizeof(a), cs->last_pad_ml);
        bled_stamp_format_time(&cs->last_pad_time, b, sizeof(b));
        snprintf(buf, sizeof(buf), "Last: %s mL at %s", a, b);
    } else {
        snprintf(buf, sizeof(buf), "Waiting for the first pad");
    }
    ui_set_text(s_card_pads.sub, buf);

    int level = bled_ebl_level(cs->total_ebl_ml);
    ui_format_ml(a, sizeof(a), cs->total_ebl_ml);
    ui_set_text(s_card_ebl.value, a);
    ui_set_text_color(s_card_ebl.value, level == 0 ? UI_COLOR_BLOOD : ui_level_color(level));
    ui_set_text(s_card_ebl.sub, level == 2 ? "mL cumulative - ALERT" : (level == 1 ? "mL cumulative - WARNING" : "mL cumulative"));
    ui_set_border(s_card_ebl.card, level == 0 ? UI_COLOR_BORDER : ui_level_color(level), level == 0 ? 1 : 3);
}

/* ---------------------------------------------------------------------- */
/* Page builders                                                          */
/* ---------------------------------------------------------------------- */

static void build_main_page(lv_obj_t *page)
{
    /* Whole page is a centered vertical flex: [clock group] then [cards row],
     * evenly spaced - this is what keeps the time, date and Set Time button
     * vertically aligned as one centered block instead of pinned to the top. */
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *clock_group = lv_obj_create(page);
    lv_obj_set_size(clock_group, lv_pct(100), 160);
    lv_obj_set_style_pad_all(clock_group, 0, 0);
    lv_obj_set_style_border_width(clock_group, 0, 0);
    lv_obj_set_style_bg_opa(clock_group, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(clock_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(clock_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(clock_group, 12, 0);

    s_time_label = lv_label_create(clock_group);
    lv_label_set_text(s_time_label, "--:--:--");
    #if LV_FONT_MONTSERRAT_48
        lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    #endif

    s_date_label = lv_label_create(clock_group);
    #if LV_FONT_MONTSERRAT_20
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_20, 0);
    #endif
    lv_label_set_text(s_date_label, "Reading RTC...");

    lv_obj_t *set_time_btn = lv_button_create(clock_group);
    lv_obj_t *set_time_label = lv_label_create(set_time_btn);
    lv_label_set_text(set_time_label, "Set Time");
    lv_obj_add_event_cb(set_time_btn, open_menu_page, LV_EVENT_CLICKED, NULL);

    /* --- Case status cards row --- */
    lv_obj_t *cards = lv_obj_create(page);
    lv_obj_set_size(cards, lv_pct(100), 160);
    lv_obj_set_style_border_width(cards, 0, 0);
    lv_obj_set_style_bg_opa(cards, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_info_card(cards, &s_card_elapsed, "Case elapsed");
    make_info_card(cards, &s_card_pads, "Pads measured");
    make_info_card(cards, &s_card_ebl, "Blood loss (EBL)");
}

static void build_menu_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Set Date & Time");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *btn_time = lv_button_create(page);
    lv_obj_set_size(btn_time, 240, 70);
    lv_obj_align(btn_time, LV_ALIGN_CENTER, 0, -50);
    lv_obj_t *l1 = lv_label_create(btn_time);
    lv_label_set_text(l1, "Set Time");
    lv_obj_center(l1);
    lv_obj_add_event_cb(btn_time, open_set_time_page, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_date = lv_button_create(page);
    lv_obj_set_size(btn_date, 240, 70);
    lv_obj_align(btn_date, LV_ALIGN_CENTER, 0, 40);
    lv_obj_t *l2 = lv_label_create(btn_date);
    lv_label_set_text(l2, "Set Date");
    lv_obj_center(l2);
    lv_obj_add_event_cb(btn_date, open_set_date_page, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_back = lv_button_create(page);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *l3 = lv_label_create(btn_back);
    lv_label_set_text(l3, "Back");
    lv_obj_add_event_cb(btn_back, menu_back_cb, LV_EVENT_CLICKED, NULL);
}

static void build_set_time_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Set Time (24h)");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *rollers = lv_obj_create(page);
    lv_obj_set_size(rollers, lv_pct(100), 180);
    lv_obj_align(rollers, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_flex_flow(rollers, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rollers, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(rollers, 0, 0);

    static char hour_opts[128], min_opts[256], sec_opts[256];
    build_numeric_options(hour_opts, sizeof(hour_opts), 0, 23);
    build_numeric_options(min_opts, sizeof(min_opts), 0, 59);
    build_numeric_options(sec_opts, sizeof(sec_opts), 0, 59);

    s_roller_hour = make_roller(rollers, hour_opts, 90);
    s_roller_min = make_roller(rollers, min_opts, 90);
    s_roller_sec = make_roller(rollers, sec_opts, 90);

    lv_obj_t *btn_row = lv_obj_create(page);
    lv_obj_set_size(btn_row, lv_pct(100), 70);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(btn_row, 0, 0);

    lv_obj_t *save_btn = lv_button_create(btn_row);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_add_event_cb(save_btn, set_time_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_btn = lv_button_create(btn_row);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_add_event_cb(cancel_btn, set_time_cancel_cb, LV_EVENT_CLICKED, NULL);
}

static void build_set_date_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Set Date");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *rollers = lv_obj_create(page);
    lv_obj_set_size(rollers, lv_pct(100), 180);
    lv_obj_align(rollers, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_flex_flow(rollers, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rollers, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(rollers, 0, 0);

    static char year_opts[128], month_opts[64], day_opts[128];
    build_year_options(year_opts, sizeof(year_opts), SET_TIME_YEAR_MIN, SET_TIME_YEAR_MAX);
    build_numeric_options(month_opts, sizeof(month_opts), 1, 12);
    build_numeric_options(day_opts, sizeof(day_opts), 1, 31);

    s_roller_year = make_roller(rollers, year_opts, 110);
    s_roller_month = make_roller(rollers, month_opts, 90);
    s_roller_day = make_roller(rollers, day_opts, 90);

    lv_obj_t *btn_row = lv_obj_create(page);
    lv_obj_set_size(btn_row, lv_pct(100), 70);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(btn_row, 0, 0);

    lv_obj_t *save_btn = lv_button_create(btn_row);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_add_event_cb(save_btn, set_date_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_btn = lv_button_create(btn_row);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_add_event_cb(cancel_btn, set_date_cancel_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------------------------------------------------------------------- */
/* Public entry point                                                     */
/* ---------------------------------------------------------------------- */

void ui_clock_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);

    s_page_main = make_page(parent);
    s_page_menu = make_page(parent);
    s_page_set_time = make_page(parent);
    s_page_set_date = make_page(parent);

    build_main_page(s_page_main);
    build_menu_page(s_page_menu);
    build_set_time_page(s_page_set_time);
    build_set_date_page(s_page_set_date);

    show_page(s_page_main);

    /* Refresh immediately, then every second. */
    clock_timer_cb(NULL);
    lv_timer_create(clock_timer_cb, 1000, NULL);
    cards_timer_cb(NULL);
    lv_timer_create(cards_timer_cb, 1000, NULL);
}

