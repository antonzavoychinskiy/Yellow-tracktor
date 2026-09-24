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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void debug_input_probe_task(void *arg)
{
    (void)arg;
    for (;;) {
        joystick_adc_sample_t js = {0};
        joystick_adc_hal_read(&js);

        /* Активный уровень везде low (см. config.h): 0 — нажата/удержана. */
        int back    = gpio_get_level(MODULE_BUTTON_BACK_GPIO);
        int start   = gpio_get_level(MODULE_BUTTON_START_GPIO);
        int enc_sw  = gpio_get_level(MODULE_ENCODER_SW_GPIO);
        int dead_man = gpio_get_level(MODULE_DEAD_MAN_GPIO);

        ESP_LOGI("debug_input",
                 "joy x=%d y=%d | back=%d start=%d enc_sw=%d dead_man=%d (0=нажата)",
                 js.x, js.y, back, start, enc_sw, dead_man);

        vTaskDelay(pdMS_TO_TICKS(300));
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
