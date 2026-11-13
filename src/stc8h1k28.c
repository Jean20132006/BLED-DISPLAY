#include "stc8h1k28.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>

static const char *TAG = "STC8H1K28";

struct stc8h1k28_dev_t {
    i2c_master_dev_handle_t i2c_dev;
};

esp_err_t stc8h1k28_init(i2c_master_bus_handle_t bus_handle,
                          stc8h1k28_handle_t *extender_handle,
                          const i2c_device_config_t *config)
{
    ESP_RETURN_ON_FALSE(bus_handle && extender_handle && config, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    stc8h1k28_handle_t dev = calloc(1, sizeof(struct stc8h1k28_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "Memory allocation failed");

    esp_err_t err = i2c_master_bus_add_device(bus_handle, config, &dev->i2c_dev);
    if (err != ESP_OK) {
        free(dev);
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to add I2C device");
    }

    *extender_handle = dev;
    ESP_LOGI(TAG, "stc8h1k28 initialized successfully");
    return ESP_OK;
}

void stc8h1k28_deinit(stc8h1k28_handle_t dev)
{
    if (dev) {
        if (dev->i2c_dev) {
            i2c_master_bus_rm_device(dev->i2c_dev);
        }
        free(dev);
    }
}

static esp_err_t stc8h1k28_send(stc8h1k28_handle_t dev, uint8_t value)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    esp_err_t err = i2c_master_transmit(dev->i2c_dev, &value, 1, -1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cmd %d FAILED: %s", value, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "cmd %d sent OK", value);
    }
    return err;
}

esp_err_t stc8h1k28_set_brightness(stc8h1k28_handle_t dev, uint8_t brightness)
{
    return stc8h1k28_send(dev, brightness);
}

esp_err_t stc8h1k28_display_off(stc8h1k28_handle_t dev)
{
    return stc8h1k28_send(dev, STC8H1K28_CMD_DISPLAY_OFF);
}

esp_err_t stc8h1k28_buzzer_on(stc8h1k28_handle_t dev)
{
    return stc8h1k28_send(dev, STC8H1K28_CMD_BUZZER_ON);
}

esp_err_t stc8h1k28_buzzer_off(stc8h1k28_handle_t dev)
{
    return stc8h1k28_send(dev, STC8H1K28_CMD_BUZZER_OFF);
}

esp_err_t stc8h1k28_speaker_on(stc8h1k28_handle_t dev)
{
    return stc8h1k28_send(dev, STC8H1K28_CMD_SPEAKER_ON);
}

esp_err_t stc8h1k28_speaker_off(stc8h1k28_handle_t dev)
{
    return stc8h1k28_send(dev, STC8H1K28_CMD_SPEAKER_OFF);
}
