# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

idf_component_register(
    SRCS
        "app_main.c"
        "crowpanel_bsp.c"
        "crowpanel_touch.c"
        "stc8h1k28.c"
        "pcf8563.c"
        "wifi_manager.c"
        "ui_app.c"
        "ui_home.c"
        "ui_theme.c"
        "ui_statusbar.c"
        "ui_clock.c"
        "ui_calendar.c"
        "ui_newcase.c"
        "ui_records.c"
        "ui_connections.c"
        "img_profile_avatar.c"
        "bled_proto.c"
        "bled_time.c"
        "bled_link.c"
        "bled_storage.c"
        "bled_core.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp_timer
        nvs_flash
        esp_lcd_touch_gt911
        lvgl
        esp_wifi
        esp_netif
        esp_event
        fatfs
        sdmmc
)
  

