/**
 * @file bled_link.h
 * @brief Link to the BLED measurement unit (the second ESP32-S3 with the
 *        optical sensor board).  Wire format: see bled_proto.h.
 *
 * Two interchangeable back ends, selected by BLED_LINK_SIMULATE in
 * bled_config.h:
 *   - real:      UART0 on the CrowPanel's UART0-OUT port (RX IO44, TX IO43),
 *                a background task receives and parses frames.
 *   - simulated: a small state machine that behaves like a measurement unit
 *                so the whole UI can be demonstrated without any wiring.
 *
 * Thread-safety rule (same as everywhere in this project): LVGL is not
 * thread-safe.  The receive task only pushes bled_event_t items into a queue;
 * bled_core.c drains that queue from inside an LVGL timer.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "bled_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLED_UNIT_IDLE = 0,
    BLED_UNIT_BASELINE = 1,     /* optical baseline running */
    BLED_UNIT_CASE = 2,         /* a case is active: ready for / measuring pads */
    BLED_UNIT_ERROR = 3,
} bled_unit_state_t;

typedef enum {
    BLED_EVT_STATUS,            /* periodic status */
    BLED_EVT_BASE_PROGRESS,
    BLED_EVT_BASE_DONE,
    BLED_EVT_PAD,
    BLED_EVT_ERROR,
} bled_event_type_t;

typedef struct {
    bled_event_type_t type;
    union {
        struct { bled_unit_state_t state; bool cartridge; bool on_battery; int battery_pct; } status;
        struct { int pct; } progress;
        struct { bool ok; int i540; int i850; } base;
        struct { int number; float ebl_ml; } pad;
        struct { int code; } error;
    } u;
} bled_event_t;

typedef struct {
    bool     connected;         /* a valid frame arrived within BLED_LINK_TIMEOUT_MS */
    bool     simulated;
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t bad_messages;      /* checksum fine, but unknown type or invalid fields */
} bled_link_info_t;

esp_err_t bled_link_init(void);

/** Call every ~100 ms (from an LVGL timer): heartbeat, timeouts and the simulator. */
void bled_link_poll(uint32_t now_ms);

QueueHandle_t bled_link_get_event_queue(void);
void bled_link_get_info(bled_link_info_t *out);
bool bled_link_is_simulated(void);

/* commands to the unit (non-blocking; fire and forget - bled_core.c re-sends when the status disagrees) */
void bled_link_cmd_baseline(void);
void bled_link_cmd_case_start(const char *id, float hb_gdl);
void bled_link_cmd_case_end(void);

/** Simulated unit only: pretend a pad was just measured (no-op with the real link). */
void bled_link_sim_inject_pad(void);

/** Turn a checksummed message into an event; false if unknown type or invalid fields. */
bool bled_link_decode(const bled_msg_t *msg, bled_event_t *evt);

#ifndef ESP_PLATFORM
/* host test hooks: bytes "received" from the unit, and the frames the display transmitted */
void   bled_link_host_rx(const char *bytes);
size_t bled_link_host_take_tx(char *out, size_t cap);
#endif

#ifdef __cplusplus
}
#endif
