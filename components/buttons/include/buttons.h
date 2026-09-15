#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "debounce.h"

/*
 * Кнопки моментного действия: «Пуск» (FR-40.4 — удержание не требуется
 * и не имеет смысла) и «Назад». Обе — простое событие "нажата" по
 * отдебаунсенному переднему фронту.
 */

typedef struct {
    debounce_t db;
    bool prev_level; /* для детекции переднего фронта */
} button_t;

void button_init(button_t *b, bool initial_level, uint32_t stable_delay_us);

/* Чистое обновление: подать сырое показание и время. Возвращает true
 * ровно в тот вызов, когда кнопка перешла в нажатое состояние. */
bool button_update(button_t *b, bool raw_pressed, int64_t now_us);

/* ---- HAL (ESP-IDF) ---- */

void buttons_hal_init(void);

/* Вызываются периодически из RT-цикла. true — было зафиксировано
 * нажатие (однократно на нажатие). */
bool buttons_hal_start_pressed(void);
bool buttons_hal_back_pressed(void);
