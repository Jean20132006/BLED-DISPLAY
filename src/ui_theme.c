/**
 * @file ui_theme.c
 * @brief See ui_theme.h.
 */
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

void ui_theme_init(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, UI_COLOR_PRIMARY, UI_COLOR_OK, false, LV_FONT_DEFAULT);
    if (theme) {
        lv_display_set_theme(disp, theme);
    }
    lv_obj_set_style_bg_color(lv_screen_active(), UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
}

lv_obj_t *ui_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int32_t w, int32_t h, ui_btn_kind_t kind, lv_event_cb_t cb,
                    void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, 0);

    lv_color_t bg, fg;
    switch (kind) {
        case UI_BTN_SUCCESS: bg = UI_COLOR_OK;      fg = lv_color_white(); break;
        case UI_BTN_DANGER:  bg = UI_COLOR_ALERT;   fg = lv_color_white(); break;
        case UI_BTN_NEUTRAL: bg = UI_COLOR_CARD;    fg = UI_COLOR_TEXT;    break;
        case UI_BTN_PRIMARY:
        default:             bg = UI_COLOR_PRIMARY; fg = lv_color_white(); break;
    }
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, fg, 0);
    if (kind == UI_BTN_NEUTRAL) {
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_BORDER, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_TINT, LV_STATE_PRESSED);
    }
    /* disabled: flat grey, clearly not tappable */
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(btn, UI_COLOR_MUTED, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DISABLED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_MD, 0);
    lv_obj_center(label);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

void ui_button_set_text(lv_obj_t *btn, const char *text)
{
    ui_set_text(lv_obj_get_child(btn, 0), text);
}

void ui_button_set_enabled(lv_obj_t *btn, bool enabled)
{
    bool is_disabled = lv_obj_has_state(btn, LV_STATE_DISABLED);
    if (enabled && is_disabled) {
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
    } else if (!enabled && !is_disabled) {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (!cur || strcmp(cur, text) != 0) {
        lv_label_set_text(label, text);
    }
}

void ui_set_text_color(lv_obj_t *obj, lv_color_t color)
{
    if (lv_color_to_u32(lv_obj_get_style_text_color(obj, 0)) != lv_color_to_u32(color)) {
        lv_obj_set_style_text_color(obj, color, 0);
    }
}

lv_color_t ui_level_color(int level)
{
    return level >= 2 ? UI_COLOR_ALERT : (level == 1 ? UI_COLOR_WARN : UI_COLOR_OK);
}

void ui_format_ml(char *out, size_t out_size, float ml)
{
    int t = (int)(ml * 10.0f + 0.5f);
    if (t < 0) {
        t = 0;
    }
    if (t > 999999) {
        t = 999999;
    }
    snprintf(out, out_size, "%d.%d", t / 10, t % 10);
}

void ui_label_oneline(lv_obj_t *label, int32_t width)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

void ui_set_bg_color(lv_obj_t *obj, lv_color_t color)
{
    if (lv_color_to_u32(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)) != lv_color_to_u32(color)) {
        lv_obj_set_style_bg_color(obj, color, 0);
    }
}

void ui_set_border(lv_obj_t *obj, lv_color_t color, int32_t width)
{
    if (lv_obj_get_style_border_width(obj, LV_PART_MAIN) != width) {
        lv_obj_set_style_border_width(obj, width, 0);
    }
    if (lv_color_to_u32(lv_obj_get_style_border_color(obj, LV_PART_MAIN)) != lv_color_to_u32(color)) {
        lv_obj_set_style_border_color(obj, color, 0);
    }
}

void ui_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
