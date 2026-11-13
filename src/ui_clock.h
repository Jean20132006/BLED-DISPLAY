#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the Clock tab inside parent: RTC clock + Set Time/Set Date pages, and live cards for the
 *  active BLED case (elapsed time, pads measured, cumulative blood loss). Starts its own LVGL timers. */
void ui_clock_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
