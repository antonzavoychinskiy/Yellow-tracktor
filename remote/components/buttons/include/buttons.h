#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "debounce.h"

/*
 * Кнопка «Назад» — простое событие "нажата" по отдебаунсенному
 * переднему фронту.
 *
 * Кнопка «Пуск» тоже отдаёт фронт (используется как сигнал "начать
 * подтверждение"), но в AUTO дополнительно нужен непрерывный уровень
 * удержания — FR-40.5: заполнение полосы подтверждения на дисплее и
 * отмена при раннем отпускании (см. auto_sequence). Уровень читается
 * геттером ниже отдельно от фронта, без повторного вызова
 * button_update() (он мутирует debounce/prev_level ровно один раз за
 * тик — см. buttons_hal_start_pressed()/buttons_hal_start_held()).
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

/* Текущий отдебаунсенный уровень «Пуск» (true — удерживается прямо
 * сейчас). Вызывать ПОСЛЕ buttons_hal_start_pressed() в том же тике —
 * читает уже обновлённое в этом тике состояние, само ничего не
 * мутирует. */
bool buttons_hal_start_held(void);

/* «Мёртвая рука» (FR-13/FR-14): отдельная кнопка на своём GPIO, не
 * связанная с джойстиком. Нужен только уровень удержания, фронт не
 * используется — сама читает GPIO и обновляет debounce, вызывать один
 * раз за тик RT-цикла. */
bool buttons_hal_dead_man_held(void);
