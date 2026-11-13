/**
 * @file pcf8563.h
 * @brief Driver for the PCF8563 real-time clock on the shared I2C bus
 *        (I2C addr 0x51). Standard NXP register map: 0x02-0x08 hold
 *        seconds/minutes/hours/day/weekday/month/year in BCD.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcf8563_dev_t *pcf8563_handle_t;

typedef struct {
    uint8_t year;    /* 0-99, meaning 2000-2099 */
    uint8_t month;   /* 1-12 */
    uint8_t day;     /* 1-31 */
    uint8_t weekday; /* 0-6, 0 = Sunday */
    uint8_t hour;    /* 0-23 */
    uint8_t minute;  /* 0-59 */
    uint8_t second;  /* 0-59 */
} pcf8563_time_t;

esp_err_t pcf8563_init(i2c_master_bus_handle_t bus_handle,
                        pcf8563_handle_t *rtc_handle,
                        const i2c_device_config_t *config);

void pcf8563_deinit(pcf8563_handle_t dev);

esp_err_t pcf8563_get_time(pcf8563_handle_t dev, pcf8563_time_t *time);
esp_err_t pcf8563_set_time(pcf8563_handle_t dev, const pcf8563_time_t *time);

#ifdef __cplusplus
}
#endif
