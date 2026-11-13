/**
 * @file bled_core.c
 * @brief See bled_core.h.
 */
#include "bled_core.h"
#include "bled_storage.h"
#include "crowpanel_bsp.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "bled_core";

static bled_system_t s_sys;
static bled_case_t s_case;
static uint32_t s_rev;
static uint32_t s_now;

static int s_last_unit_pad;                     /* last pad number the unit reported in this case */
static int s_saved_pads;                        /* pads already written to the SD file */
static float s_pad_ml[BLED_MAX_PADS];
static bled_stamp_t s_pad_t[BLED_MAX_PADS];

static uint32_t s_base_started, s_base_last_cmd;
static int s_base_attempts;
static bool s_base_seen;
static uint32_t s_last_case_cmd, s_next_rtc_check, s_next_flush;
static uint32_t s_start_ms;

/* bounded copy that never trips -Wstringop-truncation / -Wformat-truncation */
static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void touch(void)
{
    s_rev++;
}

/* ---------------------------------------------------------------------- */
/* Small helpers                                                          */
/* ---------------------------------------------------------------------- */

bool bled_id_valid(const char *id)
{
    if (!id) {
        return false;
    }
    size_t n = strlen(id);
    if (n < 1 || n > BLED_CASE_ID_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)id[i];
        if (!(isalnum(c) || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

bool bled_hb_valid(float hb)
{
    return hb >= BLED_HB_MIN_GDL && hb <= BLED_HB_MAX_GDL;      /* false for NaN, too */
}

int bled_ebl_level(float ml)
{
    if (ml >= BLED_EBL_ALERT_ML) {
        return 2;
    }
    if (ml >= BLED_EBL_WARN_ML) {
        return 1;
    }
    return 0;
}

bled_stamp_t bled_now(void)
{
    bled_stamp_t s;
    memset(&s, 0, sizeof(s));
    pcf8563_time_t t;
    if (bsp_rtc_get_time(&t) == ESP_OK) {
        s.year = t.year;
        s.month = t.month;
        s.day = t.day;
        s.hour = t.hour;
        s.minute = t.minute;
        s.second = t.second;
        if (!bled_stamp_valid(&s)) {
            memset(&s, 0, sizeof(s));              /* a fresh/dead-battery RTC reads zeros */
        }
    }
    return s;
}

int32_t bled_case_elapsed_s(void)
{
    if (!s_case.active) {
        return -1;
    }
    bled_stamp_t now = bled_now();
    if (bled_stamp_valid(&s_case.start) && bled_stamp_valid(&now)) {
        int32_t d = bled_stamp_diff_s(&s_case.start, &now);
        if (d >= 0) {
            return d;
        }
    }
    return (int32_t)((s_now - s_start_ms) / 1000u);    /* RTC unavailable: fall back to the tick clock */
}

const char *bled_start_result_text(bled_start_result_t r)
{
    switch (r) {
        case BLED_START_OK:           return "OK";
        case BLED_START_ERR_ACTIVE:   return "A case is already active";
        case BLED_START_ERR_ID:       return "Invalid case number";
        case BLED_START_ERR_HB:       return "Baseline Hb out of range";
        case BLED_START_ERR_BASELINE: return "Run the system baseline first";
        case BLED_START_ERR_UNIT:     return "Measurement unit offline";
    }
    return "Unknown error";
}

const char *bled_unit_state_text(bled_unit_state_t s)
{
    switch (s) {
        case BLED_UNIT_IDLE:     return "Idle";
        case BLED_UNIT_BASELINE: return "Baseline running";
        case BLED_UNIT_CASE:     return "Case active";
        case BLED_UNIT_ERROR:    return "Error";
    }
    return "?";
}

const char *bled_error_text(int code)
{
    static char buf[24];
    switch (code) {
        case 1:  return "No cartridge";
        case 2:  return "Sensor fault";
        case 3:  return "Reagent empty";
        case 4:  return "Optical over-range";
        default: snprintf(buf, sizeof(buf), "Unit error %d", code); return buf;
    }
}

const bled_system_t *bled_sys(void) { return &s_sys; }
const bled_case_t *bled_case(void) { return &s_case; }
uint32_t bled_core_revision(void) { return s_rev; }

/* ---------------------------------------------------------------------- */
/* SD persistence of the active case                                      */
/* ---------------------------------------------------------------------- */

/* Make the SD file match the model: create it if needed, then append any pads it is missing.
 * Safe to call repeatedly - this is also how the file heals after the card was out for a while. */
static bool flush_case(void)
{
    if (!s_case.active) {
        return true;
    }
    bool was_warning = s_case.sd_warning;
    bool ok = true;
    if (!bled_storage_ready()) {
        ok = false;
    } else {
        if (!s_case.persisted) {
            bled_case_header_t h = { s_case.id, s_case.hb_gdl, s_case.start, s_sys.base_i540, s_sys.base_i850 };
            if (bled_storage_case_create(&h, s_case.file)) {
                s_case.persisted = true;
                s_saved_pads = 0;
            } else {
                ok = false;
            }
        }
        while (ok && s_saved_pads < s_case.pad_count) {
            int i = s_saved_pads;
            if (bled_storage_case_append_pad(s_case.file, i + 1, &s_pad_t[i], s_pad_ml[i])) {
                s_saved_pads++;
            } else {
                ok = false;
            }
        }
    }
    s_case.sd_warning = !ok;
    if (was_warning != s_case.sd_warning) {
        touch();
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Baseline                                                               */
/* ---------------------------------------------------------------------- */

static void base_fail(const char *why)
{
    s_sys.base_state = BLED_BASE_FAILED;
    copy_str(s_sys.base_msg, sizeof(s_sys.base_msg), why);
    ESP_LOGW(TAG, "baseline failed: %s", why);
    touch();
}

bool bled_baseline_start(void)
{
    if (s_case.active) {
        return false;
    }
    if (!s_sys.unit_online) {
        base_fail("Measurement unit offline");
        return false;
    }
    s_sys.base_state = BLED_BASE_RUNNING;
    s_sys.base_pct = 0;
    s_sys.base_msg[0] = '\0';
    s_base_started = s_now;
    s_base_last_cmd = s_now;
    s_base_attempts = 1;
    s_base_seen = false;
    bled_link_cmd_baseline();
    touch();
    return true;
}

/* ---------------------------------------------------------------------- */
/* Case actions                                                           */
/* ---------------------------------------------------------------------- */

bled_start_result_t bled_case_start(const bled_draft_t *d)
{
    if (s_case.active) {
        return BLED_START_ERR_ACTIVE;
    }
    if (!d || !bled_id_valid(d->id)) {
        return BLED_START_ERR_ID;
    }
    if (!bled_hb_valid(d->hb_gdl)) {
        return BLED_START_ERR_HB;
    }
    if (s_sys.base_state != BLED_BASE_DONE) {
        return BLED_START_ERR_BASELINE;
    }
    if (!s_sys.unit_online) {
        return BLED_START_ERR_UNIT;
    }

    memset(&s_case, 0, sizeof(s_case));
    s_case.active = true;
    copy_str(s_case.id, sizeof(s_case.id), d->id);
    s_case.hb_gdl = d->hb_gdl;
    s_case.start = bled_now();
    s_start_ms = s_now;
    s_last_unit_pad = 0;
    s_saved_pads = 0;

    bled_link_cmd_case_start(s_case.id, s_case.hb_gdl);
    s_last_case_cmd = s_now;
    flush_case();                                    /* creates the SD file; a missing card only raises sd_warning */
    ESP_LOGI(TAG, "case %s started (baseline Hb %.1f g/dL)", s_case.id, (double)s_case.hb_gdl);
    touch();
    return BLED_START_OK;
}

void bled_case_end(void)
{
    if (!s_case.active) {
        return;
    }
    flush_case();
    if (s_case.persisted && bled_storage_ready()) {
        bled_stamp_t end = bled_now();
        bled_storage_case_close(s_case.file, &end);
    }
    bled_link_cmd_case_end();
    ESP_LOGI(TAG, "case %s ended: %d pads, %.1f mL", s_case.id, s_case.pad_count, (double)s_case.total_ebl_ml);
    memset(&s_case, 0, sizeof(s_case));
    s_sys.base_state = BLED_BASE_IDLE;               /* a new case needs a new cartridge and a new baseline */
    s_sys.base_pct = 0;
    touch();
}

void bled_sim_pad(void)
{
    bled_link_sim_inject_pad();
}

/* ---------------------------------------------------------------------- */
/* Events from the measurement unit                                       */
/* ---------------------------------------------------------------------- */

static void case_warn(const char *text)
{
    copy_str(s_case.warning, sizeof(s_case.warning), text);
    touch();
}

static void handle_pad(int number, float ml)
{
    if (!s_case.active) {
        ESP_LOGW(TAG, "pad %d received but no case is active: ignored", number);
        return;
    }
    if (number <= s_last_unit_pad) {                 /* a re-sent frame, not a new pad */
        ESP_LOGW(TAG, "duplicate pad %d ignored", number);
        return;
    }
    if (!(ml >= 0.0f && ml <= BLED_PAD_EBL_MAX_ML)) {
        s_last_unit_pad = number;                    /* do not wait for this pad number again */
        case_warn("Pad value out of range - ignored");
        ESP_LOGW(TAG, "pad %d value %.1f mL rejected", number, (double)ml);
        return;
    }
    if (s_case.pad_count >= BLED_MAX_PADS) {
        case_warn("Pad limit reached - not recorded");
        return;
    }
    s_last_unit_pad = number;
    int i = s_case.pad_count++;
    s_pad_ml[i] = ml;
    s_pad_t[i] = bled_now();
    s_case.total_ebl_ml += ml;
    s_case.last_pad_ml = ml;
    s_case.last_pad_time = s_pad_t[i];
    s_case.warning[0] = '\0';
    flush_case();
    touch();
}

static void handle_event(const bled_event_t *e)
{
    switch (e->type) {
        case BLED_EVT_STATUS:
            s_sys.unit_state = e->u.status.state;
            s_sys.cartridge = e->u.status.cartridge;
            s_sys.on_battery = e->u.status.on_battery;
            s_sys.battery_pct = e->u.status.battery_pct;
            if (e->u.status.state != BLED_UNIT_ERROR) {
                s_sys.unit_error = 0;
            }
            if (e->u.status.state == BLED_UNIT_BASELINE) {
                s_base_seen = true;
            }
            touch();
            break;
        case BLED_EVT_BASE_PROGRESS:
            if (s_sys.base_state == BLED_BASE_RUNNING) {
                s_base_seen = true;
                s_sys.base_pct = e->u.progress.pct;
                touch();
            }
            break;
        case BLED_EVT_BASE_DONE:
            if (s_sys.base_state != BLED_BASE_RUNNING) {
                break;
            }
            if (e->u.base.ok) {
                s_sys.base_state = BLED_BASE_DONE;
                s_sys.base_pct = 100;
                s_sys.base_i540 = e->u.base.i540;
                s_sys.base_i850 = e->u.base.i850;
                s_sys.base_time = bled_now();
                s_sys.base_msg[0] = '\0';
                touch();
            } else {
                base_fail("Unit reported a baseline failure");
            }
            break;
        case BLED_EVT_PAD:
            handle_pad(e->u.pad.number, e->u.pad.ebl_ml);
            break;
        case BLED_EVT_ERROR:
            s_sys.unit_error = e->u.error.code;
            if (s_sys.base_state == BLED_BASE_RUNNING) {
                base_fail(bled_error_text(e->u.error.code));
            }
            touch();
            break;
    }
}

/* ---------------------------------------------------------------------- */
/* Periodic work                                                          */
/* ---------------------------------------------------------------------- */

void bled_core_tick(uint32_t now_ms)
{
    s_now = now_ms;
    bled_link_poll(now_ms);

    bled_link_info_t li;
    bled_link_get_info(&li);
    if (s_sys.unit_online != li.connected || s_sys.unit_simulated != li.simulated) {
        s_sys.unit_online = li.connected;
        s_sys.unit_simulated = li.simulated;
        touch();
    }

    QueueHandle_t q = bled_link_get_event_queue();
    bled_event_t e;
    while (q && xQueueReceive(q, &e, 0) == pdTRUE) {
        handle_event(&e);
    }

    /* baseline supervision: the link has no per-command acknowledgement, so re-send BASE a few times */
    if (s_sys.base_state == BLED_BASE_RUNNING) {
        if (!s_sys.unit_online) {
            base_fail("Measurement unit went offline");
        } else if ((uint32_t)(now_ms - s_base_started) > 90000u) {
            base_fail("Baseline timed out");
        } else if (!s_base_seen && (uint32_t)(now_ms - s_base_last_cmd) >= 2000u) {
            if (s_base_attempts >= 3) {
                base_fail("No response from the unit");
            } else {
                s_base_attempts++;
                bled_link_cmd_baseline();
                s_base_last_cmd = now_ms;
            }
        }
    }

    /* case supervision: if the unit rebooted mid-case it reports "idle" - tell it about the case again */
    if (s_case.active && s_sys.unit_online && s_sys.unit_state == BLED_UNIT_IDLE &&
        (uint32_t)(now_ms - s_last_case_cmd) >= 3000u) {
        bled_link_cmd_case_start(s_case.id, s_case.hb_gdl);
        s_last_case_cmd = now_ms;
    }

    /* keep trying to get unsaved data onto the SD card */
    if (s_case.active && s_case.sd_warning && (int32_t)(now_ms - s_next_flush) >= 0) {
        s_next_flush = now_ms + 3000;
        flush_case();
    }

    if ((int32_t)(now_ms - s_next_rtc_check) >= 0) {
        s_next_rtc_check = now_ms + 1000;
        pcf8563_time_t t;
        bool ok = bsp_rtc_get_time(&t) == ESP_OK;
        if (ok != s_sys.rtc_ok) {
            s_sys.rtc_ok = ok;
            touch();
        }
    }
}

esp_err_t bled_core_init(void)
{
    memset(&s_sys, 0, sizeof(s_sys));
    memset(&s_case, 0, sizeof(s_case));
    s_sys.battery_pct = -1;
    s_sys.base_state = BLED_BASE_IDLE;
    esp_err_t err = bled_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage init failed: %s", esp_err_to_name(err));
    }
    esp_err_t lerr = bled_link_init();
    if (lerr != ESP_OK) {
        ESP_LOGE(TAG, "link init failed: %s", esp_err_to_name(lerr));
    }
    return (err != ESP_OK) ? err : lerr;
}
