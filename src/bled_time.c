/**
 * @file bled_time.c
 * @brief See bled_time.h.
 */
#include "bled_time.h"

#include <stdio.h>
#include <string.h>

static bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

static bool time_of_day_valid(const bled_stamp_t *t)
{
    return t->hour < 24 && t->minute < 60 && t->second < 60;
}

bool bled_stamp_valid(const bled_stamp_t *t)
{
    if (!t || t->month < 1 || t->month > 12 || t->day < 1 || t->year > 99) {
        return false;
    }
    return t->day <= days_in_month(2000 + t->year, t->month) && time_of_day_valid(t);
}

/* Days from 2000-01-01 to y-m-d (Howard Hinnant's civil-from-days algorithm, shifted epoch). */
static int32_t days_from_2000(int y, int m, int d)
{
    y -= (m <= 2);
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int32_t days_from_0000_03_01 = era * 146097 + doe;
    /* 2000-01-01 is 730425 days after 0000-03-01 */
    return days_from_0000_03_01 - 730425;
}

int64_t bled_stamp_to_seconds(const bled_stamp_t *t)
{
    if (!bled_stamp_valid(t)) {
        return 0;
    }
    int32_t days = days_from_2000(2000 + t->year, t->month, t->day);
    return (int64_t)days * 86400 + (int64_t)t->hour * 3600 + (int64_t)t->minute * 60 + t->second;
}

int32_t bled_stamp_diff_s(const bled_stamp_t *a, const bled_stamp_t *b)
{
    if (!bled_stamp_valid(a) || !bled_stamp_valid(b)) {
        return 0;
    }
    return (int32_t)(bled_stamp_to_seconds(b) - bled_stamp_to_seconds(a));
}

void bled_stamp_format(const bled_stamp_t *t, char *out, size_t out_size)
{
    if (!bled_stamp_valid(t)) {
        snprintf(out, out_size, "--");
        return;
    }
    snprintf(out, out_size, "20%02u-%02u-%02u %02u:%02u:%02u", (unsigned)t->year, (unsigned)t->month,
             (unsigned)t->day, (unsigned)t->hour, (unsigned)t->minute, (unsigned)t->second);
}

void bled_stamp_format_time(const bled_stamp_t *t, char *out, size_t out_size)
{
    if (!t || !time_of_day_valid(t)) {
        snprintf(out, out_size, "--:--:--");
        return;
    }
    snprintf(out, out_size, "%02u:%02u:%02u", (unsigned)t->hour, (unsigned)t->minute, (unsigned)t->second);
}

void bled_stamp_format_short(const bled_stamp_t *t, char *out, size_t out_size)
{
    if (!bled_stamp_valid(t)) {
        snprintf(out, out_size, "--");
        return;
    }
    snprintf(out, out_size, "%02u/%02u/20%02u %02u:%02u", (unsigned)t->day, (unsigned)t->month, (unsigned)t->year,
             (unsigned)t->hour, (unsigned)t->minute);
}

static bool two_digits(const char *s, unsigned *out)
{
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') {
        return false;
    }
    *out = (unsigned)((s[0] - '0') * 10 + (s[1] - '0'));
    return true;
}

bool bled_stamp_parse_time(const char *s, bled_stamp_t *out)
{
    unsigned h, m, sec;
    if (!s || strlen(s) != 8 || s[2] != ':' || s[5] != ':') {
        return false;
    }
    if (!two_digits(s, &h) || !two_digits(s + 3, &m) || !two_digits(s + 6, &sec) || h > 23 || m > 59 || sec > 59) {
        return false;
    }
    out->hour = (uint8_t)h;
    out->minute = (uint8_t)m;
    out->second = (uint8_t)sec;
    return true;
}

bool bled_stamp_parse(const char *s, bled_stamp_t *out)
{
    /* "YYYY-MM-DD HH:MM:SS" */
    unsigned c, yy, mo, d;
    if (!s || strlen(s) != 19 || s[4] != '-' || s[7] != '-' || s[10] != ' ') {
        return false;
    }
    if (!two_digits(s, &c) || !two_digits(s + 2, &yy) || !two_digits(s + 5, &mo) || !two_digits(s + 8, &d)) {
        return false;
    }
    if (c != 20) {
        return false;                           /* RTC range is 2000..2099 */
    }
    bled_stamp_t t = { .year = (uint8_t)yy, .month = (uint8_t)mo, .day = (uint8_t)d };
    if (!bled_stamp_parse_time(s + 11, &t) || !bled_stamp_valid(&t)) {
        return false;
    }
    *out = t;
    return true;
}

void bled_format_duration(int32_t seconds, char *out, size_t out_size)
{
    if (seconds < 0) {
        snprintf(out, out_size, "--");
        return;
    }
    unsigned h = (unsigned)(seconds / 3600);
    unsigned m = (unsigned)((seconds % 3600) / 60);
    unsigned s = (unsigned)(seconds % 60);
    if (h > 99) {
        h = 99;
    }
    snprintf(out, out_size, "%02u:%02u:%02u", h, m, s);
}
