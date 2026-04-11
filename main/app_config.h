/*
 * app_config.h —— 工程「可调参数」集中地
 *
 * 新手提示：
 * - 改 GPIO、灯亮度、摩斯快慢时，优先只改本文件，不用在多个 .c 里找魔法数字。
 * - 宏 #define 在预处理阶段替换为常量，不占 RAM，适合配置项。
 */
#pragma once

/** WS2812（或兼容灯带）数据线所接 GPIO。ESP32-S3 UNO 板载灯常为 48，换板请对照原理图。 */
#define MORSE_LED_GPIO        48

/** 灯带上 LED 颗数。本工程只用 1 颗板载灯。 */
#define MORSE_LED_COUNT       1

/**
 * 摩斯「单位时间」长度，单位：毫秒。
 * 标准摩斯里：点 = 1 单位，划 = 3 单位，符号间间隔 = 1 单位……
 * 数值越大，播放越慢、越容易听清/看清。
 */
#define MORSE_UNIT_MS         120

/** 播放/预览时红灯的最大亮度（0～255，具体观感因灯而异）。 */
#define MORSE_LED_BRIGHT_PLAY 45

/** 空闲时黄灯的 R/G 亮度（B 固定为 0，R=G 即黄色）。 */
#define MORSE_LED_BRIGHT_IDLE 25

/**
 * 「发送播放」请求排队长度。
 * 用户连续点发送时，每条命令先入队，播放任务依次消费；满则接口返回 queue full。
 */
#define MORSE_PLAY_QUEUE_LEN  4

/** 网页/API 传入的摩斯字符串最大长度（含结尾 \\0 前的字符）。超长会被截断或拒绝，视各接口校验而定。 */
#define MORSE_PATTERN_MAX     256
