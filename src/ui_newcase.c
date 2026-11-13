/**
 * @file ui_newcase.c
 * @brief See ui_newcase.h.
 *
 * Navigation uses plain in-tab panels that are shown/hidden (the same pattern
 * as the Calendar / Connections tabs): no lv_msgbox, no LVGL animations.
 * Data typed on a step is kept in s_draft the moment it changes, so going
 * Back and forth never loses anything.
 */
#include "ui_newcase.h"
#include "ui_theme.h"
#include "bled_core.h"
#include "bled_proto.h"
#include "bled_storage.h"

#include <stdio.h>
#include <string.h>

#define STEP_COUNT 4
#define BODY_Y     52
#define FOOTER_H   60

static const char *const STEP_NAMES[STEP_COUNT] = { "Case number", "Patient baseline", "System baseline", "Review" };

static lv_obj_t *s_root, *s_wizard, *s_active;
static int s_step;
static bled_draft_t s_draft;
static bool s_hb_ok;                       /* the Hb text currently in the box is a valid value */

/* stepper */
static lv_obj_t *s_dot[STEP_COUNT], *s_dot_lbl[STEP_COUNT], *s_step_lbl[STEP_COUNT];
static lv_obj_t *s_panel[STEP_COUNT];

/* step 1 / 2 */
static lv_obj_t *s_ta_id, *s_err_id, *s_ta_hb, *s_err_hb;

/* step 3 */
static lv_obj_t *s_b_link, *s_b_cart, *s_b_state, *s_b_btn, *s_b_bar, *s_b_msg, *s_b_vals;

/* step 4 */
static lv_obj_t *s_r_id, *s_r_hb, *s_r_base, *s_r_sd, *s_r_msg;

/* footer */
static lv_obj_t *s_btn_back, *s_btn_cancel, *s_btn_next, *s_hint;

/* active case panel */
static lv_obj_t *s_a_id, *s_a_sub, *s_a_pads, *s_a_last, *s_a_warn, *s_a_ebl_card, *s_a_ebl, *s_a_level;
static lv_obj_t *s_a_sim_btn;
static lv_obj_t *s_confirm, *s_confirm_text;

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void format_hb(char *out, size_t n, float hb)
{
    int t = (int)(hb * 10.0f + 0.5f);
    if (t < 0) {
        t = 0;
    } else if (t > 999) {
        t = 999;
    }
    snprintf(out, n, "%d.%d", t / 10, t % 10);
}

/* ---------------------------------------------------------------------- */
/* Step navigation                                                        */
/* ---------------------------------------------------------------------- */

static void update_stepper(void)
{
    for (int i = 0; i < STEP_COUNT; i++) {
        bool done = i < s_step, current = i == s_step;
        lv_obj_set_style_bg_color(s_dot[i], done ? UI_COLOR_OK : (current ? UI_COLOR_PRIMARY : UI_COLOR_CARD), 0);
        lv_obj_set_style_border_width(s_dot[i], (done || current) ? 0 : 2, 0);
        lv_obj_set_style_border_color(s_dot[i], UI_COLOR_BORDER, 0);
        if (done) {
            ui_set_text(s_dot_lbl[i], LV_SYMBOL_OK);
        } else {
            char n[4];
            snprintf(n, sizeof(n), "%d", i + 1);
            ui_set_text(s_dot_lbl[i], n);
        }
        ui_set_text_color(s_dot_lbl[i], (done || current) ? lv_color_white() : UI_COLOR_MUTED);
        ui_set_text_color(s_step_lbl[i], current ? UI_COLOR_PRIMARY_D : (done ? UI_COLOR_TEXT : UI_COLOR_MUTED));
    }
}

