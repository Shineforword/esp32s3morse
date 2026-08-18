/**
 * morse_buzzer.c —— 无源蜂鸣器 LEDC：摩斯固定频率 + 简谱 1～7 可变频率
 *
 * 用互斥锁协调：摩斯预览/整段与网页「七声」互斥，避免同时改 LEDC 频率与占空比。
 */

#include "morse_buzzer.h"

#include "app_config.h"

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stddef.h>

static const char *TAG = "morse_buzz";

#define BUZZ_TIMER   LEDC_TIMER_1
#define BUZZ_MODE    LEDC_LOW_SPEED_MODE
#define BUZZ_CHAN    LEDC_CHANNEL_1
#define BUZZ_RES     LEDC_TIMER_13_BIT
#define BUZZ_MAX_DUT ((1U << 13) - 1U)

/** C4 大调 1～7 音（Hz），简谱 do re mi fa sol la si */
static const uint16_t SCALE_HZ[7] = {262, 294, 330, 349, 392, 440, 494};

/** 网页按键单音时长（毫秒） */
#define SCALE_NOTE_MS 220

/** 内置旋律：每音时长与音间休止（毫秒）；《两只老虎》等整曲共用 */
#define MELODY_NOTE_MS 320
#define MELODY_GAP_MS  110
/** 整曲播放前等待蜂鸣器锁的最长时间（毫秒），需大于整首时长 */
#define MELODY_MUTEX_WAIT_MS 35000

static bool s_ok;
static SemaphoreHandle_t s_buzz_mtx;

static void buzz_apply_morse_freq(void)
{
    esp_err_t e = ledc_set_freq(BUZZ_MODE, BUZZ_TIMER, MORSE_BUZZER_FREQ_HZ);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "ledc_set_freq morse: %s", esp_err_to_name(e));
    }
}

void morse_buzzer_init(void)
{
    s_ok = false;

#if MORSE_BUZZER_GPIO < 0
    ESP_LOGI(TAG, "buzzer disabled (MORSE_BUZZER_GPIO < 0)");
    return;
#endif

    s_buzz_mtx = xSemaphoreCreateMutex();
    if (!s_buzz_mtx) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    ledc_timer_config_t tcfg = {
        .speed_mode = BUZZ_MODE,
        .duty_resolution = BUZZ_RES,
        .timer_num = BUZZ_TIMER,
        .freq_hz = MORSE_BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&tcfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(e));
        return;
    }

    ledc_channel_config_t ccfg = {
        .gpio_num = MORSE_BUZZER_GPIO,
        .speed_mode = BUZZ_MODE,
        .channel = BUZZ_CHAN,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZ_TIMER,
        .duty = 0,
        .hpoint = 0,
#if MORSE_BUZZER_ACTIVE_LOW
        .flags = {.output_invert = 1},
#else
        .flags = {.output_invert = 0},
#endif
    };
    e = ledc_channel_config(&ccfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config: %s", esp_err_to_name(e));
        return;
    }

    /* 初始化阶段尚无并发，直接关声；互斥在 s_ok 置真后生效 */
    buzz_apply_morse_freq();
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, 0);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);

    s_ok = true;
    ESP_LOGI(TAG, "ok gpio=%d freq=%dHz active_low=%d", MORSE_BUZZER_GPIO, MORSE_BUZZER_FREQ_HZ,
             MORSE_BUZZER_ACTIVE_LOW);
}

void morse_buzzer_on(void)
{
#if MORSE_BUZZER_GPIO < 0
    return;
#endif
    if (!s_ok) {
        return;
    }
    xSemaphoreTake(s_buzz_mtx, portMAX_DELAY);
    buzz_apply_morse_freq();
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, BUZZ_MAX_DUT / 2);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
}

void morse_buzzer_off(void)
{
#if MORSE_BUZZER_GPIO < 0
    return;
#endif
    if (!s_ok) {
        return;
    }
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, 0);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
    xSemaphoreGive(s_buzz_mtx);
}

void morse_buzzer_force_idle(void)
{
#if MORSE_BUZZER_GPIO < 0
    return;
#endif
    if (!s_ok) {
        return;
    }
    xSemaphoreTake(s_buzz_mtx, portMAX_DELAY);
    buzz_apply_morse_freq();
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, 0);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
    xSemaphoreGive(s_buzz_mtx);
}

bool morse_buzzer_play_scale_note(uint8_t note_1_to_7, uint32_t duration_ms)
{
#if MORSE_BUZZER_GPIO < 0
    (void)note_1_to_7;
    (void)duration_ms;
    return false;
#endif
    if (!s_ok || note_1_to_7 < 1 || note_1_to_7 > 7) {
        return false;
    }
    if (duration_ms == 0) {
        duration_ms = SCALE_NOTE_MS;
    }

    if (xSemaphoreTake(s_buzz_mtx, pdMS_TO_TICKS(3500)) != pdTRUE) {
        return false;
    }

    uint32_t hz = SCALE_HZ[note_1_to_7 - 1];
    esp_err_t e = ledc_set_freq(BUZZ_MODE, BUZZ_TIMER, hz);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "ledc_set_freq %lu: %s", (unsigned long)hz, esp_err_to_name(e));
    }
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, BUZZ_MAX_DUT / 2);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, 0);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
    buzz_apply_morse_freq();
    xSemaphoreGive(s_buzz_mtx);
    return true;
}

/** 按简谱表播完（已占 s_buzz_mtx） */
static void buzz_play_notes_locked(const uint8_t *notes, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        uint8_t n = notes[i];
        if (n < 1 || n > 7) {
            continue;
        }
        uint32_t hz = SCALE_HZ[n - 1];
        esp_err_t e = ledc_set_freq(BUZZ_MODE, BUZZ_TIMER, hz);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "ledc_set_freq %lu: %s", (unsigned long)hz, esp_err_to_name(e));
        }
        ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, BUZZ_MAX_DUT / 2);
        ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
        vTaskDelay(pdMS_TO_TICKS(MELODY_NOTE_MS));
        ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, 0);
        ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
        vTaskDelay(pdMS_TO_TICKS(MELODY_GAP_MS));
    }
    buzz_apply_morse_freq();
    ledc_set_duty(BUZZ_MODE, BUZZ_CHAN, 0);
    ledc_update_duty(BUZZ_MODE, BUZZ_CHAN);
}

static bool buzz_melody_take_and_play(const uint8_t *notes, size_t byte_len, const char *done_log)
{
#if MORSE_BUZZER_GPIO < 0
    (void)notes;
    (void)byte_len;
    (void)done_log;
    return false;
#endif
    if (!s_ok) {
        return false;
    }
    if (xSemaphoreTake(s_buzz_mtx, pdMS_TO_TICKS(MELODY_MUTEX_WAIT_MS)) != pdTRUE) {
        return false;
    }
    buzz_play_notes_locked(notes, byte_len);
    xSemaphoreGive(s_buzz_mtx);
    ESP_LOGI(TAG, "%s", done_log);
    return true;
}

bool morse_buzzer_play_two_tigers(void)
{
    /* 简谱：两只老虎…真奇怪（C 大调） */
    static const uint8_t k_two_tigers[] = {
        1, 2, 3, 1, 1, 2, 3, 1,
        3, 4, 5, 3, 4, 5,
        5, 6, 5, 4, 3, 1, 5, 6, 5, 4, 3, 1,
        1, 5, 1, 1, 5, 1,
    };
    return buzz_melody_take_and_play(k_two_tigers, sizeof(k_two_tigers), "two tigers done");
}
