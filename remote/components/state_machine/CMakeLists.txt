# state_machine_core.c — чистое ядро, без ESP-IDF зависимостей (кроме
# config.h/ardurover_modes.h, которые сами по себе просто #define).
# state_machine_task.c — HAL/задача. esp_task_wdt.h в ESP-IDF 6.0.1
# (framework-espidf 4.60001.0, проверено при первой реальной сборке,
# этап 10) принадлежит компоненту esp_system, а не отдельному
# "esp_task_wdt".
idf_component_register(
    SRCS "state_machine_core.c" "state_machine_task.c"
    INCLUDE_DIRS "include"
    REQUIRES config keyswitch telemetry_source mavlink_bridge control_loop esp_timer freertos esp_system
)

# state_machine_task.c включает common/mavlink.h напрямую (MAV_CMD_*/
# MAV_RESULT_*) — тот же -Waddress-of-packed-member из вендоренного
# кода, см. components/mavlink_bridge/CMakeLists.txt.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=address-of-packed-member)
