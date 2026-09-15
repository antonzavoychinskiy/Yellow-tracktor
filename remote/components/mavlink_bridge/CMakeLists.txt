# Библиотека MAVLink (mavlink/) — headers-only (все функции static
# inline), отдельная компиляция не требуется, только INCLUDE_DIRS.
idf_component_register(
    SRCS "mavlink_bridge.c"
    INCLUDE_DIRS "include" "mavlink"
    REQUIRES config esp_driver_uart esp_timer freertos
)

# Вендоренная библиотека (mavlink/, см. VENDORED.md) берёт адрес полей
# packed-структур в сообщениях диалекта common, которые прошивка не
# использует — с -Werror ESP-IDF это останавливает сборку. Проверено
# на ESP-IDF 6.0.1.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=address-of-packed-member)
