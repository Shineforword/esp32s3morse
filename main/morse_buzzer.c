/**
 * morse_buzzer.c —— 无源蜂鸣器用 LEDC 输出方波频率（嘀嗒声）
 *
 * 接线（三脚模块 GND / I-O / VCC，低电平导通类）：
 *   VCC → 开发板 3V3（声小可试 5V，勿超过模块标注）
 *   GND → GND
 *   I-O → app_config.h 里 MORSE_BUZZER_GPIO（默认 GPIO4，勿用 GPIO48 灯带脚）
 */

#include "morse_buzzer.h"

#include "app_config.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "morse_buzz";

#define BUZZ_TIMER   LEDC_TIMER_1
#define BUZZ_MODE    LEDC_LOW_SPEED_MODE
#define BUZZ_CHAN    LEDC_CHANNEL_1
#define BUZZ_RES     LEDC_TIMER_13_BIT
#define BUZZ_MAX_DUT ((1U << 13) - 1U)

static bool s_ok;

void morse_buzzer_init(void)
{
    s_ok = false;
#if MORSE_BUZZER_GPIO < 0
    ESP_LOGI(TAG, "buzzer disabled (MORSE_BUZZER_GPIO < 0)");
    return;
#endif

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
        /* 低电平导通：占空比 0 经反相后引脚为高，静音不一直吸合蜂鸣片 */
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

    morse_buzzer_off();
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
    /* 约 50% 占空比方波，音量适中；可改 BUZZ_MAX_DUT / 4 等调节 */
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
}
