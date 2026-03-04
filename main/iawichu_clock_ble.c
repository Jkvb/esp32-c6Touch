#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Drivers de pantalla ESP-IDF
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "lvgl.h"

static const char *TAG = "IAWICHU_C6";

// --- CONFIGURACIÓN DE PINES ---
#define LCD_HOST       SPI2_HOST
#define PIN_NUM_SCLK   22
#define PIN_NUM_MOSI   23
#define PIN_NUM_MISO   -1
#define PIN_NUM_LCD_DC 21
#define PIN_NUM_LCD_CS 7
#define PIN_NUM_LCD_RST 18

#define LCD_H_RES      240
#define LCD_V_RES      240

lv_obj_t *lbl_time;

// Callback de fin de transferencia DMA
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_display_t * disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// Función de volcado de LVGL a la pantalla
static void lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
}

// Timer para actualizar el reloj cada segundo
static void clock_timer_cb(lv_timer_t * t) {
    static int seg = 0;
    static int min = 0;
    static int hour = 12;
    char buf[16];
    
    seg++;
    if(seg >= 60) { seg = 0; min++; }
    if(min >= 60) { min = 0; hour++; }
    if(hour >= 24) hour = 0;

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, min, seg);
    lv_label_set_text(lbl_time, buf);
}

void app_main(void) {
    ESP_LOGI(TAG, "Arrancando sistema...");

    // 1. Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Configurar Bus SPI
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 3. Configurar Panel ST7789
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    // 4. Inicializar LVGL 9
    lv_init();
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    
    // Asignar el display al contexto de IO para el callback de DMA
    io_config.user_ctx = disp;

    // 5. Buffer de dibujo en RAM DMA (Ajustado para C6)
    size_t dma_free = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    size_t bytes_per_line = LCD_H_RES * sizeof(lv_color_t);
    uint32_t lines = dma_free / bytes_per_line;
    if (lines > 40) lines = 40; // Solo 40 líneas para ahorrar RAM
    size_t buf_sz = bytes_per_line * lines;

    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGE(TAG, "No se pudo reservar RAM DMA!");
        return;
    }
    lv_display_set_buffers(disp, buf, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 6. UI Básica
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lbl_time = lv_label_create(scr);
    lv_label_set_text(lbl_time, "00:00:00");
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x00FF00), 0); // Verde
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_32, 0); // Fuente base LVGL
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, 0);

    lv_timer_create(clock_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "Loop principal iniciado");
    while (1) {
        uint32_t delay = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(delay > 5 ? delay : 5));
    }
}
