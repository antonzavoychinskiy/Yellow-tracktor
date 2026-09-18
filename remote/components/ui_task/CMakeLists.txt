idf_component_register(
    SRCS "ui_task.c"
    INCLUDE_DIRS "include"
    REQUIRES config state_machine mission_ui auto_sequence buzzer ui freertos
)
