idf_component_register(
    SRCS "control_loop_core.c" "control_loop_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES config nunchuk telemetry_source freertos
)
