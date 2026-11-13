/**
 * @file stc8h1k28.h
 * @brief Driver for the STC8H1K28 IO-extender used on CrowPanel Advance
 *        boards (backlight, buzzer, speaker/amp control over I2C).
 *
 * Command protocol confirmed for hardware v1.4 / firmware v1.3 against
 * Elecrow's official wiki + GitHub README (the older v1.1/v1.2 boards use
 * a different 0x05-0x18 scheme - not applicable here).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stc8h1k28_dev_t *stc8h1k28_handle_t;

/* Raw command values, in case you need them directly. */
#define STC8H1K28_CMD_BRIGHTNESS_MAX 0    /* max brightness              */
#define STC8H1K28_CMD_BRIGHTNESS_MIN 244  /* dimmest (still on)          */
#define STC8H1K28_CMD_DISPLAY_OFF    245
#define STC8H1K28_CMD_BUZZER_ON      246
#define STC8H1K28_CMD_BUZZER_OFF     247
#define STC8H1K28_CMD_SPEAKER_ON     248
#define STC8H1K28_CMD_SPEAKER_OFF    249

/**
 * Attach the STC8H1K28 as a device on an existing I2C master bus.
 * @param bus_handle     Bus created with i2c_new_master_bus()
 * @param extender_handle Out: opaque device handle
 * @param config         Device config (address 0x30, desired scl_speed_hz)
 */
esp_err_t stc8h1k28_init(i2c_master_bus_handle_t bus_handle,
                          stc8h1k28_handle_t *extender_handle,
                          const i2c_device_config_t *config);

void stc8h1k28_deinit(stc8h1k28_handle_t dev);

/** Raw brightness write: 0 = max, 244 = dimmest ("on"). */
esp_err_t stc8h1k28_set_brightness(stc8h1k28_handle_t dev, uint8_t brightness);

esp_err_t stc8h1k28_display_off(stc8h1k28_handle_t dev);
esp_err_t stc8h1k28_buzzer_on(stc8h1k28_handle_t dev);
esp_err_t stc8h1k28_buzzer_off(stc8h1k28_handle_t dev);
esp_err_t stc8h1k28_speaker_on(stc8h1k28_handle_t dev);
esp_err_t stc8h1k28_speaker_off(stc8h1k28_handle_t dev);

#ifdef __cplusplus
}
#endif
