/**
 * wifi_softap.h —— 开发板当「无线路由器」开热点（SoftAP）
 *
 * SoftAP 模式：ESP32 自己发射 Wi‑Fi，手机连它，不经过你家路由器。
 * WIFI_SSID / WIFI_PASS 会被 morse_http.c 里内嵌网页引用，改名字需只改本头文件一处。
 */
#pragma once

/** 手机搜到的 Wi‑Fi 名称 */
#define WIFI_SSID      "ESP32-Morse"

/** Wi‑Fi 密码，至少 8 位（WPA2）；若改为空字符串则开放网络（不推荐） */
#define WIFI_PASS      "12345678"

/** 初始化并启动热点；需保证此前已 nvs_flash_init（在 app_main 里已做） */
void wifi_softap_init(void);
