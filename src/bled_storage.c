/**
 * @file bled_storage.c
 * @brief See bled_storage.h.
 *
 * Layout of this file:
 *   1. shared state + small helpers
 *   2. case file writing / reading (plain stdio + dirent, identical on the
 *      device and in the host test build)
 *   3. mounting: ESP-IDF SPI SD driver + background task (device only,
 *      #ifdef ESP_PLATFORM) or a directory stand-in (host tests)
 */
#include "bled_storage.h"
#include "bled_proto.h"          /* bled_arg_int / bled_arg_float: strict parsers */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bled_storage";

/* ====================================================================== */
/* 1. State + helpers                                                     */
/* ====================================================================== */

#define ROOT_MAX 64
#define NAME_LEN 12              /* "C0000042.CSV" */

static char s_root[ROOT_MAX] = BLED_SD_MOUNT_POINT;
static volatile bool s_ready;
static bled_sd_info_t s_info = { .state = BLED_SD_ABSENT, .capacity_bytes = 0, .records = -1, .message = "No SD card" };
static SemaphoreHandle_t s_info_lock;     /* protects s_info (written by the mount task) */
static SemaphoreHandle_t s_io_lock;       /* serialises file I/O against unmounting */

static char s_names[BLED_MAX_RECORDS][NAME_LEN + 1];
static int s_name_count;
static int s_name_pos;

/* bounded copy that GCC's truncation warnings never flag (same approach as wifi_manager.c) */
static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void info_set(bled_sd_state_t state, const char *prefix, const char *detail)
{
    xSemaphoreTake(s_info_lock, portMAX_DELAY);
    s_info.state = state;
    char msg[sizeof(s_info.message)];
    copy_str(msg, sizeof(msg), prefix);
    if (detail && detail[0]) {
        size_t used = strlen(msg);
        if (used + 3 < sizeof(msg)) {
            msg[used++] = ':';
            msg[used++] = ' ';
            copy_str(msg + used, sizeof(msg) - used, detail);
        }
    }
    copy_str(s_info.message, sizeof(s_info.message), msg);
    xSemaphoreGive(s_info_lock);
}

static void info_set_records(int n)
{
    xSemaphoreTake(s_info_lock, portMAX_DELAY);
    s_info.records = n;
    xSemaphoreGive(s_info_lock);
}

void bled_storage_get_info(bled_sd_info_t *out)
{
    xSemaphoreTake(s_info_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_info_lock);
}

bool bled_storage_ready(void)
{
    return s_ready;
}

/* directory holding the case files: <root>/BLED */
static void dir_path(char *out, size_t n)
{
    snprintf(out, n, "%s/%s", s_root, BLED_SD_DIR_NAME);
}

/* a file name exactly like C0000042.CSV (digits only, upper or lower case extension) */
static bool is_case_file(const char *name)
{
    if (strlen(name) != NAME_LEN || toupper((unsigned char)name[0]) != 'C') {
        return false;
    }
    for (int i = 1; i <= 7; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    return name[8] == '.' && toupper((unsigned char)name[9]) == 'C' && toupper((unsigned char)name[10]) == 'S' &&
           toupper((unsigned char)name[11]) == 'V';
}

/* Build <root>/BLED/<FILE>. The name is validated and normalised to the upper-case form this module
 * creates, so behaviour is identical on FATFS (case-insensitive) and on a case-sensitive host. */
static bool file_path(char *out, size_t n, const char *file)
{
    if (!file || !is_case_file(file)) {
        return false;
    }
    char up[NAME_LEN + 1];
    for (int i = 0; i < NAME_LEN; i++) {
        up[i] = (char)toupper((unsigned char)file[i]);
    }
    up[NAME_LEN] = '\0';
    char dir[96];
    dir_path(dir, sizeof(dir));
    snprintf(out, n, "%s/%s", dir, up);
    return true;
}

static void sanitize_id(char *dst, size_t dst_size, const char *src)
{
    size_t k = 0;
    for (; src && src[k] && k + 1 < dst_size; k++) {
        unsigned char c = (unsigned char)src[k];
        dst[k] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';   /* never a comma or newline in the CSV */
    }
    dst[k] = '\0';
}

static void mark_lost(const char *why);

/* ====================================================================== */
/* 2. Writing                                                             */
/* ====================================================================== */

static bool ensure_dir(void)
{
    char dir[96];
    dir_path(dir, sizeof(dir));
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir %s failed (errno %d)", dir, errno);
        return false;
    }
    return true;
}

