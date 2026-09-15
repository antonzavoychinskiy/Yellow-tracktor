idf_component_register(
    SRCS "display_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config esp_driver_spi esp_lcd
)
