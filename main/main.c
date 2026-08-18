/*
 * main.c —— 程序入口（只做启动顺序，业务在 components/）
 *
 * 功能开关：idf.py menuconfig →「学习示例开关」
 */

#include "sdkconfig.h"

#include "morse_http.h"
#include "wifi_softap.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#if CONFIG_DEMO_MORSE
#include "board_led.h"
#include "morse_player.h"
#include "led_strip.h"
#endif

#if CONFIG_DEMO_MUSIC && !CONFIG_DEMO_MORSE
#include "morse_buzzer.h"
#endif

static const char *TAG = "app_main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

#if CONFIG_DEMO_MORSE
    led_strip_handle_t strip = NULL;
    ESP_ERROR_CHECK(board_led_init(&strip));
    morse_player_init(strip);
#elif CONFIG_DEMO_MUSIC
    morse_buzzer_init();
#endif

    wifi_softap_init();
    morse_http_start();

    ESP_LOGI(TAG, "ready (morse=%d music=%d lcd=%d tank=%d mqtt=%d)",
#if CONFIG_DEMO_MORSE
             1
#else
             0
#endif
             ,
#if CONFIG_DEMO_MUSIC
             1
#else
             0
#endif
             ,
#if CONFIG_DEMO_LCD
             1
#else
             0
#endif
             ,
#if CONFIG_DEMO_TANK
             1
#else
             0
#endif
             ,
#if CONFIG_DEMO_MQTT
             1
#else
             0
#endif
    );
}