static unsigned next_sequence(void)
{
    char dir[96];
    dir_path(dir, sizeof(dir));
    DIR *d = opendir(dir);
    unsigned max = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (is_case_file(e->d_name)) {
                unsigned v = (unsigned)strtoul(e->d_name + 1, NULL, 10);
                if (v > max) {
                    max = v;
                }
            }
        }
        closedir(d);
    }
    return max + 1;
}

/* flush + fsync + close; false if anything went wrong */
static bool finish_file(FILE *f)
{
    bool ok = (fflush(f) == 0);
    ok = ok && (fsync(fileno(f)) == 0);
    ok = (fclose(f) == 0) && ok;
    return ok;
}

bool bled_storage_case_create(const bled_case_header_t *hdr, char file_out[13])
{
    if (!hdr || !hdr->id || !s_ready) {
        return false;
    }
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    bool ok = false;
    FILE *f = NULL;

    if (!ensure_dir()) {
        goto done;
    }
    unsigned seq = next_sequence();
    if (seq > 9999999u) {
        ESP_LOGE(TAG, "case file numbers exhausted");
        goto done;
    }
    char name[NAME_LEN + 1];
    snprintf(name, sizeof(name), "C%07u.CSV", seq);

    char path[160];
    if (!file_path(path, sizeof(path), name)) {
        goto done;
    }
    f = fopen(path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "cannot create %s (errno %d)", path, errno);
        goto done;
    }

    char id[BLED_CASE_ID_MAX + 1];
    sanitize_id(id, sizeof(id), hdr->id);
    char start[24];
    bled_stamp_format(&hdr->start, start, sizeof(start));
    if (start[0] == '-') {
        copy_str(start, sizeof(start), "0000-00-00 00:00:00");     /* RTC unavailable: keep the line parseable-looking */
    }

    fprintf(f, "BLEDCASE,1\n");
    fprintf(f, "CASE,%s\n", id);
    fprintf(f, "START,%s\n", start);
    fprintf(f, "HB,%.1f\n", (double)hdr->hb_gdl);
    fprintf(f, "BASE,%d,%d\n", hdr->base_i540, hdr->base_i850);
    ok = finish_file(f);
    f = NULL;
    if (ok) {
        copy_str(file_out, 13, name);
        xSemaphoreTake(s_info_lock, portMAX_DELAY);
        if (s_info.records >= 0) {
            s_info.records++;
        }
        xSemaphoreGive(s_info_lock);
    }
done:
    if (f) {
        fclose(f);
    }
    xSemaphoreGive(s_io_lock);
    if (!ok) {
        mark_lost("Write failed");
    }
    return ok;
}

bool bled_storage_case_append_pad(const char *file, int number, const bled_stamp_t *t, float ebl_ml)
{
    char path[160];
    if (!s_ready || !file_path(path, sizeof(path), file) || !t) {
        return false;
    }
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    bool ok = false;
    FILE *f = fopen(path, "ab");
    if (f) {
        char tm[16];
        bled_stamp_format_time(t, tm, sizeof(tm));
        if (tm[0] == '-') {
            copy_str(tm, sizeof(tm), "00:00:00");
        }
        fprintf(f, "PAD,%d,%s,%.1f\n", number, tm, (double)ebl_ml);
        ok = finish_file(f);
    }
    xSemaphoreGive(s_io_lock);
    if (!ok) {
        mark_lost("Write failed");
    }
    return ok;
}

bool bled_storage_case_close(const char *file, const bled_stamp_t *end)
{
    char path[160];
    if (!s_ready || !file_path(path, sizeof(path), file) || !end) {
        return false;
    }
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    bool ok = false;
    FILE *f = fopen(path, "ab");
    if (f) {
        char ts[24];
        bled_stamp_format(end, ts, sizeof(ts));
        if (ts[0] == '-') {
            copy_str(ts, sizeof(ts), "0000-00-00 00:00:00");
        }
        fprintf(f, "END,%s\n", ts);
        ok = finish_file(f);
    }
    xSemaphoreGive(s_io_lock);
    if (!ok) {
        mark_lost("Write failed");
    }
    return ok;
}

