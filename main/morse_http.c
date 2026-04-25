/**
 * morse_http.c —— ESP HTTP Server：卡片首页 /、/morse、/music、/lcd 与 /api 下各接口
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
#include "morse_buzzer.h"
#include "morse_decode.h"
#include "lcd_st7789.h"
#include "morse_player.h"
#include "wifi_softap.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "morse_http";

/*
 * 内嵌网页：编译进固件。WIFI_SSID 来自 wifi_softap.h。
 */
static const char DASHBOARD_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>功能</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;padding:16px;max-width:560px;margin:auto;background:#eef2f7;}"
    "h1{font-size:1.35rem;color:#1a1a2e;}"
    ".hint{color:#555;font-size:14px;}"
    ".grid{display:grid;grid-template-columns:1fr;gap:14px;margin-top:18px;}"
    "@media(min-width:480px){.grid{grid-template-columns:1fr 1fr;}}"
    ".card.lcd h2{color:#5b21b6;}"
    ".card{background:#fff;border-radius:14px;padding:16px;box-shadow:0 2px 10px rgba(0,0,0,.07);"
    "text-decoration:none;color:inherit;display:block;border:1px solid #e1e4e8;transition:transform .08s;}"
    ".card:active{transform:scale(0.98);}"
    ".card h2{margin:0 0 8px;font-size:1.08rem;color:#0969da;}"
    ".card p{margin:0;font-size:14px;color:#555;line-height:1.45;}"
    ".card .go{font-size:12px;color:#888;margin-top:10px;}"
    "</style></head><body>"
    "<h1>功能</h1>"
    "<p class=\"hint\">已连热点 <strong>" WIFI_SSID "</strong> 后，点卡片进入。</p>"
    "<div class=\"grid\">"
    "<a class=\"card\" href=\"/morse\"><h2>摩斯控制</h2>"
    "<p>网页输入点划，灯点红划绿，蜂鸣器同步嘀嗒。</p><span class=\"go\">点击进入 →</span></a>"
    "<a class=\"card\" href=\"/music\"><h2>七声简谱</h2>"
    "<p>单音 1～7；《两只老虎》整曲（蜂鸣器简易版）。</p><span class=\"go\">点击进入 →</span></a>"
    "<a class=\"card lcd\" href=\"/lcd\"><h2>屏幕 Hello</h2>"
    "<p>ST7789 TFT：点按钮在屏上显示 Hello World（首次会初始化 SPI）。</p><span class=\"go\">点击进入 →</span></a>"
    "<a class=\"card lcd\" href=\"/tank\"><h2>全屏 Tank</h2>"
    "<p>将 tank.png 缩放铺满 240×280 后整屏显示（首次会初始化 SPI）。</p><span class=\"go\">点击进入 →</span></a>"
    "</div></body></html>";

static const char MORSE_PAGE_HTML[] =
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
    "<p class=\"nav\" style=\"margin:0 0 10px\"><a href=\"/\">← 功能首页</a></p>"
    "<h1>摩斯控制</h1>"
    "<p class=\"hint\">连接热点 <strong>" WIFI_SSID "</strong>（开放网络，无需密码）。黄常态；点红灯、划绿灯闪一下反馈；点「发送播放」整段按点红划绿播放；接无源蜂鸣器时同步嘀嗒声。接 ST7789 屏时会上屏显示当前解码与摩斯串。</p>"
    "<p class=\"hint\">大按钮：<strong>短按=点</strong>，<strong>长按=划</strong>（约≥280ms）。</p>"
    "<div class=\"hint\">当前摩斯串</div>"
    "<div id=\"pat\"></div>"
    "<div class=\"hint\">实时解码（板端，随输入更新）</div>"
    "<div id=\"dec\"></div>"
    "<button type=\"button\" id=\"key\">按住发码（松开判定）</button>"
    "<div class=\"row\">"
    "<button type=\"button\" id=\"endL\">字母间 /</button>"
    "<button type=\"button\" id=\"endW\">单词间 //</button>"
    "<button type=\"button\" id=\"bs\">退格</button>"
    "<button type=\"button\" id=\"clr\">清空</button>"
    "<button type=\"button\" id=\"send\">发送播放</button>"
    "</div>"
    "<script>"
    "const T_DOT=280;"
    "let t0=0,buf='';"
    "const pat=document.getElementById('pat'),dec=document.getElementById('dec'),key=document.getElementById('key');"
    "function sync(){pat.textContent=buf||'(空)';}"
    "function refreshDecode(){"
    "fetch('/api/decode',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({pattern:buf})}).then(function(r){return r.json();}).then(function(j){"
    "if(j.ok){dec.textContent=buf?'实时: '+j.decoded:'(空)';}else{dec.textContent='解码错误';}"
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
    "document.getElementById('clr').onclick=function(){buf='';sync();refreshDecode();};"
    "document.getElementById('send').onclick=async function(){"
    "if(!buf){dec.textContent='请先输入摩斯';return;}"
    "try{"
    "var r=await fetch('/api/play',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({pattern:buf})});"
    "var j=await r.json();"
    "if(j.ok){dec.textContent='已发送播放 | 解码: '+j.decoded+' | 摩斯: '+j.pattern;}else{dec.textContent='错误: '+(j.error||'');}"
    "}catch(e){dec.textContent='请求失败: '+e;}"
    "};"
    "sync();refreshDecode();"
    "</script></body></html>";

