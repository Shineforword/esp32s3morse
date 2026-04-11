/**
 * wifi_softap.c —— ESP-IDF Wi‑Fi 驱动：仅 AP 模式（WIFI_MODE_AP）
 *
 * 典型流程：netif → event loop → 创建默认 AP 接口 → esp_wifi_init → set_config → start。
 */

#include "wifi_softap.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <string.h>

static const char *TAG = "wifi_softap";

/** Wi‑Fi 信道 1–13（国内常用 1、6、11）；单 AP 时与干扰环境有关 */
#define WIFI_CHANNEL   1

/** 允许同时连接热点的手机/电脑数量上限（还可受 sdkconfig 里 DHCP 数量限制） */
#define MAX_STA_CONN   4

void wifi_softap_init(void)
{
    /* LwIP 协议栈与网络接口层 */
    ESP_ERROR_CHECK(esp_netif_init());
    /* 默认事件循环：Wi‑Fi 事件可投递到这里（本工程未细听事件，仅创建） */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* 为 SoftAP 创建默认 netif，之后 TCP/IP 与 AP 绑定 */
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = (uint8_t)strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    /* 无密码时只能开放热点，否则手机无法按 WPA 连接 */
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
    /* 192.168.4.1 是 ESP-IDF SoftAP 默认网关 IP，一般无需改 */
    ESP_LOGI(TAG, "手机连热点后打开 http://192.168.4.1/");
}
