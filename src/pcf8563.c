#include "pcf8563.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>

static const char *TAG = "PCF8563";

struct pcf8563_dev_t {
    i2c_master_dev_handle_t i2c_dev;
};

static inline uint8_t bcd2dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

static inline uint8_t dec2bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

esp_err_t pcf8563_init(i2c_master_bus_handle_t bus_handle,
                        pcf8563_handle_t *rtc_handle,
                        const i2c_device_config_t *config)
{
    ESP_RETURN_ON_FALSE(bus_handle && rtc_handle && config, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    pcf8563_handle_t dev = calloc(1, sizeof(struct pcf8563_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "Memory allocation failed");

    esp_err_t err = i2c_master_bus_add_device(bus_handle, config, &dev->i2c_dev);
    if (err != ESP_OK) {
        free(dev);
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to add I2C device");
    }

    *rtc_handle = dev;
    ESP_LOGI(TAG, "pcf8563 initialized successfully");
    return ESP_OK;
}

void pcf8563_deinit(pcf8563_handle_t dev)
{
    if (dev) {
        if (dev->i2c_dev) {
            i2c_master_bus_rm_device(dev->i2c_dev);
        }
        free(dev);
    }
}

esp_err_t pcf8563_get_time(pcf8563_handle_t dev, pcf8563_time_t *time)
{
    ESP_RETURN_ON_FALSE(dev && time, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    uint8_t reg = 0x02; /* first clock register (seconds) */
    uint8_t raw[7] = { 0 };

    esp_err_t err = i2c_master_transmit_receive(dev->i2c_dev, &reg, 1, raw, sizeof(raw), -1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "get_time FAILED: %s", esp_err_to_name(err));
        return err;
    }

    time->second  = bcd2dec(raw[0] & 0x7F); /* bit7 is the VL (voltage-low) flag, ignored here */
    time->minute  = bcd2dec(raw[1] & 0x7F);
    time->hour    = bcd2dec(raw[2] & 0x3F);
    time->day     = bcd2dec(raw[3] & 0x3F);
    time->weekday = raw[4] & 0x07;
    time->month   = bcd2dec(raw[5] & 0x1F); /* bit7 is the century flag, ignored here */
    time->year    = bcd2dec(raw[6]);
    return ESP_OK;
}

esp_err_t pcf8563_set_time(pcf8563_handle_t dev, const pcf8563_time_t *time)
{
    ESP_RETURN_ON_FALSE(dev && time, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    uint8_t buf[8];
    buf[0] = 0x02; /* start register */
    buf[1] = dec2bcd(time->second) & 0x7F;
    buf[2] = dec2bcd(time->minute) & 0x7F;
    buf[3] = dec2bcd(time->hour) & 0x3F;
    buf[4] = dec2bcd(time->day) & 0x3F;
    buf[5] = time->weekday & 0x07;
    buf[6] = dec2bcd(time->month) & 0x1F;
    buf[7] = dec2bcd(time->year);

    esp_err_t err = i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), -1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_time FAILED: %s", esp_err_to_name(err));
    }
    return err;
}
