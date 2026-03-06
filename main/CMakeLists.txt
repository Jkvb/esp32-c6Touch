idf_component_register(
    SRCS
        "main.c"
        "display_st7789_lvgl.c"
        "ui_clock.c"
        "imu_qmi8658.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_timer esp_lcd driver lvgl esp_event esp_netif esp_wifi nvs_flash lwip
)
