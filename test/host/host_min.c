#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "crowpanel_bsp.h"
#include "host_min.h"
struct host_queue { uint8_t *buf; UBaseType_t len, size, head, count; };
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size) { struct host_queue *q = calloc(1, sizeof(*q)); q->buf = calloc(len, item_size); q->len = len; q->size = item_size; return q; }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t w) { (void)w; if (!q || q->count >= q->len) return pdFALSE; memcpy(q->buf + ((q->head + q->count) % q->len) * q->size, item, q->size); q->count++; return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t w) { (void)w; if (!q || q->count == 0) return pdFALSE; memcpy(item, q->buf + q->head * q->size, q->size); q->head = (q->head + 1) % q->len; q->count--; return pdTRUE; }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q ? q->count : 0; }
__attribute__((weak)) uint32_t host_sim_now_ms(void) { return 0; }   /* tests that need virtual time define their own */
TickType_t xTaskGetTickCount(void) { return host_sim_now_ms(); }
host_bsp_state_t g_bsp = { .rtc = { .year = 26, .month = 10, .day = 17, .weekday = 6, .hour = 9, .minute = 41, .second = 7 }, .rtc_ok = true, .backlight = 10 };
esp_err_t bsp_rtc_get_time(pcf8563_time_t *t) { if (!g_bsp.rtc_ok) return ESP_FAIL; *t = g_bsp.rtc; return ESP_OK; }
