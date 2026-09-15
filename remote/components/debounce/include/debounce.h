#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Простой антидребезг по времени устойчивости (FR-39): уровень
 * считается новым стабильным значением только после того, как он не
 * менялся не менее stable_delay_us подряд.
 *
 * Чистая логика, без зависимости от ESP-IDF (время передаётся снаружи
 * через now_us) — тестируется на хосте (env native, этап 9).
 */

typedef struct {
    bool stable_level;
    bool candidate_level;
    int64_t candidate_since_us;
    bool have_candidate;
    uint32_t stable_delay_us;
} debounce_t;

void debounce_init(debounce_t *d, bool initial_level, uint32_t stable_delay_us);

/* Подать новое сырое показание. Возвращает true, если stable_level
 * изменился в результате этого вызова. */
bool debounce_update(debounce_t *d, bool raw_level, int64_t now_us);

static inline bool debounce_level(const debounce_t *d)
{
    return d->stable_level;
}
