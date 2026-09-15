idf_component_register(
    SRCS "nunchuk_decode.c" "nunchuk_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config driver esp_driver_gpio esp_timer
)
