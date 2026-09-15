#pragma once

#include "config.h"
#include "mavlink_bridge.h"

/*
 * Единая точка входа для верхних уровней (state_machine, control_loop,
 * mission_ui): они работают ТОЛЬКО через telemetry_source_*, не зная,
 * реальный это MAVLink по TELEM2 или синтетический генератор
 * (NFR-15..18).
 *
 * Выбор реализации — на этапе компиляции, через MODULE_SYNTHETIC_MODE
 * (platformio.ini: env:release -> 0, env:debug_synthetic -> 1). Это
 * ОТДЕЛЬНАЯ СБОРКА, а не рантайм-флаг — соответствует NFR-17
 * ("предпочтителен отдельная сборка, а не только компиляционный флаг"
 * в смысле осознанности выбора: две разные прошивки, а не одна с
 * незаметным тумблером).
 *
 * Типы событий/структур переиспользуются из mavlink_bridge.h —
 * синтетический генератор эмулирует их форму (в т.ч. текстовые
 * STATUSTEXT вида "MLIST ..."/"MLOAD ...", как их шлёт
 * mission_select.lua), а не сам протокол MAVLink.
 */

esp_err_t telemetry_source_init(void);

bool telemetry_source_poll_event(mavlink_bridge_event_t *out_event);
bool telemetry_source_get_last_heartbeat(mavlink_heartbeat_info_t *out);
int64_t telemetry_source_last_heartbeat_age_ms(void);
void telemetry_source_get_telemetry_snapshot(mavlink_telemetry_snapshot_t *out);

void telemetry_source_send_heartbeat(void);
void telemetry_source_send_arm_disarm(bool arm);
void telemetry_source_send_set_mode(uint32_t custom_mode);
void telemetry_source_send_param_set_float(const char *param_id, float value);
void telemetry_source_send_manual_control(int16_t x, int16_t y, int16_t z, int16_t r,
                                           uint16_t buttons);

/* Константа времени компиляции — для несъёмного баннера "РЕЖИМ ОТЛАДКИ"
 * (NFR-17), UI спрашивает один раз при отрисовке статус-бара. */
static inline bool telemetry_source_is_synthetic(void)
{
    return MODULE_SYNTHETIC_MODE != 0;
}
