/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * main.c —— 程序入口（只做「启动顺序」，业务在其它模块）
 *
 * 整体在做什么：
 *   手机连开发板开的 Wi‑Fi 热点 → 浏览器打开网页 → 点按输入摩斯 → 板子 WS2812 闪灯播放。
 *
 * 启动顺序（很重要）：
 *   1) NVS        —— Wi‑Fi 等组件常依赖的非易失存储，必须先初始化。
 *   2) WS2812    —— 创建灯带驱动，得到 strip 句柄。
 *   3) morse_player —— 内部会建队列、互斥锁、播放任务；需要 strip 已就绪。
 *   4) wifi_softap —— 开热点，手机才能连上。
 *   5) morse_http —— 开 HTTP 服务，浏览器才能访问网页与 /api/ 下的 JSON 接口。
 */

#include "app_config.h"
#include "morse_http.h"
#include "morse_player.h"
#include "wifi_softap.h"

#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"
#include "nvs_flash.h"

/** 串口日志里方括号前的标签，方便过滤： idf.py monitor 里搜 morse_ap */
static const char *TAG = "morse_ap";

/**
 * app_main 是 ESP-IDF 规定的用户入口（相当于普通 C 程序的 main）。
 * 从这里返回在嵌入式里很少用；通常用死循环任务跑系统。
 */
void app_main(void)
{
    /* ---------- NVS（Non-Volatile Storage）---------- */
    esp_err_t err = nvs_flash_init();
    /*
     * 若分区已满或 NVS 版本升级，官方示例会擦除后重试。
     * 擦除会清空该 NVS 分区数据；本工程几乎不写 NVS，但 Wi‑Fi 栈仍建议先 init。
     */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* ---------- WS2812 灯带（RMT 驱动）---------- */
    led_strip_handle_t strip;
    const led_strip_config_t strip_config = {
        .strip_gpio_num = MORSE_LED_GPIO, /* 数据线引脚 */
        .max_leds = MORSE_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        /* WS2812 灯珠内部顺序多为 GRB，与「红绿蓝」字面顺序不同，库会负责转换 */
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {.invert_out = 0},
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10MHz，满足 WS2812 时序 */
        .flags = {.with_dma = 0},
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    ESP_LOGI(TAG, "WS2812 ok gpio=%d", MORSE_LED_GPIO);

    /* ---------- 各子系统（顺序勿随意调换）---------- */
    morse_player_init(strip); /* 依赖 strip */
    wifi_softap_init();       /* 热点：先网络再 HTTP 也可，此处先 player 再 Wi‑Fi 无强依赖 */
    morse_http_start();       /* HTTP 依赖 Wi‑Fi 已启动 */
}
