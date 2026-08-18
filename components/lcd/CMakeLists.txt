set(srcs "")
if(CONFIG_DEMO_LCD)
    list(APPEND srcs "lcd_st7789.c" "font8x8_basic.c")
endif()
if(CONFIG_DEMO_TANK)
    list(APPEND srcs "tank_rgb565.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES board driver esp_lcd heap freertos
)
