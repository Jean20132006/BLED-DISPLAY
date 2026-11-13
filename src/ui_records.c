/**
 * @file ui_records.c
 * @brief See ui_records.h.
 *
 * Reading the SD card is slow compared to a UI frame, so the list is filled
 * incrementally: bled_storage_list_begin() only collects file names, then a
 * short LVGL timer parses one or two files per tick and appends rows.
 */
#include "ui_records.h"
#include "ui_theme.h"
#include "bled_core.h"
#include "bled_storage.h"

#include <stdio.h>
#include <string.h>

#define LIST_COLS 5
static const int LIST_COL_W[LIST_COLS] = { 190, 190, 80, 120, 188 };
#define PAD_COLS 4
static const int PAD_COL_W[PAD_COLS] = { 70, 130, 150, 150 };

#define COL_ZEBRA lv_color_hex(0xF5F8FB)

static lv_obj_t *s_page_list, *s_page_detail;

/* list page */
static lv_obj_t *s_status, *s_table, *s_empty, *s_empty_btn, *s_hdr;
static bled_record_summary_t s_sums[BLED_MAX_RECORDS];
static int s_rows;
static lv_timer_t *s_scan_timer;
static bool s_dirty = true;
static bool s_visible;
static bool s_was_ready;
static uint32_t s_last_rev;

/* detail page */
static bled_record_detail_t s_detail;
static lv_obj_t *s_d_title, *s_d_chip, *s_d_ebl, *s_d_level, *s_d_rows[5], *s_d_chart_cap, *s_d_chart, *s_d_table, *s_d_msg;
static lv_chart_series_t *s_d_series;
static int32_t s_chart_vals[BLED_MAX_PADS];

/* ---------------------------------------------------------------------- */
/* Shared table styling                                                   */
/* ---------------------------------------------------------------------- */

static void table_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if (base->part != LV_PART_ITEMS) {
        return;
    }
    uint32_t row = base->id1, col = base->id2;
    lv_draw_task_type_t type = lv_draw_task_get_type(task);

    if (type == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *fill = (lv_draw_fill_dsc_t *)lv_draw_task_get_draw_dsc(task);
        fill->color = (row % 2) ? COL_ZEBRA : lv_color_white();
        fill->opa = LV_OPA_COVER;
    } else if (type == LV_DRAW_TASK_TYPE_LABEL && obj == s_table && (int)row < s_rows) {
        lv_draw_label_dsc_t *lab = (lv_draw_label_dsc_t *)lv_draw_task_get_draw_dsc(task);
        const bled_record_summary_t *r = &s_sums[row];
        if (col == 3) {
            lab->color = bled_ebl_level(r->total_ebl_ml) == 0 ? UI_COLOR_BLOOD : ui_level_color(bled_ebl_level(r->total_ebl_ml));
        } else if (col == 4) {
            lab->color = r->closed ? UI_COLOR_MUTED : UI_COLOR_WARN;
        }
    }
}

