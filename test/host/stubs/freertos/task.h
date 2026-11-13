#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
static inline BaseType_t xTaskCreate(TaskFunction_t f, const char *n, uint32_t s, void *a, UBaseType_t p, TaskHandle_t *h) { (void)f; (void)n; (void)s; (void)a; (void)p; if (h) *h = 0; return pdPASS; }
static inline void vTaskDelay(TickType_t t) { (void)t; }
extern TickType_t xTaskGetTickCount(void);
