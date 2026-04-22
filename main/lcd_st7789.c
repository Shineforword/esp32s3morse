#include "lcd_st7789.h"
#include "app_config.h"

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

extern const uint8_t font8x8_basic[128][8];

static const char *TAG = "lcd_st7789";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static uint16_t *s_line_dma;
static bool s_hw_ready;
/** 多任务（httpd、morse_player）共用 SPI 刷屏，无锁会总线冲突卡死；绘行中 delay 避免饿死 WiFi */
static SemaphoreHandle_t s_lcd_mx;

static void lcd_draw_lock(void)
{
    if (!s_lcd_mx) {
        s_lcd_mx = xSemaphoreCreateMutex();
    }
    if (s_lcd_mx) {
        (void)xSemaphoreTake(s_lcd_mx, portMAX_DELAY);
    }
}

static void lcd_draw_unlock(void)
{
    if (s_lcd_mx) {
        xSemaphoreGive(s_lcd_mx);
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | ((uint16_t)b >> 3));
}

static void draw_fill_rect(esp_lcd_panel_handle_t panel, int x, int y, int w, int h, uint16_t color)
{
    if (!s_line_dma) {
        return;
    }
    if (w > LCD_H_RES) {
        w = LCD_H_RES;
    }
    for (int i = 0; i < w; i++) {
        s_line_dma[i] = color;
    }
    for (int row = 0; row < h; row++) {
        esp_lcd_panel_draw_bitmap(panel, x, y + row, x + w, y + row + 1, s_line_dma);
        if ((row & 15) == 15) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void draw_char(esp_lcd_panel_handle_t panel, int x, int y, char ch, uint16_t fg, uint16_t bg)
{
    unsigned uc = (unsigned char)ch;
    if (uc >= 128) {
        uc = ' ';
    }
    const uint8_t *g = font8x8_basic[uc];
    static uint16_t block[8 * 8] __attribute__((aligned(16)));
    int k = 0;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool on = (g[row] & (1U << col)) != 0;
            block[k++] = on ? fg : bg;
        }
    }
    esp_lcd_panel_draw_bitmap(panel, x, y, x + 8, y + 8, block);
}

static void draw_string(esp_lcd_panel_handle_t panel, int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++) {
        if (*s < 32) {
            continue;
        }
        draw_char(panel, x, y, *s, fg, bg);
        x += 8;
        if (x + 8 > LCD_H_RES) {
            break;
        }
    }
}

static void paint_hello(void)
{
    /* 底色偏亮，便于确认「不是只有背光」；仍黑再查接线与 LCD_GAP / SPI 模式 */
    uint16_t bg = rgb565(0x20, 0x50, 0x90);
    uint16_t fg = rgb565(0xff, 0xff, 0xff);

    draw_fill_rect(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, bg);

    const char *msg = "Hello World";
    int text_w = (int)strlen(msg) * 8;
    int x0 = (LCD_H_RES - text_w) / 2;
    int y0 = LCD_V_RES / 2 - 4;
    if (x0 < 0) {
        x0 = 0;
    }
    draw_string(s_panel, x0, y0, msg, fg, bg);
}

#define LCD_MORSE_LINE_CHARS (LCD_H_RES / 8)

