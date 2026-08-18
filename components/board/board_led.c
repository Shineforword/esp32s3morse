#include "board_led.h"

#include "app_config.h"

#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "board_led";

esp_err_t board_led_init(led_strip_handle_t *out_strip)
{
    if (out_strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

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

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, out_strip);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WS2812 ok gpio=%d", MORSE_LED_GPIO);
    }
    return err;
}
