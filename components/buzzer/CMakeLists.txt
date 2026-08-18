set(srcs "")
if(CONFIG_DEMO_MORSE OR CONFIG_DEMO_MUSIC)
    list(APPEND srcs "morse_buzzer.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES board driver freertos
)
