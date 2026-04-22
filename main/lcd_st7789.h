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

/**
 * 清屏并显示当前摩斯串与解码结果（会先 ensure_init）。
 * pattern / decoded 可为 NULL 或空串，空时屏上显示 "-"。
 */
esp_err_t lcd_st7789_show_morse_live(const char *pattern, const char *decoded);

/** 预览点/划时在屏上简短提示（会先 ensure_init）。 */
esp_err_t lcd_st7789_show_morse_preview(char sym_dot_or_dash);

esp_lcd_panel_handle_t lcd_st7789_get_panel(void);