static void refresh_footer(void)
{
    const bled_system_t *sys = bled_sys();
    bool next_ok = false;
    const char *hint = "";
    switch (s_step) {
        case 0:
            next_ok = bled_id_valid(s_draft.id);
            hint = "The case number is saved with every pad measurement";
            break;
        case 1:
            next_ok = s_hb_ok;
            hint = "Patient's pre-operative hemoglobin (lab value)";
            break;
        case 2:
            next_ok = sys->base_state == BLED_BASE_DONE;
            hint = next_ok ? "Baseline complete - continue" : "Run the system baseline to continue";
            break;
        default:
            next_ok = bled_id_valid(s_draft.id) && bled_hb_valid(s_draft.hb_gdl) && sys->base_state == BLED_BASE_DONE &&
                      sys->unit_online;
            hint = "Check the details, then start the case";
            break;
    }
    ui_button_set_enabled(s_btn_next, next_ok);
    ui_set_text(s_hint, hint);
    ui_button_set_text(s_btn_next, s_step == STEP_COUNT - 1 ? LV_SYMBOL_OK "  Start case" : "Next  " LV_SYMBOL_RIGHT);

    /* colour of the Next / Start case button: only touched when the step kind changes (see ui_set_bg_color) */
    static int s_last_next_kind = -1;
    int kind = (s_step == STEP_COUNT - 1) ? 1 : 0;
    if (kind != s_last_next_kind) {
        s_last_next_kind = kind;
        lv_color_t col = kind ? UI_COLOR_OK : UI_COLOR_PRIMARY;
        lv_obj_set_style_bg_color(s_btn_next, col, 0);
        lv_obj_set_style_bg_color(s_btn_next, lv_color_darken(col, 40), LV_STATE_PRESSED);
    }

    ui_set_hidden(s_btn_back, s_step == 0);
    ui_set_hidden(s_btn_cancel, s_step == 0);
}

static void refresh_baseline(void);
static void refresh_review(void);

static void goto_step(int step)
{
    if (step < 0) {
        step = 0;
    } else if (step >= STEP_COUNT) {
        step = STEP_COUNT - 1;
    }
    s_step = step;
    for (int i = 0; i < STEP_COUNT; i++) {
        if (i == step) {
            lv_obj_remove_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_remove_state(s_ta_id, LV_STATE_FOCUSED);
    lv_obj_remove_state(s_ta_hb, LV_STATE_FOCUSED);
    if (step == 0) {
        lv_obj_add_state(s_ta_id, LV_STATE_FOCUSED);
    } else if (step == 1) {
        lv_obj_add_state(s_ta_hb, LV_STATE_FOCUSED);
    }
    update_stepper();
    refresh_baseline();
    refresh_review();
    refresh_footer();
}

static void wizard_reset(void)
{
    memset(&s_draft, 0, sizeof(s_draft));
    s_hb_ok = false;
    lv_textarea_set_text(s_ta_id, "");
    lv_textarea_set_text(s_ta_hb, "");
    ui_set_text(s_err_id, "");
    ui_set_text(s_err_hb, "");
    ui_set_text(s_r_msg, "");
    goto_step(0);
}

static void advance(void)
{
    switch (s_step) {
        case 0:
            if (bled_id_valid(s_draft.id)) {
                goto_step(1);
            }
            break;
        case 1:
            if (s_hb_ok) {
                goto_step(2);
            }
            break;
        case 2:
            if (bled_sys()->base_state == BLED_BASE_DONE) {
                goto_step(3);
            }
            break;
        default: {
            bled_start_result_t r = bled_case_start(&s_draft);
            if (r == BLED_START_OK) {
                wizard_reset();
            } else {
                ui_set_text(s_r_msg, bled_start_result_text(r));
            }
            break;
        }
    }
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    advance();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    goto_step(s_step - 1);                 /* everything typed so far is still in s_draft and the text boxes */
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    wizard_reset();
}

static void kb_ready_cb(lv_event_t *e)
{
    (void)e;
    advance();                             /* the keyboard's OK / Enter key acts like Next */
}

/* ---------------------------------------------------------------------- */
/* Steps 1 and 2: text entry                                              */
/* ---------------------------------------------------------------------- */

static void id_changed_cb(lv_event_t *e)
{
    (void)e;
    copy_str(s_draft.id, sizeof(s_draft.id), lv_textarea_get_text(s_ta_id));
    ui_set_text(s_err_id, (s_draft.id[0] == '\0' || bled_id_valid(s_draft.id)) ? "" : "Letters, digits, - and _ only");
    refresh_footer();
}

static void hb_changed_cb(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(s_ta_hb);
    float v = 0.0f;
    s_hb_ok = false;
    if (txt[0] == '\0') {
        ui_set_text(s_err_hb, "");
    } else if (!bled_arg_float(txt, &v)) {
        ui_set_text(s_err_hb, "Enter a number such as 13.2");
    } else if (!bled_hb_valid(v)) {
        ui_set_text(s_err_hb, "Must be between 4.0 and 20.0 g/dL");
    } else {
        s_hb_ok = true;
        s_draft.hb_gdl = v;
        ui_set_text(s_err_hb, "");
    }
    refresh_footer();
}

static lv_obj_t *make_ta(lv_obj_t *parent, lv_coord_t x, lv_coord_t w, const char *placeholder, const char *accepted,
                         uint32_t max_len, lv_event_cb_t changed)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_accepted_chars(ta, accepted);
    lv_textarea_set_max_length(ta, max_len);
    lv_obj_set_size(ta, w, 52);
    lv_obj_set_pos(ta, x, 34);
    lv_obj_set_style_text_font(ta, UI_FONT_XL, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_PRIMARY, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta, changed, LV_EVENT_VALUE_CHANGED, NULL);
    return ta;
}

static lv_obj_t *make_kb(lv_obj_t *parent, lv_obj_t *ta, lv_keyboard_mode_t mode)
{
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, lv_pct(100), 190);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(kb, mode);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_text_font(kb, UI_FONT_XL, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_ready_cb, LV_EVENT_READY, NULL);
    return kb;
}

