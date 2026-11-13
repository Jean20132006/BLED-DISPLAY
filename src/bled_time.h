/**
 * @file bled_time.h
 * @brief Tiny calendar helpers for case timestamps. Pure C, no libc time
 *        functions (so no time-zone or newlib 32/64-bit time_t surprises):
 *        the PCF8563 RTC already holds local wall-clock time, which is
 *        exactly what gets stored and shown.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Local wall-clock time as read from the RTC. year is 0..99 meaning 2000..2099.
 *  An all-zero value means "unknown / RTC not available". */
typedef struct {
    uint8_t year, month, day, hour, minute, second;
} bled_stamp_t;

/** true if every field is a real calendar value (month 1-12, day valid for the month ...). */
bool bled_stamp_valid(const bled_stamp_t *t);

/** Seconds since 2000-01-01 00:00:00 (0 if invalid). */
int64_t bled_stamp_to_seconds(const bled_stamp_t *t);

/** (b - a) in seconds; 0 if either stamp is invalid. */
int32_t bled_stamp_diff_s(const bled_stamp_t *a, const bled_stamp_t *b);

/** "2026-10-17 09:12:03", or "--" if invalid.  out needs >= 20 bytes. */
void bled_stamp_format(const bled_stamp_t *t, char *out, size_t out_size);

/** "09:12:03", or "--:--:--" if the time of day is not valid.  out needs >= 9 bytes. */
void bled_stamp_format_time(const bled_stamp_t *t, char *out, size_t out_size);

/** "17/10/2026 09:12", or "--" (compact form used in tables).  out needs >= 17 bytes. */
void bled_stamp_format_short(const bled_stamp_t *t, char *out, size_t out_size);

/** Parse "YYYY-MM-DD HH:MM:SS" (exactly that format). */
bool bled_stamp_parse(const char *s, bled_stamp_t *out);

/** Parse "HH:MM:SS" into the time-of-day fields of *out (date fields untouched). */
bool bled_stamp_parse_time(const char *s, bled_stamp_t *out);

/** "1:05:09" / "12:05" style duration for a number of seconds (negative -> "--"). */
void bled_format_duration(int32_t seconds, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
