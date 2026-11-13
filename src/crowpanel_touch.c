#include "crowpanel_touch.h"
#include "crowpanel_bsp.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

static const char *TAG = "crowpanel_touch";

esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *ret_touch)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.dev_addr = BSP_TOUCH_I2C_ADDR;
    io_config.scl_speed_hz = BSP_I2C_FREQ_HZ;

    /* Attaches as a second device on the same i2c_master bus the
     * STC8H1K28 lives on (see bsp_i2c_init()). Passing a
     * i2c_master_bus_handle_t here (rather than a raw i2c_port_t)
     * routes through the new i2c_master driver, not the legacy one. */
    esp_err_t err = esp_lcd_new_panel_io_i2c(bsp_i2c_get_bus_handle(), &io_config, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_PIN_RST, /* -1: reset is owned by the STC8H1K28 */
        .int_gpio_num = BSP_TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    err = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, ret_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_gt911 failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "GT911 touch controller ready (addr 0x%02X)", BSP_TOUCH_I2C_ADDR);
    return ESP_OK;
}
