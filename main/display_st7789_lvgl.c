#include "display_st7789_lvgl.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "board_i2c.h"
#include "ui_clock.h"

static const char *TAG = "DISP";

/* WAVESHARE ESP32-C6-LCD-1.9 (ST7789 170x320 con offset X=35)
   MOSI=GPIO4, SCK=GPIO5, DC=GPIO6, CS=GPIO7, RST=GPIO14, BL=GPIO15
*/
#define LCD_HOST         SPI2_HOST
#define PIN_NUM_SCLK     5
#define PIN_NUM_MOSI     4
#define PIN_NUM_MISO     -1
#define PIN_NUM_LCD_DC   6
#define PIN_NUM_LCD_CS   7
#define PIN_NUM_LCD_RST  14
#define PIN_NUM_LCD_BL   15

#define LCD_H_RES        170
#define LCD_V_RES        320
#define LCD_X_GAP        35
#define LCD_Y_GAP        0
#define LCD_DRAW_BUF_LINES 40

/* Si el BL parpadea o no se queda prendido, cambia a 0 (activo-bajo) */
#define LCD_BL_ACTIVE_HIGH   0

static esp_timer_handle_t s_lv_tick_timer;

/* Touch (CST816S) */
#define TOUCH_ADDR_CST816   0x15

static bool s_touch_inited = false;
static lv_indev_t *s_touch_indev = NULL;

static esp_err_t touch_rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return board_i2c_read_reg(TOUCH_ADDR_CST816, reg, buf, len, 30);
}

static esp_err_t touch_probe(void)
{
    uint8_t b[2] = {0};
    esp_err_t r = touch_rd(0xA7, b, sizeof(b)); /* ChipID/FwVer */
    if (r != ESP_OK) return r;
    ESP_LOGI(TAG, "Touch detectado addr=0x%02X chip=0x%02X fw=0x%02X", TOUCH_ADDR_CST816, b[0], b[1]);
    return ESP_OK;
}

static void touch_scan_bus(void)
{
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (board_i2c_probe(addr, 10) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device encontrado: 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "I2C scan: no se detectaron dispositivos en SDA=%d/SCL=%d",
                 BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    }
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t p[6] = {0};
    esp_err_t r = touch_rd(0x01, p, sizeof(p));
    if (r != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        ui_clock_set_touch_debug(0, 0, false);
        return;
    }

    uint8_t points = p[1] & 0x0F;
    bool pressed = points > 0;
    if (!pressed) {
        data->state = LV_INDEV_STATE_RELEASED;
        ui_clock_set_touch_debug(0, 0, false);
        return;
    }

    int16_t x = (int16_t)(((p[2] & 0x0F) << 8) | p[3]);
    int16_t y = (int16_t)(((p[4] & 0x0F) << 8) | p[5]);

    /* Ajuste de touch según la rotación activa del display */
    disp_rot_t rot = display_st7789_get_rotation();
    int16_t tx = x;
    int16_t ty = y;

    switch (rot) {
        case DISP_ROT_90:
            tx = y;
            ty = (int16_t)(LCD_H_RES - 1 - x);
            break;
        case DISP_ROT_180:
            tx = (int16_t)(LCD_H_RES - 1 - x);
            ty = (int16_t)(LCD_V_RES - 1 - y);
            break;
        case DISP_ROT_270:
            tx = (int16_t)(LCD_V_RES - 1 - y);
            ty = x;
            break;
        case DISP_ROT_0:
        default:
            break;
    }

    x = tx;
    y = ty;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int16_t max_x = (rot == DISP_ROT_90 || rot == DISP_ROT_270)
                        ? (LCD_V_RES - 1)
                        : (LCD_H_RES - 1);
    int16_t max_y = (rot == DISP_ROT_90 || rot == DISP_ROT_270)
                        ? (LCD_H_RES - 1)
                        : (LCD_V_RES - 1);
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
    ui_clock_set_touch_debug(x, y, true);
}

static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_disp = NULL;
static atomic_uchar s_rot = DISP_ROT_0;
static QueueHandle_t s_rotation_queue = NULL;

/* DMA done -> LVGL flush ready */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    esp_lcd_panel_handle_t panel_handle =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    esp_err_t result = esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1,
                                                 (void *)px_map);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LCD flush falló: %s", esp_err_to_name(result));
        /* No habrá callback DMA cuando el envío falla: libera a LVGL aquí. */
        lv_display_flush_ready(disp);
    }
}

static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

