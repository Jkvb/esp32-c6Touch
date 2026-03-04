idf_component_register(
    SRCS
        "main.c"
        "display_st7789_lvgl.c"
        "ui_clock.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_timer
        esp_lcd
        driver
        lvgl
)