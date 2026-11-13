#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the Connections tab (Wi-Fi / Bluetooth) inside parent. */
void ui_connections_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
