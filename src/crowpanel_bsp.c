#include "crowpanel_bsp.h"
#include "stc8h1k28.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "crowpanel_bsp";
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static stc8h1k28_handle_t s_ioext = NULL;
static pcf8563_handle_t s_rtc = NULL;

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t ioext_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_IOEXT_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    err = stc8h1k28_init(s_i2c_bus, &s_ioext, &ioext_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stc8h1k28_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus up (SDA=%d SCL=%d @ %d Hz)", BSP_I2C_SDA, BSP_I2C_SCL, BSP_I2C_FREQ_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return s_i2c_bus;
}

esp_err_t bsp_rtc_init(void)
{
    if (s_rtc) {
        return ESP_OK;
    }
    if (!s_i2c_bus) {
        ESP_LOGE(TAG, "bsp_rtc_init() called before bsp_i2c_init()");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t rtc_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_RTC_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    esp_err_t err = pcf8563_init(s_i2c_bus, &s_rtc, &rtc_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pcf8563_init failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t bsp_rtc_get_time(pcf8563_time_t *time)
{
    if (!s_rtc) {
        return ESP_ERR_INVALID_STATE;
    }
    return pcf8563_get_time(s_rtc, time);
}

esp_err_t bsp_rtc_set_time(const pcf8563_time_t *time)
{
    if (!s_rtc) {
        return ESP_ERR_INVALID_STATE;
    }
    return pcf8563_set_time(s_rtc, time);
}

esp_err_t bsp_display_backlight_set(uint8_t level)
{
    if (level == 0) {
        return bsp_display_backlight_off();
    }
    if (level > 10) {
        level = 10;
    }

    /* Map UI level 1-10 onto the extender's native 0-244 scale, where
     * 0 = max brightness and 244 = dimmest. level=10 -> 0 (max),
     * level=1 -> 244 (dimmest). Single write, no special sequencing. */
    uint8_t value = (uint8_t)(244 - ((uint32_t)(level - 1) * 244) / 9);
    return stc8h1k28_set_brightness(s_ioext, value);
}

esp_err_t bsp_display_backlight_off(void)
{
    return stc8h1k28_display_off(s_ioext);
}

esp_err_t bsp_buzzer_set(bool on)
{
    return on ? stc8h1k28_buzzer_on(s_ioext) : stc8h1k28_buzzer_off(s_ioext);
}

esp_err_t bsp_speaker_set(bool on)
{
    return on ? stc8h1k28_speaker_on(s_ioext) : stc8h1k28_speaker_off(s_ioext);
}

esp_err_t bsp_lcd_init(esp_lcd_panel_handle_t *ret_panel)
{
    ESP_LOGI(TAG, "Configuring RGB LCD panel (DIS02170A v1.4, %dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .psram_trans_align = 64,
        .data_width = 16,
        .bits_per_pixel = 16,
        .de_gpio_num = BSP_LCD_PIN_DE,
        .pclk_gpio_num = BSP_LCD_PIN_PCLK,
        .vsync_gpio_num = BSP_LCD_PIN_VSYNC,
        .hsync_gpio_num = BSP_LCD_PIN_HSYNC,
        .disp_gpio_num = BSP_LCD_PIN_DISP_EN,
        .data_gpio_nums = {
            BSP_LCD_PIN_DATA0,  BSP_LCD_PIN_DATA1,  BSP_LCD_PIN_DATA2,  BSP_LCD_PIN_DATA3,
            BSP_LCD_PIN_DATA4,  BSP_LCD_PIN_DATA5,  BSP_LCD_PIN_DATA6,  BSP_LCD_PIN_DATA7,
            BSP_LCD_PIN_DATA8,  BSP_LCD_PIN_DATA9,  BSP_LCD_PIN_DATA10, BSP_LCD_PIN_DATA11,
            BSP_LCD_PIN_DATA12, BSP_LCD_PIN_DATA13, BSP_LCD_PIN_DATA14, BSP_LCD_PIN_DATA15,
        },
        .timings = {
            /* Known-good starting point for this 800x480 truncated-RGB565
             * panel family. If you see noise/tearing/shifted image, try
             * lowering pclk_hz to 12-14 MHz first. */
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = BSP_LCD_H_RES,
            .v_res = BSP_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = true,
        },
        .flags.fb_in_psram = true,
        .num_fbs = 2,                              /* double buffer, tear-free */
        .bounce_buffer_size_px = BSP_LCD_H_RES * 20 /* internal-SRAM bounce buffer,
                                                        reduces PSRAM bus contention */
    };

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, ret_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*ret_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*ret_panel));

    ESP_LOGI(TAG, "RGB panel ready");
    return ESP_OK;
}
