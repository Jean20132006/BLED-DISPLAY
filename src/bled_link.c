/**
 * @file bled_link.c
 * @brief See bled_link.h.
 *
 * Only the raw UART read/write is ESP-IDF specific (and only compiled when
 * BLED_LINK_SIMULATE == 0).  Frame handling, decoding, timeouts and the
 * simulator are plain C and are exercised by host tests in both modes.
 */
#include "bled_link.h"
#include "bled_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"

#if defined(ESP_PLATFORM) && !BLED_LINK_SIMULATE
#include "driver/uart.h"
#include "driver/gpio.h"
#endif

static const char *TAG = "bled_link";

static QueueHandle_t s_events;
static bled_parser_t s_parser;                 /* used by the receive path only */
static volatile uint32_t s_last_rx;            /* tick of the last checksum-valid frame */
static volatile bool s_ever_rx;
static volatile uint32_t s_frames_ok, s_crc_errors, s_bad_msgs;
static uint32_t s_poll_now;
#if !BLED_LINK_SIMULATE
static uint32_t s_next_ping;
#endif

static uint32_t now_ticks(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)xTaskGetTickCount();      /* CONFIG_FREERTOS_HZ=1000, so 1 tick = 1 ms */
#else
    return s_poll_now;                         /* host tests drive time explicitly */
#endif
}

static void push(const bled_event_t *e)
{
    if (xQueueSend(s_events, e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event %d", (int)e->type);
    }
}

/* ---------------------------------------------------------------------- */
/* Decoding (syntax only - range/plausibility checks live in bled_core.c) */
/* ---------------------------------------------------------------------- */

bool bled_link_decode(const bled_msg_t *m, bled_event_t *e)
{
    int a, b, c, d;
    float f;
    memset(e, 0, sizeof(*e));

    if (strcmp(m->type, "STAT") == 0 && m->argc >= 4) {            /* extra args are ignored: forward compatible */
        if (!bled_arg_int(m->argv[0], &a) || !bled_arg_int(m->argv[1], &b) || !bled_arg_int(m->argv[2], &c) ||
            !bled_arg_int(m->argv[3], &d)) {
            return false;
        }
        if (a < 0 || a > 3 || (b != 0 && b != 1) || (c != 0 && c != 1) || d < -1 || d > 100) {
            return false;
        }
        e->type = BLED_EVT_STATUS;
        e->u.status.state = (bled_unit_state_t)a;
        e->u.status.cartridge = (b == 1);
        e->u.status.on_battery = (c == 1);
        e->u.status.battery_pct = d;
        return true;
    }
    if (strcmp(m->type, "BPRG") == 0 && m->argc == 1) {
        if (!bled_arg_int(m->argv[0], &a) || a < 0 || a > 100) {
            return false;
        }
        e->type = BLED_EVT_BASE_PROGRESS;
        e->u.progress.pct = a;
        return true;
    }
    if (strcmp(m->type, "BDON") == 0 && m->argc == 3) {
        if (!bled_arg_int(m->argv[0], &a) || (a != 0 && a != 1) || !bled_arg_int(m->argv[1], &b) ||
            !bled_arg_int(m->argv[2], &c)) {
            return false;
        }
        e->type = BLED_EVT_BASE_DONE;
        e->u.base.ok = (a == 1);
        e->u.base.i540 = b;
        e->u.base.i850 = c;
        return true;
    }
    if (strcmp(m->type, "PAD") == 0 && m->argc == 2) {
        if (!bled_arg_int(m->argv[0], &a) || a < 1 || a > 9999 || !bled_arg_float(m->argv[1], &f)) {
            return false;
        }
        e->type = BLED_EVT_PAD;
        e->u.pad.number = a;
        e->u.pad.ebl_ml = f;
        return true;
    }
    if (strcmp(m->type, "ERR") == 0 && m->argc == 1) {
        if (!bled_arg_int(m->argv[0], &a)) {
            return false;
        }
        e->type = BLED_EVT_ERROR;
        e->u.error.code = a;
        return true;
    }
    return false;
}

/* one received byte (called from the UART task on the device, from tests on the host) */
static void on_byte(uint8_t byte)
{
    bled_msg_t msg;
    uint32_t crc_before = s_parser.crc_errors;
    bool got = bled_parser_feed(&s_parser, byte, &msg);
    s_crc_errors = s_parser.crc_errors;
    (void)crc_before;
    if (!got) {
        return;
    }
    s_frames_ok = s_parser.frames_ok;
    s_last_rx = now_ticks();                   /* any checksum-valid frame proves the unit is alive */
    s_ever_rx = true;
    bled_event_t evt;
    if (bled_link_decode(&msg, &evt)) {
        push(&evt);
    } else {
        s_bad_msgs++;
        ESP_LOGW(TAG, "ignoring message %s (unknown or invalid fields)", msg.type);
    }
}

/* ---------------------------------------------------------------------- */
/* Transport                                                              */
/* ---------------------------------------------------------------------- */

#if defined(ESP_PLATFORM) && !BLED_LINK_SIMULATE

/* UART setup runs in its own task pinned to core 1, a short time after start-up, so that it never competes with
 * the LCD / LVGL initialisation.  The pins are routed explicitly and RX gets a pull-up so that an unconnected
 * line idles high.  s_uart_ready stops transport_write() from using the driver before it exists. */
static volatile bool s_uart_ready;

static void rx_loop(void)
{
    uint8_t buf[64];
    for (;;) {
        int n = uart_read_bytes(BLED_LINK_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            on_byte(buf[i]);
        }
    }
}

static void uart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));               /* let the LCD / LVGL finish starting */
    const uart_config_t cfg = {
        .baud_rate = BLED_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(BLED_LINK_UART_NUM, 1024, 1024, 0, NULL, 0) != ESP_OK ||
        uart_param_config(BLED_LINK_UART_NUM, &cfg) != ESP_OK ||
        uart_set_pin(BLED_LINK_UART_NUM, BLED_LINK_PIN_TX, BLED_LINK_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "UART setup failed - the measurement-unit link is disabled");
        vTaskDelete(NULL);
    }
    gpio_set_pull_mode(BLED_LINK_PIN_RX, GPIO_PULLUP_ONLY);   /* idle level high if RX is unconnected */
    s_uart_ready = true;
    ESP_LOGI(TAG, "UART%d ready (TX IO%d, RX IO%d)", BLED_LINK_UART_NUM, BLED_LINK_PIN_TX, BLED_LINK_PIN_RX);
    rx_loop();
}