static const char MUSIC_PAGE_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>七声</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;padding:16px;max-width:520px;margin:auto;background:#f6f7fb;}"
    "h1{font-size:1.25rem;color:#222;}"
    ".nav{margin-bottom:12px;font-size:15px;}"
    ".hint{color:#555;font-size:14px;line-height:1.5;margin-bottom:14px;}"
    ".keys{display:flex;flex-wrap:wrap;gap:10px;justify-content:center;}"
    ".keys button{width:64px;height:64px;border-radius:12px;border:none;font-size:20px;font-weight:700;"
    "background:#238636;color:#fff;touch-action:manipulation;}"
    ".keys button:active{background:#196f2e;}"
    ".melodies{margin-top:20px;display:flex;flex-direction:column;gap:10px;}"
    ".melodies button{width:100%;padding:14px 16px;font-size:16px;border:none;border-radius:12px;"
    "font-weight:600;touch-action:manipulation;color:#fff;}"
    ".melodies button#tiger{background:#d97706;}"
    ".melodies button#tiger:active{background:#b45309;}"
    ".melodies button:disabled{opacity:.55;}"
    "</style></head><body>"
    "<p class=\"nav\"><a href=\"/\">← 功能首页</a></p>"
    "<h1>七声简谱</h1>"
    "<p class=\"hint\">按 1～7 发单音（板载蜂鸣器）。下方可播放《两只老虎》整曲（七声近似）。摩斯正在播时可能排队或忙。</p>"
    "<div class=\"keys\" id=\"k\"></div>"
    "<div class=\"melodies\">"
    "<button type=\"button\" id=\"tiger\">播放《两只老虎》</button>"
    "</div>"
    "<script>"
    "var k=document.getElementById('k');"
    "for(var n=1;n<=7;n++){"
    "(function(note){var b=document.createElement('button');b.textContent=note;b.type='button';"
    "b.onclick=function(){"
    "fetch('/api/tone',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({note:note})}).then(function(r){return r.json();}).then(function(j){"
    "if(!j.ok&&j.busy)b.style.opacity='0.5';setTimeout(function(){b.style.opacity='1';},120);"
    "}).catch(function(){});};k.appendChild(b);})(n);}"
    "async function playMelody(url,b){b.disabled=true;try{var r=await fetch(url,{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:'{}'});var j=await r.json();"
    "if(!j.ok&&j.busy)b.style.opacity='0.6';setTimeout(function(){b.style.opacity='1';},200);}"
    "catch(e){}b.disabled=false;}"
    "document.getElementById('tiger').onclick=function(){playMelody('/api/play_twotigers',this);};"
    "</script></body></html>";

