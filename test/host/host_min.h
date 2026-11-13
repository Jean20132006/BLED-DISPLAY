#pragma once
/* Minimal host layer for the logic tests: a ring-buffer FreeRTOS queue and a fake RTC. No LVGL needed. */
#include <stdbool.h>
#include <stdint.h>
#include "pcf8563.h"
typedef struct { pcf8563_time_t rtc; bool rtc_ok; uint8_t backlight; bool buzzer, speaker; } host_bsp_state_t;
extern host_bsp_state_t g_bsp;
uint32_t host_sim_now_ms(void);
