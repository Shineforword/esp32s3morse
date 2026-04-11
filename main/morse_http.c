/**
 * morse_http.c —— ESP HTTP Server：主页 + /api/preview、/api/decode、/api/play
 *
 * 新手速览：
 *   - httpd_req_t *req：代表「当前这一条 HTTP 请求」，读 body、设响应头、发 body 都用它。
 *   - 每个 URI 对应一个 static esp_err_t xxx_handler(httpd_req_t *req)，在 morse_http_start 里注册。
 *   - 本文件用极简手写方式解析 JSON（搜关键字），避免引入 cJSON 依赖；格式需与网页 JS 发送的一致。
 *
 * 栈空间注意：play 接口响应体可能较长，大缓冲使用 static，避免在 httpd 任务栈上分配导致溢出。
 */

#include "morse_http.h"

#include "app_config.h"
#include "morse_decode.h"
#include "morse_player.h"
#include "wifi_softap.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "morse_http";

/*
 * 内嵌网页：单个大字符串常量，编译进固件，无需 SPIFFS。
 * WIFI_SSID / WIFI_PASS 来自 wifi_softap.h，与真实热点一致。
 * 前端 JS 通过 fetch 调 POST /api/preview 等；大按钮用 pointerdown/up 时长区分点(.)与划(-)。
 */
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
    "</strong>。黄常态；点/划时红灯闪一下反馈；点「发送播放」整段红灯。</p>"
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
    "<div class=\"hint\">实时解码（板端，随输入更新）</div>"
    "<div id=\"dec\"></div>"
    "<script>"
    "const T_DOT=280;"
    "let t0=0,buf='';"
    "const pat=document.getElementById('pat'),dec=document.getElementById('dec'),key=document.getElementById('key');"
    "function sync(){pat.textContent=buf||'(空)';}"
    "function refreshDecode(){"
    "if(!buf){dec.textContent='(空)';return;}"
    "fetch('/api/decode',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({pattern:buf})}).then(function(r){return r.json();}).then(function(j){"
    "if(j.ok){dec.textContent='实时: '+j.decoded;}else{dec.textContent='解码错误';}"
    "}).catch(function(){});"
    "}"
    "key.addEventListener('pointerdown',function(e){e.preventDefault();t0=performance.now();});"
    "key.addEventListener('pointerup',function(e){e.preventDefault();if(!t0)return;var d=performance.now()-t0;t0=0;"
    "var sym=(d<T_DOT)?'.':'-';buf+=sym;sync();"
    "fetch('/api/preview',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({sym:sym})}).catch(function(){});"
    "refreshDecode();});"
    "key.addEventListener('pointercancel',function(){t0=0;});"
    "document.getElementById('endL').onclick=function(){buf+='/';sync();refreshDecode();};"
    "document.getElementById('endW').onclick=function(){buf+='//';sync();refreshDecode();};"
    "document.getElementById('bs').onclick=function(){buf=buf.slice(0,-1);sync();refreshDecode();};"
    "document.getElementById('clr').onclick=function(){buf='';sync();dec.textContent='(空)';};"
    "document.getElementById('send').onclick=async function(){"
    "if(!buf){dec.textContent='请先输入摩斯';return;}"
    "try{"
    "var r=await fetch('/api/play',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({pattern:buf})});"
    "var j=await r.json();"
    "if(j.ok){dec.textContent='已发送播放 | 解码: '+j.decoded+' | 摩斯: '+j.pattern;}else{dec.textContent='错误: '+(j.error||'');}"
    "}catch(e){dec.textContent='请求失败: '+e;}"
    "};"
    "sync();dec.textContent='(空)';"
    "</script></body></html>";

/**
 * 从 POST body 里解析 {"sym":"."} 或 {"sym":"-"}。
 * 非严格 JSON 解析器，仅适合本工程固定格式。
 */
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

/**
 * 从 POST body 解析 {"pattern":"...---..."}；支持 pattern 为空串 ""（两个引号紧挨）。
 * 对 \\ " 与 \\ \\ 做简单转义，便于将来扩展。
 */
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
    if (*p == '"') {
        out[0] = '\0';
        return true;
    }
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
    return true;
}

/** GET / → 返回内嵌 HTML，浏览器主页面 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

/** GET /favicon.ico → 返回 204，减少浏览器反复请求时的无意义 404 日志 */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, "", 0);
}

/**
 * POST /api/preview —— 用户每输入一个点或划，网页调用；板子红灯闪一下作反馈。
 * 若整段正在播放（互斥锁占用），返回 JSON busy，不闪灯。
 */