/* ====================================================================== */
/* 2b. Reading                                                            */
/* ====================================================================== */

/* Parse one case file.  detail != NULL -> also keep the pad rows. */
static bool parse_file(const char *path, const char *name, bled_record_summary_t *sum, bled_record_detail_t *detail)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    memset(sum, 0, sizeof(*sum));
    copy_str(sum->file, sizeof(sum->file), name);
    bool have_case = false, have_start = false;
    if (detail) {
        detail->n = 0;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *comma = strchr(line, ',');
        if (!comma) {
            continue;
        }
        *comma = '\0';
        const char *key = line;
        char *val = comma + 1;

        if (strcmp(key, "CASE") == 0) {
            sanitize_id(sum->id, sizeof(sum->id), val);
            have_case = sum->id[0] != '\0';
        } else if (strcmp(key, "START") == 0) {
            have_start = bled_stamp_parse(val, &sum->start);
            if (!have_start) {
                have_start = true;                    /* keep the record even if the RTC was unavailable (0000-00-00) */
                memset(&sum->start, 0, sizeof(sum->start));
            }
        } else if (strcmp(key, "HB") == 0) {
            float v;
            if (bled_arg_float(val, &v)) {
                sum->hb_gdl = v;
            }
        } else if (strcmp(key, "BASE") == 0) {
            char *c2 = strchr(val, ',');
            if (c2) {
                *c2 = '\0';
                int a, b;
                if (bled_arg_int(val, &a) && bled_arg_int(c2 + 1, &b)) {
                    sum->base_i540 = a;
                    sum->base_i850 = b;
                }
            }
        } else if (strcmp(key, "PAD") == 0) {
            /* PAD,<n>,<HH:MM:SS>,<mL> - a truncated last line (power cut) simply fails to parse and is skipped */
            char *c2 = strchr(val, ',');
            char *c3 = c2 ? strchr(c2 + 1, ',') : NULL;
            if (!c2 || !c3) {
                continue;
            }
            *c2 = '\0';
            *c3 = '\0';
            int n;
            float ml;
            bled_stamp_t t = sum->start;
            if (!bled_arg_int(val, &n) || !bled_stamp_parse_time(c2 + 1, &t) || !bled_arg_float(c3 + 1, &ml) || ml < 0.0f ||
                ml > BLED_PAD_EBL_MAX_ML) {
                continue;
            }
            if (detail && detail->n < BLED_MAX_PADS) {
                detail->pad_time[detail->n] = t;
                detail->pad_ml[detail->n] = ml;
                detail->n++;
            }
            sum->pads++;
            sum->total_ebl_ml += ml;
        } else if (strcmp(key, "END") == 0) {
            if (bled_stamp_parse(val, &sum->end)) {
                sum->closed = true;
            } else {
                sum->closed = true;                    /* closed, but with an unreadable timestamp */
                memset(&sum->end, 0, sizeof(sum->end));
            }
        }
        /* unknown keys (including BLEDCASE) are ignored on purpose: forward compatible */
    }
    fclose(f);

    if (!have_case || !have_start) {
        return false;                                  /* not one of ours / header missing */
    }
    if (detail) {
        detail->sum = *sum;
    }
    return true;
}

int bled_storage_list_begin(void)
{
    s_name_count = 0;
    s_name_pos = 0;
    if (!s_ready) {
        return 0;
    }
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    char dir[96];
    dir_path(dir, sizeof(dir));
    DIR *d = opendir(dir);
    int total = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!is_case_file(e->d_name)) {
                continue;
            }
            total++;
            char up[NAME_LEN + 1];
            for (int i = 0; i < NAME_LEN; i++) {
                up[i] = (char)toupper((unsigned char)e->d_name[i]);
            }
            up[NAME_LEN] = '\0';

            /* keep the BLED_MAX_RECORDS newest (= highest-numbered) names, sorted newest first */
            int pos = s_name_count;
            if (s_name_count < BLED_MAX_RECORDS) {
                s_name_count++;
            } else if (strcmp(up, s_names[BLED_MAX_RECORDS - 1]) < 0) {
                continue;                                  /* older than everything we already keep */
            } else {
                pos = BLED_MAX_RECORDS - 1;
            }
            while (pos > 0 && strcmp(up, s_names[pos - 1]) > 0) {
                memcpy(s_names[pos], s_names[pos - 1], sizeof(s_names[pos]));
                pos--;
            }
            memcpy(s_names[pos], up, sizeof(up));
        }
        closedir(d);
    }
    xSemaphoreGive(s_io_lock);
    info_set_records(total);
    return s_name_count;
}

