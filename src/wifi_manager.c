/**
 * @file wifi_manager.c
 * @brief See wifi_manager.h.
 */
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_manager";

#define NVS_NAMESPACE "wifi_cfg"

/* Bounded string copy that GCC's -Wstringop-truncation / -Wformat-truncation
 * never flag, unlike strncpy()/snprintf() - both of those warnings target
 * that specific family of "checked" string functions, not manual
 * memcpy+explicit-null copies. Our max-length SSID/password strings are
 * *intentionally* truncated at dest_size-1 if they're exactly that long -
 * not a bug, just the field's max length - so this sidesteps two different
 * compiler warnings for the exact same non-issue instead of chasing them
 * one at a time. */
static void bounded_strcpy(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

typedef enum {
    WIFI_CMD_SCAN,
    WIFI_CMD_CONNECT,
    WIFI_CMD_DISCONNECT,
} wifi_cmd_type_t;

typedef struct {
    wifi_cmd_type_t type;
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];
    char password[WIFI_MANAGER_PASS_MAX_LEN];
} wifi_cmd_t;

static QueueHandle_t s_cmd_queue;
static QueueHandle_t s_event_queue;
static volatile bool s_connected = false;

/* Remembered across the async connect so the got-IP event handler knows
 * what to persist to NVS on success (the event itself doesn't carry the
 * password back to us). */
static char s_pending_ssid[WIFI_MANAGER_SSID_MAX_LEN];
static char s_pending_pass[WIFI_MANAGER_PASS_MAX_LEN];

/* ---------------------------------------------------------------------- */
/* NVS credential storage                                                 */
/* ---------------------------------------------------------------------- */

static void save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open (write) failed");
        return;
    }
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", password);
    nvs_commit(h);
    nvs_close(h);
}

static void clear_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
}

static bool load_credentials(char *ssid_out, size_t ssid_size, char *pass_out, size_t pass_size)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t ssid_len = ssid_size;
    size_t pass_len = pass_size;
    esp_err_t e1 = nvs_get_str(h, "ssid", ssid_out, &ssid_len);
    esp_err_t e2 = nvs_get_str(h, "pass", pass_out, &pass_len);
    nvs_close(h);
    return (e1 == ESP_OK && e2 == ESP_OK);
}

/* ---------------------------------------------------------------------- */
/* WiFi/IP event handler                                                  */
/* ---------------------------------------------------------------------- */

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        wifi_mgr_event_t evt = { .type = WIFI_MGR_EVT_DISCONNECTED };
        xQueueSend(s_event_queue, &evt, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip_event = (ip_event_got_ip_t *)event_data;

        s_connected = true;
        if (s_pending_ssid[0] != '\0') {
            save_credentials(s_pending_ssid, s_pending_pass);
        }

        wifi_mgr_event_t evt = { .type = WIFI_MGR_EVT_CONNECTED };
        bounded_strcpy(evt.ssid, sizeof(evt.ssid), s_pending_ssid);
        snprintf(evt.ip_addr, sizeof(evt.ip_addr), IPSTR, IP2STR(&ip_event->ip_info.ip));
        xQueueSend(s_event_queue, &evt, 0);
    }
}

/* ---------------------------------------------------------------------- */
/* Command implementations (run on the WiFi manager task)                 */
/* ---------------------------------------------------------------------- */

static void do_scan(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true); /* blocking - fine, we're not on the LVGL task */

    wifi_mgr_event_t evt = { .type = WIFI_MGR_EVT_SCAN_DONE, .result_count = 0 };

    if (err == ESP_OK) {
        uint16_t num = WIFI_MANAGER_MAX_SCAN_RESULTS;
        wifi_ap_record_t ap_records[WIFI_MANAGER_MAX_SCAN_RESULTS];
        esp_wifi_scan_get_ap_records(&num, ap_records);

        for (int i = 0; i < num && i < WIFI_MANAGER_MAX_SCAN_RESULTS; i++) {
            bounded_strcpy(evt.results[i].ssid, sizeof(evt.results[i].ssid), (const char *)ap_records[i].ssid);
            evt.results[i].rssi = ap_records[i].rssi;
            evt.results[i].secured = (ap_records[i].authmode != WIFI_AUTH_OPEN);
            evt.result_count++;
        }
    } else {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
    }

    xQueueSend(s_event_queue, &evt, 0);
}

