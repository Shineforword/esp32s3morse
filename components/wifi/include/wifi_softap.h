/**
 * wifi_softap.h —— 开发板开 SoftAP；可选同时 STA 连入局域网（APSTA）
 *
 * 手机连热点访问网页；若在 app_config.h 配置了 MQTT_STA_WIFI_SSID，则 STA 会连路由器，
 * 以便访问与香橙派等同网段的 Mosquitto。
 * WIFI_SSID / WIFI_PASS 会被 morse_http.c 里内嵌网页引用，改名字需只改本头文件一处。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** 手机搜到的 Wi‑Fi 名称 */
#define WIFI_SSID      "ESP32-Morse"

/** 空字符串 = 开放热点（无密码，WIFI_AUTH_OPEN）；若设密码须 ≥8 位以用 WPA2 */
#define WIFI_PASS      ""

/** 初始化并启动热点；需保证此前已 nvs_flash_init（在 app_main 里已做） */
void wifi_softap_init(void);

/** 已配置非空 MQTT_STA_WIFI_SSID（见 app_config.h）时为 true，表示会尝试 STA 上网 */
bool wifi_sta_lan_is_configured(void);

/**
 * 在 APSTA 模式下等待 STA 获得 IPv4。
 * @return 在 timeout_ms 内拿到过 IP 则 true（拿到后 bit 会保持，后续调用通常立即成功）
 */
bool wifi_sta_wait_for_ip(uint32_t timeout_ms);
