/**
 * @file wifi_manager.h
 * @brief STA-mode WiFi manager: scan, connect, auto-reconnect using
 *        credentials saved in NVS. All actual WiFi operations run on a
 *        dedicated task (commands come in via a queue) - results/status
 *        changes come back out via a second queue.
 *
 * Same thread-safety rule as the Wiegand/RTC drivers: never call LVGL
 * functions from the WiFi task or its event handler directly. Drain
 * wifi_manager_get_event_queue() from the task that owns
 * lv_timer_handler() instead.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_SCAN_RESULTS 20
#define WIFI_MANAGER_SSID_MAX_LEN     33
#define WIFI_MANAGER_PASS_MAX_LEN     65

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];
    int8_t rssi;
    bool secured;
} wifi_ap_info_t;

typedef enum {
    WIFI_MGR_EVT_SCAN_DONE,
    WIFI_MGR_EVT_CONNECTED,
    WIFI_MGR_EVT_DISCONNECTED,
} wifi_mgr_event_type_t;

typedef struct {
    wifi_mgr_event_type_t type;

    /* valid when type == WIFI_MGR_EVT_SCAN_DONE */
    wifi_ap_info_t results[WIFI_MANAGER_MAX_SCAN_RESULTS];
    int result_count;

    /* valid when type == WIFI_MGR_EVT_CONNECTED */
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];
    char ip_addr[16];
} wifi_mgr_event_t;

/** Bring up WiFi in STA mode and start the manager task. Also attempts to
 *  auto-connect using credentials saved in NVS from a previous session,
 *  if any. */
esp_err_t wifi_manager_init(void);

/** Queue of scan results / connection status changes - drain from the
 *  LVGL task only, see the thread-safety note above. */
QueueHandle_t wifi_manager_get_event_queue(void);

/** Request an async scan; result arrives via the event queue. */
void wifi_manager_scan(void);

/** Request an async connect attempt; saves credentials to NVS on success. */
void wifi_manager_connect(const char *ssid, const char *password);

/** Disconnect and forget the saved credentials. */
void wifi_manager_disconnect(void);

bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
