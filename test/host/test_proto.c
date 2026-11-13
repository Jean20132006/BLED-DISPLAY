#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bled_proto.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static bool feed_str(bled_parser_t *p, const char *s, bled_msg_t *m)
{
    bool got = false;
    for (; *s; s++) if (bled_parser_feed(p, (uint8_t)*s, m)) got = true;
    return got;
}

int main(int argc, char **argv)
{
    /* mode used by the python cross-check: read bodies from stdin, print CRCs */
    if (argc > 1 && strcmp(argv[1], "crc") == 0) {
        char line[256];
        while (fgets(line, sizeof line, stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            printf("%04X\n", bled_crc16((const uint8_t *)line, strlen(line)));
        }
        return 0;
    }

    /* 1. standard check value */
    CHECK(bled_crc16((const uint8_t *)"123456789", 9) == 0x29B1);
    CHECK(bled_crc16((const uint8_t *)"", 0) == 0xFFFF);

    /* 2. encode */
    char out[BLED_PROTO_ENCODED_MAX];
    size_t n = bled_proto_encode(out, sizeof out, "PAD,3,41.2");
    CHECK(n == strlen(out) && n == 1 + 10 + 1 + 4 + 2);
    CHECK(out[0] == '$' && out[11] == '*' && out[n - 2] == '\r' && out[n - 1] == '\n');
    printf("encoded: %s", out);

    /* 3. round trip */
    bled_parser_t p; bled_parser_init(&p);
    bled_msg_t m;
    CHECK(feed_str(&p, out, &m));
    CHECK(strcmp(m.type, "PAD") == 0 && m.argc == 2 && strcmp(m.argv[0], "3") == 0 && strcmp(m.argv[1], "41.2") == 0);
    float f; int i;
    CHECK(bled_arg_int(m.argv[0], &i) && i == 3);
    CHECK(bled_arg_float(m.argv[1], &f) && f > 41.19f && f < 41.21f);
    CHECK(p.frames_ok == 1 && p.crc_errors == 0);

    /* no-arg message */
    n = bled_proto_encode(out, sizeof out, "PING");
    CHECK(n > 0 && feed_str(&p, out, &m) && strcmp(m.type, "PING") == 0 && m.argc == 0);

    /* 4. corrupted payload and corrupted CRC are rejected and counted */
    bled_proto_encode(out, sizeof out, "STAT,0,1,0,87");
    char bad[BLED_PROTO_ENCODED_MAX]; strcpy(bad, out); bad[7] ^= 0x01;
    uint32_t before = p.crc_errors;
    CHECK(!feed_str(&p, bad, &m) && p.crc_errors == before + 1);
    strcpy(bad, out); bad[strlen(bad) - 3] = (bad[strlen(bad) - 3] == '0') ? '1' : '0';
    CHECK(!feed_str(&p, bad, &m) && p.crc_errors == before + 2);
    CHECK(feed_str(&p, out, &m) && strcmp(m.type, "STAT") == 0 && m.argc == 4);   /* parser recovered */

    /* 5. resynchronisation: garbage, a truncated frame, then a good frame */
    bled_proto_encode(out, sizeof out, "BPRG,40");
    char stream[256]; snprintf(stream, sizeof stream, "xx\x01\xff$PAD,1,2*12$%s", out);
    CHECK(feed_str(&p, stream, &m) && strcmp(m.type, "BPRG") == 0);

    /* 6. bare LF line end accepted */
    bled_proto_encode(out, sizeof out, "ENDC");
    char *cr = strchr(out, '\r'); memmove(cr, cr + 1, strlen(cr));   /* drop the \r */
    CHECK(feed_str(&p, out, &m) && strcmp(m.type, "ENDC") == 0);

    /* 7. runaway frame (no terminator) is dropped, next frame still works */
    uint32_t fe = p.format_errors;
    bled_parser_feed(&p, '$', &m);
    for (int k = 0; k < BLED_PROTO_MAX_FRAME + 20; k++) bled_parser_feed(&p, 'A', &m);
    CHECK(p.format_errors > fe);
    bled_proto_encode(out, sizeof out, "PING");
    CHECK(feed_str(&p, out, &m));

    /* 8. too many args / bad TYPE / empty body */
    bled_proto_encode(out, sizeof out, "X,1,2,3,4,5,6,7,8,9");
    CHECK(!feed_str(&p, out, &m));
    bled_proto_encode(out, sizeof out, "x,1");           /* lower-case type */
    CHECK(!feed_str(&p, out, &m));
    CHECK(bled_proto_encode(out, sizeof out, "") == 0);
    CHECK(bled_proto_encode(out, sizeof out, "A B") == 0);    /* space not allowed */
    CHECK(bled_proto_encode(out, sizeof out, "A*B") == 0);
    CHECK(bled_proto_encode(out, 8, "PING") == 0);             /* buffer too small */
    char longbody[200]; memset(longbody, 'A', 199); longbody[199] = 0;
    CHECK(bled_proto_encode(out, sizeof out, longbody) == 0);

    /* 9. strict numeric parsers */
    CHECK(bled_arg_int("0", &i) && i == 0);
    CHECK(bled_arg_int("-12", &i) && i == -12);
    CHECK(bled_arg_int("123456789", &i) && i == 123456789);
    CHECK(!bled_arg_int("", &i) && !bled_arg_int("-", &i) && !bled_arg_int("1x", &i) && !bled_arg_int("1234567890", &i) &&
          !bled_arg_int(" 1", &i) && !bled_arg_int("+1", &i) && !bled_arg_int("1.0", &i) && !bled_arg_int(NULL, &i));
    CHECK(bled_arg_float("13.2", &f) && f > 13.19f && f < 13.21f);
    CHECK(bled_arg_float("-0.5", &f) && f < -0.49f && f > -0.51f);
    CHECK(bled_arg_float("7", &f) && f == 7.0f);
    CHECK(!bled_arg_float("", &f) && !bled_arg_float(".5", &f) && !bled_arg_float("5.", &f) && !bled_arg_float("1e3", &f) &&
          !bled_arg_float("nan", &f) && !bled_arg_float("inf", &f) && !bled_arg_float("1.2.3", &f) && !bled_arg_float("1.1234567", &f));

    /* 10. fuzz: random bytes biased towards the delimiter characters; never crash,
     *     and any frame that *does* get accepted must satisfy the invariants */
    srand(12345);
    static const char interesting[] = "$*,\r\n0123456789ABCDEFPAT.-";
    long accepted = 0;
    bled_parser_init(&p);
    for (long k = 0; k < 3000000; k++) {
        uint8_t b = (rand() % 4 == 0) ? (uint8_t)rand() : (uint8_t)interesting[rand() % (sizeof(interesting) - 1)];
        if (bled_parser_feed(&p, b, &m)) {
            accepted++;
            CHECK(m.argc >= 0 && m.argc <= BLED_PROTO_MAX_ARGS && strlen(m.type) >= 1 && strlen(m.type) < sizeof(m.type));
        }
    }
    printf("fuzz: 3,000,000 bytes, %ld accidental valid frames, crc_errors=%u format_errors=%u\n",
           accepted, (unsigned)p.crc_errors, (unsigned)p.format_errors);

    /* fuzz 2: valid frames with random single-byte corruption must never be accepted unless CRC still matches */
    long escaped = 0, trials = 200000;
    for (long k = 0; k < trials; k++) {
        char body[40]; snprintf(body, sizeof body, "PAD,%d,%d.%d", rand() % 200, rand() % 500, rand() % 10);
        bled_proto_encode(out, sizeof out, body);
        size_t len = strlen(out);
        size_t pos = 1 + (size_t)rand() % (len - 3);               /* anywhere between '$' and before CRLF */
        char orig = out[pos]; char c;
        do { c = (char)(rand() % 94 + 33); } while (c == orig || c == '$');
        out[pos] = c;
        bled_parser_init(&p);
        if (feed_str(&p, out, &m)) escaped++;
    }
    printf("single-byte corruption: %ld of %ld escaped detection (expected 0)\n", escaped, trials);
    CHECK(escaped == 0);

    printf(fails ? "\nPROTO TESTS FAILED (%d)\n" : "\nPROTO TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
