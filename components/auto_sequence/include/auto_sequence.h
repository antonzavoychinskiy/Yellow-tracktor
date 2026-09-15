#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Кнопка «Пуск» и подпоследовательность перехода в AUTO (раздел 5.8,
 * FR-40.x, диаграмма 8А.3). Активен, пока верхний автомат — в
 * SM_STATE_AUTO; вопрос "мы сейчас в AUTO" решает вызывающая сторона.
 *
 * Не гейтится COMMAND_ACK — только HEARTBEAT (armed/custom_mode) +
 * таймаут, как и остальные последовательности арминга в проекте;
 * причина отказа для оператора берётся из STATUSTEXT отдельным путём
 * (общая "витрина" STATUSTEXT уровня UI, этап 8), не через этот модуль.
 *
 * Чистое ядро — тестируется на хосте (этап 9).
 */

typedef enum {
    AUTO_SEQ_READY,        /* маршрут загружен, ждём «Пуск» */
    AUTO_SEQ_BLOCKED,      /* FR-40.1: маршрут не загружен — «Пуск» игнорируется */
    AUTO_SEQ_ARMING,       /* FR-40: арм отправлен */
    AUTO_SEQ_ARM_FAILED,   /* FR-40.2: отказ/таймаут — повтор доступен новым нажатием «Пуск» */
    AUTO_SEQ_SETTING_MODE, /* FR-40.3: арминг подтверждён, SET_MODE AUTO отправлен */
    AUTO_SEQ_MOVING,       /* режим AUTO подтверждён по HEARTBEAT */
} auto_sequence_state_t;

typedef struct {
    auto_sequence_state_t state;
    int64_t state_entered_ms;
} auto_sequence_ctx_t;

typedef struct {
    bool active;                   /* sm_state == SM_STATE_AUTO */
    bool route_loaded_this_cycle;  /* FR-24.1, из mission_ui */
    bool start_pressed;            /* FR-40.4: моментное действие */
    bool have_heartbeat;
    bool armed;
    uint32_t custom_mode;          /* см. ardurover_modes.h */
    int64_t now_ms;
} auto_sequence_inputs_t;

typedef struct {
    bool cmd_send_arm;
    bool cmd_send_mode_auto;
} auto_sequence_outputs_t;

void auto_sequence_init(auto_sequence_ctx_t *ctx);
void auto_sequence_tick(auto_sequence_ctx_t *ctx, const auto_sequence_inputs_t *in,
                         auto_sequence_outputs_t *out);

/* ---- HAL (ESP-IDF) ---- */

void auto_sequence_hal_init(void);

/* Вызывать из UI-задачи на каждом тике, active = (sm_state ==
 * SM_STATE_AUTO). Сигнатура без типов state_machine — по той же
 * причине, что и у control_loop (см. его комментарий). */
void auto_sequence_hal_tick(bool active);

auto_sequence_state_t auto_sequence_hal_get_state(void);