/** 单行最多 LCD_MORSE_LINE_CHARS 个字符；超长尾部用 "..." */
static void morse_copy_trunc_line(char *dst, size_t dstsz, const char *src)
{
    if (dstsz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t max = dstsz - 1;
    size_t n = strlen(src);
    if (n <= max) {
        memcpy(dst, src, n + 1);
        return;
    }
    if (max < 4) {
        return;
    }
    size_t keep = max - 3;
    memcpy(dst, src, keep);
    memcpy(dst + keep, "...", 4);
}

static void paint_morse_live(const char *pattern, const char *decoded)
{
    uint16_t bg = rgb565(0xff, 0xff, 0xff);
    uint16_t fg = rgb565(0xf8, 0x00, 0x00);

    draw_fill_rect(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, bg);
    draw_string(s_panel, 4, 8, "MORSE", fg, bg);

    char line[LCD_MORSE_LINE_CHARS + 1];
    const char *d = (decoded && decoded[0]) ? decoded : "-";
    morse_copy_trunc_line(line, sizeof(line), d);
    draw_string(s_panel, 0, 28, line, fg, bg);

    const char *p = (pattern && pattern[0]) ? pattern : "-";
    size_t plen = strlen(p);
    if (plen <= (size_t)LCD_MORSE_LINE_CHARS) {
        morse_copy_trunc_line(line, sizeof(line), p);
        draw_string(s_panel, 0, 44, line, fg, bg);
    } else {
        memcpy(line, p, (size_t)LCD_MORSE_LINE_CHARS);
        line[LCD_MORSE_LINE_CHARS] = '\0';
        draw_string(s_panel, 0, 44, line, fg, bg);
        morse_copy_trunc_line(line, sizeof(line), p + LCD_MORSE_LINE_CHARS);
        draw_string(s_panel, 0, 60, line, fg, bg);
    }
}

static void paint_morse_preview(char sym)
{
    uint16_t bg = rgb565(0xff, 0xff, 0xff);
    uint16_t fg = rgb565(0xf8, 0x00, 0x00);

    draw_fill_rect(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, bg);
    draw_string(s_panel, 4, 8, "MORSE", fg, bg);
    const char *row = (sym == '-') ? "- PREVIEW" : ". PREVIEW";
    draw_string(s_panel, 0, 40, row, fg, bg);
}

esp_lcd_panel_handle_t lcd_st7789_get_panel(void)
{
    return s_panel;
}

#if LCD_ST7789_LVGL_EXTRA_INIT
/**
 * LVGL st7789.c 中的电源/伽马片段：esp_lcd 自带 ST7789 初始化较简，部分 1.69 模组会因此只有背光。
 * 分两段：SLPOUT 前 / SLPOUT+MADCTL+COLMOD 之后（与 esp_lcd 的 panel_init 衔接）。
 */
static esp_err_t lcd_st7789_lvgl_extra_pre_slpout(void)
{
    static const uint8_t d_cf[] = {0x00, 0x83, 0x30};
    static const uint8_t d_ed[] = {0x64, 0x03, 0x12, 0x81};
    static const uint8_t d_e8[] = {0x85, 0x01, 0x79};
    static const uint8_t d_cb[] = {0x39, 0x2c, 0x00, 0x34, 0x02};
    static const uint8_t d_f7[] = {0x20};
    static const uint8_t d_ea[] = {0x00, 0x00};
    static const uint8_t d_c0[] = {0x26};
    static const uint8_t d_c1[] = {0x11};
    static const uint8_t d_c5[] = {0x35, 0x3e};
    static const uint8_t d_c7[] = {0xbe};

    esp_err_t e;
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xcf, d_cf, sizeof(d_cf));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xed, d_ed, sizeof(d_ed));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xe8, d_e8, sizeof(d_e8));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xcb, d_cb, sizeof(d_cb));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xf7, d_f7, sizeof(d_f7));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xea, d_ea, sizeof(d_ea));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xc0, d_c0, sizeof(d_c0));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xc1, d_c1, sizeof(d_c1));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xc5, d_c5, sizeof(d_c5));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xc7, d_c7, sizeof(d_c7));
    return e;
}

