# main 组件：列出参与编译的 .c 与头文件搜索路径（"." 表示本目录，与 #include "xxx.h" 对应）
# REQUIRES：本组件会 #include 到的 ESP-IDF 其它组件，链接时自动拉取依赖
idf_component_register(
    SRCS "main.c"
         "morse_decode.c"
         "morse_player.c"
         "morse_buzzer.c"
         "wifi_softap.c"
         "morse_http.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip driver esp_http_server esp_wifi esp_netif nvs_flash esp_event
)
