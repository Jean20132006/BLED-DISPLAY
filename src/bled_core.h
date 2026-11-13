/**
 * @file bled_core.h
 * @brief BLED application model: measurement-unit status, optical baseline,
 *        and the active patient case (pads, cumulative blood loss, SD
 *        persistence).  UI code only reads this model and calls the action
 *        functions below; it never talks to UART or SD directly.
 *
 * Everything here runs on the LVGL task (bled_core_tick() is called from an
 * LVGL timer), so no locking is needed between the UI and the model.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "bled_config.h"
#include "bled_link.h"
#include "bled_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLED_BASE_IDLE = 0,
    BLED_BASE_RUNNING,
    BLED_BASE_DONE,
    BLED_BASE_FAILED,
} bled_base_state_t;

typedef struct {
    bool              unit_online;
    bool              unit_simulated;
    bled_unit_state_t unit_state;
    bool              cartridge;
    bool              on_battery;
    int               battery_pct;          /* -1 unknown */
    int               unit_error;           /* 0 = none (reported by the unit) */
    bled_base_state_t base_state;
    int               base_pct;
    int               base_i540, base_i850;
    bled_stamp_t      base_time;
    char              base_msg[44];         /* reason when base_state == FAILED */
    bool              rtc_ok;
} bled_system_t;

typedef struct {
    bool         active;
    char         id[BLED_CASE_ID_MAX + 1];
    float        hb_gdl;
    bled_stamp_t start;
    char         file[13];                  /* SD file name once created */
    bool         persisted;                 /* the SD file exists */
    bool         sd_warning;                /* some data is NOT on the SD card yet */
    int          pad_count;
    float        total_ebl_ml;
    float        last_pad_ml;
    bled_stamp_t last_pad_time;
    char         warning[44];               /* last data warning (rejected pad, pad limit ...) */
} bled_case_t;

/** Values being entered by the New Case wizard. */
typedef struct {
    char  id[BLED_CASE_ID_MAX + 1];
    float hb_gdl;
} bled_draft_t;

typedef enum {
    BLED_START_OK = 0,
    BLED_START_ERR_ACTIVE,
    BLED_START_ERR_ID,
    BLED_START_ERR_HB,
    BLED_START_ERR_BASELINE,
    BLED_START_ERR_UNIT,
} bled_start_result_t;

/** Initialise storage, link and model. Call once before the UI is created. */
esp_err_t bled_core_init(void);

/** Call every ~100 ms from an LVGL timer. */
void bled_core_tick(uint32_t now_ms);

const bled_system_t *bled_sys(void);
const bled_case_t *bled_case(void);

/** Increments whenever anything visible in the model changed (cheap change detection for the UI). */
uint32_t bled_core_revision(void);

/* ---- actions ---- */
bool bled_baseline_start(void);                         /* false if the unit is offline or a case is active */
bled_start_result_t bled_case_start(const bled_draft_t *draft);
void bled_case_end(void);
void bled_sim_pad(void);                                /* simulated unit only */

/* ---- helpers shared with the UI ---- */
bool bled_id_valid(const char *id);
bool bled_hb_valid(float hb_gdl);
int  bled_ebl_level(float ml);                          /* 0 normal, 1 warning, 2 alert */
bled_stamp_t bled_now(void);                            /* RTC time, all-zero if unavailable */
int32_t bled_case_elapsed_s(void);                      /* -1 when no case is active */
const char *bled_start_result_text(bled_start_result_t r);
const char *bled_unit_state_text(bled_unit_state_t s);
const char *bled_error_text(int code);

#ifdef __cplusplus
}
#endif
