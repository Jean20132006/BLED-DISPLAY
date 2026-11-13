/**
 * @file ui_records.h
 * @brief Records tab: lists the patient cases stored on the SD card (newest
 *        first) and shows one case in detail (summary, cumulative blood-loss
 *        chart and every pad measurement).
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_records_create(lv_obj_t *parent);

/** Tell the tab whether it is currently on screen (ui_app.c calls this when the tab changes).
 *  Becoming visible refreshes the list if anything may have changed. */
void ui_records_set_visible(bool visible);

#ifdef __cplusplus
}
#endif
