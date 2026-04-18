#pragma once

/** 无源蜂鸣器：LEDC PWM 发声；与灯在 morse_player 里同步开关 */
void morse_buzzer_init(void);
void morse_buzzer_on(void);
void morse_buzzer_off(void);
