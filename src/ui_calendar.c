/**
 * @file ui_calendar.c
 * @brief Calendar tab: lv_calendar widget, tap a date to add an event
 *        title. Dates with an event are highlighted. Events are kept in
 *        RAM only for now (not persisted across reboot).
 *
 *        The "add event" flow is a plain in-tab page with a textarea +
 *        on-screen keyboard (not lv_msgbox - that widget crashed the
 *        board on this build). Same page-swap pattern as the Home and
 *        Clock & Weather tabs.
 */
#include "ui_calendar.h"
#include "ui_theme.h"
#include "crowpanel_bsp.h"
#include "pcf8563.h"
#include <string.h>
#include <stdio.h>

#define MAX_EVENTS 40
#define EVENT_TITLE_LEN 40

typedef struct {
    lv_calendar_date_t date;
    char title[EVENT_TITLE_LEN];
} calendar_event_t;

static calendar_event_t s_events[MAX_EVENTS];
static int s_event_count = 0;
static lv_calendar_date_t s_highlighted[MAX_EVENTS];

static lv_obj_t *s_page_calendar;
static lv_obj_t *s_page_add_event;

static lv_obj_t *s_calendar;
static lv_obj_t *s_event_title_label;
static lv_obj_t *s_event_textarea;
static lv_obj_t *s_keyboard;

static lv_calendar_date_t s_pending_date;

/* ---------------------------------------------------------------------- */
/* Page navigation                                                        */
/* ---------------------------------------------------------------------- */

static void show_page(lv_obj_t *page)
{
    lv_obj_add_flag(s_page_calendar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_add_event, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(page, 12, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);
    return page;
}

/* ---------------------------------------------------------------------- */
/* Event storage                                                          */
/* ---------------------------------------------------------------------- */

static void refresh_highlights(void)
{
    int n = 0;
    for (int i = 0; i < s_event_count && n < MAX_EVENTS; i++) {
        s_highlighted[n++] = s_events[i].date;
    }
    lv_calendar_set_highlighted_dates(s_calendar, s_highlighted, n);
}

static void add_event(lv_calendar_date_t date, const char *title)
{
    if (s_event_count >= MAX_EVENTS || title == NULL || title[0] == '\0') {
        return;
    }
    s_events[s_event_count].date = date;
    strncpy(s_events[s_event_count].title, title, EVENT_TITLE_LEN - 1);
    s_events[s_event_count].title[EVENT_TITLE_LEN - 1] = '\0';
    s_event_count++;
    refresh_highlights();
}

/* ---------------------------------------------------------------------- */
/* "Add event" page                                                       */
/* ---------------------------------------------------------------------- */

static void event_save_cb(lv_event_t *e)
{
    (void)e;
    const char *title = lv_textarea_get_text(s_event_textarea);
    add_event(s_pending_date, title);
    show_page(s_page_calendar);
}

static void event_cancel_cb(lv_event_t *e)
{
    (void)e;
    show_page(s_page_calendar);
}

static void open_add_event_page(lv_calendar_date_t date)
{
    s_pending_date = date;

    char buf[48];
    snprintf(buf, sizeof(buf), "Add event - %02d/%02d/%d", date.day, date.month, (int)date.year);
    lv_label_set_text(s_event_title_label, buf);

    lv_textarea_set_text(s_event_textarea, "");
    lv_keyboard_set_textarea(s_keyboard, s_event_textarea);

    show_page(s_page_add_event);
}

/* ---------------------------------------------------------------------- */
/* Calendar event                                                         */
/* ---------------------------------------------------------------------- */

static void calendar_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *cal = lv_event_get_target_obj(e);
    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(cal, &date)) {
        open_add_event_page(date);
    }
}

/* ---------------------------------------------------------------------- */
/* Page builders                                                          */
/* ---------------------------------------------------------------------- */

static void build_calendar_page(lv_obj_t *page)
{
    s_calendar = lv_calendar_create(page);
    lv_obj_set_size(s_calendar, 460, 346);
    lv_obj_align(s_calendar, LV_ALIGN_CENTER, 0, -6);

    /* Header with prev/next month arrows. */
    lv_calendar_header_arrow_create(s_calendar);

    /* Default to today's date from the RTC; fall back to a fixed date if
     * the RTC isn't set yet (fresh/dead-battery chip reads all zero). */
    pcf8563_time_t t;
    uint16_t year = 2026;
    uint8_t month = 7, day = 23;
    if (bsp_rtc_get_time(&t) == ESP_OK && t.year > 0) {
        year = 2000 + t.year;
        month = t.month;
        day = t.day;
    }
    lv_calendar_set_today_date(s_calendar, year, month, day);
    lv_calendar_set_showed_date(s_calendar, year, month);

    lv_obj_add_event_cb(s_calendar, calendar_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *hint = lv_label_create(page);
    lv_label_set_text(hint, "Tap a date to add an event");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void build_add_event_page(lv_obj_t *page)
{
    s_event_title_label = lv_label_create(page);
    lv_label_set_text(s_event_title_label, "Add event");
    lv_obj_align(s_event_title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_event_textarea = lv_textarea_create(page);
    lv_textarea_set_one_line(s_event_textarea, true);
    lv_textarea_set_placeholder_text(s_event_textarea, "Event title");
    lv_obj_set_width(s_event_textarea, lv_pct(100));
    lv_obj_align_to(s_event_textarea, s_event_title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    lv_obj_t *btn_row = lv_obj_create(page);
    lv_obj_set_size(btn_row, lv_pct(100), 50);
    lv_obj_align_to(btn_row, s_event_textarea, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(btn_row, 0, 0);

    lv_obj_t *save_btn = lv_button_create(btn_row);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_add_event_cb(save_btn, event_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_btn = lv_button_create(btn_row);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_add_event_cb(cancel_btn, event_cancel_cb, LV_EVENT_CLICKED, NULL);

    s_keyboard = lv_keyboard_create(page);
    lv_obj_set_size(s_keyboard, lv_pct(100), 200);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
}

/* ---------------------------------------------------------------------- */
/* Public entry point                                                     */
/* ---------------------------------------------------------------------- */

void ui_calendar_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);

    s_page_calendar = make_page(parent);
    s_page_add_event = make_page(parent);

    build_calendar_page(s_page_calendar);
    build_add_event_page(s_page_add_event);

    show_page(s_page_calendar);
}