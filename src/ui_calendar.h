#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the Calendar tab inside parent. Tapping a date opens a dialog
 *  to add an event title for that day; dates with events are highlighted. */
void ui_calendar_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
