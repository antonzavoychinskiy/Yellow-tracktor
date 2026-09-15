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
#include "nunchuk.h"
#include "display.h"
#include "telemetry_source.h"
#include "control_loop.h"
#include "mission_ui.h"
#include "auto_sequence.h"
#include "state_machine_task.h"
#include "ui_task.h"

static const char *TAG = "module_main";

void app_main(void)
{
    ESP_LOGI(TAG, "esp_display module starting (synthetic_mode=%d)",
             MODULE_SYNTHETIC_MODE);

    keyswitch_hal_init();
    buttons_hal_init();
    encoder_hal_init();
    nunchuk_hal_init();
    display_hal_init();
    control_loop_hal_init();
    mission_ui_hal_init();
    auto_sequence_hal_init();

    ESP_ERROR_CHECK(telemetry_source_init());
    ESP_ERROR_CHECK(state_machine_start());
    ESP_ERROR_CHECK(ui_task_start());

    ESP_LOGI(TAG, "RT task on core 0, UI task on core 1 started");
}
