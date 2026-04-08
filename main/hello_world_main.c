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

static void hsv_to_rgb_u8(uint16_t hue_deg, uint8_t sat, uint8_t val,
                          uint8_t *r, uint8_t *g, uint8_t *b)
{
    // hue: [0, 359], sat/val: [0, 255]
    // 这里用整数算法避免引入浮点开销，足够驱动板载 WS2812 做平滑彩虹渐变。
    const uint8_t region = (hue_deg / 60) % 6;      // 0..5
    const uint16_t rem = (hue_deg % 60) * 255 / 60; // 0..255

    const uint16_t p = ((uint16_t)val * (255 - sat)) / 255;
    const uint16_t q = ((uint16_t)val * (255 - ((uint16_t)sat * rem) / 255)) / 255;
    const uint16_t t = ((uint16_t)val * (255 - ((uint16_t)sat * (255 - rem)) / 255)) / 255;

    switch (region) {
        case 0: *r = val; *g = (uint8_t)t; *b = (uint8_t)p; break;
        case 1: *r = (uint8_t)q; *g = val; *b = (uint8_t)p; break;
        case 2: *r = (uint8_t)p; *g = val; *b = (uint8_t)t; break;
        case 3: *r = (uint8_t)p; *g = (uint8_t)q; *b = val; break;
        case 4: *r = (uint8_t)t; *g = (uint8_t)p; *b = val; break;
        default:*r = val; *g = (uint8_t)p; *b = (uint8_t)q; break;
    }
}

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
        // 彩虹渐变：Hue 0..359 循环。brightness/speed 都可以随手改着玩。
        const uint8_t brightness = 40;        // 0..255（WS2812 很亮，建议先从 10~60 开始）
        const TickType_t frame = pdMS_TO_TICKS(15); // 越小越平滑但更“快”

        for (uint16_t hue = 0; hue < 360; hue++) {
            uint8_t r = 0, g = 0, b = 0;
            hsv_to_rgb_u8(hue, 255, brightness, &r, &g, &b);
            led_strip_set_pixel(strip, 0, r, g, b);
            led_strip_refresh(strip);
            vTaskDelay(frame);
        }
    }
}
