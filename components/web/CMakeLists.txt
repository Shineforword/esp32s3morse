idf_component_register(
    SRCS "morse_http.c"
    INCLUDE_DIRS "include"
    REQUIRES board buzzer morse lcd wifi mqtt_lan esp_http_server
)
