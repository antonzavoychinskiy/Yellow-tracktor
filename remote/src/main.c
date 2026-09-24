/*
 * Точка входа прошивки Модуля управления платформой.
 *
 * Инициализирует драйверы периферии, затем источник телеметрии
 * (реальный MAVLink или синтетический — MODULE_SYNTHETIC_MODE) и две
 * задачи: RT-уровень (state_machine, ядро 0) и уровень интерфейса
 * (ui_task: меню/маршруты/«Пуск»/LVGL, ядро 1) — см. NFR-1..3.
 *
 * Сборка не запускается до появления стенда (см. platformio.ini).
 */

#include "config.h"
#include "esp_err.h"
#include "esp_log.h"

#include "keyswitch.h"
#include "buttons.h"
#include "encoder.h"
#include "joystick_adc.h"
#include "display.h"
#include "telemetry_source.h"
#include "control_loop.h"
#include "mission_ui.h"
#include "auto_sequence.h"
#include "buzzer.h"
#include "state_machine_task.h"
#include "ui_task.h"

static const char *TAG = "module_main";

#if MODULE_SYNTHETIC_MODE
/*
 * Временный отладочный вывод сырых показаний входов — только в сборке
 * debug_synthetic, для стендовой проверки пульта на столе без
 * Pixhawk. Читает GPIO напрямую (не через buttons_hal_*), чтобы не
 * "съедать" фронты нажатий, которые в этом же тике должны увидеть
 * state_machine/ui_task. Удалить после того, как стенд подтверждён.
 */
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void debug_input_probe_task(void *arg)
{
    (void)arg;

    /* Раз в 300 мс — грубый снимок джойстика, для него точный тайминг не
     * важен. */
    int64_t last_joy_log_us = 0;

    /* Кнопки/энкодер логируются СРАЗУ при каждом изменении сырого
     * уровня (не по таймеру) — иначе короткий дребезг короче периода
     * опроса просто не попадёт в лог. Поэтому сам опрос идёт часто
     * (каждые 2 мс), а печать — только при реальной смене уровня. */
    int prev_back = gpio_get_level(MODULE_BUTTON_BACK_GPIO);
    int prev_start = gpio_get_level(MODULE_BUTTON_START_GPIO);
    int prev_enc_sw = gpio_get_level(MODULE_ENCODER_SW_GPIO);
    int prev_dead_man = gpio_get_level(MODULE_DEAD_MAN_GPIO);

    for (;;) {
        int64_t now = esp_timer_get_time();

        int back    = gpio_get_level(MODULE_BUTTON_BACK_GPIO);
        int start   = gpio_get_level(MODULE_BUTTON_START_GPIO);
        int enc_sw  = gpio_get_level(MODULE_ENCODER_SW_GPIO);
        int dead_man = gpio_get_level(MODULE_DEAD_MAN_GPIO);

        if (back != prev_back) {
            ESP_LOGI("debug_input", "EDGE back: %d -> %d", prev_back, back);
            prev_back = back;
        }
        if (start != prev_start) {
            ESP_LOGI("debug_input", "EDGE start: %d -> %d", prev_start, start);
            prev_start = start;
        }
        if (enc_sw != prev_enc_sw) {
            ESP_LOGI("debug_input", "EDGE enc_sw: %d -> %d", prev_enc_sw, enc_sw);
            prev_enc_sw = enc_sw;
        }
        if (dead_man != prev_dead_man) {
            ESP_LOGI("debug_input", "EDGE dead_man: %d -> %d", prev_dead_man, dead_man);
            prev_dead_man = dead_man;
        }

        if (now - last_joy_log_us >= 300000) {
            joystick_adc_sample_t js = {0};
            joystick_adc_hal_read(&js);
            /* Обратное преобразование joystick_adc_center.c (raw ->
             * centered) — печатаем сырое значение ADC, оно и нужно для
             * подбора MODULE_JOYSTICK_ADC_CENTER_*. Учитывает инверсию:
             * centered = invert ? -(raw-center) : (raw-center). */
            int raw_x = MODULE_JOYSTICK_X_INVERT ? (MODULE_JOYSTICK_ADC_CENTER_X - js.x)
                                                  : (js.x + MODULE_JOYSTICK_ADC_CENTER_X);
            int raw_y = MODULE_JOYSTICK_Y_INVERT ? (MODULE_JOYSTICK_ADC_CENTER_Y - js.y)
                                                  : (js.y + MODULE_JOYSTICK_ADC_CENTER_Y);
            ESP_LOGI("debug_input", "joy raw_x=%d raw_y=%d (centered x=%d y=%d)",
                     raw_x, raw_y, js.x, js.y);
            last_joy_log_us = now;
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "esp_display module starting (synthetic_mode=%d)",
             MODULE_SYNTHETIC_MODE);

    keyswitch_hal_init();
    buttons_hal_init();
    encoder_hal_init();
    ESP_ERROR_CHECK(joystick_adc_hal_init());
    display_hal_init();
    control_loop_hal_init();
    mission_ui_hal_init();
    auto_sequence_hal_init();
    buzzer_hal_init();

    ESP_ERROR_CHECK(telemetry_source_init());
    ESP_ERROR_CHECK(state_machine_start());
    ESP_ERROR_CHECK(ui_task_start());

    ESP_LOGI(TAG, "RT task on core 0, UI task on core 1 started");

#if MODULE_SYNTHETIC_MODE
    xTaskCreate(debug_input_probe_task, "debug_input_probe", 3072, NULL, 1, NULL);
#endif
}
