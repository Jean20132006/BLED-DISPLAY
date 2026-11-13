/**
 * @file crowpanel_bsp.h
 * @brief Board support for ELECROW CrowPanel ESP32 Advanced 7.0" (DIS02170A v1.4)
 *
 * MCU: ESP32-S3-WROOM-1-N16R8
 * LCD: 800x480 RGB565 parallel, truncated bus (R3-R7 / G2-G7 / B3-B7)
 * Touch: GT911 (I2C, addr 0x5D)
 * IO-extender: STC8H1K28 (I2C, addr 0x30) - backlight, buzzer, speaker,
 *              battery charger status, GT911 reset. Driven via the
 *              stc8h1k28 component (see stc8h1k28.h) on the new
 *              i2c_master driver.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "pcf8563.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== Panel geometry ===================== */
#define BSP_LCD_H_RES   800
#define BSP_LCD_V_RES   480

/* ===================== RGB565 parallel data pins =====================
 * Bit order matches the physical wiring on the schematic: bit0 (LSB) is
 * the least-significant routed Blue line (B3), bit15 (MSB) is the
 * most-significant routed Red line (R7).
 * ======================================================================= */
#define BSP_LCD_PIN_DATA0   21  /* B3 */
#define BSP_LCD_PIN_DATA1   47  /* B4 */
#define BSP_LCD_PIN_DATA2   48  /* B5 */
#define BSP_LCD_PIN_DATA3   45  /* B6 */
#define BSP_LCD_PIN_DATA4   38  /* B7 */
#define BSP_LCD_PIN_DATA5   9   /* G2 */
#define BSP_LCD_PIN_DATA6   10  /* G3 */
#define BSP_LCD_PIN_DATA7   11  /* G4 */
#define BSP_LCD_PIN_DATA8   12  /* G5 */
#define BSP_LCD_PIN_DATA9   13  /* G6 */
#define BSP_LCD_PIN_DATA10  14  /* G7 */
#define BSP_LCD_PIN_DATA11  7   /* R3 */
#define BSP_LCD_PIN_DATA12  17  /* R4 */
#define BSP_LCD_PIN_DATA13  18  /* R5 */
#define BSP_LCD_PIN_DATA14  3   /* R6 */
#define BSP_LCD_PIN_DATA15  46  /* R7 */

#define BSP_LCD_PIN_HSYNC   40
#define BSP_LCD_PIN_VSYNC   41
#define BSP_LCD_PIN_DE      42
#define BSP_LCD_PIN_PCLK    39
#define BSP_LCD_PIN_DISP_EN (-1)  /* not wired - display enable is handled
                                      by the STC8H1K28 over I2C, see below */

/* ===================== Shared I2C bus =====================
 * Shared by GT911 touch, PCF8563 RTC and the STC8H1K28 IO-extender.
 * Uses the new i2c_master driver (driver/i2c_master.h) throughout - do
 * not mix with the legacy driver/i2c.h API on this bus.
 * ============================================================= */
#define BSP_I2C_NUM         I2C_NUM_0
#define BSP_I2C_SDA         15
#define BSP_I2C_SCL         16
#define BSP_I2C_FREQ_HZ     400000

/* ===================== Touch (GT911) ===================== */
#define BSP_TOUCH_I2C_ADDR  0x5D
#define BSP_TOUCH_PIN_INT   1
#define BSP_TOUCH_PIN_RST   (-1)  /* reset is driven by the STC8H1K28,
                                      not by an ESP32 GPIO */

/* ===================== STC8H1K28 IO-extender =====================
 * See stc8h1k28.h for the command protocol (confirmed hardware v1.4 /
 * firmware v1.3 against Elecrow's official wiki + GitHub README).
 * ==================================================================== */
#define BSP_IOEXT_I2C_ADDR  0x30

/* ===================== PCF8563 RTC ===================== */
#define BSP_RTC_I2C_ADDR    0x51

/* ===================== API ===================== */

/** Bring up the shared I2C bus and the STC8H1K28 device on it (idempotent). */
esp_err_t bsp_i2c_init(void);

/** Attach the PCF8563 RTC on the shared I2C bus (idempotent, call after bsp_i2c_init()). */
esp_err_t bsp_rtc_init(void);

/** Read the current time from the RTC. */
esp_err_t bsp_rtc_get_time(pcf8563_time_t *time);

/** Set the RTC's current time (e.g. from a one-time setup screen). */
esp_err_t bsp_rtc_set_time(const pcf8563_time_t *time);

/** Shared I2C bus handle, for touch (or anything else) to attach to. */
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

/**
 * Set backlight brightness.
 * @param level 0 = screen off, 1-10 = brightness steps (10 = max).
 *              Internally mapped to the extender's native 0-244 scale
 *              (0 = max, 244 = min) plus the dedicated 245 "off" command.
 */
esp_err_t bsp_display_backlight_set(uint8_t level);

/** Convenience wrapper: turn the screen fully off. */
esp_err_t bsp_display_backlight_off(void);

/** Buzzer on/off. */
esp_err_t bsp_buzzer_set(bool on);

/** Onboard speaker amp (NS4168) on/off. */
esp_err_t bsp_speaker_set(bool on);

/**
 * Configure and initialize the RGB LCD panel.
 * Allocates two full frame buffers in PSRAM (double buffered).
 */
esp_err_t bsp_lcd_init(esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