static const char LCD_PAGE_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>屏幕 Hello</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;padding:16px;max-width:520px;margin:auto;background:#f5f3ff;}"
    "h1{font-size:1.25rem;color:#1e1b4b;}"
    ".nav{margin-bottom:12px;font-size:15px;}"
    ".hint{color:#555;font-size:14px;line-height:1.5;margin-bottom:16px;}"
    "#go{width:100%;padding:14px 16px;font-size:16px;border:none;border-radius:12px;font-weight:600;"
    "background:#5b21b6;color:#fff;touch-action:manipulation;}"
    "#go:active{background:#4c1d95;}"
    "#go:disabled{opacity:.55;}"
    "#msg{margin-top:14px;font-size:15px;color:#333;min-height:1.4em;}"
    "</style></head><body>"
    "<p class=\"nav\"><a href=\"/\">← 功能首页</a></p>"
    "<h1>屏幕 Hello World</h1>"
    "<p class=\"hint\">在 ST7789 上清屏并居中显示 <strong>Hello World</strong>。首次点击会初始化 SPI；失败请看串口日志与 <code>app_config.h</code> 引脚。</p>"
    "<button type=\"button\" id=\"go\">在屏幕上显示 Hello World</button>"
    "<p id=\"msg\"></p>"
    "<script>"
    "document.getElementById('go').onclick=async function(){"
    "var b=this,m=document.getElementById('msg');b.disabled=true;m.textContent='发送中…';"
    "try{var r=await fetch('/api/lcd_hello',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});"
    "var j=await r.json();"
    "if(j.ok){m.textContent='已刷新到屏幕。';}else{m.textContent='失败: '+(j.error||'未知');}"
    "}catch(e){m.textContent='请求失败: '+e;}"
    "b.disabled=false;};"
    "</script></body></html>";

static const char TANK_PAGE_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>全屏 Tank</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;padding:16px;max-width:520px;margin:auto;background:#ecfdf5;}"
    "h1{font-size:1.25rem;color:#134e4a;}"
    ".nav{margin-bottom:12px;font-size:15px;}"
    ".hint{color:#555;font-size:14px;line-height:1.5;margin-bottom:16px;}"
    "#go{width:100%;padding:14px 16px;font-size:16px;border:none;border-radius:12px;font-weight:600;"
    "background:#0f766e;color:#fff;touch-action:manipulation;}"
    "#go:active{background:#115e59;}"
    "#go:disabled{opacity:.55;}"
    "#msg{margin-top:14px;font-size:15px;color:#333;min-height:1.4em;}"
    "</style></head><body>"
    "<p class=\"nav\"><a href=\"/\">← 功能首页</a></p>"
    "<h1>全屏 tank.png</h1>"
    "<p class=\"hint\">图源 <code>main/tank.png</code>，固件内为 240×280 RGB565。色彩可在 <code>app_config.h</code> 调 "
    "<code>LCD_IMAGE_COMPENSATE_INVERT</code> / <code>LCD_IMAGE_SWAP_RB</code>。</p>"
    "<button type=\"button\" id=\"go\">在屏幕上全屏显示 Tank</button>"
    "<p id=\"msg\"></p>"
    "<script>"
    "document.getElementById('go').onclick=async function(){"
    "var b=this,m=document.getElementById('msg');b.disabled=true;m.textContent='发送中…';"
    "try{var r=await fetch('/api/lcd_tank',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});"
    "var j=await r.json();"
    "if(j.ok){m.textContent='已全屏显示。';}else{m.textContent='失败: '+(j.error||'未知');}"
    "}catch(e){m.textContent='请求失败: '+e;}"
    "b.disabled=false;};"
    "</script></body></html>";

/**
 * 解析 JSON 体 {\"note\":3} 或 {\"note\": 7}，note 为 1～7。
 */
static bool json_extract_note(const char *body, int *note_out)
{
    const char *p = strstr(body, "\"note\"");
    if (!p) {
        return false;
    }
    p = strchr(p, ':');
    if (!p) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p < '1' || *p > '7') {
        return false;
    }
    *note_out = *p - '0';
    return true;
}

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

