set(srcs "")
if(CONFIG_DEMO_MQTT)
    list(APPEND srcs "mqtt_lan.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES board wifi mqtt freertos
)
