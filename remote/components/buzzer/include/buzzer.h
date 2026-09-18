#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Зуммер подтверждения пуска AUTO (FR-40.5..40.7). Активный зуммер —
 * прошивке достаточно гейтить GPIO HIGH/LOW со своей частотой, тон
 * генерирует сам модуль. Меандр 50% скважности: пищит/молчит поровну
 * внутри периода 1/frequency_hz.
 *
 * Чистая логика (buzzer_square_on) не зависит от ESP-IDF — тестируется
 * на хосте.
 */

typedef enum {
    BUZZER_MODE_OFF = 0,
    BUZZER_MODE_BEEP_HOLD,      /* FR-40.5: фаза удержания — 1 Гц */
    BUZZER_MODE_BEEP_COUNTDOWN, /* FR-40.6: фаза отсчёта — 2 Гц */
} buzzer_mode_t;

/* true, если в момент now_ms зуммер должен быть включён (меандр
 * 50%/50% с частотой freq_hz). freq_hz == 0 -> всегда false. */
bool buzzer_square_on(int64_t now_ms, uint32_t freq_hz);

/* ---- HAL (ESP-IDF) ---- */

void buzzer_hal_init(void);

/* Задать текущий режим — вызывать при каждой смене состояния
 * auto_sequence (см. ui_task). Дешёвая операция, без сайд-эффектов на
 * GPIO — фактическое переключение делает buzzer_hal_tick(). */
void buzzer_hal_set_mode(buzzer_mode_t mode);

/* Вызывать периодически (из UI-задачи, каждый тик) — пересчитывает
 * меандр от текущего времени и выставляет GPIO. */
void buzzer_hal_tick(void);
