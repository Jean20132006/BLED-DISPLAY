#pragma once
#include "esp_err.h"
#include "esp_log.h"
#define ESP_RETURN_ON_FALSE(a, err, tag, msg) do { if (!(a)) { ESP_LOGE(tag, "%s", msg); return (err); } } while (0)
#define ESP_RETURN_ON_ERROR(x, tag, msg) do { esp_err_t _e = (x); if (_e != ESP_OK) { ESP_LOGE(tag, "%s", msg); return _e; } } while (0)
