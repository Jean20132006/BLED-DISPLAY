/**
 * @file bled_proto.c
 * @brief See bled_proto.h.  Pure C: no ESP-IDF, no FreeRTOS, no LVGL.
 */
#include "bled_proto.h"

#include <string.h>

uint16_t bled_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Characters allowed inside a frame body. Deliberately small: letters,
 * digits and a few separators. No spaces, '$', '*' or control characters,
 * so a frame can never be confused with its own delimiters. */
static bool body_char_ok(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == ',' || c == '.' || c == '-' || c == '_' || c == '+' || c == ':';
}

size_t bled_proto_encode(char *out, size_t out_size, const char *body)
{
    if (!out || !body) {
        return 0;
    }
    size_t n = strlen(body);
    if (n == 0 || n > BLED_PROTO_MAX_BODY) {
        return 0;
    }
    /* '$' + body + '*' + 4 hex + "\r\n" + NUL */
    if (out_size < n + 9) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (!body_char_ok(body[i])) {
            return 0;
        }
    }

    static const char hex[] = "0123456789ABCDEF";
    uint16_t crc = bled_crc16((const uint8_t *)body, n);

    size_t k = 0;
    out[k++] = '$';
    memcpy(out + k, body, n);
    k += n;
    out[k++] = '*';
    out[k++] = hex[(crc >> 12) & 0xF];
    out[k++] = hex[(crc >> 8) & 0xF];
    out[k++] = hex[(crc >> 4) & 0xF];
    out[k++] = hex[crc & 0xF];
    out[k++] = '\r';
    out[k++] = '\n';
    out[k] = '\0';
    return k;
}

void bled_parser_init(bled_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

/* Upper-case hex only, exactly as the frame format specifies: accepting
 * 'a'..'f' as well would let two different byte sequences spell the same
 * frame, which is harmless but needlessly loose for a checksummed link. */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Validate checksum + syntax of the collected frame and split it into
 * type + arguments. Returns true if *msg is now valid. */
static bool finish_frame(bled_parser_t *p, bled_msg_t *msg)
{
    size_t n = p->len;
    if (n > 0 && p->buf[n - 1] == '\r') {
        n--;                                  /* accept both CRLF and bare LF line ends */
    }
    /* at least one body character, then '*', then 4 hex digits */
    if (n < 6 || p->buf[n - 5] != '*') {
        p->format_errors++;
        return false;
    }

    uint16_t received = 0;
    for (size_t i = n - 4; i < n; i++) {
        int v = hex_value(p->buf[i]);
        if (v < 0) {
            p->format_errors++;
            return false;
        }
        received = (uint16_t)((received << 4) | (uint16_t)v);
    }

    size_t body_len = n - 5;
    if (bled_crc16((const uint8_t *)p->buf, body_len) != received) {
        p->crc_errors++;
        return false;
    }

    for (size_t i = 0; i < body_len; i++) {
        if (!body_char_ok(p->buf[i])) {
            p->format_errors++;
            return false;
        }
    }

    memcpy(msg->raw, p->buf, body_len);
    msg->raw[body_len] = '\0';

    /* Split on commas in place: first field is TYPE, the rest are args. */
    char *field = msg->raw;
    int count = 0;
    const char *fields[BLED_PROTO_MAX_ARGS + 1];
    for (;;) {
        if (count > BLED_PROTO_MAX_ARGS) {
            p->format_errors++;               /* too many arguments */
            return false;
        }
        fields[count++] = field;
        char *comma = strchr(field, ',');
        if (!comma) {
            break;
        }
        *comma = '\0';
        field = comma + 1;
    }

    size_t type_len = strlen(fields[0]);
    if (type_len == 0 || type_len >= sizeof(msg->type)) {
        p->format_errors++;
        return false;
    }
    for (size_t i = 0; i < type_len; i++) {
        char c = fields[0][i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            p->format_errors++;
            return false;
        }
    }
    memcpy(msg->type, fields[0], type_len + 1);

    msg->argc = count - 1;
    for (int i = 0; i < msg->argc; i++) {
        msg->argv[i] = fields[i + 1];
    }
    for (int i = msg->argc; i < BLED_PROTO_MAX_ARGS; i++) {
        msg->argv[i] = "";
    }
    return true;
}

bool bled_parser_feed(bled_parser_t *p, uint8_t byte, bled_msg_t *msg)
{
    if (byte == '$') {                         /* start (or restart) of a frame */
        p->len = 0;
        p->in_frame = true;
        return false;
    }
    if (!p->in_frame) {
        return false;                          /* garbage between frames */
    }
    if (byte == '\n') {
        p->in_frame = false;
        bool ok = finish_frame(p, msg);
        if (ok) {
            p->frames_ok++;
        }
        return ok;
    }
    if (p->len >= BLED_PROTO_MAX_FRAME) {      /* runaway frame: drop it */
        p->in_frame = false;
        p->format_errors++;
        return false;
    }
    p->buf[p->len++] = (char)byte;
    return false;
}

bool bled_arg_int(const char *s, int *out)
{
    if (!s || !out) {
        return false;
    }
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (*s == '\0') {
        return false;
    }
    long v = 0;
    int digits = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || ++digits > 9) {
            return false;
        }
        v = v * 10 + (*s - '0');
    }
    *out = (int)(neg ? -v : v);
    return true;
}

bool bled_arg_float(const char *s, float *out)
{
    if (!s || !out) {
        return false;
    }
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (*s < '0' || *s > '9') {
        return false;                          /* a leading digit is required: ".5" is rejected */
    }
    long ip = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        if (++digits > 9) {
            return false;
        }
        ip = ip * 10 + (*s - '0');
        s++;
    }
    double v = (double)ip;
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') {
            return false;                      /* "5." is rejected */
        }
        long frac = 0;
        double scale = 1.0;
        int fdigits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++fdigits > 6) {
                return false;
            }
            frac = frac * 10 + (*s - '0');
            scale *= 10.0;
            s++;
        }
        v += (double)frac / scale;
    }
    if (*s != '\0') {
        return false;
    }
    *out = (float)(neg ? -v : v);
    return true;
}
