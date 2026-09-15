idf_component_register(
    SRCS "keyswitch_decode.c" "keyswitch_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config debounce esp_driver_gpio esp_timer
)