static lv_obj_t *make_table(lv_obj_t *parent, int cols, const int *widths, int32_t w, int32_t h)
{
    lv_obj_t *t = lv_table_create(parent);
    lv_table_set_column_count(t, (uint32_t)cols);
    for (int i = 0; i < cols; i++) {
        lv_table_set_column_width(t, (uint32_t)i, widths[i]);
    }
    lv_obj_set_size(t, w, h);
    lv_obj_set_style_text_font(t, UI_FONT_MD, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(t, 12, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(t, 11, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(t, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(t, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_color(t, UI_COLOR_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(t, UI_COLOR_TINT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(t, 0, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(t, table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    return t;
}

static lv_obj_t *make_header(lv_obj_t *parent, const char *const *names, const int *widths, int cols, int32_t w)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, w, 30);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, UI_COLOR_NAVY, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    int x = 12;
    for (int i = 0; i < cols; i++) {
        lv_obj_t *l = ui_label(hdr, names[i], UI_FONT_SM, lv_color_white());
        lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
        x += widths[i];
    }
    return hdr;
}

/* ---------------------------------------------------------------------- */
/* List                                                                   */
/* ---------------------------------------------------------------------- */

static void update_status_text(void)
{
    bled_sd_info_t sd;
    bled_storage_get_info(&sd);
    char buf[96];
    if (sd.state == BLED_SD_READY) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD "  SD card ready   |   %d record%s", s_rows, s_rows == 1 ? "" : "s");
        ui_set_text_color(s_status, UI_COLOR_OK);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING "  SD card not available");
        ui_set_text_color(s_status, UI_COLOR_WARN);
    }
    ui_set_text(s_status, buf);
}

static void show_empty(const char *text, bool retry)
{
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hdr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    ui_set_text(s_empty, text);
    if (retry) {
        lv_obj_remove_flag(s_empty_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_empty_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_row(const bled_record_summary_t *s)
{
    if (s_rows >= BLED_MAX_RECORDS) {
        return;
    }
    s_sums[s_rows] = *s;
    char buf[40], ml[16];
    uint32_t r = (uint32_t)s_rows;
    lv_table_set_cell_value(s_table, r, 0, s->id);
    bled_stamp_format_short(&s->start, buf, sizeof(buf));
    lv_table_set_cell_value(s_table, r, 1, buf);
    snprintf(buf, sizeof(buf), "%d", s->pads);
    lv_table_set_cell_value(s_table, r, 2, buf);
    ui_format_ml(ml, sizeof(ml), s->total_ebl_ml);
    lv_table_set_cell_value(s_table, r, 3, ml);
    lv_table_set_cell_value(s_table, r, 4, s->closed ? "Closed" : "Open / interrupted");
    s_rows++;
}

static void scan_tick_cb(lv_timer_t *timer)
{
    for (int k = 0; k < 2; k++) {
        bled_record_summary_t s;
        if (!bled_storage_list_next(&s)) {
            lv_timer_pause(timer);
            if (s_rows == 0) {
                show_empty("No records yet.\nCompleted and running cases appear here.", false);
            }
            update_status_text();
            return;
        }
        add_row(&s);
    }
    update_status_text();
}

static void start_scan(void)
{
    s_dirty = false;
    s_rows = 0;
    lv_table_set_row_count(s_table, 0);
    lv_timer_pause(s_scan_timer);
    if (!bled_storage_ready()) {
        show_empty("SD card not available.\nInsert a FAT32 card (DIP switches S1/S0 = 1/1).", true);
        update_status_text();
        return;
    }
    int n = bled_storage_list_begin();
    update_status_text();
    if (n == 0) {
        show_empty("No records yet.\nCompleted and running cases appear here.", false);
        return;
    }
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_empty_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_hdr, LV_OBJ_FLAG_HIDDEN);
    lv_timer_resume(s_scan_timer);
}

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    start_scan();
}

static void retry_cb(lv_event_t *e)
{
    (void)e;
    bled_storage_request_remount();
    s_dirty = true;                        /* the housekeeping timer rescans as soon as the card is ready */
    ui_set_text(s_empty, "Trying to mount the SD card...");
}

static void housekeeping_cb(lv_timer_t *timer)
{
    (void)timer;
    bool ready = bled_storage_ready();
    if (ready != s_was_ready) {
        s_was_ready = ready;
        s_dirty = true;
    }
    if (bled_core_revision() != s_last_rev) {
        s_last_rev = bled_core_revision();
        if (!s_visible) {
            s_dirty = true;                /* something changed while the tab was hidden */
        }
    }
    if (s_dirty && s_visible && lv_obj_has_flag(s_page_list, LV_OBJ_FLAG_HIDDEN) == false) {
        start_scan();
    }
}

void ui_records_set_visible(bool visible)
{
    s_visible = visible;
    if (visible) {
        s_last_rev = bled_core_revision();
        if (s_dirty && lv_obj_has_flag(s_page_list, LV_OBJ_FLAG_HIDDEN) == false) {
            start_scan();
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Detail                                                                 */
/* ---------------------------------------------------------------------- */

static void back_to_list_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_page_list, LV_OBJ_FLAG_HIDDEN);
}

static void fill_detail(void)
{
    const bled_record_summary_t *s = &s_detail.sum;
    char buf[96], a[16], b[24];

    snprintf(buf, sizeof(buf), "Case %s", s->id);
    ui_set_text(s_d_title, buf);
    ui_set_text(s_d_chip, s->closed ? "CLOSED" : "OPEN / INTERRUPTED");
    ui_set_text_color(s_d_chip, s->closed ? UI_COLOR_MUTED : UI_COLOR_WARN);

    int level = bled_ebl_level(s->total_ebl_ml);
    ui_format_ml(a, sizeof(a), s->total_ebl_ml);
    ui_set_text(s_d_ebl, a);
    ui_set_text_color(s_d_ebl, level == 0 ? UI_COLOR_BLOOD : ui_level_color(level));
    ui_set_text(s_d_level, level == 2 ? "Above alert limit" : (level == 1 ? "Above warning limit" : "Within limits"));
    ui_set_text_color(s_d_level, level == 0 ? UI_COLOR_MUTED : ui_level_color(level));

    bled_stamp_format(&s->start, b, sizeof(b));
    snprintf(buf, sizeof(buf), "Started: %s", b);
    ui_set_text(s_d_rows[0], buf);
    if (s->closed) {
        bled_stamp_format(&s->end, b, sizeof(b));
        snprintf(buf, sizeof(buf), "Ended: %s", b);
    } else {
        snprintf(buf, sizeof(buf), "Ended: not recorded");
    }
    ui_set_text(s_d_rows[1], buf);
    if (s->closed) {
        bled_format_duration(bled_stamp_diff_s(&s->start, &s->end), b, sizeof(b));
        snprintf(buf, sizeof(buf), "Duration: %s", b);
    } else {
        snprintf(buf, sizeof(buf), "Duration: --");
    }
    ui_set_text(s_d_rows[2], buf);
    {
        int t = (int)(s->hb_gdl * 10.0f + 0.5f);
        if (t < 0) {
            t = 0;
        } else if (t > 999) {
            t = 999;
        }
        snprintf(buf, sizeof(buf), "Baseline Hb: %d.%d g/dL", t / 10, t % 10);
    }
    ui_set_text(s_d_rows[3], buf);
    snprintf(buf, sizeof(buf), "Pads: %d", s->pads);
    ui_set_text(s_d_rows[4], buf);

    /* cumulative chart */
    int n = s_detail.n;
    float cum = 0.0f;
    for (int i = 0; i < n; i++) {
        cum += s_detail.pad_ml[i];
        s_chart_vals[i] = (int32_t)(cum + 0.5f);
    }
    int ymax = ((int)(cum / 100.0f) + 1) * 100;
    lv_chart_set_axis_range(s_d_chart, LV_CHART_AXIS_PRIMARY_Y, 0, ymax);
    lv_chart_set_point_count(s_d_chart, n < 2 ? 2 : (uint32_t)n);
    lv_chart_set_all_values(s_d_chart, s_d_series, LV_CHART_POINT_NONE);
    if (n > 0) {
        lv_chart_set_series_values(s_d_chart, s_d_series, s_chart_vals, (size_t)n);
    }
    lv_chart_refresh(s_d_chart);
    snprintf(buf, sizeof(buf), "Cumulative blood loss per pad  (scale 0 - %d mL)", ymax);
    ui_set_text(s_d_chart_cap, buf);

    /* pad table */
    lv_table_set_row_count(s_d_table, 0);
    float running = 0.0f;
    for (int i = 0; i < n; i++) {
        uint32_t r = (uint32_t)i;
        running += s_detail.pad_ml[i];
        snprintf(buf, sizeof(buf), "%d", i + 1);
        lv_table_set_cell_value(s_d_table, r, 0, buf);
        bled_stamp_format_time(&s_detail.pad_time[i], b, sizeof(b));
        lv_table_set_cell_value(s_d_table, r, 1, b);
        ui_format_ml(a, sizeof(a), s_detail.pad_ml[i]);
        lv_table_set_cell_value(s_d_table, r, 2, a);
        ui_format_ml(a, sizeof(a), running);
        lv_table_set_cell_value(s_d_table, r, 3, a);
    }
    ui_set_text(s_d_msg, n == 0 ? "No pads were recorded for this case." : "");
}

static void table_select_cb(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target_obj(e);
    uint32_t row = 0, col = 0;
    lv_table_get_selected_cell(t, &row, &col);
    if ((int)row >= s_rows) {
        return;
    }
    if (!bled_storage_load(s_sums[row].file, &s_detail)) {
        update_status_text();
        ui_set_text(s_status, LV_SYMBOL_WARNING "  Could not read that record");
        return;
    }
    fill_detail();
    lv_obj_add_flag(s_page_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void build_list(lv_obj_t *p)
{
    lv_obj_t *title = ui_label(p, "Patient Records", UI_FONT_XL, UI_COLOR_TEXT);
    lv_obj_set_pos(title, 16, 8);
    s_status = ui_label(p, "", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_set_pos(s_status, 16, 36);
    lv_obj_t *refresh = ui_button(p, LV_SYMBOL_REFRESH "  Refresh", 130, 40, UI_BTN_NEUTRAL, refresh_cb, NULL);
    lv_obj_set_pos(refresh, 800 - 16 - 130, 6);

    static const char *const names[LIST_COLS] = { "Case number", "Started", "Pads", "EBL (mL)", "Status" };
    s_hdr = make_header(p, names, LIST_COL_W, LIST_COLS, 768);
    lv_obj_set_pos(s_hdr, 16, 58);
    s_table = make_table(p, LIST_COLS, LIST_COL_W, 768, 402 - 58 - 30 - 8);
    lv_obj_set_pos(s_table, 16, 88);
    lv_obj_add_event_cb(s_table, table_select_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_empty = ui_label(p, "", UI_FONT_LG, UI_COLOR_MUTED);
    lv_obj_set_width(s_empty, 640);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, -20);
    s_empty_btn = ui_button(p, "Retry", 160, 48, UI_BTN_PRIMARY, retry_cb, NULL);
    lv_obj_align(s_empty_btn, LV_ALIGN_CENTER, 0, 70);
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_empty_btn, LV_OBJ_FLAG_HIDDEN);
}

static void build_detail(lv_obj_t *p)
{
    lv_obj_t *back = ui_button(p, LV_SYMBOL_LEFT "  Records", 150, 40, UI_BTN_NEUTRAL, back_to_list_cb, NULL);
    lv_obj_set_pos(back, 16, 6);
    s_d_title = ui_label(p, "Case", UI_FONT_XL, UI_COLOR_TEXT);
    lv_obj_set_pos(s_d_title, 186, 14);
    s_d_chip = ui_label(p, "", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(s_d_chip, LV_ALIGN_TOP_RIGHT, -20, 16);

    lv_obj_t *left = ui_card(p, 250, 336);
    lv_obj_set_pos(left, 16, 54);
    lv_obj_t *cap = ui_label(left, "Total blood loss", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);
    s_d_ebl = ui_label(left, "0.0", UI_FONT_HUGE, UI_COLOR_BLOOD);
    lv_obj_align(s_d_ebl, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *unit = ui_label(left, "mL", UI_FONT_LG, UI_COLOR_MUTED);
    lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 84);
    s_d_level = ui_label(left, "", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_align(s_d_level, LV_ALIGN_TOP_MID, 0, 112);
    for (int i = 0; i < 5; i++) {
        s_d_rows[i] = ui_label(left, "", UI_FONT_SM, UI_COLOR_TEXT);
        ui_label_oneline(s_d_rows[i], 224);
        lv_obj_align(s_d_rows[i], LV_ALIGN_TOP_LEFT, 0, 150 + i * 30);
    }

    lv_obj_t *chart_card = ui_card(p, 502, 150);
    lv_obj_set_pos(chart_card, 282, 54);
    lv_obj_set_style_pad_all(chart_card, 8, 0);
    s_d_chart_cap = ui_label(chart_card, "", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_align(s_d_chart_cap, LV_ALIGN_TOP_LEFT, 0, 0);
    s_d_chart = lv_chart_create(chart_card);
    lv_obj_set_size(s_d_chart, 470, 104);
    lv_obj_align(s_d_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(s_d_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_d_chart, 4, 6);
    lv_obj_set_style_bg_opa(s_d_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_d_chart, 0, 0);
    lv_obj_set_style_line_width(s_d_chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_size(s_d_chart, 8, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_d_chart, UI_COLOR_BORDER, LV_PART_MAIN);
    s_d_series = lv_chart_add_series(s_d_chart, UI_COLOR_BLOOD, LV_CHART_AXIS_PRIMARY_Y);

    static const char *const pad_names[PAD_COLS] = { "Pad", "Time", "This pad (mL)", "Cumulative (mL)" };
    lv_obj_t *hdr = make_header(p, pad_names, PAD_COL_W, PAD_COLS, 502);
    lv_obj_set_pos(hdr, 282, 212);
    s_d_table = make_table(p, PAD_COLS, PAD_COL_W, 502, 150);
    lv_obj_set_pos(s_d_table, 282, 242);
    lv_obj_set_style_pad_ver(s_d_table, 8, LV_PART_ITEMS);
    s_d_msg = ui_label(p, "", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_set_pos(s_d_msg, 296, 262);
}

void ui_records_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);
    s_page_list = make_page(parent);
    s_page_detail = make_page(parent);
    build_list(s_page_list);
    build_detail(s_page_detail);
    lv_obj_add_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);

    s_scan_timer = lv_timer_create(scan_tick_cb, 25, NULL);
    lv_timer_pause(s_scan_timer);
    lv_timer_create(housekeeping_cb, 1000, NULL);
    s_was_ready = bled_storage_ready();
    s_last_rev = bled_core_revision();
    update_status_text();
    start_scan();
}
