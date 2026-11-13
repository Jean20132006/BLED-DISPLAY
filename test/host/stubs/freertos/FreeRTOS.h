#pragma once
/* host stand-in: single-process, pthread-free, enough for the queue/timer APIs the UI code uses */
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1
