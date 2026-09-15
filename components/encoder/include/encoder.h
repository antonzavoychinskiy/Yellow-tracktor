#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "debounce.h"

/*
 * Энкодер выбора маршрута: вращение (CLK/DT) + кнопка «OK»/«Загрузить»
 * (SW). Короткое нажатие — вход в карточку (FR-22), долгое — запуск
 * загрузки (FR-23), с индикацией прогресса удержания (FR-23.1).
 * FR-39.1: паразитное вращение в момент нажатия должно подавляться.
 */

/* ---- Чистое ядро квадратурного декодера (тестируется на хосте) ----
 *
 * Табличный full-step декодер (классическая устойчивая к дребезгу
 * схема: событие направления генерируется только на завершённом
 * "щелчке", а не на каждом фронте A/B).
 */

typedef struct {
    uint8_t state; /* внутреннее состояние таблицы переходов */
} encoder_quad_t;

typedef enum {
    ENCODER_DIR_NONE = 0,
    ENCODER_DIR_CW = 1,
    ENCODER_DIR_CCW = -1,
} encoder_dir_t;

void encoder_quad_init(encoder_quad_t *q);

/* Подать текущие уровни CLK (a) и DT (b). Возвращает направление,
 * если в результате этого шага завершился детент, иначе NONE. */
encoder_dir_t encoder_quad_step(encoder_quad_t *q, bool a, bool b);

/* ---- Чистая классификация нажатия кнопки энкодера ---- */

typedef enum {
    ENC_BTN_EVENT_NONE = 0,
    ENC_BTN_EVENT_SHORT_CLICK,
    ENC_BTN_EVENT_LONG_FIRED,
} encoder_btn_event_t;

typedef struct {
    debounce_t db;
    bool prev_level;
    bool held;
    int64_t press_started_us;
    bool long_fired;
} encoder_button_t;

void encoder_button_init(encoder_button_t *eb, bool initial_level, uint32_t stable_delay_us);

/* Вызывать периодически (не только по фронту) — иначе долгое
 * нажатие не будет обнаружено без отпускания. */
encoder_btn_event_t encoder_button_update(encoder_button_t *eb, bool raw_pressed,
                                           int64_t now_us, uint32_t longpress_us);

/* Прогресс удержания в ‰ (0..1000) для индикации (FR-23.1). 0, если
 * кнопка не удерживается. */
uint32_t encoder_button_hold_progress_permille(const encoder_button_t *eb, int64_t now_us,
                                                uint32_t longpress_us);

/* ---- HAL (ESP-IDF) ---- */

void encoder_hal_init(void);

/* Накопленное чистое число детентов со времени предыдущего вызова
 * (положительное — по часовой, отрицательное — против); обнуляется
 * при каждом чтении. Вращение, попавшее в защитное окно вокруг
 * нажатия кнопки (FR-39.1), не учитывается. */
int32_t encoder_hal_take_delta(void);

encoder_btn_event_t encoder_hal_button_update(void);
uint32_t encoder_hal_hold_progress_permille(void);
