idf_component_register(
    SRCS "joystick_adc_center.c" "joystick_adc_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config esp_adc
)
