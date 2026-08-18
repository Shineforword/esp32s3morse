/*
 * main.c —— 程序入口（只做启动顺序，业务在 components/）
 *
 *   1) NVS
 *   2) 板载 WS2812
 *   3) morse_player（依赖灯带）
 *   4) SoftAP（可选 APSTA）
 *   5) HTTP 门户
 */

#include "board_led.h"
#include "morse_http.h"
#include "morse_player.h"
#include "wifi_softap.h"

#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"
#include "nvs_flash.h"

static const char *TAG = "app_main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    led_strip_handle_t strip = NULL;
    ESP_ERROR_CHECK(board_led_init(&strip));

    morse_player_init(strip);
    wifi_softap_init();
    morse_http_start();

    ESP_LOGI(TAG, "ready");
}
