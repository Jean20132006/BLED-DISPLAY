/**
 * @file app_main.c
 * @brief Entry point for the CrowPanel ESP32 Advanced 7.0" (DIS02170A v1.4) demo.
 *
 * Bring-up order:
 *   1. NVS
 *   2. Shared I2C bus + STC8H1K28 + PCF8563 RTC
 *   3. RGB LCD panel (two PSRAM frame buffers)
 *   4. LVGL core + display driver (tear-free full-refresh double buffer)
 *   5. GT911 touch + LVGL input driver
 *   6. BLED core (SD storage, measurement-unit link, case model)
 *   7. App UI (status bar + 6-tab view: Home / New Case / Records / Clock /
 *      Calendar / Connections)
 *   8. WiFi manager
 *   9. lv_timer_handler() pump task
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"

#include "crowpanel_bsp.h"
#include "crowpanel_touch.h"
#include "ui_app.h"
#include "bled_core.h"
#include "wifi_manager.h"

/* Set to 1 to bypass LVGL entirely and just paint solid red/green/blue full
 * screen for 2s each on boot. Useful to confirm the RGB panel timings are
 * correct (you should see clean, full-screen, un-shifted colors) before
 * troubleshooting anything in the LVGL layer. Set back to 0 once confirmed. */
#define CROWPANEL_RAW_PANEL_TEST 0

/* Set to 1 to scan the whole I2C bus on boot and log every address that
 * ACKs. Useful to confirm the GT911 (0x5D) and STC8H1K28 (0x30) are both
 * actually present/responding before digging into driver-level issues. */
#define CROWPANEL_I2C_SCAN 0

static const char *TAG = "app_main";

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static lv_display_t *s_disp = NULL;
static lv_indev_t *s_indev = NULL;

/* ---------------------------------------------------------------------- */
/* LVGL <-> RGB panel glue                                                */
/* ---------------------------------------------------------------------- */

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    /* In DIRECT mode, area is whatever actually changed (often a small
     * region, e.g. just the clock text) rather than always the full
     * screen - esp_lcd_panel_draw_bitmap() handles partial-area writes
     * into the target frame buffer fine either way. */
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void lvgl_touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);

    esp_lcd_touch_read_data(tp);

    uint16_t x[1], y[1];
    uint8_t point_num = 0;
    bool pressed = esp_lcd_touch_get_coordinates(tp, x, y, NULL, &point_num, 1);

    if (pressed && point_num > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x[0];
        data->point.y = y[0];
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

/* The LVGL task is created early (while internal RAM is still plentiful: its 16 KB stack needs ONE free block of
 * internal RAM) but only starts drawing once start-up is finished. */
static volatile bool s_lvgl_go;

static void lvgl_timer_task(void *arg)
{
    (void)arg;
    while (!s_lvgl_go) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#if CROWPANEL_I2C_SCAN
static void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d SCL=%d)...", BSP_I2C_SDA, BSP_I2C_SCL);
    i2c_master_bus_handle_t bus = bsp_i2c_get_bus_handle();
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        esp_err_t err = i2c_master_probe(bus, addr, 50);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan done, %d device(s) found "
                  "(expect 0x30 STC8H1K28, 0x5D GT911, 0x51 PCF8563)", found);
}
#endif

#if CROWPANEL_RAW_PANEL_TEST
/* Paints solid full-screen colors directly through esp_lcd_panel_draw_bitmap(),
 * with no LVGL involved at all. If the panel timings/pin map are correct you
 * should see three clean, full, un-shifted colors in turn. If you see
 * nothing, noise, a shifted/rolling image, or wrong colors, the problem is
 * in the RGB timings (crowpanel_bsp.c -> panel_config.timings) rather than
 * in LVGL. */
static void raw_panel_test(esp_lcd_panel_handle_t panel, void *buf1, void *buf2)
{
    const size_t px_count = BSP_LCD_H_RES * BSP_LCD_V_RES;
    uint16_t *b1 = (uint16_t *)buf1;
    uint16_t *b2 = (uint16_t *)buf2;
    const uint16_t colors[3] = { 0xF800 /* red */, 0x07E0 /* green */, 0x001F /* blue */ };
    const char *names[3] = { "RED", "GREEN", "BLUE" };

    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "raw_panel_test: painting %s", names[i]);
        for (size_t p = 0; p < px_count; p++) {
            b1[p] = colors[i];
            b2[p] = colors[i];
        }
        esp_lcd_panel_draw_bitmap(panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, b1);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_lcd_panel_draw_bitmap(panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, b2);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "raw_panel_test: done, continuing to LVGL init");
}
#endif

