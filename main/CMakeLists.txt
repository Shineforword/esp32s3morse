idf_component_register(
    SRCS "hello_world_main.c"
    INCLUDE_DIRS ""
    REQUIRES led_strip esp_http_server esp_wifi esp_netif nvs_flash esp_event
)
