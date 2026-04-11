/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * SoftAP + 网页：短按/长按摩斯输入，红灯播放，常态黄灯；/ 字母间隔，// 单词间隔。
 */

#include "app_config.h"
#include "morse_http.h"
#include "morse_player.h"
#include "wifi_softap.h"

#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"
#include "nvs_flash.h"

static const char *TAG = "morse_ap";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    led_strip_handle_t strip;
    const led_strip_config_t strip_config = {
        .strip_gpio_num = MORSE_LED_GPIO,
        .max_leds = MORSE_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {.invert_out = 0},
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags = {.with_dma = 0},
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    ESP_LOGI(TAG, "WS2812 ok gpio=%d", MORSE_LED_GPIO);

    morse_player_init(strip);
    wifi_softap_init();
    morse_http_start();
}
