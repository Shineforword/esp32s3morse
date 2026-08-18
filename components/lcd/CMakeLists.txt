idf_component_register(
    SRCS "lcd_st7789.c" "font8x8_basic.c" "tank_rgb565.c"
    INCLUDE_DIRS "include"
    REQUIRES board driver esp_lcd heap freertos
)
