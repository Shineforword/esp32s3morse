/**
 * board_led.h —— 板载 WS2812 创建（RMT）
 */
#pragma once

#include "esp_err.h"
#include "led_strip.h"

/** 按 app_config.h 创建灯带，成功时 *out_strip 有效 */
esp_err_t board_led_init(led_strip_handle_t *out_strip);
