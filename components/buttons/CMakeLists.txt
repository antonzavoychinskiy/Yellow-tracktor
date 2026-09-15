idf_component_register(
    SRCS "button_decode.c" "buttons_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config debounce esp_driver_gpio esp_timer
)
