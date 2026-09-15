idf_component_register(
    SRCS "encoder_decode.c" "encoder_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config debounce esp_driver_gpio esp_timer freertos
)
