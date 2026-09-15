#pragma once

/*
 * Номера режимов ArduRover (custom_mode в HEARTBEAT / параметр
 * MAV_CMD_DO_SET_MODE). Не часть протокола MAVLink — специфика
 * прошивки ArduPilot Rover (стабильные, давно устоявшиеся значения).
 * Используются только три режима, которыми оперирует Модуль (раздел 4).
 */

#define ROVER_MODE_MANUAL  0
#define ROVER_MODE_HOLD    4
#define ROVER_MODE_AUTO    10
