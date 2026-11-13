/**
 * @file bled_storage.h
 * @brief Patient-case records on the SD card (TF slot).
 *
 * One CSV text file per case, /sdcard/BLED/Cnnnnnnn.CSV (8.3 name, because
 * the project's FATFS has long file names disabled; the real case number is
 * stored inside the file).  Plain text so it opens directly in Excel / a
 * text editor on a PC - handy for comparing against lab reference values.
 *
 *     BLEDCASE,1                     file format version
 *     CASE,12345                     case number as typed by the user
 *     START,2026-10-17 09:12:03      case start (RTC local time)
 *     HB,13.2                        patient baseline hemoglobin (g/dL)
 *     BASE,48213,51877               optical baseline readings (unit specific)
 *     PAD,1,09:20:11,35.2            pad number, time of day, blood volume (mL)
 *     PAD,2,09:31:40,41.8
 *     END,2026-10-17 09:50:02        case closed
 *
 * After the 5 header lines the file is APPEND-ONLY: every pad is written
 * with its own open/append/fsync/close, and totals are always re-computed
 * from the PAD rows when a file is read.  A power cut can therefore at
 * worst lose or truncate the very last row - it can never corrupt earlier
 * data (SRD MCU-7).  Unknown line types are ignored, so newer firmware can
 * add fields without breaking older readers.
 *
 * Threading: file operations are meant to be called from the LVGL task.  The
 * background task that (re)mounts the card never touches LVGL; it only
 * publishes state that bled_storage_get_info() copies out.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "bled_config.h"
#include "bled_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLED_SD_ABSENT = 0,   /* no card found (yet) */
    BLED_SD_MOUNTING,
    BLED_SD_READY,
    BLED_SD_ERROR,        /* card found but unusable / lost */
} bled_sd_state_t;

typedef struct {
    bled_sd_state_t state;
    uint64_t        capacity_bytes;   /* 0 if unknown */
    int             records;          /* number of case files on the card, -1 if unknown */
    char            message[56];      /* short human-readable status or last error */
} bled_sd_info_t;

typedef struct {
    char         file[13];                      /* "C0000042.CSV" */
    char         id[BLED_CASE_ID_MAX + 1];
    bled_stamp_t start;
    bled_stamp_t end;                           /* all zero while the case is still open */
    float        hb_gdl;
    int          base_i540, base_i850;
    int          pads;
    float        total_ebl_ml;
    bool         closed;
} bled_record_summary_t;

typedef struct {
    bled_record_summary_t sum;
    int          n;                             /* pad rows actually stored below */
    bled_stamp_t pad_time[BLED_MAX_PADS];       /* time of day filled in; date = start date */
    float        pad_ml[BLED_MAX_PADS];
} bled_record_detail_t;

typedef struct {
    const char  *id;
    float        hb_gdl;
    bled_stamp_t start;
    int          base_i540, base_i850;
} bled_case_header_t;

/** Mount the SD card in the background (device) and keep retrying while no card is ready. */
esp_err_t bled_storage_init(void);

void bled_storage_get_info(bled_sd_info_t *out);
bool bled_storage_ready(void);

/** Ask the background task to try mounting again right now ("Retry" button). */
void bled_storage_request_remount(void);

/* ---------------- writing ---------------- */

/** Create a new case file and write the header. file_out receives its 8.3 name. */
bool bled_storage_case_create(const bled_case_header_t *hdr, char file_out[13]);
bool bled_storage_case_append_pad(const char *file, int number, const bled_stamp_t *t, float ebl_ml);
bool bled_storage_case_close(const char *file, const bled_stamp_t *end);

/* ---------------- reading ---------------- */

/** Scan the case directory; returns how many records will be listed (newest first, capped). */
int  bled_storage_list_begin(void);
/** Parse the next record (one file per call so the UI can stay responsive). false when done. */
bool bled_storage_list_next(bled_record_summary_t *out);
/** Load one record including all of its pad rows. */
bool bled_storage_load(const char *file, bled_record_detail_t *out);

#ifndef ESP_PLATFORM
/* host test hooks: use a plain directory as the "SD card" */
void bled_storage_host_set_root(const char *dir);
void bled_storage_host_set_ready(bool ready);
#endif

#ifdef __cplusplus
}
#endif
