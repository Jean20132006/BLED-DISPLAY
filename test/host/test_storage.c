#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bled_storage.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    static char buf[16384]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); return buf;
}
static void write_raw(const char *path, const char *text) { FILE *f = fopen(path, "wb"); fputs(text, f); fclose(f); }

int main(void)
{
    char root[] = "/tmp/bled_sd_XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    bled_storage_host_set_root(root);
    char dir[160]; snprintf(dir, sizeof dir, "%s/BLED", root);

    /* not ready: everything refuses */
    bled_storage_host_set_ready(false);
    bled_case_header_t hdr = { "12345", 13.2f, { 26, 10, 17, 9, 12, 3 }, 48213, 51877 };
    char f1[13];
    CHECK(!bled_storage_case_create(&hdr, f1));
    CHECK(bled_storage_list_begin() == 0);
    bled_sd_info_t info; bled_storage_get_info(&info);
    CHECK(info.state == BLED_SD_ABSENT && info.records == -1);

    /* ready: create, append, close */
    bled_storage_host_set_ready(true);
    bled_storage_get_info(&info);
    CHECK(info.state == BLED_SD_READY && info.records == 0 && info.capacity_bytes > 0);
    CHECK(bled_storage_case_create(&hdr, f1));
    CHECK(strcmp(f1, "C0000001.CSV") == 0);
    bled_stamp_t t1 = { 26, 10, 17, 9, 20, 11 }, t2 = { 26, 10, 17, 9, 31, 40 }, tend = { 26, 10, 17, 9, 50, 2 };
    CHECK(bled_storage_case_append_pad(f1, 1, &t1, 35.2f));
    CHECK(bled_storage_case_append_pad(f1, 2, &t2, 41.8f));
    CHECK(bled_storage_case_close(f1, &tend));
    char p1[200]; snprintf(p1, sizeof p1, "%s/%s", dir, f1);
    const char *expect =
        "BLEDCASE,1\nCASE,12345\nSTART,2026-10-17 09:12:03\nHB,13.2\nBASE,48213,51877\n"
        "PAD,1,09:20:11,35.2\nPAD,2,09:31:40,41.8\nEND,2026-10-17 09:50:02\n";
    char *got = slurp(p1);
    CHECK(got && strcmp(got, expect) == 0);
    if (got && strcmp(got, expect) != 0) printf("--- got ---\n%s\n", got);

    /* second case, left open; unsafe characters in the id must not break the CSV */
    bled_case_header_t hdr2 = { "AB,C\nD-1_", 11.0f, { 26, 10, 18, 1, 2, 3 }, 1, 2 };
    char f2[13];
    CHECK(bled_storage_case_create(&hdr2, f2) && strcmp(f2, "C0000002.CSV") == 0);
    CHECK(bled_storage_case_append_pad(f2, 1, &t1, 20.0f));
    char p2[200]; snprintf(p2, sizeof p2, "%s/%s", dir, f2);
    got = slurp(p2);
    CHECK(got && strstr(got, "CASE,AB_C_D-1_\n") != NULL);

    /* listing: newest first, summaries computed from the PAD rows */
    CHECK(bled_storage_list_begin() == 2);
    bled_record_summary_t s;
    CHECK(bled_storage_list_next(&s) && strcmp(s.file, "C0000002.CSV") == 0 && strcmp(s.id, "AB_C_D-1_") == 0 &&
          !s.closed && s.pads == 1 && s.total_ebl_ml > 19.99f && s.total_ebl_ml < 20.01f);
    CHECK(bled_storage_list_next(&s) && strcmp(s.file, "C0000001.CSV") == 0 && strcmp(s.id, "12345") == 0 &&
          s.closed && s.pads == 2 && s.total_ebl_ml > 76.99f && s.total_ebl_ml < 77.01f && s.hb_gdl > 13.19f && s.hb_gdl < 13.21f &&
          s.base_i540 == 48213 && s.base_i850 == 51877 && s.start.hour == 9 && s.end.minute == 50);
    CHECK(!bled_storage_list_next(&s));

    /* full load */
    static bled_record_detail_t d;
    CHECK(bled_storage_load("C0000001.CSV", &d) && d.n == 2 && d.pad_ml[1] > 41.79f && d.pad_ml[1] < 41.81f &&
          d.pad_time[1].minute == 31 && d.pad_time[1].day == 17 && d.sum.pads == 2);
    CHECK(bled_storage_load("c0000001.csv", &d));                       /* case-insensitive */

    /* hostile / invalid names never touch the file system outside the case folder */
    CHECK(!bled_storage_load("../C0000001.CSV", &d) && !bled_storage_load("C0000001.TXT", &d) &&
          !bled_storage_load("/etc/passwd", &d) && !bled_storage_load("", &d) && !bled_storage_load(NULL, &d) &&
          !bled_storage_load("C000001.CSV", &d) && !bled_storage_load("C00000A1.CSV", &d));
    CHECK(!bled_storage_case_append_pad("../x.CSV", 1, &t1, 1.0f));

    /* power-cut simulation: truncate the last PAD line at every possible byte; earlier rows must always survive */
    char full[4096]; strcpy(full, expect);
    const char *last_pad = strstr(full, "PAD,2,"); size_t cut_from = (size_t)(last_pad - full);
    size_t total_len = strlen(full), ok_cuts = 0, cuts = 0;
    for (size_t cut = cut_from; cut < total_len; cut++) {
        char tmp[4096]; memcpy(tmp, full, cut); tmp[cut] = 0;
        write_raw(p1, tmp);
        bled_record_detail_t dd; cuts++;
        if (bled_storage_load("C0000001.CSV", &dd) && dd.sum.pads >= 1 && dd.pad_ml[0] > 35.19f && dd.pad_ml[0] < 35.21f) ok_cuts++;
    }
    printf("truncation at %zu different byte offsets: earlier data intact in %zu\n", cuts, ok_cuts);
    CHECK(cuts == ok_cuts);
    write_raw(p1, "BLEDCASE,1\nCASE,12345\nSTART,2026-10-17 09:12:03\nHB,13.2\nPAD,1,09:20:11,35.2\nPAD,2,09:3");   /* cut mid-line, no newline */
    CHECK(bled_storage_load("C0000001.CSV", &d) && d.sum.pads == 1 && !d.sum.closed);

    /* garbage and foreign files are skipped, not listed */
    char p3[200]; snprintf(p3, sizeof p3, "%s/C0000003.CSV", dir);
    unsigned char junk[512]; srand(5); for (int i = 0; i < 512; i++) junk[i] = (unsigned char)rand();
    FILE *jf = fopen(p3, "wb"); fwrite(junk, 1, sizeof junk, jf); fclose(jf);
    char p4[200]; snprintf(p4, sizeof p4, "%s/NOTES.TXT", dir); write_raw(p4, "hello");
    char p5[200]; snprintf(p5, sizeof p5, "%s/C0000004.CSV", dir); write_raw(p5, "BLEDCASE,1\nCASE,ONLYID\n");   /* missing START */
    CHECK(bled_storage_list_begin() == 4);        /* 4 candidate case-named files ... */
    int listed = 0; while (bled_storage_list_next(&s)) listed++;
    CHECK(listed == 2);                           /* ... but only the 2 valid ones are shown */
    unlink(p3); unlink(p5); unlink(p4);

    /* out-of-range pad values and bad numbers in a file are ignored */
    write_raw(p1, "BLEDCASE,1\nCASE,X1\nSTART,2026-10-17 09:12:03\nHB,13.2\nPAD,1,09:20:11,35.2\nPAD,2,09:21:00,9999.0\nPAD,3,25:00:00,5.0\nPAD,x,09:22:00,5.0\nPAD,4,09:23:00,-3.0\nPAD,5,09:24:00,10.0\n");
    CHECK(bled_storage_load("C0000001.CSV", &d) && d.sum.pads == 2 && d.sum.total_ebl_ml > 45.19f && d.sum.total_ebl_ml < 45.21f);

    /* >BLED_MAX_RECORDS files: keep the newest ones, sorted descending */
    for (int i = 10; i < 10 + 260; i++) {
        char pp[200]; snprintf(pp, sizeof pp, "%s/C%07d.CSV", dir, i);
        write_raw(pp, "BLEDCASE,1\nCASE,BULK\nSTART,2026-10-17 09:12:03\nHB,12.0\nPAD,1,09:20:11,10.0\n");
    }
    int shown = bled_storage_list_begin();
    CHECK(shown == BLED_MAX_RECORDS);
    bled_storage_get_info(&info);
    CHECK(info.records == 262);                    /* the real number of files is still reported */
    int idx = 0, prev = 99999999; bool desc = true;
    while (bled_storage_list_next(&s)) { int n = atoi(s.file + 1); if (n >= prev) desc = false; prev = n; idx++; }
    CHECK(idx == BLED_MAX_RECORDS && desc && prev == 269 - BLED_MAX_RECORDS + 1);   /* newest = C0000269 ... down to C0000070 */

    /* the card disappears: writes fail and the state flips to ERROR */
    bled_storage_host_set_ready(true);
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir); CHECK(system(cmd) == 0);
    write_raw(dir, "this is a file where the folder used to be");           /* dir path is now a regular file -> mkdir/fopen fail */
    CHECK(!bled_storage_case_create(&hdr, f1));
    bled_storage_get_info(&info);
    CHECK(!bled_storage_ready() && info.state == BLED_SD_ERROR);

    snprintf(cmd, sizeof cmd, "rm -rf %s", root); int rc = system(cmd); (void)rc;
    printf(fails ? "STORAGE TESTS FAILED (%d)\n" : "STORAGE TESTS PASSED\n", fails);
    return fails != 0;
}
