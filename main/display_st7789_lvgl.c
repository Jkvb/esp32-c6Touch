#include "display_st7789_lvgl.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

static const char *TAG = "DISP";

/* ========= WAVESHARE ESP32-C6-LCD-1.9 (ST7789 170x320 con offset X=35) =========
   Pines (segÃºn Waveshare):
     MOSI=GPIO4, SCK=GPIO5, DC=GPIO6, CS=GPIO7, RST=GPIO14, BL=GPIO15
   ResoluciÃ³n visible: 170x320
   Offset: X=35, Y=0
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

/* Waveshare 1.9" suele usar BL activo-alto (GPIO15=1 enciende). */
#define LCD_BL_ACTIVE_HIGH   1
/* Si ves colores raros o pantalla en negro pero BL sÃ­ enciende, prueba:
   - LCD_INVERT_COLOR 1
   - LCD_MIRROR_Y 1
   - LCD_SWAP_XY 1 (rotaciÃ³n 90)
*/
#define LCD_INVERT_COLOR  0
#define LCD_MIRROR_X      0
#define LCD_MIRROR_Y      0
#define LCD_SWAP_XY       0

#define TOUCH_I2C_PORT      I2C_NUM_0
#define TOUCH_I2C_SDA       20
#define TOUCH_I2C_SCL       21
#define TOUCH_I2C_FREQ_HZ   400000
#define TOUCH_CST816_ADDR   0x15

static bool s_touch_pressed = false;
static uint16_t s_touch_x = 0;
static uint16_t s_touch_y = 0;
static bool s_touch_ready = false;
static uint32_t s_touch_reads = 0;

static esp_timer_handle_t s_lv_tick_timer;

static esp_err_t touch_i2c_ping(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd, pdMS_TO_TICKS(30));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t touch_cst816_init(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = TOUCH_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(TOUCH_I2C_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch: i2c_param_config fallo: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(TOUCH_I2C_PORT, cfg.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Touch: i2c_driver_install fallo: %s", esp_err_to_name(err));
        return err;
    }

    err = touch_i2c_ping(TOUCH_CST816_ADDR);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch CST816 no detectado en 0x%02X (SDA=%d,SCL=%d): %s",
                 TOUCH_CST816_ADDR, TOUCH_I2C_SDA, TOUCH_I2C_SCL, esp_err_to_name(err));
        return err;
    }

    s_touch_ready = true;
    ESP_LOGI(TAG, "Touch CST816 detectado en 0x%02X (SDA=%d,SCL=%d)",
             TOUCH_CST816_ADDR, TOUCH_I2C_SDA, TOUCH_I2C_SCL);
    return ESP_OK;
}

static bool touch_cst816_read_point(uint16_t *x, uint16_t *y, bool *pressed)
{
    uint8_t reg = 0x02;
    uint8_t data[6] = {0};
    esp_err_t err = i2c_master_write_read_device(TOUCH_I2C_PORT,
                                                  TOUCH_CST816_ADDR,
                                                  &reg,
                                                  1,
                                                  data,
                                                  sizeof(data),
                                                  pdMS_TO_TICKS(20));
    if (err != ESP_OK) {
        return false;
    }

    uint8_t points = data[1] & 0x0F;
    if (points == 0) {
        *pressed = false;
        return true;
    }

    uint16_t tx = (uint16_t)(((data[2] & 0x0F) << 8) | data[3]);
    uint16_t ty = (uint16_t)(((data[4] & 0x0F) << 8) | data[5]);

    if (tx >= LCD_H_RES) tx = LCD_H_RES - 1;
    if (ty >= LCD_V_RES) ty = LCD_V_RES - 1;

    *x = tx;
    *y = ty;
    *pressed = true;
    return true;
}

static void lvgl_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    (void)indev;

    if (s_touch_ready) {
        bool pressed = false;
        uint16_t x = s_touch_x;
        uint16_t y = s_touch_y;
        if (touch_cst816_read_point(&x, &y, &pressed)) {
            s_touch_pressed = pressed;
            s_touch_x = x;
            s_touch_y = y;
            s_touch_reads++;

            if ((s_touch_reads % 50U) == 1U && pressed) {
                ESP_LOGI(TAG, "Touch activo x=%u y=%u", (unsigned)x, (unsigned)y);
            }
        }
    }

    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    data->state = s_touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

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

/* LVGL -> ST7789 flush */
static void lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    esp_lcd_panel_handle_t panel_handle =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, (void *)px_map);
}

/* LVGL tick (2ms) */
static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

lv_display_t* display_st7789_lvgl_init(void)
{
    /* 0) Backlight ON */
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(PIN_NUM_LCD_BL, LCD_BL_ACTIVE_HIGH ? 1 : 0);

    /* 1) SPI bus */
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* 2) Panel IO (SPI) */
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

    /* 3) Panel ST7789 */
    /* Orden BGR corrige tinte magenta en varios ST7789/Waveshare. */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* >>> CLAVE: offset/gap para este panel (X=35) <<< */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_X_GAP, LCD_Y_GAP));

#if LCD_SWAP_XY
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
#endif
#if (LCD_MIRROR_X || LCD_MIRROR_Y)
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, LCD_MIRROR_X, LCD_MIRROR_Y));
#endif
#if LCD_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* 4) LVGL */
    lv_init();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

#if LV_COLOR_DEPTH == 16
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
#endif

    /* 5) Buffers DMA */
    size_t dma_free = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    size_t bytes_per_line = LCD_H_RES * sizeof(lv_color_t);
    uint32_t lines = (uint32_t)(dma_free / bytes_per_line);
    if (lines > 60) lines = 60;
    if (lines < 10) lines = 10;

    size_t buf_sz = bytes_per_line * lines;
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGE(TAG, "No se pudo reservar RAM DMA para LVGL (%u bytes).", (unsigned)buf_sz);
        return NULL;
    }

    lv_display_set_buffers(disp, buf, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* 5.1) Touch -> LVGL input */
    if (touch_cst816_init() == ESP_OK) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
        ESP_LOGI(TAG, "Touch LVGL input registrado");
    } else {
        ESP_LOGW(TAG, "Touch no inicializado; UI seguirá solo por gestos simulados/botones");
    }

    /* 6) Hook DMA-done callback -> flush_ready */
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp));

    /* 7) Tick timer LVGL */
    const esp_timer_create_args_t targs = {
        .callback = &lv_tick_cb,
        .name = "lv_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_lv_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lv_tick_timer, 2000)); /* 2ms */

    ESP_LOGI(TAG, "Display + LVGL listo (%dx%d), gap(%d,%d).", LCD_H_RES, LCD_V_RES, LCD_X_GAP, LCD_Y_GAP);
    return disp;
}
