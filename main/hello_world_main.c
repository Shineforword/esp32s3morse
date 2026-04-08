/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"

void app_main(void)
{
    const char *TAG = "rgb";

    // 板载可寻址 RGB（常见是 WS2812/NeoPixel）需要严格的单线时序，ESP-IDF 推荐用 RMT 外设来发波形。
    // 这类 ESP32-S3 UNO / N16R8 板载 WS2812 数据脚常见接在 GPIO48（你这块板已验证可用）。
    // 如果你换了板子不亮：优先把这里改成 38 试试，或按 PCB 丝印/原理图的 IO 号填写。
    const int led_gpio = 48;
    const int led_count = 1;

    led_strip_handle_t strip = NULL;
    led_strip_config_t strip_config = {
        .strip_gpio_num = led_gpio,
        .max_leds = led_count,
        .led_model = LED_MODEL_WS2812,
        // WS2812 常见颜色顺序是 GRB（不是 RGB），写错会出现“颜色对不上”的现象。
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = 0,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        // 10MHz 分辨率（100ns/计数）对 WS2812 时序足够细；过低可能导致颜色不稳/闪烁。
        .resolution_hz = 10 * 1000 * 1000,
        .flags = {
            .with_dma = 0,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "WS2812 init ok, gpio=%d", led_gpio);

    while (1) {
        // 这里用最直观的“红→绿→蓝→灭”做自检：能循环变色说明 RMT 时序和数据脚都没问题。
        led_strip_set_pixel(strip, 0, 255, 0, 0);
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Green
        led_strip_set_pixel(strip, 0, 0, 255, 0);
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Blue
        led_strip_set_pixel(strip, 0, 0, 0, 255);
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Off
        led_strip_clear(strip);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
