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
/* TFT：IC **ST7789V3**，**240(RGB)×280**，丝印常见 **1.69 TFT V1.1**（与 v1.4 同类） */
/* 引脚说明（排针 1→8，与卖家「引脚说明」表一致）：                            */
/*   1=GND  2=VCC(3.3V)  3=SCL  4=SDA  5=RES  6=DC  7=CS  8=BLK               */
/* BLK：默认开背光；**低电平关背光**。接 3V3 常亮可不占用 GPIO。               */
/* 下表 GPIO 为 SCL/SDA/RES/DC/CS 的接法；勿与灯 48、蜂鸣器 4 共用同一脚。     */
/* -------------------------------------------------------------------------- */

/**
 * SPI 主机：SPI2/SPI3 均可；若一种组合只有背光，换另一种试。
 */
#define LCD_SPI_HOST SPI3_HOST

/** 屏 SCL → ESP32 SPI CLK；屏 SDA → MOSI */
#define LCD_PIN_SCLK 12
#define LCD_PIN_MOSI 11

/** RES / DC / CS */
#define LCD_PIN_RST 14
#define LCD_PIN_DC  13
#define LCD_PIN_CS  10

/**
 * BLK 由硬件接 3V3 常亮：保持 -1。
 * 若 BLK 改接 GPIO：须保持**高电平**背光才亮（低电平关）；再在 lcd_st7789.c 里上电拉高。
 */
#define LCD_PIN_BLK_GPIO (-1)

/** 横向 240 子像素列（RGB）、纵向 280 行；竖屏时宽=240、高=280。 */
#define LCD_H_RES 240
#define LCD_V_RES 280

/** 杜邦线长或黑屏时优先 10M；稳定后再提到 20M、40M */
#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)

/**
 * 1.69 / 240×280 在 ST7789 的 320 行显存里常「垂直居中」：行偏移 (320−280)/2 = **20**。
 * Adafruit ST7789 对 1.69″ 等屏也用该算法；GAP 不对时易整屏黑、只有背光。
 * 若画面整体偏上/下，可改 0 或 40 微调。
 */
#define LCD_GAP_X 0
#define LCD_GAP_Y 20

/** SPI 模式：0 与 3 最常见；仅背光时务必与杜邦线质量一起试另一种 */
#define LCD_SPI_MODE 3

/**
 * 1：在 esp_lcd 最小初始化之外再发 **LVGL 常用电源/伽马**（易解决「只有背光」）。
 * 若与某批屏冲突可改 0 关闭。
 */
#define LCD_ST7789_LVGL_EXTRA_INIT 1

/** 1=RGB565 小端 RAM；0=大端（与 esp_lcd ST7789 默认 RAMCTRL 更接近）。黑屏时两种都试 */
#define LCD_RGB565_LITTLE_ENDIAN 1

/** 1=BGR 顺序；红字变蓝、蓝变红时改为 1（多数 ST7789 IPS 模组为 BGR） */
#define LCD_ST7789_BGR 1

/**
 * 1：与 Adafruit 通用 ST7789 初始化里的 INVON 一致，多数 1.69 IPS 需要。
 * 若颜色/对比异常可改 0。
 */
#define LCD_INVERT_COLOR 0

/**
 * 全屏位图（如 tank）像素补偿，与 LCD_INVERT_COLOR / BGR 组合有关。
 * 色彩发灰、像负片时试改 LCD_IMAGE_COMPENSATE_INVERT；红蓝反试 LCD_IMAGE_SWAP_RB。
 */
#define LCD_IMAGE_COMPENSATE_INVERT 1
#define LCD_IMAGE_SWAP_RB         0

/** 横竖交换：方向不对改 0/1 */
#define LCD_SWAP_XY 1

/**
 * 镜像修正（esp_lcd_panel_mirror）：文字左右镜像开 **LCD_MIRROR_X**；
 * 上下镜像开 **LCD_MIRROR_Y**；仍不对可 **X/Y 组合** 试 0/1。
 */
#define LCD_MIRROR_X 0
#define LCD_MIRROR_Y 1
