/**
 * wifi_softap.c —— ESP-IDF Wi‑Fi：SoftAP；可选 APSTA 连入局域网（供 MQTT 等）
 *
 * 典型流程：netif → event loop → 默认 AP（及可选 STA）接口 → esp_wifi_init → set_config → start。
 */

#include "wifi_softap.h"

#include "sdkconfig.h"
#include "app_config.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

static const char *TAG = "wifi_softap";

/** Wi‑Fi 信道 1–13（国内常用 1、6、11）；单 AP 时与干扰环境有关 */
#define WIFI_CHANNEL   1

/** 允许同时连接热点的手机/电脑数量上限（还可受 sdkconfig 里 DHCP 数量限制） */
#define MAX_STA_CONN   4

#define STA_GOT_IP_BIT BIT0

static EventGroupHandle_t s_sta_ip_group;

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_sta_ip_group) {
            xEventGroupSetBits(s_sta_ip_group, STA_GOT_IP_BIT);
        }
        ESP_LOGI(TAG, "STA 已获得 IPv4，可访问局域网 broker");
    }
}

bool wifi_sta_lan_is_configured(void)
{
#if CONFIG_DEMO_MQTT
    return MQTT_STA_WIFI_SSID[0] != '\0';
#else
    return false;
#endif
}

bool wifi_sta_wait_for_ip(uint32_t timeout_ms)
{
    if (!wifi_sta_lan_is_configured() || s_sta_ip_group == NULL) {
        return false;
    }
    if (xEventGroupGetBits(s_sta_ip_group) & STA_GOT_IP_BIT) {
        return true;
    }
    EventBits_t bits = xEventGroupWaitBits(s_sta_ip_group, STA_GOT_IP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & STA_GOT_IP_BIT) != 0;
}

void wifi_softap_init(void)
{
    /* LwIP 协议栈与网络接口层 */
    ESP_ERROR_CHECK(esp_netif_init());
    /* 默认事件循环：Wi‑Fi 事件可投递到这里（本工程未细听事件，仅创建） */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* 为 SoftAP 创建默认 netif，之后 TCP/IP 与 AP 绑定 */
    esp_netif_create_default_wifi_ap();

    const bool use_sta = wifi_sta_lan_is_configured();
    if (use_sta) {
        esp_netif_create_default_wifi_sta();
        s_sta_ip_group = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                              &ip_event_handler, NULL, NULL));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 先设模式，再写接口配置：v5.x 的 set_config 会校验当前 mode，
     * 顺序反了会返回 ESP_ERR_WIFI_MODE（0x3005）并 abort。 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(use_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP));

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

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    if (use_sta) {
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, MQTT_STA_WIFI_SSID, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char *)sta_cfg.sta.password, MQTT_STA_WIFI_PASS, sizeof(sta_cfg.sta.password) - 1);
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    if (use_sta) {
        esp_err_t cr = esp_wifi_connect();
        if (cr != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(cr));
        } else {
            ESP_LOGI(TAG, "STA 正在连接 SSID:%s（与 Mosquitto 同网段）", MQTT_STA_WIFI_SSID);
        }
    }

    if (strlen(WIFI_PASS) == 0) {
        ESP_LOGI(TAG, "SoftAP SSID:%s (open, no password)", WIFI_SSID);
    } else {
        ESP_LOGI(TAG, "SoftAP SSID:%s WPA2 password set", WIFI_SSID);
    }
    /* 192.168.4.1 是 ESP-IDF SoftAP 默认网关 IP，一般无需改 */
    ESP_LOGI(TAG, "手机连热点后打开 http://192.168.4.1/");
}
