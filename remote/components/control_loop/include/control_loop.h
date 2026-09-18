#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Локальное ручное управление (раздел 5.3, FR-11..19, NFR-4..6).
 * Вызывается ровно раз за тик RT-цикла (20 Гц, см.
 * state_machine_task.c), пока верхний автомат находится в
 * SM_STATE_LOCAL_ACTIVE — т.е. армлено и MANUAL уже подтверждён;
 * вопросы арминга/режима этот модуль не решает (см. state_machine.h).
 *
 * Чистое ядро — тестируется на хосте (этап 9).
 */

typedef struct {
    bool initial_neutral_confirmed; /* FR-17 */
    bool have_first_reading;        /* FR-15 */
    int16_t last_sent_x;            /* для ограничения скорости нарастания, FR-19 */
    int16_t last_sent_y;
} control_loop_state_t;

typedef struct {
    bool valid;           /* показание осей получено в этом тике (FR-37.1 — при сбое чтения ADC false) */
    int16_t raw_x;         /* центрировано, см. joystick_adc_center() */
    int16_t raw_y;
    bool dead_man_held;    /* отдельная кнопка «мёртвая рука» */
} control_loop_joystick_input_t;

typedef struct {
    int16_t out_x;               /* -1000..1000, готово для MANUAL_CONTROL */
    int16_t out_y;
    bool movement_allowed;
    bool waiting_initial_neutral; /* FR-17: для подсказки на экране */
    bool dead_man_engaged;
} control_loop_output_t;

/* Вызывается один раз при входе в SM_STATE_LOCAL_ACTIVE (FR-15:
 * заново требуется нейтраль + первое показание при каждом новом
 * входе в LOCAL). */
void control_loop_reset(control_loop_state_t *st);

void control_loop_tick(control_loop_state_t *st, const control_loop_joystick_input_t *in,
                        control_loop_output_t *out);

/* ---- HAL (ESP-IDF) ---- */

/* Вызвать один раз в app_main() до старта RT-задачи. */
void control_loop_hal_init(void);

/* Вызывать из RT-цикла на каждом тике (после sm_tick), передавая
 * active = (sm_state == SM_STATE_LOCAL_ACTIVE). Сигнатура намеренно
 * не зависит от типов state_machine — иначе получился бы циклический
 * зависимый граф компонентов (state_machine вызывает control_loop, а
 * control_loop не должен требовать state_machine обратно). При active
 * == true читает джойстик и отправляет MANUAL_CONTROL; при переходе
 * false -> true выполняет control_loop_reset() (FR-15). */
void control_loop_hal_tick(bool active);

/* Потокобезопасные геттеры для UI (этап 8, экран "Локальное управление"). */
bool control_loop_hal_get_dead_man_engaged(void);
bool control_loop_hal_get_movement_allowed(void);
bool control_loop_hal_get_waiting_initial_neutral(void);
