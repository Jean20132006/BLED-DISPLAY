#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the Home tab (sidebar menu: Brightness / Sound / About) inside parent. */
void ui_home_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