/* ---------------------------------------------------------------------- */
/* app_main                                                               */
/* ---------------------------------------------------------------------- */

void app_main(void)
{
#if (BLED_LINK_UART_NUM == 0) && !BLED_LINK_SIMULATE
    /* The unit link owns UART0 (the console UART): keep logs off the wire. */
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    /* ---- NVS ---- */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* ---- I2C (touch + IO-extender + RTC share this bus) ---- */
    ESP_ERROR_CHECK(bsp_i2c_init());
    if (bsp_rtc_init() != ESP_OK) {
        ESP_LOGW(TAG, "RTC init failed - Clock tab will show '--:--:--'");
    }

#if CROWPANEL_I2C_SCAN
    i2c_bus_scan();
#endif

    /* Keep the screen off until the first frame is actually rendered. */
    bsp_display_backlight_off();

    /* ---- RGB LCD panel ---- */
    ESP_ERROR_CHECK(bsp_lcd_init(&s_panel));

    void *buf1 = NULL;
    void *buf2 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &buf1, &buf2));
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "esp_lcd_rgb_panel_get_frame_buffer returned NULL buffer(s)");
        abort();
    }

#if CROWPANEL_RAW_PANEL_TEST
    /* Turn the backlight on now so the raw color test is actually visible,
     * then run it before touching LVGL at all. */
    ESP_ERROR_CHECK(bsp_display_backlight_set(10));
    raw_panel_test(s_panel, buf1, buf2);
#endif

    /* ---- LVGL core ---- */
    lv_init();

    s_disp = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_user_data(s_disp, s_panel);
    /* LV_DISPLAY_RENDER_MODE_DIRECT: redraw only what actually changed
     * (e.g. just the clock text once a second) instead of the whole
     * screen every time. With 2 real hardware frame buffers, LVGL
     * automatically copies forward whatever didn't change so both stay
     * in sync - this is the purpose-built mode for exactly this setup,
     * and avoids the visible flash that full-screen redraws caused on
     * the Clock & Weather tab. */
    lv_display_set_buffers(s_disp, buf1, buf2,
                            BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
                            LV_DISPLAY_RENDER_MODE_DIRECT);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = &lvgl_tick_timer_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000)); /* 2 ms */

    /* ---- Touch ---- */
    err = bsp_touch_init(&s_touch);
    if (err == ESP_OK) {
        s_indev = lv_indev_create();
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, lvgl_touchpad_read_cb);
        lv_indev_set_user_data(s_indev, s_touch);
    } else {
        ESP_LOGW(TAG, "Touch init failed (%s) - continuing without touch input", esp_err_to_name(err));
    }

    /* ---- BLED core: SD storage, measurement-unit link, case model ---- */
    if (bled_core_init() != ESP_OK) {
        ESP_LOGW(TAG, "BLED core init reported a problem - see the log above (SD card / unit link may be unavailable)");
    }

    /* ---- UI ---- */
    ui_app_create();

    /* ---- LVGL task: create it NOW, before WiFi takes most of the internal RAM ---- */
    if (xTaskCreate(lvgl_timer_task, "lvgl_timer", 16384, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "LVGL TASK NOT CREATED - out of internal memory");
    }

    /* ---- WiFi (Connections tab) ---- */
    if (wifi_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "WiFi manager init failed - Connections tab will show no networks");
    }

    /* ---- Pump lv_timer_handler() ---- */
    /* Stack: 16 KB. The first frame of the original UI already needed ~7 KB of the old 8 KB; the BLED UI
     * (status bar, wizard, tables) needs a little more, and this task also performs the SD-card file
     * writes of a running case. An overflow here shows up as "stack overflow in task lvgl_timer". */
    s_lvgl_go = true;                              /* everything is initialised: let LVGL start drawing */

    /* First frame is rendered asynchronously by the task above; give it a
     * moment before turning the backlight on to avoid a flash of garbage. */
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(bsp_display_backlight_set(10));

    ESP_LOGI(TAG, "CrowPanel DIS02170A v1.4 up and running");
}



