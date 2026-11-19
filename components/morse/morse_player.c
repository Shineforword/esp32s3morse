/**
 * morse_player.c —— WS2812 摩斯：点红灯、划绿灯，空闲黄灯；无源蜂鸣器与点划同步嘀嗒
 *
 * FreeRTOS 概念速查：
 *   - 队列（Queue）：任务间传「播放哪一条 pattern」；http 任务发，player 任务收。
 *   - 互斥锁（Mutex）：同一时刻只允许一个逻辑改灯（预览 OR 整段播放）。
 *   - vTaskDelay：让出 CPU 并延时；用毫秒换算为 tick：pdMS_TO_TICKS(ms)。
 */

#include "morse_player.h"

#include "sdkconfig.h"

#include "app_config.h"
#include "morse_buzzer.h"
#include "morse_decode.h"
#if CONFIG_DEMO_LCD
#include "lcd_st7789.h"
#endif

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

#include <string.h>

static const char *TAG = "morse_play";

/** 队列里每一项就是「待播放的一整串摩斯」 */
typedef struct {
    char pattern[MORSE_PATTERN_MAX];
} play_msg_t;

static led_strip_handle_t s_strip;
static QueueHandle_t s_play_queue;
static SemaphoreHandle_t s_led_mutex;

/** 设置第 0 颗灯颜色并刷新到灯带（WS2812 需 refresh 才真正输出波形） */
static void rgb_set(led_strip_handle_t strip, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
}

/** 关灯（全 0） */
static void rgb_off(led_strip_handle_t strip)
{
    led_strip_clear(strip);
}

/**
 * 按标准摩斯节奏播放 pattern（阻塞式，直到播完）。
 * 点(.)亮红，划(-)亮绿；与 morse_decode 使用相同的 '/'、'//' 分隔语义。
 */
static void morse_play_pattern(led_strip_handle_t strip, const char *pattern, uint32_t unit_ms, uint8_t brightness)
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
                if (*u == '.') {
                    rgb_set(strip, brightness, 0, 0);
                } else {
                    rgb_set(strip, 0, brightness, 0);
                }
                morse_buzzer_on();
                uint32_t on_ms = (*u == '-') ? (unit_ms * 3) : unit_ms;
                vTaskDelay(pdMS_TO_TICKS(on_ms));
                morse_buzzer_off();
                rgb_off(strip);
                vTaskDelay(pdMS_TO_TICKS(unit_ms));
            }
            t = letter_end;
            while (t < word_end && *t == '/') {
                t++;
            }
            /* 同单词内下一字母：再补 2 单位静音，与标准「字母间 3」一致（符号后已有 1） */
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
        /* 单词间：再补 6 单位（符号间隔已占 1，共 7） */
        if (*s) {
            vTaskDelay(pdMS_TO_TICKS(unit_ms * 6));
        }
    }
}

/** 空闲态：黄灯（红绿等亮、蓝关） */
static void led_idle_yellow(void)
{
    rgb_set(s_strip, MORSE_LED_BRIGHT_IDLE, MORSE_LED_BRIGHT_IDLE, 0);
}

/** 单次输入反馈：点红灯、划绿灯，再回黄灯 */
static void preview_flash_symbol(char sym)
{
    const uint32_t u = MORSE_UNIT_MS;
    if (sym == '.') {
        rgb_set(s_strip, MORSE_LED_BRIGHT_PLAY, 0, 0);
    } else {
        rgb_set(s_strip, 0, MORSE_LED_BRIGHT_PLAY, 0);
    }
    morse_buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(sym == '-' ? (u * 3) : u));
    morse_buzzer_off();
    rgb_off(s_strip);
    vTaskDelay(pdMS_TO_TICKS(u));
    led_idle_yellow();
}

/**
 * 播放任务：死循环里阻塞读队列；收到消息后占锁、整段播放、恢复黄灯、释放锁。
 */
static void morse_player_task(void *arg)
{
    (void)arg;
    play_msg_t msg;
    /* 首帧黄灯只在此任务、互斥锁内完成。曾在 init 末尾再刷一次，与本任务并发
     * led_strip_refresh / rmt_enable，会报 channel not in init state。 */
    xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    morse_buzzer_force_idle();
    led_idle_yellow();
    xSemaphoreGive(s_led_mutex);

    for (;;) {
        if (xQueueReceive(s_play_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "play: %s", msg.pattern);
#if CONFIG_DEMO_LCD
        {
            char dec[128];
            morse_decode_pattern(msg.pattern, dec, sizeof(dec));
            /* LCD 在占灯互斥锁之前刷完，避免长时间占锁；SPI 与 WiFi 共存靠 lcd 内 yield + 互斥 */
            (void)lcd_st7789_show_morse_live(msg.pattern, dec);
        }
#endif
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
        morse_buzzer_force_idle();
        morse_play_pattern(s_strip, msg.pattern, MORSE_UNIT_MS, MORSE_LED_BRIGHT_PLAY);
        morse_buzzer_force_idle();
        led_idle_yellow();
        xSemaphoreGive(s_led_mutex);
    }
}

void morse_player_init(led_strip_handle_t strip)
{
    s_strip = strip;

    morse_buzzer_init();

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
    /* 不在此处刷灯：首帧黄灯由播放任务在互斥锁内完成，避免与任务并发抢 RMT */
}

bool morse_player_preview_symbol(char sym)
{
    /* 第二个参数 0：不等待，拿不到锁立刻返回 false（给 HTTP 返回 busy） */
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