/** GET / → 卡片功能首页 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
}

/** GET /morse → 摩斯控制页 */
static esp_err_t morse_page_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, MORSE_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/** GET /music → 七声简谱页 */
static esp_err_t music_page_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, MUSIC_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/** GET /lcd → TFT Hello World 页 */
static esp_err_t lcd_page_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, LCD_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/** GET /tank → 全屏 tank 图说明页 */
static esp_err_t tank_page_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, TANK_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/** POST /api/tone body: {\"note\":1..7}，蜂鸣器播单音 */
static esp_err_t tone_post_handler(httpd_req_t *req)
{
    char body[64] = {0};
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

    int note = 0;
    if (!json_extract_note(body, &note)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad note\"}");
        return ESP_OK;
    }

    if (!morse_buzzer_play_scale_note((uint8_t)note, 0)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"busy\":true}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/** POST /api/play_twotigers —— 蜂鸣器播完整《两只老虎》（约数秒，占蜂鸣器互斥） */
static esp_err_t twotigers_post_handler(httpd_req_t *req)
{
    int len = req->content_len;
    if (len < 0 || len > 128) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad length\"}");
        return ESP_OK;
    }
    if (len > 0) {
        char drain[128];
        int r = httpd_req_recv(req, drain, len > (int)sizeof(drain) - 1 ? (int)sizeof(drain) - 1 : len);
        if (r <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no body\"}");
            return ESP_OK;
        }
    }

    if (!morse_buzzer_play_two_tigers()) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"busy\":true}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/** POST /api/lcd_hello —— ST7789 清屏并显示 Hello World（body 可为 {}） */
static esp_err_t lcd_hello_post_handler(httpd_req_t *req)
{
    int len = req->content_len;
    if (len < 0 || len > 128) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad length\"}");
        return ESP_OK;
    }
    if (len > 0) {
        char drain[128];
        int r = httpd_req_recv(req, drain, len > (int)sizeof(drain) - 1 ? (int)sizeof(drain) - 1 : len);
        if (r <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no body\"}");
            return ESP_OK;
        }
    }

    esp_err_t e = lcd_st7789_show_hello();
    if (e != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"lcd_init\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/** POST /api/lcd_tank —— ST7789 全屏显示 tank（body 可为 {}） */
static esp_err_t lcd_tank_post_handler(httpd_req_t *req)
{
    int len = req->content_len;
    if (len < 0 || len > 128) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad length\"}");
        return ESP_OK;
    }
    if (len > 0) {
        char drain[128];
        int r = httpd_req_recv(req, drain, len > (int)sizeof(drain) - 1 ? (int)sizeof(drain) - 1 : len);
        if (r <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no body\"}");
            return ESP_OK;
        }
    }

    esp_err_t e = lcd_st7789_show_tank();
    if (e != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"lcd_init\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/** GET /favicon.ico → 返回 204，减少浏览器反复请求时的无意义 404 日志 */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, "", 0);
}

/**
 * POST /api/preview —— 用户每输入一个点或划，网页调用；板子点红、划绿闪一下作反馈。
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
    (void)lcd_st7789_show_morse_preview(sym);

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

    (void)lcd_st7789_show_morse_live(pattern, decoded);

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

    (void)lcd_st7789_show_morse_live(pattern, decoded);

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
    /* 默认 max_uri_handlers=8，本工程注册项多于 8，不加大则 /api/preview、decode、play 会注册失败→404 */
    config.max_uri_handlers = 20;
    /* 默认 4096 对 decode/play 等 handler + 解析器偏紧，提到 16K 更稳 */
    config.stack_size = 16384;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        /* user_ctx 可传自定义指针；本工程各 handler 不需要 */
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);
        httpd_uri_t morse_page = {.uri = "/morse", .method = HTTP_GET, .handler = morse_page_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &morse_page);
        httpd_uri_t music_page = {.uri = "/music", .method = HTTP_GET, .handler = music_page_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &music_page);
        httpd_uri_t lcd_page = {.uri = "/lcd", .method = HTTP_GET, .handler = lcd_page_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &lcd_page);
        httpd_uri_t tank_page = {.uri = "/tank", .method = HTTP_GET, .handler = tank_page_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &tank_page);
        httpd_uri_t fav = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &fav);
        httpd_uri_t tone = {.uri = "/api/tone", .method = HTTP_POST, .handler = tone_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &tone);
        httpd_uri_t twotigers = {.uri = "/api/play_twotigers", .method = HTTP_POST, .handler = twotigers_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &twotigers);
        httpd_uri_t lcd_hello = {.uri = "/api/lcd_hello", .method = HTTP_POST, .handler = lcd_hello_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &lcd_hello);
        httpd_uri_t lcd_tank = {.uri = "/api/lcd_tank", .method = HTTP_POST, .handler = lcd_tank_post_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &lcd_tank);
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
