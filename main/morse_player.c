#include "morse_player.h"

#include "app_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

#include <string.h>

static const char *TAG = "morse_play";

typedef struct {
    char pattern[MORSE_PATTERN_MAX];
} play_msg_t;

static led_strip_handle_t s_strip;
static QueueHandle_t s_play_queue;
static SemaphoreHandle_t s_led_mutex;

static void rgb_set(led_strip_handle_t strip, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
}

static void rgb_off(led_strip_handle_t strip)
{
    led_strip_clear(strip);
}

static void morse_play_red(led_strip_handle_t strip, const char *pattern, uint32_t unit_ms, uint8_t brightness)
{
    const char *s = pattern;
    while (*s == '/') {
        s++;
    }

    while (*s) {
        const char *word_break = strstr(s, "//");
        const char *word_end = word_break ? word_break : s + strlen(s);

        const char *t = s;
        while (t < word_end) {
            while (t < word_end && *t == '/') {
                t++;
            }
            if (t >= word_end) {
                break;
            }
            const char *slash = memchr(t, '/', (size_t)(word_end - t));
            const char *letter_end = slash ? slash : word_end;

            for (const char *u = t; u < letter_end; u++) {
                if (*u != '.' && *u != '-') {
                    continue;
                }
                rgb_set(strip, brightness, 0, 0);
                uint32_t on_ms = (*u == '-') ? (unit_ms * 3) : unit_ms;
                vTaskDelay(pdMS_TO_TICKS(on_ms));
                rgb_off(strip);
                vTaskDelay(pdMS_TO_TICKS(unit_ms));
            }
            t = letter_end;
            while (t < word_end && *t == '/') {
                t++;
            }
            if (t < word_end) {
                vTaskDelay(pdMS_TO_TICKS(unit_ms * 2));
            }
        }

        if (!word_break) {
            break;
        }
        s = word_break + 2;
        while (*s == '/') {
            s++;
        }
        if (*s) {
            vTaskDelay(pdMS_TO_TICKS(unit_ms * 6));
        }
    }
}

static void led_idle_yellow(void)
{
    rgb_set(s_strip, MORSE_LED_BRIGHT_IDLE, MORSE_LED_BRIGHT_IDLE, 0);
}

static void preview_flash_symbol(char sym)
{
    const uint32_t u = MORSE_UNIT_MS;
    rgb_set(s_strip, MORSE_LED_BRIGHT_PLAY, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(sym == '-' ? (u * 3) : u));
    rgb_off(s_strip);
    vTaskDelay(pdMS_TO_TICKS(u));
    led_idle_yellow();
}

static void morse_player_task(void *arg)
{
    (void)arg;
    play_msg_t msg;
    led_idle_yellow();

    for (;;) {
        if (xQueueReceive(s_play_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "play: %s", msg.pattern);
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
        morse_play_red(s_strip, msg.pattern, MORSE_UNIT_MS, MORSE_LED_BRIGHT_PLAY);
        led_idle_yellow();
        xSemaphoreGive(s_led_mutex);
    }
}

void morse_player_init(led_strip_handle_t strip)
{
    s_strip = strip;

    s_led_mutex = xSemaphoreCreateMutex();
    if (!s_led_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    s_play_queue = xQueueCreate(MORSE_PLAY_QUEUE_LEN, sizeof(play_msg_t));
    if (!s_play_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return;
    }
    xTaskCreate(morse_player_task, "morse_player", 4096, NULL, 5, NULL);
    led_idle_yellow();
}

bool morse_player_preview_symbol(char sym)
{
    if (xSemaphoreTake(s_led_mutex, 0) != pdTRUE) {
        return false;
    }
    preview_flash_symbol(sym);
    xSemaphoreGive(s_led_mutex);
    return true;
}

bool morse_player_submit_play(const char *pattern)
{
    play_msg_t msg;
    strncpy(msg.pattern, pattern, sizeof(msg.pattern) - 1);
    msg.pattern[sizeof(msg.pattern) - 1] = '\0';
    return xQueueSend(s_play_queue, &msg, pdMS_TO_TICKS(500)) == pdTRUE;
}
