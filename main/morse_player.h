/**
 * morse_player.h —— 摩斯「灯效播放」：队列、预览、与 WS2812 句柄绑定
 *
 * 设计要点：
 *   - 整段播放在独立 FreeRTOS 任务里执行，避免阻塞 HTTP 请求处理太久。
 *   - 预览（单点/划闪一下）与整段播放共用互斥锁，避免两路同时改灯颜色。
 */
#pragma once

#include "led_strip.h"

#include <stdbool.h>

/**
 * 初始化播放子系统：保存灯带句柄、创建互斥锁与队列、启动 morse_player 任务。
 * 应在 led_strip_new_rmt_device 成功之后调用一次。
 */
void morse_player_init(led_strip_handle_t strip);

/**
 * 预览一个符号：红灯亮「点或划」对应时长，再恢复黄灯。
 * @param sym  '.' 或 '-'
 * @return     true 已执行预览；false 表示互斥锁被占用（例如正在整段播放），未闪灯
 */
bool morse_player_preview_symbol(char sym);

/**
 * 将一整条摩斯串加入播放队列（非阻塞）；实际亮灯顺序由后台任务执行。
 * @param pattern 与网页/API 相同格式的字符串
 * @return        true 入队成功；false 队列满（约 0.5 秒内未排上）
 */
bool morse_player_submit_play(const char *pattern);
