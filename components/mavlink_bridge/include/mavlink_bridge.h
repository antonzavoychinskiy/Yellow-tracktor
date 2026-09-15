#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ardurover_modes.h"

/*
 * Обёртка над протоколом MAVLink v2 (библиотека common/mavlink.h,
 * вендорена в mavlink/, см. VENDORED.md) поверх UART TELEM2.
 *
 * Верхние уровни (state_machine, control_loop, mission_ui) не знают о
 * структуре MAVLink-сообщений напрямую — только об этом API и о
 * mavlink_bridge_event_t.
 *
 * Приём работает через отдельную задачу (RX task), которая парсит
 * входящий поток побайтно и складывает декодированные события в
 * очередь; mavlink_bridge_poll_event() — неблокирующее чтение очереди.
 */

typedef enum {
    MAVLINK_EVENT_HEARTBEAT,
    MAVLINK_EVENT_COMMAND_ACK,
    MAVLINK_EVENT_STATUSTEXT,
} mavlink_event_type_t;

typedef struct {
    uint8_t base_mode;
    uint32_t custom_mode;   /* см. ardurover_modes.h */
    uint8_t system_status;
    bool armed;             /* (base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0 */
} mavlink_heartbeat_info_t;

/*
 * Периодическая телеметрия (FR-32) + путевая скорость для детекции
 * остановки (FR-5). В отличие от HEARTBEAT/COMMAND_ACK/STATUSTEXT это
 * не дискретные события, а "текущее известное значение" — отдельный
 * потокобезопасный снимок, а не очередь (иначе UI/RT-уровню пришлось
 * бы разбирать поток пакетов ради последнего значения).
 */
typedef struct {
    bool have_heartbeat;
    mavlink_heartbeat_info_t heartbeat;

    bool have_vfr_hud;
    float groundspeed_mps;      /* VFR_HUD.groundspeed — для FR-5 */

    bool have_sys_status;
    float battery_voltage_v;    /* SYS_STATUS.voltage_battery / 1000 */
    float battery_current_a;    /* SYS_STATUS.current_battery / 100, <0 — не передаётся */

    bool have_gps;
    uint8_t gps_fix_type;       /* GPS_RAW_INT.fix_type */
    uint8_t gps_satellites_visible;

    bool have_mission_current;
    uint16_t mission_current_seq; /* MISSION_CURRENT.seq — текущая путевая точка */
} mavlink_telemetry_snapshot_t;

typedef struct {
    uint16_t command;  /* MAV_CMD_* */
    uint8_t result;    /* MAV_RESULT_* */
} mavlink_command_ack_info_t;

typedef struct {
    uint8_t severity;  /* MAV_SEVERITY_* */
    char text[51];     /* STATUSTEXT.text[50] + NUL */
} mavlink_statustext_info_t;

typedef struct {
    mavlink_event_type_t type;
    union {
        mavlink_heartbeat_info_t heartbeat;
        mavlink_command_ack_info_t command_ack;
        mavlink_statustext_info_t statustext;
    } data;
} mavlink_bridge_event_t;

/* Инициализация UART (TELEM2, 115200, п. 3.2) и запуск RX-задачи. */
esp_err_t mavlink_bridge_init(void);

/* Неблокирующее чтение следующего декодированного события. Возвращает
 * false, если очередь пуста. Вызывается из задачи уровня интерфейса
 * (NFR-1: не блокирует RT-уровень). */
bool mavlink_bridge_poll_event(mavlink_bridge_event_t *out_event);

/* Снимок последнего принятого HEARTBEAT — thread-safe, для RT-уровня
 * (FR-8, FR-9), не требует разбора очереди событий. */
bool mavlink_bridge_get_last_heartbeat(mavlink_heartbeat_info_t *out);

/* Снимок всей периодической телеметрии (FR-32, FR-5) — thread-safe. */
void mavlink_bridge_get_telemetry_snapshot(mavlink_telemetry_snapshot_t *out);

/* Возраст последнего принятого HEARTBEAT в мс. Используется для
 * детекции потери связи (FR-38, FR-44) вместе с
 * MODULE_MAVLINK_LINK_TIMEOUT_MS. Возвращает -1, если HEARTBEAT ещё
 * ни разу не был получен. */
int64_t mavlink_bridge_last_heartbeat_age_ms(void);

/* ---- Отправка ---- */

/* Периодический собственный HEARTBEAT — стандартная практика
 * MAVLink-узла, обозначенного как GCS (sysid = MAV_GCS_SYSID),
 * помогает ArduPilot отслеживать наличие GCS-канала. */
void mavlink_bridge_send_heartbeat(void);

/* MAV_CMD_COMPONENT_ARM_DISARM. force всегда false — FR-7.1 запрещает
 * принудительный арминг/дизарм (magic-обход проверок). */
void mavlink_bridge_send_arm_disarm(bool arm);

/* MAV_CMD_DO_SET_MODE, custom_mode — см. ardurover_modes.h. */
void mavlink_bridge_send_set_mode(uint32_t custom_mode);

/* PARAM_SET с float-значением (SCR_USER1/SCR_USER2, раздел 9). */
void mavlink_bridge_send_param_set_float(const char *param_id, float value);

/* MANUAL_CONTROL. Оси x/y/z/r и buttons — как в MAVLink (-1000..1000,
 * z оставляем 0 — платформа наземная, не использует ось throttle
 * отдельно от x/y; см. control_loop, этап 5). */
void mavlink_bridge_send_manual_control(int16_t x, int16_t y, int16_t z, int16_t r,
                                         uint16_t buttons);
