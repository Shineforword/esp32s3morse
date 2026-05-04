#pragma once

#include "esp_err.h"

/**
 * 在 STA 已获 IP 的前提下，向 MQTT_BROKER_URI 发布 MQTT_TEST_TOPIC / MQTT_TEST_PAYLOAD。
 * 若未配置 MQTT_STA_WIFI_SSID 或 STA 无 IP，返回相应错误；成功则 broker 应收到一条 QoS1 消息。
 */
esp_err_t mqtt_lan_publish_test(void);
