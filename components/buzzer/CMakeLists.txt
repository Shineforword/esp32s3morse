idf_component_register(
    SRCS "morse_buzzer.c"
    INCLUDE_DIRS "include"
    REQUIRES board driver freertos
)