static esp_err_t preview_post_handler(httpd_req_t *req)
{
    char body[80] = {0};
    int len = req->content_len;
    /* content_len 由客户端 Content-Length 给出；防恶意超大 body 撑爆栈 */
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

    if (!morse_player_preview_symbol(sym)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"busy\":true}");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/**
 * POST /api/decode —— 仅解码，不播放；网页实时显示「当前摩斯串」对应的字母/数字。
 * 响应里的 decoded 若含 " 或 \\，会先转义再拼进 JSON，避免格式断裂。
 */
static esp_err_t decode_post_handler(httpd_req_t *req)
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

    char pattern[MORSE_PATTERN_MAX];
    if (!json_extract_pattern(body, pattern, sizeof(pattern))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no pattern\"}");
        return ESP_OK;
    }

    char decoded[128];
    morse_decode_pattern(pattern, decoded, sizeof(decoded));

    /* 把 decoded 嵌入 JSON 字符串字段前，必须对 " 与 \\ 转义 */
    char dec_esc[sizeof(decoded) * 2];
    size_t ei = 0;
    for (size_t i = 0; decoded[i] && ei + 2 < sizeof(dec_esc); i++) {
        if (decoded[i] == '"' || decoded[i] == '\\') {
            dec_esc[ei++] = '\\';
        }
        dec_esc[ei++] = decoded[i];
    }
    dec_esc[ei] = '\0';

    char resp[384];
    int n = snprintf(resp, sizeof(resp), "{\"ok\":true,\"decoded\":\"%s\"}", dec_esc);
    if (n < 0 || n >= (int)sizeof(resp)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"response too long\"}");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

/**
 * POST /api/play —— 把整串摩斯加入播放队列，并立即返回 JSON（含解码结果）。
 * 播放本身在 morse_player 任务里异步执行，本 handler 不必等灯闪完。
 * s_esc / s_dec_esc / s_play_resp 用 static：避免在 httpd 默认栈上开大数组导致栈溢出死机。
 */
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

    char pattern[MORSE_PATTERN_MAX];
    if (!json_extract_pattern(body, pattern, sizeof(pattern))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no pattern\"}");
        return ESP_OK;
    }
    if (pattern[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"empty pattern\"}");
        return ESP_OK;
    }

    char decoded[128];
    morse_decode_pattern(pattern, decoded, sizeof(decoded));

    if (!morse_player_submit_play(pattern)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"queue full\"}");
        return ESP_OK;
    }

    static char s_esc[MORSE_PATTERN_MAX * 2];
    static char s_dec_esc[128 * 2];
    static char s_play_resp[1536];

    size_t ei = 0;
    for (size_t i = 0; pattern[i] && ei + 2 < sizeof(s_esc); i++) {
        if (pattern[i] == '"' || pattern[i] == '\\') {
            s_esc[ei++] = '\\';
        }
        s_esc[ei++] = pattern[i];
    }
    s_esc[ei] = '\0';

    ei = 0;
    for (size_t i = 0; decoded[i] && ei + 2 < sizeof(s_dec_esc); i++) {
        if (decoded[i] == '"' || decoded[i] == '\\') {
            s_dec_esc[ei++] = '\\';
        }
        s_dec_esc[ei++] = decoded[i];
    }
    s_dec_esc[ei] = '\0';

    int n = snprintf(s_play_resp, sizeof(s_play_resp),
                     "{\"ok\":true,\"pattern\":\"%s\",\"decoded\":\"%s\"}", s_esc, s_dec_esc);
    if (n < 0 || n >= (int)sizeof(s_play_resp)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"response too long\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, s_play_resp);
}

void morse_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    /* 默认 4096 对 decode/play 等 handler + 解析器偏紧，提到 16K 更稳 */
    config.stack_size = 16384;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        /* user_ctx 可传自定义指针；本工程各 handler 不需要 */
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);
        httpd_uri_t fav = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &fav);
        httpd_uri_t preview = {.uri = "/api/preview", .method = HTTP_POST, .handler = preview_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &preview);
        httpd_uri_t decode = {.uri = "/api/decode", .method = HTTP_POST, .handler = decode_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &decode);
        httpd_uri_t play = {.uri = "/api/play", .method = HTTP_POST, .handler = play_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &play);
        ESP_LOGI(TAG, "HTTP server started");
        return;
    }
    ESP_LOGE(TAG, "httpd_start failed");
}
