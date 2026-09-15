# Оба .c компилируются всегда; фактическое содержимое каждого
# активируется через #if MODULE_SYNTHETIC_MODE — так что в конкретной
# сборке реально линкуется только одна реализация, без дублирования
# символов и без CMake-условий на уровне списка исходников.
idf_component_register(
    SRCS "telemetry_source_real.c" "telemetry_source_synthetic.c"
    INCLUDE_DIRS "include"
    REQUIRES config mavlink_bridge esp_timer freertos
)

# telemetry_source_synthetic.c включает common/mavlink.h напрямую —
# см. components/mavlink_bridge/CMakeLists.txt.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=address-of-packed-member)
