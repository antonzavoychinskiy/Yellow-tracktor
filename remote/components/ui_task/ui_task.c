#include "ui_task.h"
#include "config.h"
#include "state_machine_task.h"
#include "mission_ui.h"
#include "auto_sequence.h"
#include "buzzer.h"
#include "ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UI_TASK_STACK       8192 /* LVGL — с запасом */
#define UI_TASK_PRIO         (tskIDLE_PRIORITY + 2) /* ниже RT-уровня — NFR-1 */
#define UI_TASK_CORE          1                       /* NFR-3 */
#define UI_TICK_PERIOD_MS    20

#if MODULE_SYNTHETIC_MODE
/*
 * Временная диагностика долгих итераций UI-задачи (стенд без Pixhawk,
 * подозрение — редкие "подвисания" ui_tick() дольше периода опроса
 * энкодера/кнопок пропускают короткое нажатие целиком). Удалить вместе
 * с остальной отладкой в src/main.c после подтверждения/опровержения.
 */
#include "esp_log.h"
#include "esp_timer.h"
#define UI_TICK_STALL_WARN_MS  (UI_TICK_PERIOD_MS * 2)
#endif

static bool is_off_state(sm_state_t s)
{
    return s == SM_STATE_OFF_WAIT_STOP || s == SM_STATE_OFF_WAIT_DISARM_CONFIRM ||
           s == SM_STATE_OFF_IDLE || s == SM_STATE_OFF_FAILED;
}

static buzzer_mode_t buzzer_mode_for_auto_seq_state(auto_sequence_state_t s)
{
    switch (s) {
    case AUTO_SEQ_CONFIRM_HOLD: return BUZZER_MODE_BEEP_HOLD;           /* FR-40.5 */
    case AUTO_SEQ_CONFIRM_COUNTDOWN: return BUZZER_MODE_BEEP_COUNTDOWN; /* FR-40.6 */
    default: return BUZZER_MODE_OFF;
    }
}

static void ui_task_fn(void *arg)
{
    /* LVGL инициализируется здесь же, а не в main.c — все обращения к
     * нему должны идти из одного и того же task-контекста (LVGL 8.3
     * без явной блокировки не потокобезопасен). */
    ESP_ERROR_CHECK(ui_init());

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
#if MODULE_SYNTHETIC_MODE
        int64_t iter_start_us = esp_timer_get_time();
#endif
        sm_state_t sm_state = state_machine_get_state();
        mission_ui_hal_tick(is_off_state(sm_state)); /* FR-27 */
        auto_sequence_hal_tick(sm_state == SM_STATE_AUTO); /* FR-40.x */
        buzzer_hal_set_mode(buzzer_mode_for_auto_seq_state(auto_sequence_hal_get_state())); /* FR-40.7 */
        buzzer_hal_tick();
        ui_tick(); /* FR-41..46 */

#if MODULE_SYNTHETIC_MODE
        int64_t iter_ms = (esp_timer_get_time() - iter_start_us) / 1000;
        if (iter_ms > UI_TICK_STALL_WARN_MS) {
            ESP_LOGW("ui_task", "STALL: iteration took %lld ms (period %d ms)",
                     (long long)iter_ms, UI_TICK_PERIOD_MS);
        }
#endif

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_TICK_PERIOD_MS));
    }
}

esp_err_t ui_task_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(ui_task_fn, "ui_task", UI_TASK_STACK, NULL,
                                             UI_TASK_PRIO, NULL, UI_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
