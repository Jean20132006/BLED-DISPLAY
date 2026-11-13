#include "ui_app.h"
#include "ui_home.h"
#include "ui_newcase.h"
#include "ui_records.h"
#include "ui_clock.h"
#include "ui_calendar.h"
#include "ui_connections.h"
#include "ui_statusbar.h"
#include "ui_theme.h"
#include "bled_core.h"
#include "crowpanel_bsp.h"
#include "lvgl.h"

#define TAB_RECORDS 2 /* Home=0, New Case=1, Records=2, Clock=3, Calendar=4, Connections=5 */

static lv_obj_t *s_tabview;

/* Drives the BLED model (link polling, event handling, SD flushing) from the LVGL task, so the UI
 * and the model never run concurrently. */
static void core_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bled_core_tick(lv_tick_get());
}

static void tab_changed_cb(lv_event_t *e)
{
    (void)e;
    ui_records_set_visible(lv_tabview_get_tab_active(s_tabview) == TAB_RECORDS);
}

static lv_obj_t *add_tab(const char *name)
{
    lv_obj_t *tab = lv_tabview_add_tab(s_tabview, name);
    lv_obj_set_style_bg_color(tab, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    return tab;
}

void ui_app_create(void)
{
    ui_theme_init();
    ui_statusbar_create(lv_screen_active());

    s_tabview = lv_tabview_create(lv_screen_active());
    lv_obj_set_pos(s_tabview, 0, UI_STATUSBAR_H);
    lv_obj_set_size(s_tabview, BSP_LCD_H_RES, BSP_LCD_V_RES - UI_STATUSBAR_H);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabview, UI_TABBAR_H);

    /* Tabs change by tapping the tab bar only: a stray horizontal swipe on a table or chart must never
     * switch screens in the middle of a case. */
    lv_obj_remove_flag(lv_tabview_get_content(s_tabview), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_font(bar, UI_FONT_MD, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar, UI_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar, UI_COLOR_PRIMARY, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bar, UI_COLOR_TINT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bar, UI_COLOR_PRIMARY, LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t *tab_home = add_tab("Home");
    lv_obj_t *tab_new = add_tab("New Case");
    lv_obj_t *tab_rec = add_tab("Records");
    lv_obj_t *tab_clock = add_tab("Clock");
    lv_obj_t *tab_cal = add_tab("Calendar");
    lv_obj_t *tab_conn = add_tab("Connections");

    ui_home_create(tab_home);
    ui_newcase_create(tab_new);
    ui_records_create(tab_rec);
    ui_clock_create(tab_clock);
    ui_calendar_create(tab_cal);
    ui_connections_create(tab_conn);

    lv_obj_add_event_cb(s_tabview, tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_timer_create(core_timer_cb, 100, NULL);
}
