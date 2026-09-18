#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "keyswitch.h"

/*
 * Верхнеуровневый конечный автомат Модуля (раздел 4, FR-1..FR-10,
 * FR-35). Владеет: переходами OFF/LOCAL/AUTO/WaitOff/Fault и
 * одноразовыми командами входа в каждое положение ключа (FR-10).
 *
 * НЕ владеет (делегируется другим модулям, работающим, пока этот
 * автомат находится в соответствующем состоянии):
 *   - выбором/загрузкой маршрута в OFF (mission_ui, этап 6);
 *   - джойстиком/мёртвой рукой в LOCAL_ACTIVE (control_loop, этап 5) —
 *     этот автомат лишь решает, БЕЗОПАСНО ли сейчас ехать (мониторит
 *     расхождение режима, FR-9), а не КАК формировать команду;
 *   - кнопкой «Пуск» и подпоследовательностью арм/AUTO в AUTO
 *     (auto_sequence, этап 7) — по FR-7 сам по себе переход в AUTO
 *     команд не отправляет.
 *
 * Чистое ядро (state_machine_core.c) не зависит от ESP-IDF —
 * тестируется на хосте (этап 9).
 */

typedef enum {
    SM_STATE_WAIT_OFF,             /* FR-1.1, FR-1.2: ждём OFF, в т.ч. после FAULT и «проскоченного» OFF */
    SM_STATE_OFF_WAIT_STOP,        /* FR-5 шаги 1-2: HOLD отправлен, ждём остановки */
    SM_STATE_OFF_WAIT_DISARM_CONFIRM, /* FR-5 шаги 3-4: DISARM отправлен, ждём HEARTBEAT */
    SM_STATE_OFF_IDLE,             /* дизармлено, устойчивое состояние OFF */
    SM_STATE_OFF_FAILED,           /* FR-5.1: отказ/таймаут, без автоповтора */
    SM_STATE_LOCAL_ARMING,         /* FR-6: ARM + SET_MODE MANUAL отправлены разом */
    SM_STATE_LOCAL_ARM_FAILED,     /* FR-8.1: без автоповтора */
    SM_STATE_LOCAL_ACTIVE,         /* армлено, MANUAL подтверждён — работает control_loop */
    SM_STATE_LOCAL_MODE_LOST,      /* FR-9: расхождение режима/потеря арма/связи */
    SM_STATE_AUTO,                 /* FR-7: пассивно, ждёт auto_sequence/«Пуск» */
    SM_STATE_FAULT,                /* FR-35: недопустимое положение ключа */
} sm_state_t;

typedef struct {
    sm_state_t state;
    key_position_t last_key;
    int64_t state_entered_ms;
} sm_context_t;

typedef struct {
    key_position_t key;            /* уже отдебаунсенное положение ключа */

    bool have_heartbeat;
    bool armed;
    uint32_t custom_mode;          /* см. ardurover_modes.h */

    bool have_groundspeed;
    float groundspeed_mps;

    /* Результат последней команды арминга/дизарма — потребляется
     * один раз (как элемент очереди событий), затем должен сбрасываться
     * вызывающей стороной до следующего реального ACK. */
    bool arm_ack_received;
    bool arm_ack_accepted;
    bool disarm_ack_received;
    bool disarm_ack_accepted;

    int64_t now_ms;
} sm_inputs_t;

typedef struct {
    bool cmd_send_hold;
    bool cmd_send_disarm;
    bool cmd_send_arm;
    bool cmd_send_mode_manual;
    bool cmd_send_neutral_once;    /* FR-14, FR-10.1 */
    bool warn_mode_mismatch;       /* FR-9 */
    bool warn_invalid_key;         /* FR-35 */
} sm_outputs_t;

/* Инициализация с учётом положения ключа на старте (FR-1.1, FR-1). */
void sm_init(sm_context_t *ctx, key_position_t initial_key, int64_t now_ms, sm_outputs_t *out);

/* Один шаг автомата. out обнуляется внутри перед заполнением. */
void sm_tick(sm_context_t *ctx, const sm_inputs_t *in, sm_outputs_t *out);