lv_display_t* display_st7789_lvgl_init(void)
{
    /* Backlight ON */
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(PIN_NUM_LCD_BL, LCD_BL_ACTIVE_HIGH ? 1 : 0);

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_V_RES * LCD_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Panel IO (SPI) */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* Panel ST7789 */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_X_GAP, LCD_Y_GAP));
    /* Forzar colores reales: negro=negro, blanco=blanco */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* LVGL */
    lv_init();
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!disp) {
        ESP_LOGE(TAG, "No se pudo crear el display LVGL");
        return NULL;
    }
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    /* Buffer parcial dimensionado en bytes RGB565 para la anchura máxima rotada. */
    const uint32_t max_stride = lv_draw_buf_width_to_stride(LCD_V_RES, LV_COLOR_FORMAT_RGB565);
    const size_t dma_free = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint32_t lines = LCD_DRAW_BUF_LINES;
    if ((size_t)max_stride * lines > dma_free) {
        lines = (uint32_t)(dma_free / max_stride);
    }
    if (lines == 0) {
        ESP_LOGE(TAG, "No hay RAM DMA para una línea RGB565 (%" PRIu32 " bytes).", max_stride);
        return NULL;
    }

    const size_t buf_sz = (size_t)max_stride * lines;
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGE(TAG, "No se pudo reservar RAM DMA (%u bytes).", (unsigned)buf_sz);
        return NULL;
    }

    lv_display_set_buffers(disp, buf, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    if (board_i2c_init() == ESP_OK && touch_probe() == ESP_OK) {
        s_touch_indev = lv_indev_create();
        if (s_touch_indev) {
            lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
            s_touch_inited = true;
            ESP_LOGI(TAG, "Touch LVGL listo (CST816)");
        } else {
            ESP_LOGE(TAG, "Touch detectado, pero LVGL no pudo crear el input");
        }
    } else {
        ESP_LOGW(TAG, "Touch no inicializó. Revisa pins SDA/SCL y addr 0x15.");
        touch_scan_bus();
    }

    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp));

    const esp_timer_create_args_t targs = {
        .callback = &lv_tick_cb,
        .name = "lv_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_lv_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lv_tick_timer, 2000));

    s_panel = panel_handle;
    s_disp  = disp;
    atomic_store_explicit(&s_rot, DISP_ROT_0, memory_order_release);
    s_rotation_queue = xQueueCreate(1, sizeof(disp_rot_t));
    if (!s_rotation_queue) {
        ESP_LOGW(TAG, "Sin cola de rotacion: la pantalla queda en orientacion fija");
    }

    ESP_LOGI(TAG, "Display + LVGL listo (%dx%d), gap(%d,%d), touch=%s.",
             LCD_H_RES, LCD_V_RES, LCD_X_GAP, LCD_Y_GAP, s_touch_inited ? "OK" : "OFF");
    return disp;
}

disp_rot_t display_st7789_get_rotation(void)
{
    return (disp_rot_t)atomic_load_explicit(&s_rot, memory_order_acquire);
}

bool display_st7789_touch_ready(void)
{
    return s_touch_inited;
}

void display_st7789_request_rotation(disp_rot_t rot)
{
    if (!s_rotation_queue || rot > DISP_ROT_270) return;
    xQueueOverwrite(s_rotation_queue, &rot);
}

bool display_st7789_service(void)
{
    if (!s_rotation_queue) return false;

    disp_rot_t current = display_st7789_get_rotation();
    disp_rot_t requested = current;
    if (xQueueReceive(s_rotation_queue, &requested, 0) != pdTRUE) {
        return false;
    }
    if (requested == current) {
        return false;
    }

    display_st7789_set_rotation(requested);
    return true;
}

void display_st7789_set_rotation(disp_rot_t rot)
{
    if (!s_panel || !s_disp) return;
    if (rot == display_st7789_get_rotation()) return;

    bool swap = false, mx = false, my = false;
    int w = LCD_H_RES, h = LCD_V_RES;
    int gap_x = LCD_X_GAP, gap_y = LCD_Y_GAP;

    switch (rot) {
        case DISP_ROT_0:
            swap=false; mx=false; my=false;
            w=LCD_H_RES; h=LCD_V_RES;
            gap_x=LCD_X_GAP; gap_y=LCD_Y_GAP;
            break;
        case DISP_ROT_90:
            swap=true;  mx=true;  my=false;
            w=LCD_V_RES; h=LCD_H_RES;
            gap_x=LCD_Y_GAP; gap_y=LCD_X_GAP;
            break;
        case DISP_ROT_180:
            swap=false; mx=true;  my=true;
            w=LCD_H_RES; h=LCD_V_RES;
            gap_x=LCD_X_GAP; gap_y=LCD_Y_GAP;
            break;
        case DISP_ROT_270:
            swap=true;  mx=false; my=true;
            w=LCD_V_RES; h=LCD_H_RES;
            gap_x=LCD_Y_GAP; gap_y=LCD_X_GAP;
            break;
    }

    esp_err_t result = esp_lcd_panel_swap_xy(s_panel, swap);
    if (result == ESP_OK) result = esp_lcd_panel_mirror(s_panel, mx, my);
    if (result == ESP_OK) result = esp_lcd_panel_set_gap(s_panel, gap_x, gap_y);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Rotación %d falló: %s", (int)rot, esp_err_to_name(result));
        return;
    }

    lv_display_set_resolution(s_disp, w, h);
    atomic_store_explicit(&s_rot, (unsigned char)rot, memory_order_release);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
}