static void do_connect(const char *ssid, const char *password)
{
    bounded_strcpy(s_pending_ssid, sizeof(s_pending_ssid), ssid);
    bounded_strcpy(s_pending_pass, sizeof(s_pending_pass), password);

    wifi_config_t wifi_cfg = { 0 };
    bounded_strcpy((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), ssid);
    bounded_strcpy((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), password);

    esp_wifi_disconnect(); /* in case a previous connection/attempt is active */
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        /* WIFI_EVENT_STA_DISCONNECTED will also fire shortly after a failed
         * attempt, and the UI will see that via the event queue. */
    }
}

static void do_disconnect(void)
{
    esp_wifi_disconnect();
    clear_credentials();
    s_pending_ssid[0] = '\0';
    s_pending_pass[0] = '\0';
}

/* ---------------------------------------------------------------------- */
/* Manager task                                                           */
/* ---------------------------------------------------------------------- */

static void wifi_task(void *arg)
{
    (void)arg;
    wifi_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case WIFI_CMD_SCAN:
                    do_scan();
                    break;
                case WIFI_CMD_CONNECT:
                    do_connect(cmd.ssid, cmd.password);
                    break;
                case WIFI_CMD_DISCONNECT:
                    do_disconnect();
                    break;
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Public entry points                                                    */
/* ---------------------------------------------------------------------- */

QueueHandle_t wifi_manager_get_event_queue(void)
{
    return s_event_queue;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

void wifi_manager_scan(void)
{
    wifi_cmd_t cmd = { .type = WIFI_CMD_SCAN };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

void wifi_manager_connect(const char *ssid, const char *password)
{
    wifi_cmd_t cmd = { .type = WIFI_CMD_CONNECT };
    bounded_strcpy(cmd.ssid, sizeof(cmd.ssid), ssid);
    bounded_strcpy(cmd.password, sizeof(cmd.password), password);
    xQueueSend(s_cmd_queue, &cmd, 0);
}

void wifi_manager_disconnect(void)
{
    wifi_cmd_t cmd = { .type = WIFI_CMD_DISCONNECT };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

esp_err_t wifi_manager_init(void)
{
    s_cmd_queue = xQueueCreate(4, sizeof(wifi_cmd_t));
    s_event_queue = xQueueCreate(4, sizeof(wifi_mgr_event_t));
    if (!s_cmd_queue || !s_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* Internal RAM is tight on this board (the display, LVGL and the UART link already use most of it).
     * The defaults ask for 16 static RX buffers (16 x 1600 bytes of internal DMA RAM); 8 are plenty for a
     * status/connection tab and give back about 12 KB.  rx_ba_win must stay <= 2 x static_rx_buf_num. */
    cfg.static_rx_buf_num = 8;
    cfg.dynamic_rx_buf_num = 16;
    cfg.rx_ba_win = 8;
    esp_err_t werr = esp_wifi_init(&cfg);
    if (werr != ESP_OK) {
        /* Do not crash the whole display (reboot loop, no backlight) just because WiFi could not start. */
        ESP_LOGE(TAG, "esp_wifi_init failed: %s - WiFi disabled", esp_err_to_name(werr));
        return werr;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(wifi_task, "wifi_mgr", 4096, NULL, 5, NULL);

    /* Auto-connect using saved credentials, if any. */
    char ssid[WIFI_MANAGER_SSID_MAX_LEN] = { 0 };
    char pass[WIFI_MANAGER_PASS_MAX_LEN] = { 0 };
    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "found saved credentials for \"%s\", auto-connecting", ssid);
        wifi_manager_connect(ssid, pass);
    }

    ESP_LOGI(TAG, "WiFi manager started");
    return ESP_OK;
}

