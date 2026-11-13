/* bled_link.c compiled with BLED_LINK_SIMULATE=0: exercises the real framing/decode/timeout path on the host */
#include <stdio.h>
#include <string.h>
#include "bled_link.h"
static int fails;
#include <stdint.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void rx_body(const char *body)
{
    char f[BLED_PROTO_ENCODED_MAX]; size_t n = bled_proto_encode(f, sizeof f, body); (void)n; bled_link_host_rx(f);
}
static bool pop(bled_event_t *e) { return xQueueReceive(bled_link_get_event_queue(), e, 0) == pdTRUE; }

int main(void)
{
    CHECK(!bled_link_is_simulated());
    CHECK(bled_link_init() == ESP_OK);
    bled_link_info_t li; bled_link_get_info(&li);
    CHECK(!li.connected && !li.simulated);

    /* heartbeat: PING at t=0, not again until 1000 ms later */
    char tx[256]; bled_link_poll(0); size_t n = bled_link_host_take_tx(tx, sizeof tx);
    CHECK(n > 0 && strncmp(tx, "$PING*", 6) == 0 && tx[n - 2] == '\r');
    bled_link_poll(500); CHECK(bled_link_host_take_tx(tx, sizeof tx) == 0);
    bled_link_poll(1000); CHECK(bled_link_host_take_tx(tx, sizeof tx) > 0);

    /* every transmitted frame must itself parse with a valid checksum */
    bled_parser_t p; bled_parser_init(&p); bled_msg_t m;
    bled_link_cmd_baseline(); n = bled_link_host_take_tx(tx, sizeof tx);
    bool ok = false; for (size_t i = 0; i < n; i++) ok |= bled_parser_feed(&p, (uint8_t)tx[i], &m);
    CHECK(ok && strcmp(m.type, "BASE") == 0 && m.argc == 0);
    bled_link_cmd_case_start("AB-12_x", 13.2f); n = bled_link_host_take_tx(tx, sizeof tx);
    ok = false; for (size_t i = 0; i < n; i++) ok |= bled_parser_feed(&p, (uint8_t)tx[i], &m);
    CHECK(ok && strcmp(m.type, "CASE") == 0 && m.argc == 2 && strcmp(m.argv[0], "AB-12_x") == 0 && strcmp(m.argv[1], "13.2") == 0);
    bled_link_cmd_case_end(); n = bled_link_host_take_tx(tx, sizeof tx);
    ok = false; for (size_t i = 0; i < n; i++) ok |= bled_parser_feed(&p, (uint8_t)tx[i], &m);
    CHECK(ok && strcmp(m.type, "ENDC") == 0);

    /* receive: STAT makes the unit "connected" */
    bled_link_poll(2000);
    rx_body("STAT,0,1,1,64");
    bled_event_t e; CHECK(pop(&e) && e.type == BLED_EVT_STATUS && e.u.status.state == BLED_UNIT_IDLE &&
                          e.u.status.cartridge && e.u.status.on_battery && e.u.status.battery_pct == 64);
    bled_link_get_info(&li); CHECK(li.connected && li.frames_ok == 1);

    /* all other message types */
    rx_body("BPRG,40");   CHECK(pop(&e) && e.type == BLED_EVT_BASE_PROGRESS && e.u.progress.pct == 40);
    rx_body("BDON,1,48213,51877"); CHECK(pop(&e) && e.type == BLED_EVT_BASE_DONE && e.u.base.ok && e.u.base.i540 == 48213 && e.u.base.i850 == 51877);
    rx_body("BDON,0,0,0"); CHECK(pop(&e) && e.type == BLED_EVT_BASE_DONE && !e.u.base.ok);
    rx_body("PAD,3,41.2"); CHECK(pop(&e) && e.type == BLED_EVT_PAD && e.u.pad.number == 3 && e.u.pad.ebl_ml > 41.19f && e.u.pad.ebl_ml < 41.21f);
    rx_body("ERR,2");      CHECK(pop(&e) && e.type == BLED_EVT_ERROR && e.u.error.code == 2);
    rx_body("STAT,3,0,0,-1,extra,fields"); CHECK(pop(&e) && e.u.status.state == BLED_UNIT_ERROR && e.u.status.battery_pct == -1);   /* forward compatible */

    /* invalid field values and unknown types: checksum-valid, so the unit is alive, but no event */
    uint32_t bad0 = 0; bled_link_get_info(&li); bad0 = li.bad_messages;
    rx_body("STAT,9,1,0,50"); rx_body("STAT,0,2,0,50"); rx_body("STAT,0,1,0,101"); rx_body("BPRG,101"); rx_body("BDON,2,1,1");
    rx_body("PAD,0,5.0"); rx_body("PAD,1,abc"); rx_body("HELLO,1"); rx_body("STAT,0,1");
    CHECK(!pop(&e));
    bled_link_get_info(&li); CHECK(li.bad_messages == bad0 + 9);

    /* corrupted frames are counted and ignored */
    char f[BLED_PROTO_ENCODED_MAX]; bled_proto_encode(f, sizeof f, "PAD,4,10.0"); f[5] ^= 1;
    bled_link_host_rx(f); CHECK(!pop(&e));
    bled_link_get_info(&li); CHECK(li.crc_errors == 1);

    /* byte-by-byte delivery (UART chunking) */
    bled_proto_encode(f, sizeof f, "PAD,5,12.5");
    for (char *c = f; *c; c++) { char one[2] = { *c, 0 }; bled_link_host_rx(one); }
    CHECK(pop(&e) && e.u.pad.number == 5);

    /* timeout: connected for 3 s after the last valid frame, then offline; recovers on the next frame */
    bled_link_poll(2000 + 2900); bled_link_get_info(&li); CHECK(li.connected);
    bled_link_poll(2000 + 3100); bled_link_get_info(&li); CHECK(!li.connected);
    rx_body("STAT,0,1,0,100"); CHECK(pop(&e)); bled_link_get_info(&li); CHECK(li.connected);

    /* queue overflow drops events instead of blocking or crashing */
    for (int i = 0; i < 40; i++) rx_body("BPRG,10");
    int cnt = 0; while (pop(&e)) cnt++;
    CHECK(cnt == 16);

    printf(fails ? "LINK(real) TESTS FAILED (%d)\n" : "LINK(real) TESTS PASSED\n", fails);
    return fails != 0;
}
