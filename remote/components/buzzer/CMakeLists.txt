idf_component_register(
    SRCS "buzzer_core.c" "buzzer_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config esp_driver_gpio esp_timer
)
