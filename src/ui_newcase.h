/**
 * @file ui_newcase.h
 * @brief New Case tab: a 4-step wizard (case number -> patient baseline Hb ->
 *        system baseline -> review/start) with Back on every step, which turns
 *        into the active-case panel (live totals, End case) once a case runs.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_newcase_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
