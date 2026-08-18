#pragma once

#include <stdbool.h>
#include <stdint.h>

/** 无源蜂鸣器：LEDC PWM；与灯在 morse_player 里同步开关（内部带互斥） */
void morse_buzzer_init(void);
void morse_buzzer_on(void);
void morse_buzzer_off(void);
/** 占锁、恢复摩斯频率并关声，可在任意时刻调用（如任务入口、整段播放前后） */
void morse_buzzer_force_idle(void);

/**
 * 播放简谱 1～7 单音（C 大调 do～si），与摩斯嘀嗒共用蜂鸣器硬件。
 * @param note_1_to_7 1～7
 * @param duration_ms  发声时长；0 则用默认约 220ms
 * @return 成功 true；蜂鸣器未初始化、note 非法、长时间拿不到锁则 false
 */
bool morse_buzzer_play_scale_note(uint8_t note_1_to_7, uint32_t duration_ms);

/** 整曲《两只老虎》简谱（C 大调），一次占锁播完；失败同 play_scale_note */
bool morse_buzzer_play_two_tigers(void);
