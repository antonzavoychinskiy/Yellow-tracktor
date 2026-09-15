#pragma once

#include <stdbool.h>
#include "config.h"

/*
 * Хуки управления синтетическим генератором — только для
 * интеграционных проверок отказных сценариев на этапе 9
 * (СЦ-7 "неподходящий маршрут", СЦ-9 "отказ арминга"). Продуктовая
 * логика (state_machine, mission_ui) их не вызывает.
 */

#if MODULE_SYNTHETIC_MODE

void telemetry_synthetic_set_prearm_fail(bool fail, const char *reason_text);
void telemetry_synthetic_set_route_too_far(int route_num, bool too_far);
void telemetry_synthetic_reset(void);

#endif
