#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bled_time.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(int argc, char **argv)
{
    if (argc > 1) {   /* python cross-check mode: read "y m d H M S" lines, print epoch seconds since 2000 */
        unsigned y, mo, d, h, mi, s;
        while (scanf("%u %u %u %u %u %u", &y, &mo, &d, &h, &mi, &s) == 6) {
            bled_stamp_t t = { (uint8_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s };
            printf("%lld\n", (long long)bled_stamp_to_seconds(&t));
        }
        return 0;
    }
    bled_stamp_t z = {0};
    CHECK(!bled_stamp_valid(&z));
    bled_stamp_t a = { 26, 10, 17, 9, 12, 3 };
    CHECK(bled_stamp_valid(&a));
    char buf[32];
    bled_stamp_format(&a, buf, sizeof buf);      CHECK(strcmp(buf, "2026-10-17 09:12:03") == 0);
    bled_stamp_format_time(&a, buf, sizeof buf); CHECK(strcmp(buf, "09:12:03") == 0);
    bled_stamp_format_short(&a, buf, sizeof buf);CHECK(strcmp(buf, "17/10/2026 09:12") == 0);
    bled_stamp_format(&z, buf, sizeof buf);      CHECK(strcmp(buf, "--") == 0);
    bled_stamp_t p; CHECK(bled_stamp_parse("2026-10-17 09:12:03", &p) && memcmp(&p, &a, sizeof p) == 0);
    CHECK(!bled_stamp_parse("2026-02-30 09:12:03", &p) && !bled_stamp_parse("2026-10-17 24:00:00", &p) &&
          !bled_stamp_parse("2026-10-17T09:12:03", &p) && !bled_stamp_parse("1999-10-17 09:12:03", &p) && !bled_stamp_parse("", &p));
    bled_stamp_t leap = { 28, 2, 29, 0, 0, 0 }, noleap = { 27, 2, 29, 0, 0, 0 }, c100 = { 0, 2, 29, 0, 0, 0 };
    CHECK(bled_stamp_valid(&leap) && !bled_stamp_valid(&noleap) && bled_stamp_valid(&c100) /* 2000 is a leap year */);
    bled_stamp_t m1 = { 26, 10, 17, 23, 59, 59 }, m2 = { 26, 10, 18, 0, 0, 1 };
    CHECK(bled_stamp_diff_s(&m1, &m2) == 2);                       /* across midnight */
    bled_stamp_t y1 = { 26, 12, 31, 23, 0, 0 }, y2 = { 27, 1, 1, 1, 0, 0 };
    CHECK(bled_stamp_diff_s(&y1, &y2) == 7200);                    /* across new year */
    CHECK(bled_stamp_diff_s(&z, &a) == 0);
    bled_format_duration(0, buf, sizeof buf);     CHECK(strcmp(buf, "00:00:00") == 0);
    bled_format_duration(3725, buf, sizeof buf);  CHECK(strcmp(buf, "01:02:05") == 0);
    bled_format_duration(-1, buf, sizeof buf);    CHECK(strcmp(buf, "--") == 0);
    printf(fails ? "TIME TESTS FAILED (%d)\n" : "TIME TESTS PASSED\n", fails);
    return fails != 0;
}
