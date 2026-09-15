idf_component_register(
    SRCS "mission_ui_core.c" "mission_ui_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config encoder buttons telemetry_source state_machine freertos esp_timer
)
