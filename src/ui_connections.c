/**
 * @file ui_connections.c
 * @brief Connections tab: Wi-Fi (scan/connect/status, backed by
 *        wifi_manager) and a Bluetooth placeholder (not used by BLED: the
 *        measurement unit is connected by a wired UART link).
 *
 *        Navigation is the same in-tab page-swap pattern used everywhere
 *        else in this project (not lv_msgbox). The password entry page
 *        reuses the textarea + on-screen keyboard technique from the
 *        Calendar tab's "add event" page.
 */
#include "ui_connections.h"
#include "ui_theme.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static lv_obj_t *s_page_menu;
static lv_obj_t *s_page_wifi;
static lv_obj_t *s_page_wifi_password;
static lv_obj_t *s_page_bluetooth;

static lv_obj_t *s_wifi_status_label;
static lv_obj_t *s_wifi_list;
static lv_obj_t *s_wifi_pw_title;
static lv_obj_t *s_wifi_pw_textarea;
static lv_obj_t *s_wifi_pw_keyboard;

static char s_selected_ssid[WIFI_MANAGER_SSID_MAX_LEN];
static char s_scan_ssids[WIFI_MANAGER_MAX_SCAN_RESULTS][WIFI_MANAGER_SSID_MAX_LEN];

/* Same rationale as wifi_manager.c's copy of this helper: avoids both
 * -Wstringop-truncation and -Wformat-truncation, which strncpy()/snprintf()
 * can trip for intentional max-length bounded copies like these. */
