# lvgl подтягивается менеджером компонентов ESP-IDF (idf_component.yml).
# Проверено сборкой на ESP-IDF 6.0.1: имя компонента в REQUIRES — "lvgl".
idf_component_register(
    SRCS "lvgl_port.c" "ui.c" "fonts/lv_font_ru_14.c"
    INCLUDE_DIRS "include" "fonts"
    REQUIRES config display state_machine control_loop mission_ui
             auto_sequence telemetry_source nunchuk esp_timer lvgl
)

# lv_font_conv генерирует #include "lvgl.h" только под этим макросом
# (иначе пытается "lvgl/lvgl.h") — остальные файлы компонента уже
# используют плоский путь "lvgl.h", держим это единообразно.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