static lv_obj_t *make_panel(lv_obj_t *wizard)
{
    lv_obj_t *p = lv_obj_create(wizard);
    lv_obj_set_size(p, lv_pct(100), 402 - BODY_Y - FOOTER_H);
    lv_obj_set_pos(p, 0, BODY_Y);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void build_step_id(lv_obj_t *p)
{
    lv_obj_t *t = ui_label(p, "Patient case number", UI_FONT_XL, UI_COLOR_TEXT);
    lv_obj_set_pos(t, 24, 2);
    s_ta_id = make_ta(p, 24, 380, "e.g. 2026-0412", BLED_CASE_ID_CHARS, BLED_CASE_ID_MAX, id_changed_cb);
    lv_obj_t *help = ui_label(p, "Letters, digits, - and _ (max 12).\nUse the keyboard key to switch letters / digits.", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_set_pos(help, 424, 34);
    s_err_id = ui_label(p, "", UI_FONT_SM, UI_COLOR_ALERT);
    lv_obj_set_pos(s_err_id, 424, 72);
    make_kb(p, s_ta_id, LV_KEYBOARD_MODE_NUMBER);
}

static void build_step_hb(lv_obj_t *p)
{
    lv_obj_t *t = ui_label(p, "Patient baseline hemoglobin", UI_FONT_XL, UI_COLOR_TEXT);
    lv_obj_set_pos(t, 24, 2);
    s_ta_hb = make_ta(p, 24, 200, "13.2", "0123456789.", 5, hb_changed_cb);
    lv_obj_t *unit = ui_label(p, "g/dL", UI_FONT_XL, UI_COLOR_MUTED);
    lv_obj_set_pos(unit, 234, 48);
    lv_obj_t *help = ui_label(p, "Pre-operative lab value. Accepted range 4.0 - 20.0 g/dL.\nIt is sent to the measurement unit and saved with the case.", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_set_pos(help, 320, 34);
    s_err_hb = ui_label(p, "", UI_FONT_SM, UI_COLOR_ALERT);
    lv_obj_set_pos(s_err_hb, 320, 72);
    make_kb(p, s_ta_hb, LV_KEYBOARD_MODE_NUMBER);
}

/* ---------------------------------------------------------------------- */
/* Step 3: system baseline                                                */
/* ---------------------------------------------------------------------- */

static void baseline_cb(lv_event_t *e)
{
    (void)e;
    bled_baseline_start();
    refresh_baseline();
    refresh_footer();
}

static void refresh_baseline(void)
{
    const bled_system_t *sys = bled_sys();
    char buf[96];

    if (sys->unit_online) {
        ui_set_text(s_b_link, sys->unit_simulated ? LV_SYMBOL_OK "  Link: online (simulated unit)" : LV_SYMBOL_OK "  Link: online");
        ui_set_text_color(s_b_link, sys->unit_simulated ? UI_COLOR_WARN : UI_COLOR_OK);
        ui_set_text(s_b_cart, sys->cartridge ? LV_SYMBOL_OK "  Cartridge: loaded" : LV_SYMBOL_WARNING "  Cartridge: not detected");
        ui_set_text_color(s_b_cart, sys->cartridge ? UI_COLOR_OK : UI_COLOR_WARN);
        snprintf(buf, sizeof(buf), "State: %s", bled_unit_state_text(sys->unit_state));
        ui_set_text(s_b_state, buf);
    } else {
        ui_set_text(s_b_link, LV_SYMBOL_CLOSE "  Link: measurement unit offline");
        ui_set_text_color(s_b_link, UI_COLOR_ALERT);
        ui_set_text(s_b_cart, "Cartridge: unknown");
        ui_set_text_color(s_b_cart, UI_COLOR_MUTED);
        ui_set_text(s_b_state, "Connect the unit (UART0) and wait");
    }

    bool running = sys->base_state == BLED_BASE_RUNNING;
    ui_button_set_text(s_b_btn, running ? "Running..." : (sys->base_state == BLED_BASE_DONE ? "Run again" : "Run baseline"));
    ui_button_set_enabled(s_b_btn, sys->unit_online && !running);

    int pct = (sys->base_state == BLED_BASE_RUNNING || sys->base_state == BLED_BASE_DONE) ? sys->base_pct : 0;
    if (lv_bar_get_value(s_b_bar) != pct) {
        lv_bar_set_value(s_b_bar, pct, LV_ANIM_OFF);
    }

    switch (sys->base_state) {
        case BLED_BASE_RUNNING:
            snprintf(buf, sizeof(buf), "Measuring the optical baseline... %d%%", sys->base_pct);
            ui_set_text(s_b_msg, buf);
            ui_set_text_color(s_b_msg, UI_COLOR_PRIMARY_D);
            break;
        case BLED_BASE_DONE:
            ui_set_text(s_b_msg, LV_SYMBOL_OK "  Baseline complete");
            ui_set_text_color(s_b_msg, UI_COLOR_OK);
            break;
        case BLED_BASE_FAILED:
            snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING "  Failed: %s", sys->base_msg);
            ui_set_text(s_b_msg, buf);
            ui_set_text_color(s_b_msg, UI_COLOR_ALERT);
            break;
        default:
            ui_set_text(s_b_msg, "Load the cartridge, then run the baseline");
            ui_set_text_color(s_b_msg, UI_COLOR_MUTED);
            break;
    }

    if (sys->base_state == BLED_BASE_DONE) {
        char t[16];
        bled_stamp_format_time(&sys->base_time, t, sizeof(t));
        snprintf(buf, sizeof(buf), "540 nm: %d    850 nm: %d\nMeasured at %s", sys->base_i540, sys->base_i850, t);
        ui_set_text(s_b_vals, buf);
    } else {
        ui_set_text(s_b_vals, "");
    }
}

static void build_step_baseline(lv_obj_t *p)
{
    lv_obj_t *left = ui_card(p, 360, 270);
    lv_obj_set_pos(left, 24, 8);
    lv_obj_t *lt = ui_label(left, "Measurement unit", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_align(lt, LV_ALIGN_TOP_LEFT, 0, 0);
    s_b_link = ui_label(left, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_align(s_b_link, LV_ALIGN_TOP_LEFT, 0, 44);
    s_b_cart = ui_label(left, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_align(s_b_cart, LV_ALIGN_TOP_LEFT, 0, 82);
    s_b_state = ui_label(left, "", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(s_b_state, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_obj_t *note = ui_label(left, "The baseline measures the optical reference of the fresh reagent before any blood is added. It must be repeated for every new cartridge.", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_set_width(note, 336);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *right = ui_card(p, 376, 270);
    lv_obj_set_pos(right, 400, 8);
    lv_obj_t *rt = ui_label(right, "System baseline", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_align(rt, LV_ALIGN_TOP_LEFT, 0, 0);
    s_b_btn = ui_button(right, "Run baseline", 200, 52, UI_BTN_PRIMARY, baseline_cb, NULL);
    lv_obj_align(s_b_btn, LV_ALIGN_TOP_LEFT, 0, 40);
    s_b_bar = lv_bar_create(right);
    lv_bar_set_range(s_b_bar, 0, 100);
    lv_obj_set_size(s_b_bar, 340, 14);
    lv_obj_align(s_b_bar, LV_ALIGN_TOP_LEFT, 0, 112);
    s_b_msg = ui_label(right, "", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_set_width(s_b_msg, 340);
    lv_obj_align(s_b_msg, LV_ALIGN_TOP_LEFT, 0, 138);
    s_b_vals = ui_label(right, "", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_align(s_b_vals, LV_ALIGN_TOP_LEFT, 0, 190);
}

/* ---------------------------------------------------------------------- */
/* Step 4: review                                                         */
/* ---------------------------------------------------------------------- */

static void refresh_review(void)
{
    const bled_system_t *sys = bled_sys();
    char buf[96], t[16];

    ui_set_text(s_r_id, s_draft.id[0] ? s_draft.id : "--");
    if (bled_hb_valid(s_draft.hb_gdl)) {
        format_hb(t, sizeof(t), s_draft.hb_gdl);
        snprintf(buf, sizeof(buf), "%s g/dL", t);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    ui_set_text(s_r_hb, buf);

    if (sys->base_state == BLED_BASE_DONE) {
        bled_stamp_format_time(&sys->base_time, t, sizeof(t));
        snprintf(buf, sizeof(buf), "Complete (%s)", t);
        ui_set_text(s_r_base, buf);
        ui_set_text_color(s_r_base, UI_COLOR_OK);
    } else {
        ui_set_text(s_r_base, "Not done");
        ui_set_text_color(s_r_base, UI_COLOR_ALERT);
    }

    bled_sd_info_t sd;
    bled_storage_get_info(&sd);
    if (sd.state == BLED_SD_READY) {
        ui_set_text(s_r_sd, "SD card - saved automatically");
        ui_set_text_color(s_r_sd, UI_COLOR_OK);
    } else {
        ui_set_text(s_r_sd, "SD card NOT available - data will not be saved");
        ui_set_text_color(s_r_sd, UI_COLOR_WARN);
    }
    if (!sys->unit_online && s_step == STEP_COUNT - 1) {
        ui_set_text(s_r_msg, "Measurement unit offline");
    } else if (s_step == STEP_COUNT - 1 && lv_label_get_text(s_r_msg)[0] != '\0' && sys->unit_online &&
               strcmp(lv_label_get_text(s_r_msg), "Measurement unit offline") == 0) {
        ui_set_text(s_r_msg, "");
    }
}

static lv_obj_t *review_row(lv_obj_t *card, const char *caption, lv_coord_t y)
{
    lv_obj_t *c = ui_label(card, caption, UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_t *v = ui_label(card, "--", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 230, y - 2);
    return v;
}

static void build_step_review(lv_obj_t *p)
{
    lv_obj_t *card = ui_card(p, 752, 210);
    lv_obj_set_pos(card, 24, 8);
    lv_obj_t *t = ui_label(card, "Review before starting", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    s_r_id = review_row(card, "Case number", 44);
    s_r_hb = review_row(card, "Baseline hemoglobin", 80);
    s_r_base = review_row(card, "System baseline", 116);
    s_r_sd = review_row(card, "Records saved to", 152);
    s_r_msg = ui_label(p, "", UI_FONT_MD, UI_COLOR_ALERT);
    lv_obj_set_pos(s_r_msg, 28, 232);
}

/* ---------------------------------------------------------------------- */
/* Active case panel                                                      */
/* ---------------------------------------------------------------------- */

static void open_confirm_cb(lv_event_t *e)
{
    (void)e;
    const bled_case_t *cs = bled_case();
    char buf[200], ml[16];
    ui_format_ml(ml, sizeof(ml), cs->total_ebl_ml);
    snprintf(buf, sizeof(buf), "Case %s will be closed (%d pads, %s mL).\nA new system baseline is required for the next case.%s",
             cs->id, cs->pad_count, ml, cs->sd_warning ? "\n\nWARNING: some data is NOT saved to the SD card!" : "");
    ui_set_text(s_confirm_text, buf);
    lv_obj_remove_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
}

static void close_confirm_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
}

static void end_case_cb(lv_event_t *e)
{
    (void)e;
    bled_case_end();
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    wizard_reset();
}

static void sim_pad_cb(lv_event_t *e)
{
    (void)e;
    bled_sim_pad();
}

static void refresh_active(void)
{
    const bled_case_t *cs = bled_case();
    char buf[128], a[16], b[16];

    ui_set_text(s_a_id, cs->id);
    char start[24];
    bled_stamp_format_short(&cs->start, start, sizeof(start));
    format_hb(a, sizeof(a), cs->hb_gdl);
    snprintf(buf, sizeof(buf), "Started %s   |   Baseline Hb %s g/dL", start, a);
    ui_set_text(s_a_sub, buf);

    snprintf(buf, sizeof(buf), "%d", cs->pad_count);
    ui_set_text(s_a_pads, buf);
    if (cs->pad_count > 0) {
        ui_format_ml(a, sizeof(a), cs->last_pad_ml);
        bled_stamp_format_time(&cs->last_pad_time, b, sizeof(b));
        snprintf(buf, sizeof(buf), "Last pad: %s mL at %s", a, b);
    } else {
        snprintf(buf, sizeof(buf), "Waiting for the first pad");
    }
    ui_set_text(s_a_last, buf);

    if (cs->sd_warning) {
        ui_set_text(s_a_warn, LV_SYMBOL_WARNING "  Some data is NOT saved to the SD card yet");
        ui_set_text_color(s_a_warn, UI_COLOR_WARN);
    } else if (cs->warning[0]) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING "  %s", cs->warning);
        ui_set_text(s_a_warn, buf);
        ui_set_text_color(s_a_warn, UI_COLOR_WARN);
    } else {
        ui_set_text(s_a_warn, cs->persisted ? LV_SYMBOL_OK "  Saved to the SD card" : "");
        ui_set_text_color(s_a_warn, UI_COLOR_OK);
    }

    int level = bled_ebl_level(cs->total_ebl_ml);
    ui_format_ml(a, sizeof(a), cs->total_ebl_ml);
    ui_set_text(s_a_ebl, a);
    ui_set_text_color(s_a_ebl, level == 0 ? UI_COLOR_BLOOD : ui_level_color(level));
    ui_set_text(s_a_level, level == 2 ? LV_SYMBOL_WARNING "  ALERT" : (level == 1 ? LV_SYMBOL_WARNING "  WARNING" : "Within limits"));
    ui_set_text_color(s_a_level, level == 0 ? UI_COLOR_MUTED : ui_level_color(level));
    ui_set_border(s_a_ebl_card, level == 0 ? UI_COLOR_BORDER : ui_level_color(level), level == 0 ? 1 : 3);
    ui_set_hidden(s_a_sim_btn, !bled_link_is_simulated());
}

static void build_active(lv_obj_t *root)
{
    s_active = lv_obj_create(root);
    lv_obj_set_size(s_active, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_active, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_active, 0, 0);
    lv_obj_set_style_radius(s_active, 0, 0);
    lv_obj_set_style_pad_all(s_active, 0, 0);
    lv_obj_remove_flag(s_active, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = ui_card(s_active, 410, 296);
    lv_obj_set_pos(left, 24, 14);
    lv_obj_t *tag = ui_label(left, LV_SYMBOL_OK "  ACTIVE CASE", UI_FONT_SM, UI_COLOR_OK);
    lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, 0);
    s_a_id = ui_label(left, "--", UI_FONT_NUM, UI_COLOR_TEXT);
    lv_obj_align(s_a_id, LV_ALIGN_TOP_LEFT, 0, 22);
    s_a_sub = ui_label(left, "", UI_FONT_SM, UI_COLOR_MUTED);
    ui_label_oneline(s_a_sub, 380);
    lv_obj_align(s_a_sub, LV_ALIGN_TOP_LEFT, 0, 72);
    lv_obj_t *cap = ui_label(left, "Pads measured", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 112);
    s_a_pads = ui_label(left, "0", UI_FONT_HUGE, UI_COLOR_TEXT);
    lv_obj_align(s_a_pads, LV_ALIGN_TOP_LEFT, 0, 134);
    s_a_last = ui_label(left, "", UI_FONT_MD, UI_COLOR_MUTED);
    ui_label_oneline(s_a_last, 380);
    lv_obj_align(s_a_last, LV_ALIGN_TOP_LEFT, 0, 202);
    s_a_warn = ui_label(left, "", UI_FONT_SM, UI_COLOR_OK);
    ui_label_oneline(s_a_warn, 380);
    lv_obj_align(s_a_warn, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_a_ebl_card = ui_card(s_active, 326, 296);
    lv_obj_set_pos(s_a_ebl_card, 450, 14);
    lv_obj_t *ecap = ui_label(s_a_ebl_card, "Cumulative blood loss", UI_FONT_MD, UI_COLOR_MUTED);
    lv_obj_align(ecap, LV_ALIGN_TOP_MID, 0, 0);
    s_a_ebl = ui_label(s_a_ebl_card, "0.0", UI_FONT_HUGE, UI_COLOR_BLOOD);
    lv_obj_align(s_a_ebl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *unit = ui_label(s_a_ebl_card, "mL", UI_FONT_XL, UI_COLOR_MUTED);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 28);
    s_a_level = ui_label(s_a_ebl_card, "", UI_FONT_LG, UI_COLOR_MUTED);
    lv_obj_align(s_a_level, LV_ALIGN_CENTER, 0, 66);
    char note[64];
    snprintf(note, sizeof(note), "Warning at %d mL   |   Alert at %d mL", (int)BLED_EBL_WARN_ML, (int)BLED_EBL_ALERT_ML);
    lv_obj_t *thr = ui_label(s_a_ebl_card, note, UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_align(thr, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *end = ui_button(s_active, LV_SYMBOL_STOP "  End case", 200, 52, UI_BTN_DANGER, open_confirm_cb, NULL);
    lv_obj_set_pos(end, 24, 326);
    s_a_sim_btn = ui_button(s_active, LV_SYMBOL_PLUS "  Simulate pad (demo)", 260, 52, UI_BTN_NEUTRAL, sim_pad_cb, NULL);
    lv_obj_set_pos(s_a_sim_btn, 240, 326);
}

static void build_confirm(lv_obj_t *root)
{
    s_confirm = lv_obj_create(root);
    lv_obj_set_size(s_confirm, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_confirm, 0, 0);
    lv_obj_set_style_radius(s_confirm, 0, 0);
    lv_obj_set_style_bg_color(s_confirm, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_confirm, 0, 0);
    lv_obj_remove_flag(s_confirm, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = ui_card(s_confirm, 480, 230);
    lv_obj_center(card);
    lv_obj_t *t = ui_label(card, "End this case?", UI_FONT_XL, UI_COLOR_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    s_confirm_text = ui_label(card, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_set_width(s_confirm_text, 440);
    lv_obj_align(s_confirm_text, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_t *cancel = ui_button(card, "Keep case open", 200, 48, UI_BTN_NEUTRAL, close_confirm_cb, NULL);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *ok = ui_button(card, "End case", 200, 48, UI_BTN_DANGER, end_case_cb, NULL);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------------- */
/* Wizard frame + periodic refresh                                        */
/* ---------------------------------------------------------------------- */

static void build_wizard(lv_obj_t *root)
{
    s_wizard = lv_obj_create(root);
    lv_obj_set_size(s_wizard, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_wizard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wizard, 0, 0);
    lv_obj_set_style_radius(s_wizard, 0, 0);
    lv_obj_set_style_pad_all(s_wizard, 0, 0);
    lv_obj_remove_flag(s_wizard, LV_OBJ_FLAG_SCROLLABLE);

    /* stepper */
    lv_obj_t *bar = lv_obj_create(s_wizard);
    lv_obj_set_size(bar, lv_pct(100), BODY_Y);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < STEP_COUNT; i++) {
        lv_coord_t x = 24 + i * 190;
        s_dot[i] = lv_obj_create(bar);
        lv_obj_set_size(s_dot[i], 28, 28);
        lv_obj_set_pos(s_dot[i], x, 11);
        lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(s_dot[i], 0, 0);
        lv_obj_remove_flag(s_dot[i], LV_OBJ_FLAG_SCROLLABLE);
        s_dot_lbl[i] = ui_label(s_dot[i], "", UI_FONT_SM, lv_color_white());
        lv_obj_center(s_dot_lbl[i]);
        s_step_lbl[i] = ui_label(bar, STEP_NAMES[i], UI_FONT_MD, UI_COLOR_MUTED);
        lv_obj_set_pos(s_step_lbl[i], x + 38, 14);
    }

    for (int i = 0; i < STEP_COUNT; i++) {
        s_panel[i] = make_panel(s_wizard);
    }
    build_step_id(s_panel[0]);
    build_step_hb(s_panel[1]);
    build_step_baseline(s_panel[2]);
    build_step_review(s_panel[3]);

    /* footer */
    lv_obj_t *footer = lv_obj_create(s_wizard);
    lv_obj_set_size(footer, lv_pct(100), FOOTER_H);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_bg_color(footer, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_back = ui_button(footer, LV_SYMBOL_LEFT "  Back", 130, 44, UI_BTN_NEUTRAL, back_cb, NULL);
    lv_obj_align(s_btn_back, LV_ALIGN_LEFT_MID, 16, 0);
    s_btn_cancel = ui_button(footer, "Cancel", 120, 44, UI_BTN_NEUTRAL, cancel_cb, NULL);
    lv_obj_align(s_btn_cancel, LV_ALIGN_LEFT_MID, 160, 0);
    s_hint = ui_label(footer, "", UI_FONT_SM, UI_COLOR_MUTED);
    lv_obj_set_width(s_hint, 268);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_hint, LV_ALIGN_LEFT_MID, 300, 0);
    s_btn_next = ui_button(footer, "Next", 200, 44, UI_BTN_PRIMARY, next_cb, NULL);
    lv_obj_align(s_btn_next, LV_ALIGN_RIGHT_MID, -16, 0);
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bool active = bled_case()->active;
    if (active) {
        ui_set_hidden(s_wizard, true);
        ui_set_hidden(s_active, false);
        refresh_active();
    } else {
        ui_set_hidden(s_wizard, false);
        ui_set_hidden(s_active, true);
        ui_set_hidden(s_confirm, true);
        if (s_step == 2) {
            refresh_baseline();
        }
        if (s_step == 3) {
            refresh_review();
        }
        refresh_footer();
    }
}

void ui_newcase_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    build_wizard(s_root);
    build_active(s_root);
    build_confirm(s_root);

    memset(&s_draft, 0, sizeof(s_draft));
    goto_step(0);
    lv_obj_add_flag(s_active, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(refresh_timer_cb, 250, NULL);
}