bool bled_storage_list_next(bled_record_summary_t *out)
{
    if (!s_ready) {
        return false;
    }
    while (s_name_pos < s_name_count) {
        const char *name = s_names[s_name_pos++];
        char path[160];
        if (!file_path(path, sizeof(path), name)) {
            continue;
        }
        xSemaphoreTake(s_io_lock, portMAX_DELAY);
        bool ok = parse_file(path, name, out, NULL);
        xSemaphoreGive(s_io_lock);
        if (ok) {
            return true;
        }
    }
    return false;
}

bool bled_storage_load(const char *file, bled_record_detail_t *out)
{
    char path[160];
    if (!s_ready || !out || !file_path(path, sizeof(path), file)) {
        return false;
    }
    char name[NAME_LEN + 1];
    for (int i = 0; i < NAME_LEN; i++) {
        name[i] = (char)toupper((unsigned char)file[i]);
    }
    name[NAME_LEN] = '\0';
    bled_record_summary_t sum;
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    bool ok = parse_file(path, name, &sum, out);
    xSemaphoreGive(s_io_lock);
    return ok;
}

/* ====================================================================== */
/* 3. Mounting                                                            */
/* ====================================================================== */

static int count_records(void)
{
    char dir[96];
    dir_path(dir, sizeof(dir));
    DIR *d = opendir(dir);
    int n = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (is_case_file(e->d_name)) {
                n++;
            }
        }
        closedir(d);
    }
    return n;
}

#ifdef ESP_PLATFORM
/* ---------------------------------------------------------------------- */
/* Device: ESP-IDF SPI-mode SD card on the CrowPanel's TF slot            */
/* ---------------------------------------------------------------------- */
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t *s_card;
static bool s_bus_inited;
static TaskHandle_t s_task;

static esp_err_t do_mount(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = BLED_SD_FREQ_KHZ;

    if (!s_bus_inited) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = BLED_SD_PIN_MOSI,
            .miso_io_num = BLED_SD_PIN_MISO,
            .sclk_io_num = BLED_SD_PIN_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        esp_err_t err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_bus_inited = true;
    }

    /* The card's CS line is hard-wired on this board (not a GPIO). The driver still wants a pin to
     * toggle, exactly like Elecrow's own Arduino example, so give it an unused one. */
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = BLED_SD_PIN_CS_DUMMY;
    slot_cfg.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,      /* never format a patient-data card automatically */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdspi_mount(BLED_SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        spi_bus_free(host.slot);
        s_bus_inited = false;
        s_card = NULL;
    }
    return err;
}