static esp_err_t lcd_st7789_lvgl_extra_post_slpout(void)
{
    static const uint8_t d_b1[] = {0x00, 0x1b};
    static const uint8_t d_f2[] = {0x08};
    static const uint8_t d_26[] = {0x01};
    static const uint8_t d_e0[] = {0xd0, 0x00, 0x02, 0x07, 0x0a, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0e, 0x12, 0x14, 0x17};
    static const uint8_t d_e1[] = {0xd0, 0x00, 0x02, 0x07, 0x0a, 0x28, 0x31, 0x54, 0x47, 0x0e, 0x1c, 0x17, 0x1b, 0x1e};
    static const uint8_t d_b7[] = {0x07};
    static const uint8_t d_b6[] = {0x0a, 0x82, 0x27, 0x00};

    esp_err_t e;
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xb1, d_b1, sizeof(d_b1));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xf2, d_f2, sizeof(d_f2));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0x26, d_26, sizeof(d_26));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xe0, d_e0, sizeof(d_e0));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xe1, d_e1, sizeof(d_e1));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xb7, d_b7, sizeof(d_b7));
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lcd_panel_io_tx_param(s_panel_io, 0xb6, d_b6, sizeof(d_b6));
    return e;
}
#endif /* LCD_ST7789_LVGL_EXTRA_INIT */

esp_err_t lcd_st7789_ensure_init(void)
{
    if (s_hw_ready) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 64 * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = LCD_SPI_MODE,
        .trans_queue_depth = 10,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }
    s_panel_io = io;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_ST7789_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
#if LCD_RGB565_LITTLE_ENDIAN
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
#else
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
#endif
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    vTaskDelay(pdMS_TO_TICKS(20));
#if LCD_ST7789_LVGL_EXTRA_INIT
    {
        esp_err_t e = lcd_st7789_lvgl_extra_pre_slpout();
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "lvgl extra pre init failed: %s", esp_err_to_name(e));
            return e;
        }
    }
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    vTaskDelay(pdMS_TO_TICKS(120));
#if LCD_ST7789_LVGL_EXTRA_INIT
    {
        esp_err_t e = lcd_st7789_lvgl_extra_post_slpout();
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "lvgl extra post init failed: %s", esp_err_to_name(e));
            return e;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif

    /* 先设方向与偏移，再开正常显示模式与 DISPON（顺序不当易有背光无画面） */
#if LCD_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
#endif
#if LCD_SWAP_XY
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X != 0, LCD_MIRROR_Y != 0));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y));

    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_panel_io, LCD_CMD_NORON, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!s_line_dma) {
        s_line_dma = heap_caps_aligned_alloc(16, (size_t)LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_line_dma) {
            ESP_LOGE(TAG, "DMA line buffer alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }

    s_hw_ready = true;
    ESP_LOGI(TAG, "ST7789 %dx%d SPI host=%d mode=%d gap=(%d,%d) lvgl_extra=%d ok", LCD_H_RES, LCD_V_RES,
             (int)LCD_SPI_HOST, LCD_SPI_MODE, LCD_GAP_X, LCD_GAP_Y, LCD_ST7789_LVGL_EXTRA_INIT);
    return ESP_OK;
}

esp_err_t lcd_st7789_show_hello(void)
{
    esp_err_t err = lcd_st7789_ensure_init();
    if (err != ESP_OK) {
        return err;
    }
    lcd_draw_lock();
    paint_hello();
    lcd_draw_unlock();
    ESP_LOGI(TAG, "Hello World painted");
    return ESP_OK;
}

esp_err_t lcd_st7789_show_morse_live(const char *pattern, const char *decoded)
{
    esp_err_t err = lcd_st7789_ensure_init();
    if (err != ESP_OK) {
        return err;
    }
    lcd_draw_lock();
    paint_morse_live(pattern, decoded);
    lcd_draw_unlock();
    return ESP_OK;
}

esp_err_t lcd_st7789_show_morse_preview(char sym_dot_or_dash)
{
    esp_err_t err = lcd_st7789_ensure_init();
    if (err != ESP_OK) {
        return err;
    }
    lcd_draw_lock();
    paint_morse_preview(sym_dot_or_dash);
    lcd_draw_unlock();
    return ESP_OK;
}
