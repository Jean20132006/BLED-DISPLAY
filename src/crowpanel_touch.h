#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the GT911 touch controller on the shared I2C bus.
 * Call bsp_i2c_init() before this.
 */
esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *ret_touch);

#ifdef __cplusplus
}
#endif
