# SRCS 可按 CONFIG 裁剪；REQUIRES 须写死（组件早期展开读不到 CONFIG）
set(srcs "")
if(CONFIG_DEMO_MORSE)
    list(APPEND srcs "morse_decode.c" "morse_player.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES board buzzer led_strip freertos lcd
)
