idf_component_register(
    SRCS
        "main.c"
        "board_i2c.c"
        "display_st7789_lvgl.c"
        "ui_clock.c"
        "gesture_profiles.c"
        "imu_qmi8658.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_timer esp_lcd esp_system esp_driver_i2c driver lvgl esp_event esp_netif esp_wifi nvs_flash lwip
)
