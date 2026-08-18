idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board wifi web nvs_flash morse buzzer
)