static esp_err_t transport_init(void)
{
    return xTaskCreatePinnedToCore(uart_task, "bled_uart", 4096, NULL, 5, NULL, 1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static void transport_write(const char *data, size_t n)
{
    if (!s_uart_ready) {
        return;
    }
    uart_write_bytes(BLED_LINK_UART_NUM, data, n);
}

#elif !defined(ESP_PLATFORM)

static char s_host_tx[1024];
static size_t s_host_tx_len;

static esp_err_t transport_init(void) { return ESP_OK; }

#if !BLED_LINK_SIMULATE
static void transport_write(const char *data, size_t n)
{
    if (s_host_tx_len + n < sizeof(s_host_tx)) {
        memcpy(s_host_tx + s_host_tx_len, data, n);
        s_host_tx_len += n;
    }
}
#endif

void bled_link_host_rx(const char *bytes)
{
    for (; *bytes; bytes++) {
        on_byte((uint8_t)*bytes);
    }
}

size_t bled_link_host_take_tx(char *out, size_t cap)
{
    size_t n = s_host_tx_len < cap - 1 ? s_host_tx_len : cap - 1;
    memcpy(out, s_host_tx, n);
    out[n] = '\0';
    s_host_tx_len = 0;
    return n;
}

#else  /* device, simulated unit: the UART is never touched */

static esp_err_t transport_init(void) { return ESP_OK; }

#endif

#if !BLED_LINK_SIMULATE
static void send_body(const char *body)
{
    char frame[BLED_PROTO_ENCODED_MAX];
    size_t n = bled_proto_encode(frame, sizeof(frame), body);
    if (n > 0) {
        transport_write(frame, n);
    } else {
        ESP_LOGE(TAG, "cannot encode message \"%s\"", body);
    }
}
#endif

/* ---------------------------------------------------------------------- */
/* Simulated measurement unit                                             */
/* ---------------------------------------------------------------------- */
#if BLED_LINK_SIMULATE

static struct {
    bled_unit_state_t state;
    int base_pct;
    int pad_no;
    uint32_t next_status;
    uint32_t next_base_step;
    bool started;
} sim;

static void sim_push_status(void)
{
    bled_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = BLED_EVT_STATUS;
    e.u.status.state = sim.state;
    e.u.status.cartridge = true;
    e.u.status.on_battery = false;
    e.u.status.battery_pct = 87;
    push(&e);
    s_last_rx = now_ticks();
    s_ever_rx = true;
}

static void sim_poll(uint32_t now)
{
    if (!sim.started) {
        sim.started = true;
        sim.next_status = now;
    }
    if ((int32_t)(now - sim.next_status) >= 0) {
        sim_push_status();
        sim.next_status = now + 1000;
    }
    if (sim.state == BLED_UNIT_BASELINE && (int32_t)(now - sim.next_base_step) >= 0) {
        sim.base_pct += 10;
        sim.next_base_step = now + 300;
        bled_event_t e;
        memset(&e, 0, sizeof(e));
        e.type = BLED_EVT_BASE_PROGRESS;
        e.u.progress.pct = sim.base_pct > 100 ? 100 : sim.base_pct;
        push(&e);
        if (sim.base_pct >= 100) {
            memset(&e, 0, sizeof(e));
            e.type = BLED_EVT_BASE_DONE;
            e.u.base.ok = true;
            e.u.base.i540 = 48213;             /* plausible raw sensor counts, purely illustrative */
            e.u.base.i850 = 51877;
            push(&e);
            sim.state = BLED_UNIT_IDLE;
            sim_push_status();
        }
    }
}

void bled_link_sim_inject_pad(void)
{
    static const float table[] = { 38.5f, 41.2f, 29.8f, 52.4f, 35.0f, 44.6f, 31.7f, 47.3f };
    if (sim.state != BLED_UNIT_CASE) {
        return;                                /* a real unit only reports pads while a case is active */
    }
    bled_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = BLED_EVT_PAD;
    e.u.pad.number = ++sim.pad_no;
    e.u.pad.ebl_ml = table[(sim.pad_no - 1) % (int)(sizeof(table) / sizeof(table[0]))];
    push(&e);
}

#else
void bled_link_sim_inject_pad(void) { }
#endif

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t bled_link_init(void)
{
    s_events = xQueueCreate(16, sizeof(bled_event_t));
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }
    bled_parser_init(&s_parser);
    esp_err_t err = transport_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "link transport init failed: %s", esp_err_to_name(err));
        return err;
    }
