#pragma once

/*
 * HAL/задача ESP-IDF поверх чистого ядра (state_machine.h). Отдельный
 * заголовок специально: state_machine.h должен оставаться пригодным
 * для сборки на хосте (env native, этап 9) без ESP-IDF, а этот файл
 * тянет esp_err.h/FreeRTOS-совместимые типы.
 */

#include "state_machine.h"
#include "esp_err.h"
#include "mavlink_bridge.h" /* mavlink_statustext_info_t */

/* Создаёт задачу уровня реального времени (NFR-1..3): читает ключ,
 * снимок телеметрии и события через telemetry_source, крутит sm_tick,
 * применяет результат командами telemetry_source_send_*, кормит
 * task watchdog (FR-36). Вызывать один раз при старте прошивки. */
esp_err_t state_machine_start(void);

/* Потокобезопасные геттеры для уровня интерфейса (UI, этап 8). */
sm_state_t state_machine_get_state(void);
bool state_machine_get_warn_mode_mismatch(void);
bool state_machine_get_warn_invalid_key(void);

/* STATUSTEXT ретранслируется сюда из общего потока событий
 * telemetry_source (единственный потребитель очереди событий —
 * этот модуль; остальным событие достаётся через эту "витрину",
 * иначе несколько независимых читателей одной FreeRTOS-очереди
 * растащили бы сообщения непредсказуемо). */
bool state_machine_poll_statustext(mavlink_statustext_info_t *out);
