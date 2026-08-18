# REQUIRES 在组件早期展开时不可靠地依赖 CONFIG_*，因此始终声明依赖；
# 各功能组件用条件 SRCS 决定是否真正编译（关掉则几乎不占固件）。
idf_component_register(
    SRCS "morse_http.c"
    INCLUDE_DIRS "include"
    REQUIRES board wifi esp_http_server morse buzzer lcd mqtt_lan
)
