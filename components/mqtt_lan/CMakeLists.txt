idf_component_register(
    SRCS "mqtt_lan.c"
    INCLUDE_DIRS "include"
    REQUIRES board wifi mqtt freertos
)
