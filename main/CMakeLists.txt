idf_component_register(
    SRCS
        "main.c"
        "display_st7789_lvgl.c"
        "ui_clock.c"
        "accel_qmi8658.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_timer
        esp_lcd
        driver
        lvgl
        lwip
        esp_wifi
        esp_netif
        esp_event
)