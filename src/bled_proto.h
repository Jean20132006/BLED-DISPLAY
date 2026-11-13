/**
 * @file bled_proto.h
 * @brief Wire format for the display <-> measurement-unit UART link.
 *
 * PURE C, NO OS / ESP-IDF DEPENDENCIES - copy bled_proto.c/.h unchanged into
 * the measurement unit's firmware so both ends frame and check messages the
 * same way (SRD MCU-6: every UART message carries a checksum).
 *
 * Frame (ASCII, one per line, easy to debug with any serial terminal):
 *
 *     $<TYPE>[,<arg>[,<arg>...]]*<CRC>\r\n
 *
 *   - '$'    start of frame (also re-synchronises the parser mid-garbage)
 *   - TYPE   1-7 upper-case letters / digits
 *   - args   comma separated, no spaces, no commas inside an argument
 *   - '*'    separates body from checksum
 *   - CRC    4 upper-case hex digits: CRC-16/CCITT-FALSE (poly 0x1021,
 *            init 0xFFFF, no reflection, no final XOR) over every byte
 *            between '$' and '*' (exclusive).  Check value:
 *            bled_crc16("123456789") == 0x29B1.
 *
 *   Example:  $PAD,3,41.2*XXXX\r\n   (XXXX = CRC of "PAD,3,41.2")
 *
 * Message catalogue (see docs/BLED_LINK_PROTOCOL.md for the full table):
 *
 *   display -> unit        PING                  heartbeat, every 1 s
 *                          BASE                  run the optical baseline
 *                          CASE,<id>,<hb_gdl>    case started (patient baseline Hb)
 *                          ENDC                  case ended
 *
 *   unit -> display        STAT,<state>,<cart>,<pwr>,<batt>   >= 1 per second
 *                                state 0 idle, 1 baseline running, 2 case active, 3 error
 *                                cart  0 none, 1 cartridge loaded
 *                                pwr   0 AC, 1 battery
 *                                batt  0..100 (%), -1 unknown
 *                          BPRG,<pct>            baseline progress 0..100
 *                          BDON,<ok>,<i540>,<i850>   baseline finished
 *                          PAD,<n>,<ebl_ml>      pad n measured: blood volume in mL
 *                          ERR,<code>            error report
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLED_PROTO_MAX_FRAME   96      /* longest accepted body+'*'+CRC, bytes */
#define BLED_PROTO_MAX_BODY    (BLED_PROTO_MAX_FRAME - 5)
#define BLED_PROTO_MAX_ARGS    8
#define BLED_PROTO_ENCODED_MAX (BLED_PROTO_MAX_FRAME + 5)   /* '$' + frame + "\r\n" + NUL */

/** CRC-16/CCITT-FALSE. */
uint16_t bled_crc16(const uint8_t *data, size_t len);

/**
 * Build a complete frame from a body such as "PAD,3,41.2".
 * @return frame length in bytes (without the NUL), or 0 if the body is
 *         empty/too long/contains characters that are not allowed, or the
 *         output buffer is too small (needs BLED_PROTO_ENCODED_MAX).
 */
size_t bled_proto_encode(char *out, size_t out_size, const char *body);

typedef struct {
    char        type[8];
    int         argc;
    const char *argv[BLED_PROTO_MAX_ARGS];   /* point into raw[] */
    char        raw[BLED_PROTO_MAX_FRAME];
} bled_msg_t;

typedef struct {
    char     buf[BLED_PROTO_MAX_FRAME];
    uint16_t len;
    bool     in_frame;
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t format_errors;    /* overlong frames, bad TYPE, too many args ... */
} bled_parser_t;

void bled_parser_init(bled_parser_t *p);

/**
 * Feed one received byte.  Returns true exactly when this byte completed a
 * valid frame, in which case *msg is filled in.  Invalid frames are dropped
 * and counted; the parser then waits for the next '$'.
 */
bool bled_parser_feed(bled_parser_t *p, uint8_t byte, bled_msg_t *msg);

/** Strict integer parse: optional '-', 1..9 digits, nothing else. */
bool bled_arg_int(const char *s, int *out);

/** Strict decimal parse: optional '-', digits, optional '.digits' (no exponent, no nan/inf). */
bool bled_arg_float(const char *s, float *out);

#ifdef __cplusplus
}
#endif
