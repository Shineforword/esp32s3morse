idf_component_register(
    SRCS "morse_decode.c" "morse_player.c"
    INCLUDE_DIRS "include"
    REQUIRES board buzzer lcd led_strip freertos
)
