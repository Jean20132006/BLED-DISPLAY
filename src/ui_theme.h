/**
 * @file ui_theme.h
 * @brief Shared look of the BLED UI: a clinical palette (calm blues/greys,
 *        green = OK, amber = attention, red = alert), fonts that are
 *        actually enabled in this project's sdkconfig, and a few widget
 *        helpers so every tab builds cards/buttons the same way.
 *
 * Only the Montserrat sizes this project enables are used (14, 16, 18, 20,
 * 36, 48); each macro falls back to the default font if a size is ever
 * disabled, so the build can never break on a missing font.
 */
#pragma once

#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_STATUSBAR_H 32
#define UI_TABBAR_H    46

#define UI_COLOR_PRIMARY   lv_color_hex(0x0B6BA6)   /* clinical blue */
#define UI_COLOR_PRIMARY_D lv_color_hex(0x084F7A)
#define UI_COLOR_TINT      lv_color_hex(0xE3EEF6)   /* pale blue for unselected controls */
#define UI_COLOR_BG        lv_color_hex(0xEEF2F6)   /* screen / page background */
#define UI_COLOR_CARD      lv_color_hex(0xFFFFFF)
#define UI_COLOR_BORDER    lv_color_hex(0xD3DCE5)
#define UI_COLOR_TEXT      lv_color_hex(0x1B2A38)
#define UI_COLOR_MUTED     lv_color_hex(0x5F7183)
#define UI_COLOR_OK        lv_color_hex(0x1E9E5A)
#define UI_COLOR_WARN      lv_color_hex(0xD98300)
#define UI_COLOR_ALERT     lv_color_hex(0xD0342C)
#define UI_COLOR_NAVY      lv_color_hex(0x0B2545)   /* status bar */
#define UI_COLOR_BLOOD     lv_color_hex(0xB3202A)
#define UI_COLOR_OK_BG     lv_color_hex(0xE4F5EC)
#define UI_COLOR_WARN_BG   lv_color_hex(0xFFF3DC)
#define UI_COLOR_ALERT_BG  lv_color_hex(0xFDE7E5)

#if LV_FONT_MONTSERRAT_14
#define UI_FONT_SM   (&lv_font_montserrat_14)
#else
#define UI_FONT_SM   LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_16
#define UI_FONT_MD   (&lv_font_montserrat_16)
#else
#define UI_FONT_MD   LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_18
#define UI_FONT_LG   (&lv_font_montserrat_18)
#else
#define UI_FONT_LG   LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_20
#define UI_FONT_XL   (&lv_font_montserrat_20)
#else
#define UI_FONT_XL   LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_36
#define UI_FONT_NUM  (&lv_font_montserrat_36)
#else
#define UI_FONT_NUM  LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_48
#define UI_FONT_HUGE (&lv_font_montserrat_48)
#else
#define UI_FONT_HUGE LV_FONT_DEFAULT
#endif

typedef enum {
    UI_BTN_PRIMARY,
    UI_BTN_SUCCESS,
    UI_BTN_DANGER,
    UI_BTN_NEUTRAL,
} ui_btn_kind_t;

/** Apply the palette through LVGL's default theme. Call once, before creating any widget. */
void ui_theme_init(void);

/** White rounded card with a soft border/shadow (same look as the Home tab cards). */
lv_obj_t *ui_card(lv_obj_t *parent, int32_t w, int32_t h);

/** Plain label with font + colour. */
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);

/** Touch-friendly button with a centred label; cb (optional) is called on LV_EVENT_CLICKED. */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int32_t w, int32_t h, ui_btn_kind_t kind, lv_event_cb_t cb,
                    void *user_data);
void ui_button_set_text(lv_obj_t *btn, const char *text);
void ui_button_set_enabled(lv_obj_t *btn, bool enabled);

/** Fixed-width single-line label that ends in "..." instead of wrapping (LVGL only truncates when the height is fixed). */
void ui_label_oneline(lv_obj_t *label, int32_t width);

/** lv_label_set_text only when the text really changed (avoids needless redraws in the 500 ms refresh timers). */
void ui_set_text(lv_obj_t *label, const char *text);
/** lv_obj text colour only when it really changed. */
void ui_set_text_color(lv_obj_t *label, lv_color_t color);

/** "123.4" - a blood-volume number formatted from clamped integer tenths, so the output length is provably
 *  bounded (arbitrary-float printf is rejected by the strict -Wformat-truncation checks this project builds with). */
void ui_format_ml(char *out, size_t out_size, float ml);

/** The three setters below change a style/flag only when the value really differs. LVGL redraws an object
 *  every time a style or the HIDDEN flag is set, even to the same value, so calling the plain functions from a
 *  periodic refresh makes the screen blink. Always use these in timer callbacks. */
void ui_set_bg_color(lv_obj_t *obj, lv_color_t color);
void ui_set_border(lv_obj_t *obj, lv_color_t color, int32_t width);
void ui_set_hidden(lv_obj_t *obj, bool hidden);

/** 0 normal -> green, 1 warning -> amber, 2 alert -> red. */
lv_color_t ui_level_color(int level);

#ifdef __cplusplus
}
#endif
