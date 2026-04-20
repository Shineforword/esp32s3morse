#include "lcd_st7789.h"
#include "app_config.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/task.h"

#include <string.h>

extern const uint8_t font8x8_basic[128][8];

static const char *TAG = "lcd_st7789";

static esp_lcd_panel_handle_t s_panel;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | ((uint16_t)b >> 3));
}

static void draw_fill_rect(esp_lcd_panel_handle_t panel, int x, int y, int w, int h, uint16_t color)
{
    static uint16_t line[LCD_H_RES];
    if (w > (int)sizeof(line) / sizeof(line[0])) {
        w = (int)sizeof(line) / sizeof(line[0]);
    }
    for (int i = 0; i < w; i++) {
        line[i] = color;
    }
    for (int row = 0; row < h; row++) {
        esp_lcd_panel_draw_bitmap(panel, x, y + row, x + w, y + row + 1, line);
    }
}

static void draw_char(esp_lcd_panel_handle_t panel, int x, int y, char ch, uint16_t fg, uint16_t bg)
{
    unsigned uc = (unsigned char)ch;
    if (uc >= 128) {
        uc = ' ';
    }
    const uint8_t *g = font8x8_basic[uc];
    static uint16_t block[8 * 8];
    int k = 0;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool on = (g[row] & (1U << (7 - col))) != 0;
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

esp_lcd_panel_handle_t lcd_st7789_get_panel(void)
{
    return s_panel;
}

esp_err_t lcd_st7789_init_and_show_hello(void)
{
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
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    /* SLPOUT 后控制器需稳定时间，否则易整屏不刷新 */
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

#if LCD_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
#endif
#if LCD_SWAP_XY
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y));

    uint16_t bg = rgb565(0x1a, 0x2e, 0x4a);
    uint16_t fg = rgb565(0xe0, 0xe8, 0xf0);

    draw_fill_rect(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, bg);

    const char *msg = "Hello World";
    int text_w = (int)strlen(msg) * 8;
    int x0 = (LCD_H_RES - text_w) / 2;
    int y0 = LCD_V_RES / 2 - 4;
    if (x0 < 0) {
        x0 = 0;
    }
    draw_string(s_panel, x0, y0, msg, fg, bg);

    ESP_LOGI(TAG, "ST7789 %dx%d Hello World ok", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}
