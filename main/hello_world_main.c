/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * SoftAP + 网页：短按/长按摩斯输入，红灯播放，常态绿灯；/ 字母间隔，// 单词间隔。
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "led_strip.h"
#include "nvs_flash.h"

static const char *TAG = "morse_ap";

#define LED_GPIO           48
#define LED_COUNT          1
#define MORSE_UNIT_MS      120
#define LED_BRIGHT_PLAY    45
#define LED_BRIGHT_IDLE    25

#define PLAY_QUEUE_LEN     4
#define PATTERN_MAX        256

typedef struct {
    char pattern[PATTERN_MAX];
} play_msg_t;

static led_strip_handle_t s_strip;
static QueueHandle_t s_play_queue;
static SemaphoreHandle_t s_led_mutex;

typedef struct {
    char ch;
    const char *morse;
} morse_entry_t;

static const morse_entry_t MORSE[] = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
    {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
    {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
};

static void rgb_set(led_strip_handle_t strip, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
}

static void rgb_off(led_strip_handle_t strip)
{
    led_strip_clear(strip);
}

static char char_for_morse(const char *token, size_t len)
{
    if (len == 0 || len >= 16) {
        return 0;
    }
    char buf[16];
    memcpy(buf, token, len);
    buf[len] = '\0';
    for (size_t i = 0; i < sizeof(MORSE) / sizeof(MORSE[0]); i++) {
        if (strcmp(MORSE[i].morse, buf) == 0) {
            return MORSE[i].ch;
        }
    }
    return 0;
}

/* 协议：字母用 / 分隔，单词用 // 分隔。例：.../---/... 为 SOS */
static void decode_pattern(const char *in, char *out, size_t out_len)
{
    out[0] = '\0';
    if (!in || !*in) {
        return;
    }

    char work[PATTERN_MAX + 4];
    strncpy(work, in, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    /* 去掉首尾多余 / */
    char *w = work;
    while (*w == '/') {
        w++;
    }

    size_t olen = 0;
    const char *s = w;

    while (*s) {
        const char *word_break = strstr(s, "//");
        const char *word_end = word_break ? word_break : s + strlen(s);

        const char *t = s;
        while (t < word_end) {
            while (t < word_end && *t == '/') {
                t++;
            }
            if (t >= word_end) {
                break;
            }
            const char *slash = memchr(t, '/', (size_t)(word_end - t));
            const char *tok_end = slash ? slash : word_end;
            const size_t tok_len = (size_t)(tok_end - t);
            /* 只保留 .- */
            size_t j = 0;
            char tok[16];
            for (size_t k = 0; k < tok_len && j < sizeof(tok) - 1; k++) {
                if (t[k] == '.' || t[k] == '-') {
                    tok[j++] = t[k];
                }
            }
            tok[j] = '\0';

            char ch = char_for_morse(tok, j);
            if (ch && olen + 1 < out_len) {
                out[olen++] = ch;
                out[olen] = '\0';
            } else if (j > 0 && olen + 1 < out_len) {
                out[olen++] = '?';
                out[olen] = '\0';
            }
            t = tok_end;
            while (t < word_end && *t == '/') {
                t++;
            }
        }

        if (!word_break) {
            break;
        }
        s = word_break + 2;
        while (*s == '/') {
            s++;
        }
        if (*s && olen + 1 < out_len) {
            out[olen++] = ' ';
            out[olen] = '\0';
        }
    }
}

static void morse_play_red(led_strip_handle_t strip, const char *pattern, uint32_t unit_ms, uint8_t brightness)
{
    const char *s = pattern;
    while (*s == '/') {
        s++;
    }

    while (*s) {
        const char *word_break = strstr(s, "//");
        const char *word_end = word_break ? word_break : s + strlen(s);

        const char *t = s;
        while (t < word_end) {
            while (t < word_end && *t == '/') {
                t++;
            }
            if (t >= word_end) {
                break;
            }
            const char *slash = memchr(t, '/', (size_t)(word_end - t));
            const char *letter_end = slash ? slash : word_end;

            for (const char *u = t; u < letter_end; u++) {
                if (*u != '.' && *u != '-') {
                    continue;
                }
                rgb_set(strip, brightness, 0, 0);
                uint32_t on_ms = (*u == '-') ? (unit_ms * 3) : unit_ms;
                vTaskDelay(pdMS_TO_TICKS(on_ms));
                rgb_off(strip);
                vTaskDelay(pdMS_TO_TICKS(unit_ms));
            }
            t = letter_end;
            while (t < word_end && *t == '/') {
                t++;
            }
            /* 同单词内还有下一字母：符号间隔 1 后再补 2 => 字母间隔 3 */
            if (t < word_end) {
                vTaskDelay(pdMS_TO_TICKS(unit_ms * 2));
            }
        }

        if (!word_break) {
            break;
        }
        s = word_break + 2;
        while (*s == '/') {
            s++;
        }
        /* 单词间隔 7：上一词最后一个符号后已有 1 单位静音，再补 6 */
        if (*s) {
            vTaskDelay(pdMS_TO_TICKS(unit_ms * 6));
        }
    }
}

static void led_idle_green(void)
{
    rgb_set(s_strip, 0, LED_BRIGHT_IDLE, 0);
}

/* 单个点/划的即时反馈（与整段播放共用 mutex，避免抢灯） */
static void preview_flash_symbol(char sym)
{
    const uint32_t u = MORSE_UNIT_MS;
    rgb_set(s_strip, LED_BRIGHT_PLAY, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(sym == '-' ? (u * 3) : u));
    rgb_off(s_strip);
    vTaskDelay(pdMS_TO_TICKS(u));
    led_idle_green();
}

static void morse_player_task(void *arg)
{
    (void)arg;
    play_msg_t msg;
    led_idle_green();

    for (;;) {
        if (xQueueReceive(s_play_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "play: %s", msg.pattern);
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
        morse_play_red(s_strip, msg.pattern, MORSE_UNIT_MS, LED_BRIGHT_PLAY);
        led_idle_green();
        xSemaphoreGive(s_led_mutex);
    }
}

/* ---------- Wi-Fi SoftAP ---------- */

#define WIFI_SSID      "ESP32-Morse"
#define WIFI_PASS      "12345678"
#define WIFI_CHANNEL   1
#define MAX_STA_CONN   4

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
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
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "手机连热点后打开 http://192.168.4.1/");
}

/* ---------- HTTP ---------- */

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Morse</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;padding:16px;max-width:520px;margin:auto;background:#f6f7fb;}"
    "h1{font-size:1.35rem;color:#222;}"
    ".hint{color:#555;font-size:14px;line-height:1.5;}"
    "#key{touch-action:manipulation;user-select:none;width:100%;min-height:120px;"
    "font-size:17px;margin:12px 0;background:#238636;color:#fff;border:none;border-radius:14px;}"
    "#key:active{background:#196f2e;}"
    ".row{display:flex;gap:8px;margin:10px 0;flex-wrap:wrap;}"
    ".row button{padding:11px 14px;border-radius:10px;border:none;font-size:15px;background:#e1e4ea;color:#222;}"
    "#send{background:#0969da;color:#fff;}"
    "#pat{font-family:ui-monospace,monospace;background:#111;color:#3fb950;padding:14px;border-radius:10px;"
    "word-break:break-all;min-height:3.2em;white-space:pre-wrap;}"
    "#dec{font-size:18px;margin-top:10px;padding:10px;background:#fff;border-radius:10px;border:1px solid #ddd;}"
    "</style></head><body>"
    "<h1>摩斯控制</h1>"
    "<p class=\"hint\">连接热点 <strong>" WIFI_SSID "</strong>，密码 <strong>" WIFI_PASS
    "</strong>。绿常态；点/划时红灯闪一下反馈；点「发送播放」整段红灯。</p>"
    "<p class=\"hint\">大按钮：<strong>短按=点</strong>，<strong>长按=划</strong>（约≥280ms）。</p>"
    "<button type=\"button\" id=\"key\">按住发码（松开判定）</button>"
    "<div class=\"row\">"
    "<button type=\"button\" id=\"endL\">字母间 /</button>"
    "<button type=\"button\" id=\"endW\">单词间 //</button>"
    "<button type=\"button\" id=\"bs\">退格</button>"
    "<button type=\"button\" id=\"clr\">清空</button>"
    "<button type=\"button\" id=\"send\">发送播放</button>"
    "</div>"
    "<div class=\"hint\">当前摩斯串</div>"
    "<div id=\"pat\"></div>"
    "<div class=\"hint\">解码（板端）</div>"
    "<div id=\"dec\"></div>"
    "<script>"
    "const T_DOT=280;"
    "let t0=0,buf='';"
    "const pat=document.getElementById('pat'),dec=document.getElementById('dec'),key=document.getElementById('key');"
    "function sync(){pat.textContent=buf||'(空)';}"
    "key.addEventListener('pointerdown',function(e){e.preventDefault();t0=performance.now();});"
    "key.addEventListener('pointerup',function(e){e.preventDefault();if(!t0)return;var d=performance.now()-t0;t0=0;"
    "var sym=(d<T_DOT)?'.':'-';buf+=sym;sync();"
    "fetch('/api/preview',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({sym:sym})}).catch(function(){});});"
    "key.addEventListener('pointercancel',function(){t0=0;});"
    "document.getElementById('endL').onclick=function(){buf+='/';sync();};"
    "document.getElementById('endW').onclick=function(){buf+='//';sync();};"
    "document.getElementById('bs').onclick=function(){buf=buf.slice(0,-1);sync();};"
    "document.getElementById('clr').onclick=function(){buf='';dec.textContent='';sync();};"
    "document.getElementById('send').onclick=async function(){"
    "if(!buf){dec.textContent='请先输入摩斯';return;}"
    "try{"
    "var r=await fetch('/api/play',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({pattern:buf})});"
    "var j=await r.json();"
    "if(j.ok){dec.textContent='解码: '+j.decoded+' | 摩斯: '+j.pattern;}else{dec.textContent='错误: '+(j.error||'');}"
    "}catch(e){dec.textContent='请求失败: '+e;}"
    "};"
    "sync();"
    "</script></body></html>";

static bool json_extract_sym(const char *body, char *sym_out)
{
    const char *p = strstr(body, "\"sym\"");
    if (!p) {
        return false;
    }
    p = strchr(p, ':');
    if (!p) {
        return false;
    }
    p = strchr(p, '"');
    if (!p) {
        return false;
    }
    p++;
    if ((*p == '.' || *p == '-') && p[1] == '"') {
        *sym_out = *p;
        return true;
    }
    return false;
}

static bool json_extract_pattern(const char *body, char *out, size_t out_len)
{
    const char *p = strstr(body, "\"pattern\"");
    if (!p) {
        return false;
    }
    p = strchr(p, ':');
    if (!p) {
        return false;
    }
    p = strchr(p, '"');
    if (!p) {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\' && p[1] == '"') {
            out[i++] = '"';
            p += 2;
            continue;
        }
        if (*p == '\\' && p[1] == '\\') {
            out[i++] = '\\';
            p += 2;
            continue;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    /* 避免浏览器自动请求 /favicon.ico 时刷 404 日志 */
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, "", 0);
}

static esp_err_t preview_post_handler(httpd_req_t *req)
{
    char body[80] = {0};
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad length\"}");
        return ESP_OK;
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no body\"}");
        return ESP_OK;
    }
    body[r] = '\0';

    char sym = 0;
    if (!json_extract_sym(body, &sym)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no sym\"}");
        return ESP_OK;
    }

    if (xSemaphoreTake(s_led_mutex, 0) != pdTRUE) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"busy\":true}");
    }
    preview_flash_symbol(sym);
    xSemaphoreGive(s_led_mutex);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t play_post_handler(httpd_req_t *req)
{
    char body[384] = {0};
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad length\"}");
        return ESP_OK;
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no body\"}");
        return ESP_OK;
    }
    body[r] = '\0';

    char pattern[PATTERN_MAX];
    if (!json_extract_pattern(body, pattern, sizeof(pattern))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no pattern\"}");
        return ESP_OK;
    }

    char decoded[128];
    decode_pattern(pattern, decoded, sizeof(decoded));

    play_msg_t msg;
    strncpy(msg.pattern, pattern, sizeof(msg.pattern) - 1);
    msg.pattern[sizeof(msg.pattern) - 1] = '\0';

    if (xQueueSend(s_play_queue, &msg, pdMS_TO_TICKS(500)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"queue full\"}");
        return ESP_OK;
    }

    /* JSON 响应：简单转义 pattern 中的 " \ */
    char esc[PATTERN_MAX * 2];
    size_t ei = 0;
    for (size_t i = 0; pattern[i] && ei + 2 < sizeof(esc); i++) {
        if (pattern[i] == '"' || pattern[i] == '\\') {
            esc[ei++] = '\\';
        }
        esc[ei++] = pattern[i];
    }
    esc[ei] = '\0';

    char dec_esc[sizeof(decoded) * 2];
    ei = 0;
    for (size_t i = 0; decoded[i] && ei + 2 < sizeof(dec_esc); i++) {
        if (decoded[i] == '"' || decoded[i] == '\\') {
            dec_esc[ei++] = '\\';
        }
        dec_esc[ei++] = decoded[i];
    }
    dec_esc[ei] = '\0';

    char resp[1536];
    int n = snprintf(resp, sizeof(resp),
                     "{\"ok\":true,\"pattern\":\"%s\",\"decoded\":\"%s\"}", esc, dec_esc);
    if (n < 0 || n >= (int)sizeof(resp)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"response too long\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);
        httpd_uri_t fav = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &fav);
        httpd_uri_t preview = {.uri = "/api/preview", .method = HTTP_POST, .handler = preview_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &preview);
        httpd_uri_t play = {.uri = "/api/play", .method = HTTP_POST, .handler = play_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &play);
        ESP_LOGI(TAG, "HTTP server started");
        return server;
    }
    ESP_LOGE(TAG, "httpd_start failed");
    return NULL;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {.invert_out = 0},
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags = {.with_dma = 0},
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    ESP_LOGI(TAG, "WS2812 ok gpio=%d", LED_GPIO);

    s_led_mutex = xSemaphoreCreateMutex();
    if (!s_led_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    s_play_queue = xQueueCreate(PLAY_QUEUE_LEN, sizeof(play_msg_t));
    if (!s_play_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return;
    }
    xTaskCreate(morse_player_task, "morse_player", 4096, NULL, 5, NULL);

    wifi_init_softap();
    start_webserver();

    led_idle_green();
}
