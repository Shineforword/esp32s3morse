#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

/**
 * 确保 SPI + ST7789 已初始化（首次调用时完成；引脚见 app_config.h）。
 */
esp_err_t lcd_st7789_ensure_init(void);

/**
 * 清屏并在屏上居中绘制 "Hello World"（会先 ensure_init）。
 */
esp_err_t lcd_st7789_show_hello(void);

esp_lcd_panel_handle_t lcd_st7789_get_panel(void);