#if BLED_LINK_SIMULATE
    ESP_LOGW(TAG, "SIMULATED measurement unit active (BLED_LINK_SIMULATE = 1)");
#else
    ESP_LOGI(TAG, "measurement link on UART%d TX=IO%d RX=IO%d @ %d baud", BLED_LINK_UART_NUM, BLED_LINK_PIN_TX,
             BLED_LINK_PIN_RX, BLED_LINK_BAUD);
#endif
    return ESP_OK;
}

void bled_link_poll(uint32_t now_ms)
{
    s_poll_now = now_ms;
#if BLED_LINK_SIMULATE
    sim_poll(now_ms);
#else
    if ((int32_t)(now_ms - s_next_ping) >= 0) {
        send_body("PING");
        s_next_ping = now_ms + BLED_LINK_PING_MS;
    }
#endif
}

QueueHandle_t bled_link_get_event_queue(void)
{
    return s_events;
}

bool bled_link_is_simulated(void)
{
    return BLED_LINK_SIMULATE != 0;
}

void bled_link_get_info(bled_link_info_t *out)
{
    out->simulated = bled_link_is_simulated();
    out->frames_ok = s_frames_ok;
    out->crc_errors = s_crc_errors;
    out->bad_messages = s_bad_msgs;
#if BLED_LINK_SIMULATE
    out->connected = true;
#else
    out->connected = s_ever_rx && (uint32_t)(now_ticks() - s_last_rx) < BLED_LINK_TIMEOUT_MS;
#endif
}

void bled_link_cmd_baseline(void)
{
#if BLED_LINK_SIMULATE
    sim.state = BLED_UNIT_BASELINE;
    sim.base_pct = 0;
    sim.next_base_step = s_poll_now + 300;
    sim_push_status();
#else
    send_body("BASE");
#endif
}

void bled_link_cmd_case_start(const char *id, float hb_gdl)
{
#if BLED_LINK_SIMULATE
    (void)id;
    (void)hb_gdl;
    sim.state = BLED_UNIT_CASE;
    sim.pad_no = 0;
    sim_push_status();
#else
    /* Bounded on purpose: the id is copied into a buffer of known size and the Hb is formatted from a
     * clamped integer (tenths of g/dL), so the frame body can never exceed BLED_PROTO_MAX_BODY. */
    char idbuf[BLED_CASE_ID_MAX + 1];
    size_t len = strlen(id);
    if (len > BLED_CASE_ID_MAX) {
        len = BLED_CASE_ID_MAX;
    }
    memcpy(idbuf, id, len);
    idbuf[len] = '\0';
    int tenths = (int)(hb_gdl * 10.0f + 0.5f);
    if (tenths < 0) {
        tenths = 0;
    } else if (tenths > 999) {
        tenths = 999;
    }
    char body[BLED_PROTO_MAX_BODY + 1];
    snprintf(body, sizeof(body), "CASE,%s,%d.%d", idbuf, tenths / 10, tenths % 10);
    send_body(body);
#endif
}

void bled_link_cmd_case_end(void)
{
#if BLED_LINK_SIMULATE
    sim.state = BLED_UNIT_IDLE;
    sim_push_status();
#else
    send_body("ENDC");
#endif
}