static void bounded_strcpy(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

/* ---------------------------------------------------------------------- */
/* Page navigation                                                        */
/* ---------------------------------------------------------------------- */

static void show_page(lv_obj_t *page)
{
    lv_obj_add_flag(s_page_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_wifi_password, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_bluetooth, LV_OBJ_FLAG_HIDDEN);
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

static void back_to_menu_cb(lv_event_t *e)
{
    (void)e;
    show_page(s_page_menu);
}

static void open_bluetooth_page(lv_event_t *e)
{
    (void)e;
    show_page(s_page_bluetooth);
}

/* ---------------------------------------------------------------------- */
/* Wi-Fi: scan results list                                               */
/* ---------------------------------------------------------------------- */

static void open_wifi_password_page(void);

static void wifi_list_item_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    bounded_strcpy(s_selected_ssid, sizeof(s_selected_ssid), s_scan_ssids[idx]);
    open_wifi_password_page();
}

static void populate_wifi_list(const wifi_mgr_event_t *evt)
{
    lv_obj_clean(s_wifi_list);

    if (evt->result_count == 0) {
        lv_list_add_text(s_wifi_list, "No networks found");
        return;
    }

    int count = evt->result_count;
    if (count > WIFI_MANAGER_MAX_SCAN_RESULTS) {
        count = WIFI_MANAGER_MAX_SCAN_RESULTS;
    }

    for (int i = 0; i < count; i++) {
        bounded_strcpy(s_scan_ssids[i], sizeof(s_scan_ssids[i]), evt->results[i].ssid);

        char label[64];
        snprintf(label, sizeof(label), "%s  (%d dBm, %s)",
                 evt->results[i].ssid, evt->results[i].rssi,
                 evt->results[i].secured ? "Secured" : "Open");

        lv_obj_t *btn = lv_list_add_btn(s_wifi_list, LV_SYMBOL_WIFI, label);
        lv_obj_add_event_cb(btn, wifi_list_item_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void wifi_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_clean(s_wifi_list);
    lv_list_add_text(s_wifi_list, "Scanning...");
    wifi_manager_scan();
}

static void wifi_disconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    wifi_manager_disconnect();
    lv_label_set_text(s_wifi_status_label, "Not connected");
}

static void open_wifi_page(lv_event_t *e)
{
    (void)e;
    lv_obj_clean(s_wifi_list);
    lv_list_add_text(s_wifi_list, "Scanning...");
    wifi_manager_scan();
    show_page(s_page_wifi);
}

/* ---------------------------------------------------------------------- */
/* Wi-Fi: password entry                                                  */
/* ---------------------------------------------------------------------- */

static void open_wifi_password_page(void)
{
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "Connect to \"%s\"", s_selected_ssid);
    lv_label_set_text(s_wifi_pw_title, title_buf);
    lv_textarea_set_text(s_wifi_pw_textarea, "");
    lv_keyboard_set_textarea(s_wifi_pw_keyboard, s_wifi_pw_textarea);
    show_page(s_page_wifi_password);
}

static void wifi_pw_connect_cb(lv_event_t *e)
{
    (void)e;
    const char *pw = lv_textarea_get_text(s_wifi_pw_textarea);
    wifi_manager_connect(s_selected_ssid, pw);
    lv_label_set_text_fmt(s_wifi_status_label, "Connecting to %s...", s_selected_ssid);
    show_page(s_page_wifi);
}

static void wifi_pw_cancel_cb(lv_event_t *e)
{
    (void)e;
    show_page(s_page_wifi);
}

/* ---------------------------------------------------------------------- */
/* Live status: drain the WiFi manager's event queue                      */
/* ---------------------------------------------------------------------- */

static void wifi_event_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    QueueHandle_t q = wifi_manager_get_event_queue();
    if (!q) {
        return;
    }

    wifi_mgr_event_t evt;
    while (xQueueReceive(q, &evt, 0) == pdTRUE) {
        switch (evt.type) {
            case WIFI_MGR_EVT_SCAN_DONE:
                populate_wifi_list(&evt);
                break;
            case WIFI_MGR_EVT_CONNECTED:
                lv_label_set_text_fmt(s_wifi_status_label, "Connected to %s (%s)", evt.ssid, evt.ip_addr);
                break;
            case WIFI_MGR_EVT_DISCONNECTED:
                lv_label_set_text(s_wifi_status_label, "Not connected");
                break;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Page builders                                                          */
/* ---------------------------------------------------------------------- */

static void build_menu_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Connections");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, lv_pct(100), 140);
    lv_obj_center(row);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi_btn = lv_button_create(row);
    lv_obj_set_size(wifi_btn, 220, 100);
    lv_obj_t *wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text_fmt(wifi_label, "%s  Wi-Fi", LV_SYMBOL_WIFI);
    lv_obj_center(wifi_label);
    lv_obj_add_event_cb(wifi_btn, open_wifi_page, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bt_btn = lv_button_create(row);
    lv_obj_set_size(bt_btn, 220, 100);
    lv_obj_t *bt_label = lv_label_create(bt_btn);
    lv_label_set_text_fmt(bt_label, "%s  Bluetooth", LV_SYMBOL_BLUETOOTH);
    lv_obj_center(bt_label);
    lv_obj_add_event_cb(bt_btn, open_bluetooth_page, LV_EVENT_CLICKED, NULL);
}

static void build_wifi_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_wifi_status_label = lv_label_create(page);
    lv_label_set_text(s_wifi_status_label, "Not connected");
    lv_obj_align(s_wifi_status_label, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t *btn_row = lv_obj_create(page);
    lv_obj_set_size(btn_row, lv_pct(100), 50);
    lv_obj_align(btn_row, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 12, 0);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scan_btn = lv_button_create(btn_row);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disconnect_btn = lv_button_create(btn_row);
    lv_obj_t *disconnect_label = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_label, "Disconnect / Forget");
    lv_obj_add_event_cb(disconnect_btn, wifi_disconnect_btn_cb, LV_EVENT_CLICKED, NULL);

    s_wifi_list = lv_list_create(page);
    lv_obj_set_size(s_wifi_list, lv_pct(100), 260);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, 116);

    lv_obj_t *back_btn = lv_button_create(page);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_add_event_cb(back_btn, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
}

static void build_wifi_password_page(lv_obj_t *page)
{
    s_wifi_pw_title = lv_label_create(page);
    lv_label_set_text(s_wifi_pw_title, "Connect");
    lv_obj_align(s_wifi_pw_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_wifi_pw_textarea = lv_textarea_create(page);
    lv_textarea_set_one_line(s_wifi_pw_textarea, true);
    lv_textarea_set_password_mode(s_wifi_pw_textarea, true);
    lv_textarea_set_placeholder_text(s_wifi_pw_textarea, "Password");
    lv_obj_set_width(s_wifi_pw_textarea, lv_pct(100));
    lv_obj_align_to(s_wifi_pw_textarea, s_wifi_pw_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);

    lv_obj_t *btn_row = lv_obj_create(page);
    lv_obj_set_size(btn_row, lv_pct(100), 50);
    lv_obj_align_to(btn_row, s_wifi_pw_textarea, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *connect_btn = lv_button_create(btn_row);
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_add_event_cb(connect_btn, wifi_pw_connect_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_btn = lv_button_create(btn_row);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_add_event_cb(cancel_btn, wifi_pw_cancel_cb, LV_EVENT_CLICKED, NULL);

    s_wifi_pw_keyboard = lv_keyboard_create(page);
    lv_obj_set_size(s_wifi_pw_keyboard, lv_pct(100), 200);
    lv_obj_align(s_wifi_pw_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void build_bluetooth_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *info = lv_label_create(page);
    lv_label_set_text(info, "Not used by BLED.\n"
                             "The measurement unit is connected by a wired\n"
                             "UART link (see Home > About).");
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *back_btn = lv_button_create(page);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_add_event_cb(back_btn, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------------------------------------------------------------------- */
/* Public entry point                                                     */
/* ---------------------------------------------------------------------- */

void ui_connections_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);

    s_page_menu = make_page(parent);
    s_page_wifi = make_page(parent);
    s_page_wifi_password = make_page(parent);
    s_page_bluetooth = make_page(parent);

    build_menu_page(s_page_menu);
    build_wifi_page(s_page_wifi);
    build_wifi_password_page(s_page_wifi_password);
    build_bluetooth_page(s_page_bluetooth);

    show_page(s_page_menu);

    lv_timer_create(wifi_event_timer_cb, 200, NULL);
}