static void do_unmount(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(BLED_SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    if (s_bus_inited) {
        spi_bus_free(SPI2_HOST);
        s_bus_inited = false;
    }
}

static void mark_lost(const char *why)
{
    if (!s_ready) {
        return;
    }
    s_ready = false;                           /* stop new operations immediately */
    info_set(BLED_SD_ERROR, why, NULL);
    ESP_LOGW(TAG, "SD card lost: %s", why);
    if (s_task) {
        xTaskNotifyGive(s_task);               /* wake the task so it unmounts and retries */
    }
}

static void sd_task(void *arg)
{
    (void)arg;
    bool logged_failure = false;
    vTaskDelay(pdMS_TO_TICKS(BLED_SD_START_DELAY_MS));   /* let the display/LVGL finish starting before touching SPI2 */
    for (;;) {
        if (!s_ready && s_card) {              /* a failed card is still mounted: clean up first */
            xSemaphoreTake(s_io_lock, portMAX_DELAY);
            do_unmount();
            xSemaphoreGive(s_io_lock);
        }

        if (!s_ready) {
            info_set(BLED_SD_MOUNTING, "Mounting SD card...", NULL);
            esp_err_t err = do_mount();
            if (err == ESP_OK) {
                xSemaphoreTake(s_io_lock, portMAX_DELAY);
                bool dir_ok = ensure_dir();
                xSemaphoreGive(s_io_lock);
                if (dir_ok) {
                    xSemaphoreTake(s_info_lock, portMAX_DELAY);
                    s_info.capacity_bytes = (uint64_t)s_card->csd.capacity * (uint64_t)s_card->csd.sector_size;
                    xSemaphoreGive(s_info_lock);
                    info_set_records(count_records());
                    info_set(BLED_SD_READY, "SD card ready", NULL);
                    s_ready = true;
                    logged_failure = false;
                    ESP_LOGI(TAG, "SD card mounted at %s", BLED_SD_MOUNT_POINT);
                } else {
                    info_set(BLED_SD_ERROR, "Cannot create /BLED folder", NULL);
                    xSemaphoreTake(s_io_lock, portMAX_DELAY);
                    do_unmount();
                    xSemaphoreGive(s_io_lock);
                }
            } else {
                info_set(BLED_SD_ABSENT, "No card / mount failed", esp_err_to_name(err));
                if (!logged_failure) {                       /* log once, not every retry */
                    ESP_LOGW(TAG, "SD mount failed: %s - check card, FAT32 format and DIP switches S1/S0 = 1/1",
                             esp_err_to_name(err));
                    logged_failure = true;
                }
            }
        } else {
            /* health check: reading the directory touches the card, so a removed card shows up here */
            char dir[96];
            dir_path(dir, sizeof(dir));
            xSemaphoreTake(s_io_lock, portMAX_DELAY);
            DIR *d = opendir(dir);
            if (d) {
                closedir(d);
            }
            xSemaphoreGive(s_io_lock);
            if (!d) {
                mark_lost("Card removed");
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s_ready ? 10000 : BLED_SD_RETRY_MS));
    }
}

esp_err_t bled_storage_init(void)
{
    s_info_lock = xSemaphoreCreateMutex();
    s_io_lock = xSemaphoreCreateMutex();
    if (!s_info_lock || !s_io_lock) {
        return ESP_ERR_NO_MEM;
    }
#if !BLED_SD_ENABLE
    info_set(BLED_SD_ABSENT, "SD disabled (BLED_SD_ENABLE=0)", NULL);
    return ESP_OK;
#endif
    if (xTaskCreate(sd_task, "bled_sd", 6144, NULL, 3, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void bled_storage_request_remount(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

#else /* ---------------------------------------------------------------- */
/* Host test build: a plain directory plays the role of the SD card       */
/* ---------------------------------------------------------------------- */

static void mark_lost(const char *why)
{
    s_ready = false;
    info_set(BLED_SD_ERROR, why, NULL);
}

esp_err_t bled_storage_init(void)
{
    if (!s_info_lock) {
        s_info_lock = xSemaphoreCreateMutex();
        s_io_lock = xSemaphoreCreateMutex();
    }
    return ESP_OK;
}

void bled_storage_request_remount(void)
{
    /* host: nothing to retry; tests flip the state through bled_storage_host_set_ready() */
}

void bled_storage_host_set_root(const char *dir)
{
    bled_storage_init();
    copy_str(s_root, sizeof(s_root), dir);
}

void bled_storage_host_set_ready(bool ready)
{
    bled_storage_init();
    if (ready) {
        ensure_dir();
        xSemaphoreTake(s_info_lock, portMAX_DELAY);
        s_info.capacity_bytes = 15523119104ULL;
        xSemaphoreGive(s_info_lock);
        info_set_records(count_records());
        info_set(BLED_SD_READY, "SD card ready", NULL);
    } else {
        info_set(BLED_SD_ABSENT, "No card / mount failed", "ESP_ERR_TIMEOUT");
        xSemaphoreTake(s_info_lock, portMAX_DELAY);
        s_info.capacity_bytes = 0;
        s_info.records = -1;
        xSemaphoreGive(s_info_lock);
    }
    s_ready = ready;
}
#endif
