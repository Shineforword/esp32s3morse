/**
 * mqtt_lan.c —— 局域网 Mosquitto 单次发布（与 app_config.h 中宏一致）
 */

#include "mqtt_lan.h"

#include "app_config.h"
#include "wifi_softap.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "mqtt_lan";

typedef struct {
    SemaphoreHandle_t done;
    volatile bool finished;
    esp_err_t err;
} mqtt_sync_t;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    mqtt_sync_t *s = (mqtt_sync_t *)handler_args;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = ev->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        int msg_id = esp_mqtt_client_publish(client, MQTT_TEST_TOPIC, MQTT_TEST_PAYLOAD, 0, 1, 0);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "publish 调用失败");
            if (!s->finished) {
                s->finished = true;
                s->err = ESP_FAIL;
                xSemaphoreGive(s->done);
            }
        }
        break;
    }
    case MQTT_EVENT_PUBLISHED:
        if (!s->finished) {
            s->finished = true;
            s->err = ESP_OK;
            xSemaphoreGive(s->done);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        if (!s->finished) {
            s->finished = true;
            s->err = ESP_FAIL;
            xSemaphoreGive(s->done);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (!s->finished) {
            s->finished = true;
            s->err = ESP_FAIL;
            xSemaphoreGive(s->done);
        }
        break;
    default:
        break;
    }
}

static SemaphoreHandle_t s_mqtt_mx;

esp_err_t mqtt_lan_publish_test(void)
{
    if (!wifi_sta_lan_is_configured()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_sta_wait_for_ip(20000)) {
        ESP_LOGE(TAG, "STA 未在时限内获得 IP");
        return ESP_ERR_TIMEOUT;
    }

    if (s_mqtt_mx == NULL) {
        s_mqtt_mx = xSemaphoreCreateMutex();
        if (s_mqtt_mx == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_mqtt_mx, pdMS_TO_TICKS(35000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        xSemaphoreGive(s_mqtt_mx);
        return ESP_ERR_NO_MEM;
    }

    mqtt_sync_t sync = {
        .done = done,
        .finished = false,
        .err = ESP_FAIL,
    };

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .network.disable_auto_reconnect = true,
        .network.timeout_ms = 10000,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        vSemaphoreDelete(done);
        xSemaphoreGive(s_mqtt_mx);
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, &sync);
    esp_err_t st = esp_mqtt_client_start(client);
    if (st != ESP_OK) {
        esp_mqtt_client_destroy(client);
        vSemaphoreDelete(done);
        xSemaphoreGive(s_mqtt_mx);
        return st;
    }

    const TickType_t wait_ticks = pdMS_TO_TICKS(18000);
    if (xSemaphoreTake(sync.done, wait_ticks) != pdTRUE) {
        ESP_LOGE(TAG, "等待 MQTT 完成超时");
        sync.err = ESP_ERR_TIMEOUT;
    }

    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    vSemaphoreDelete(done);

    if (sync.err == ESP_OK) {
        ESP_LOGI(TAG, "已发布 topic=%s payload=%s", MQTT_TEST_TOPIC, MQTT_TEST_PAYLOAD);
    }

    xSemaphoreGive(s_mqtt_mx);
    return sync.err;
}
