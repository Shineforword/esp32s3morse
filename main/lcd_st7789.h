#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

/**
 * 初始化 SPI + ST7789，清屏并绘制 "Hello World"。
 * 引脚见 app_config.h（LCD_PIN_*）。
 */
esp_err_t lcd_st7789_init_and_show_hello(void);

/** 供后续扩展：初始化成功后可用 panel 句柄绘图 */
esp_lcd_panel_handle_t lcd_st7789_get_panel(void);
