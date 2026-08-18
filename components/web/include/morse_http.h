/**
 * morse_http.h —— 板载 HTTP 服务：网页 + REST 风格 JSON 接口
 *
 * 启动后监听 80 端口（默认），手机连热点后访问 http://192.168.4.1/
 */
#pragma once

/** 创建 httpd 任务并注册首页、/api/preview、/api/decode、/api/play 等 URI 处理器 */
void morse_http_start(void);
