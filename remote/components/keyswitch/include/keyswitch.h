#pragma once

#include <stdbool.h>

/*
 * Ключ — трёхпозиционный последовательный переключатель LOCAL-OFF-AUTO
 * (раздел 4.1). Прошивка читает два контакта (LOCAL, AUTO).
 */

typedef enum {
    KEY_POS_OFF = 0,
    KEY_POS_LOCAL,
    KEY_POS_AUTO,
    KEY_POS_INVALID,   /* FR-35: оба контакта замкнуты одновременно */
} key_position_t;

/*
 * Чистая дешифровка положения ключа из двух УЖЕ отдебаунсенных
 * (стабильных) уровней контактов. Table по п. 4.1:
 *   LOCAL=0 AUTO=0 -> OFF
 *   LOCAL=1 AUTO=0 -> LOCAL
 *   LOCAL=0 AUTO=1 -> AUTO
 *   LOCAL=1 AUTO=1 -> INVALID (FR-35)
 *
 * Без зависимостей от ESP-IDF — тестируется на хосте.
 */
key_position_t keyswitch_decode(bool local_contact, bool auto_contact);

/* ---- HAL (ESP-IDF): опрос реальных GPIO с антидребезгом ---- */

void keyswitch_hal_init(void);

/* Вызывается периодически из RT-цикла (см. этап 4). Возвращает
 * текущее стабильное положение ключа. */
key_position_t keyswitch_hal_read(void);
