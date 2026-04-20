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
#define MORSE_UNIT_MS         200

/** 播放/预览时点（红）与划（绿）的亮度（0～255，具体观感因灯而异）。 */
#define MORSE_LED_BRIGHT_PLAY 45

/** 空闲时黄灯的 R/G 亮度（B 固定为 0，R=G 即黄色）。 */
#define MORSE_LED_BRIGHT_IDLE 25

/**
 * 无源蜂鸣器模块「I/O」脚所接 GPIO（与 WS2812 的 MORSE_LED_GPIO 分开）。
 * 不用蜂鸣器时设为 -1，固件内不初始化 LEDC。
 * 板子排针示例：GPIO4、GPIO5 等；勿占用灯带脚 48。
 */
#define MORSE_BUZZER_GPIO     4

/** PWM 方波频率（Hz），无源蜂鸣器音调；约 1500～3000 较易听清。 */
#define MORSE_BUZZER_FREQ_HZ  2000

/**
 * 1：低电平导通型模块（常见 NPN 三极管驱动，静音时引脚为高）。
 * 0：高电平导通则设 0，并视实际声大小调节占空比。
 */
#define MORSE_BUZZER_ACTIVE_LOW 1

/**
 * 「发送播放」请求排队长度。
 * 用户连续点发送时，每条命令先入队，播放任务依次消费；满则接口返回 queue full。
 */
#define MORSE_PLAY_QUEUE_LEN  4

/** 网页/API 传入的摩斯字符串最大长度（含结尾 \\0 前的字符）。超长会被截断或拒绝，视各接口校验而定。 */
#define MORSE_PATTERN_MAX     256

/* -------------------------------------------------------------------------- */
/* TFT ST7789（SPI 8 线：GND VCC SCL SDA RES DC CS BLK）                        */
/* 与 MORSE_LED_GPIO(48)、MORSE_BUZZER_GPIO(4) 勿复用引脚。                    */
/* -------------------------------------------------------------------------- */

/** SPI 主机：与 Wi‑Fi 等外设共用 SPI 时注意冲突；本工程默认用 SPI2。 */
#define LCD_SPI_HOST SPI2_HOST

/** 屏 SCL → ESP32 SPI CLK；屏 SDA → MOSI */
#define LCD_PIN_SCLK 12
#define LCD_PIN_MOSI 11

/** RES / DC / CS */
#define LCD_PIN_RST 14
#define LCD_PIN_DC  13
#define LCD_PIN_CS  10

/**
 * 背光已由模块接 3V3 常亮：保持 -1。
 * 若背光接某 GPIO，填该脚并在 lcd_st7789.c 里拉高（当前未实现，可自行加 gpio_set_level）。
 */
#define LCD_PIN_BLK_GPIO (-1)

/** 本屏可视区 240×280（竖屏：宽 240、高 280）。 */
#define LCD_H_RES 240
#define LCD_V_RES 280

/** 杜邦线较长或黑屏时可降到 20M；仍不行再试 10M */
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

/** 多数 240×280 在 ST7789 的 320 行窗口内可视区从约第 20 行开始，黑屏只有背光时先试 Y=20 */
#define LCD_GAP_X 0
#define LCD_GAP_Y 20

/** SPI 模式：多数小板用 3；仍黑屏可改 0 试 */
#define LCD_SPI_MODE 3

/**
 * RGB565 RAM 字节序：1=小端（与多数 SPI 例程一致）；黑屏可改 0 试大端
 */
#define LCD_RGB565_LITTLE_ENDIAN 1

/** 1=BGR 顺序；偏红蓝反了时试 1 */
#define LCD_ST7789_BGR 0

/** 颜色反相：黑屏可先关 0；整屏反色再开 1 */
#define LCD_INVERT_COLOR 0

/** 横竖交换：方向不对改 0/1 */
#define LCD_SWAP_XY 0

/** 镜像：左右/上下反了把 0 改成 1 试 */
#define LCD_MIRROR_X 0
#define LCD_MIRROR_Y 0
