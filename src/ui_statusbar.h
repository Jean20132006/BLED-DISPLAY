/**
 * @file ui_statusbar.h
 * @brief Permanent top status strip (like the header of a patient monitor):
 *        device name, active case + blood-loss level colour, clock and the
 *        unit / SD / Wi-Fi / power indicators.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Creates the bar at the top of parent (height UI_STATUSBAR_H) and starts its 500 ms refresh timer. */
lv_obj_t *ui_statusbar_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
