/* bled_core.c with the simulated unit and a directory standing in for the SD card */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bled_core.h"
#include "bled_storage.h"
#include "host_min.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t now;
uint32_t host_sim_now_ms(void) { return now; }
static void run(int ms) { for (int t = 0; t < ms; t += 100) { now += 100; bled_core_tick(now); } }
static void push_pad(int n, float ml) { bled_event_t e; memset(&e, 0, sizeof e); e.type = BLED_EVT_PAD; e.u.pad.number = n; e.u.pad.ebl_ml = ml; xQueueSend(bled_link_get_event_queue(), &e, 0); }
static char *slurp(const char *path) { FILE *f = fopen(path, "rb"); if (!f) return NULL; static char b[8192]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); return b; }

int main(void)
{
    char root[] = "/tmp/bled_core_XXXXXX"; CHECK(mkdtemp(root));
    bled_storage_host_set_root(root);
    CHECK(bled_core_init() == ESP_OK);
    bled_storage_host_set_ready(true);
    char dir[128]; snprintf(dir, sizeof dir, "%s/BLED", root);

    run(1500);
    const bled_system_t *s = bled_sys(); const bled_case_t *c = bled_case();
    CHECK(s->unit_online && s->unit_simulated && s->cartridge && s->battery_pct == 87 && !s->on_battery && s->rtc_ok);
    CHECK(s->base_state == BLED_BASE_IDLE && !c->active);

    /* validation */
    bled_draft_t d; memset(&d, 0, sizeof d);
    CHECK(bled_case_start(&d) == BLED_START_ERR_ID);
    strcpy(d.id, "A B"); d.hb_gdl = 13.0f; CHECK(bled_case_start(&d) == BLED_START_ERR_ID);
    CHECK(!bled_id_valid("ABCDEFGHIJKLM") && bled_id_valid("ABCDEFGHIJKL"));                  /* 13 chars rejected, 12 accepted */
    strcpy(d.id, "12345"); d.hb_gdl = 3.9f; CHECK(bled_case_start(&d) == BLED_START_ERR_HB);
    d.hb_gdl = 20.1f; CHECK(bled_case_start(&d) == BLED_START_ERR_HB);
    d.hb_gdl = NAN;  CHECK(bled_case_start(&d) == BLED_START_ERR_HB);
    d.hb_gdl = 13.2f; CHECK(bled_case_start(&d) == BLED_START_ERR_BASELINE);                 /* no baseline yet */
    CHECK(bled_id_valid("2026-0412") && bled_id_valid("a_b-9") && !bled_id_valid("") && !bled_id_valid("x,y") && !bled_id_valid(NULL));
    CHECK(bled_hb_valid(4.0f) && bled_hb_valid(20.0f) && !bled_hb_valid(-1.0f));

    /* baseline run: progress is monotonic and ends DONE with the unit's values */
    CHECK(bled_baseline_start() && s->base_state == BLED_BASE_RUNNING);
    int last = -1; bool mono = true; int seen = 0;
    for (int t = 0; t < 6000 && s->base_state == BLED_BASE_RUNNING; t += 100) { run(100); if (s->base_pct < last) mono = false; if (s->base_pct != last) seen++; last = s->base_pct; }
    CHECK(s->base_state == BLED_BASE_DONE && s->base_pct == 100 && s->base_i540 == 48213 && s->base_i850 == 51877 && mono && seen >= 5);
    CHECK(bled_stamp_valid(&s->base_time) && s->base_time.year == 26);

    /* start the case: SD file created with the header */
    uint32_t rev0 = bled_core_revision();
    CHECK(bled_case_start(&d) == BLED_START_OK);
    CHECK(bled_core_revision() != rev0);
    CHECK(c->active && strcmp(c->id, "12345") == 0 && c->persisted && !c->sd_warning && strcmp(c->file, "C0000001.CSV") == 0 && c->pad_count == 0);
    CHECK(bled_case_start(&d) == BLED_START_ERR_ACTIVE);
    CHECK(!bled_baseline_start());                              /* not while a case is running */
    run(300); CHECK(s->unit_state == BLED_UNIT_CASE);
    char path[200]; snprintf(path, sizeof path, "%s/C0000001.CSV", dir);
    char *txt = slurp(path);
    CHECK(txt && strstr(txt, "CASE,12345\n") && strstr(txt, "HB,13.2\n") && strstr(txt, "BASE,48213,51877\n"));

    /* pads from the simulated unit */
    for (int i = 0; i < 3; i++) { bled_sim_pad(); run(200); }
    CHECK(c->pad_count == 3 && fabsf(c->total_ebl_ml - 109.5f) < 0.01f && fabsf(c->last_pad_ml - 29.8f) < 0.01f && bled_ebl_level(c->total_ebl_ml) == 0);
    txt = slurp(path);
    CHECK(txt && strstr(txt, "PAD,1,") && strstr(txt, ",38.5\n") && strstr(txt, "PAD,3,") && strstr(txt, ",29.8\n"));

    /* hostile / odd events: duplicate number, out of range, negative, NaN */
    push_pad(3, 77.0f); run(200); CHECK(c->pad_count == 3);                       /* duplicate of pad 3 */
    push_pad(4, 9999.0f); run(200); CHECK(c->pad_count == 3 && c->warning[0]);    /* rejected */
    push_pad(4, 9999.0f); run(200); CHECK(c->pad_count == 3);                     /* 4 already consumed -> duplicate */
    push_pad(5, -2.0f); run(200); CHECK(c->pad_count == 3);
    push_pad(6, NAN); run(200); CHECK(c->pad_count == 3);
    push_pad(7, 10.0f); run(200); CHECK(c->pad_count == 4 && c->warning[0] == 0 && fabsf(c->total_ebl_ml - 119.5f) < 0.01f);

    /* the SD card disappears mid-case: pads are still counted, the warning is raised, nothing is lost once it returns */
    bled_storage_host_set_ready(false);
    push_pad(8, 20.0f); run(200); push_pad(9, 21.0f); run(200);
    CHECK(c->pad_count == 6 && c->sd_warning);
    bled_storage_host_set_ready(true);
    run(3500);                                                                    /* background flush retry */
    CHECK(!c->sd_warning);
    txt = slurp(path);
    int rows = 0; for (const char *q = txt; (q = strstr(q, "PAD,")); q++) rows++;
    CHECK(rows == 6 && strstr(txt, ",21.0\n"));

    /* level thresholds */
    CHECK(bled_ebl_level(499.9f) == 0 && bled_ebl_level(500.0f) == 1 && bled_ebl_level(999.9f) == 1 && bled_ebl_level(1000.0f) == 2);

    /* elapsed time follows the RTC; falls back to the tick clock if the RTC dies */
    CHECK(bled_case_elapsed_s() >= 0);
    g_bsp.rtc_ok = false; run(1100); CHECK(!s->rtc_ok && bled_case_elapsed_s() >= 0);
    bled_stamp_t nostamp = bled_now(); CHECK(!bled_stamp_valid(&nostamp));
    g_bsp.rtc_ok = true; run(1100); CHECK(s->rtc_ok);

    /* end the case: END row written, state reset, new baseline required */
    bled_case_end();
    txt = slurp(path); CHECK(txt && strstr(txt, "END,20"));
    CHECK(!c->active && s->base_state == BLED_BASE_IDLE && bled_case_elapsed_s() == -1);
    run(300); CHECK(s->unit_state == BLED_UNIT_IDLE);
    CHECK(bled_case_start(&d) == BLED_START_ERR_BASELINE);
    bled_record_summary_t rec; CHECK(bled_storage_list_begin() == 1 && bled_storage_list_next(&rec) && rec.closed && rec.pads == 6 && fabsf(rec.total_ebl_ml - 160.5f) < 0.01f);

    /* a case that starts with NO SD card still works, then saves everything when the card appears */
    bled_storage_host_set_ready(false);
    CHECK(bled_baseline_start()); run(4000); CHECK(s->base_state == BLED_BASE_DONE);
    strcpy(d.id, "NOSD1"); CHECK(bled_case_start(&d) == BLED_START_OK);
    CHECK(c->active && !c->persisted && c->sd_warning);
    bled_sim_pad(); run(300); bled_sim_pad(); run(300);
    CHECK(c->pad_count == 2);
    bled_storage_host_set_ready(true); run(3500);
    CHECK(c->persisted && !c->sd_warning && strcmp(c->file, "C0000002.CSV") == 0);
    snprintf(path, sizeof path, "%s/C0000002.CSV", dir); txt = slurp(path);
    CHECK(txt && strstr(txt, "CASE,NOSD1\n") && strstr(txt, "PAD,1,") && strstr(txt, "PAD,2,"));
    bled_case_end();

    char cmd[200]; snprintf(cmd, sizeof cmd, "rm -rf %s", root); int rc = system(cmd); (void)rc;
    printf(fails ? "CORE TESTS FAILED (%d)\n" : "CORE TESTS PASSED\n", fails);
    return fails != 0;
}
