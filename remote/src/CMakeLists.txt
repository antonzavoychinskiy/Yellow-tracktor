# "main"-компонент ESP-IDF. Список REQUIRES расширяется по мере
# появления компонентов на соответствующих этапах роадмапа.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES config keyswitch buttons encoder nunchuk display
             telemetry_source control_loop state_machine mission_ui
             auto_sequence ui_task
)
