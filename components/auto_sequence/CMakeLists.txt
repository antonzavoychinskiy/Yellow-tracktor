idf_component_register(
    SRCS "auto_sequence_core.c" "auto_sequence_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config buttons telemetry_source mission_ui mavlink_bridge freertos esp_timer
)
