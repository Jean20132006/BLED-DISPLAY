#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Build the whole application UI on the active LVGL screen: a permanent status bar plus a 6-tab view
 *  (Home / New Case / Records / Clock / Calendar / Connections). Call bled_core_init() first. */
void ui_app_create(void);

#ifdef __cplusplus
}
#endif
