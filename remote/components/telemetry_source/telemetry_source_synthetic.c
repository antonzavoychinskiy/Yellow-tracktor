#include "telemetry_source.h"

#if MODULE_SYNTHETIC_MODE

#include "telemetry_source_synthetic_debug.h"
#include "ardurover_modes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Константы диалекта (MAV_CMD_..., MAV_RESULT_..., MAV_SEVERITY_...,
 * MAV_MODE_FLAG_..., MAV_STATE_...) — не сам протокол; синтетический
 * генератор переиспользует их вместо своих, чтобы не дублировать
 * значения. -Waddress-of-packed-member из этого заголовка подавлен в
 * CMakeLists.txt компонента. */
#include "common/mavlink.h"

/*
 * Синтетический источник телеметрии (NFR-15..18): эмулирует ровно
 * то, что делает mission_select.lua + HEARTBEAT/COMMAND_ACK Pixhawk,
 * без реального MAVLink-соединения. Подтверждает логику Модуля, НЕ
 * подтверждает интеграцию с реальным автопилотом (NFR-18).
 */

static const char *TAG = "telemetry_synthetic";

typedef struct {
    int num;
    const char *name;
    float dist_m;
    int wp;
    float len_m;
} fake_route_t;

/* Те же примеры, что в требованиях (Приложение А.2) — просто для
 * узнаваемости при чтении логов/экрана в отладке. */
static const fake_route_t FAKE_ROUTES[] = {
    { 1, "Sklad_A - Angar_B", 4.0f, 10, 240.0f },
    { 2, "Angar_B - Sklad_A", 6.0f, 10, 240.0f },
    { 3, "Sklad - Pogruzka", 12.0f, 6, 380.0f },
};
#define FAKE_ROUTE_COUNT (int)(sizeof(FAKE_ROUTES) / sizeof(FAKE_ROUTES[0]))

#define EVENT_QUEUE_LEN 16

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_mutex;

static bool s_armed = false;
static uint32_t s_custom_mode = ROVER_MODE_HOLD;

static mavlink_telemetry_snapshot_t s_snapshot;
static volatile int64_t s_last_hb_time_us = -1;

static bool s_prearm_fail = false;
static char s_prearm_fail_reason[64] = "PreArm: GPS: Bad fix";
static bool s_route_too_far[FAKE_ROUTE_COUNT + 1]; /* индекс 1..N */

static void push_event(const mavlink_bridge_event_t *ev)
{
    if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event type=%d", ev->type);
    }
}

static void push_statustext(uint8_t severity, const char *fmt, ...)
{
    mavlink_bridge_event_t ev = { .type = MAVLINK_EVENT_STATUSTEXT };
    ev.data.statustext.severity = severity;

    va_list args;
    va_start(args, fmt);
    vsnprintf(ev.data.statustext.text, sizeof(ev.data.statustext.text), fmt, args);
    va_end(args);

    push_event(&ev);
}

static void publish_heartbeat_now(void)
{
    mavlink_bridge_event_t ev = { .type = MAVLINK_EVENT_HEARTBEAT };
    ev.data.heartbeat.base_mode = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED |
                                   (s_armed ? MAV_MODE_FLAG_SAFETY_ARMED : 0);
    ev.data.heartbeat.custom_mode = s_custom_mode;
    ev.data.heartbeat.system_status = MAV_STATE_ACTIVE;
    ev.data.heartbeat.armed = s_armed;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_snapshot.heartbeat = ev.data.heartbeat;
    s_snapshot.have_heartbeat = true;
    /* Правдоподобные заглушки для FR-5/FR-32 — платформа в
     * синтетическом режиме физически не движется, скорость всегда 0
     * (детекция остановки по FR-5 проходит тривиально и мгновенно,
     * что и требуется для проверки логики без имитации динамики). */
    s_snapshot.have_vfr_hud = true;
    s_snapshot.groundspeed_mps = 0.0f;
    s_snapshot.have_sys_status = true;
    s_snapshot.battery_voltage_v = 12.6f;
    s_snapshot.battery_current_a = 2.0f;
    s_snapshot.have_gps = true;
    s_snapshot.gps_fix_type = 3; /* 3D fix */
    s_snapshot.gps_satellites_visible = 10;
    s_snapshot.have_mission_current = true;
    s_snapshot.mission_current_seq = 0;
    xSemaphoreGive(s_mutex);
    s_last_hb_time_us = esp_timer_get_time();

    push_event(&ev);
}

static void heartbeat_task(void *arg)
{
    while (1) {
        publish_heartbeat_now();
        vTaskDelay(pdMS_TO_TICKS(1000)); /* штатный период HEARTBEAT ArduPilot — 1 Гц */
    }
}

esp_err_t telemetry_source_init(void)
{
    s_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(mavlink_bridge_event_t));
    s_mutex = xSemaphoreCreateMutex();
    if (s_queue == NULL || s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_armed = false;
    s_custom_mode = ROVER_MODE_HOLD;
    s_prearm_fail = false;
    memset(s_route_too_far, 0, sizeof(s_route_too_far));

    publish_heartbeat_now();

    BaseType_t ok = xTaskCreate(heartbeat_task, "synth_hb", 2048, NULL,
                                 tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGW(TAG, "=== РЕЖИМ ОТЛАДКИ — ДАННЫЕ СИНТЕТИЧЕСКИЕ (NFR-15..18) ===");
    return ESP_OK;
}

bool telemetry_source_poll_event(mavlink_bridge_event_t *out_event)
{
    return xQueueReceive(s_queue, out_event, 0) == pdTRUE;
}

bool telemetry_source_get_last_heartbeat(mavlink_heartbeat_info_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_snapshot.heartbeat;
    xSemaphoreGive(s_mutex);
    return true; /* синтетический HEARTBEAT публикуется сразу при init() */
}

void telemetry_source_get_telemetry_snapshot(mavlink_telemetry_snapshot_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_snapshot;
    xSemaphoreGive(s_mutex);
}

int64_t telemetry_source_last_heartbeat_age_ms(void)
{
    if (s_last_hb_time_us < 0) {
        return -1;
    }
    return (esp_timer_get_time() - s_last_hb_time_us) / 1000;
}

void telemetry_source_send_heartbeat(void)
{
    /* Собственный HEARTBEAT Модуля некому принимать в синтетическом
     * режиме — намеренно no-op. */
}

void telemetry_source_send_arm_disarm(bool arm)
{
    mavlink_bridge_event_t ack = { .type = MAVLINK_EVENT_COMMAND_ACK };
    ack.data.command_ack.command = MAV_CMD_COMPONENT_ARM_DISARM;

    if (arm && s_prearm_fail) {
        ack.data.command_ack.result = MAV_RESULT_DENIED;
        push_event(&ack);
        push_statustext(MAV_SEVERITY_ERROR, "%s", s_prearm_fail_reason);
        return;
    }

    s_armed = arm;
    ack.data.command_ack.result = MAV_RESULT_ACCEPTED;
    push_event(&ack);
    publish_heartbeat_now();
}

void telemetry_source_send_set_mode(uint32_t custom_mode)
{
    s_custom_mode = custom_mode;

    mavlink_bridge_event_t ack = { .type = MAVLINK_EVENT_COMMAND_ACK };
    ack.data.command_ack.command = MAV_CMD_DO_SET_MODE;
    ack.data.command_ack.result = MAV_RESULT_ACCEPTED;
    push_event(&ack);
    publish_heartbeat_now();
}

void telemetry_source_send_param_set_float(const char *param_id, float value)
{
    if (strcmp(param_id, MODULE_PARAM_REQUEST_LIST) == 0) {
        if (value == 0) {
            return;
        }
        for (int i = 0; i < FAKE_ROUTE_COUNT; i++) {
            push_statustext(MAV_SEVERITY_INFO, "MLIST %d %s",
                             FAKE_ROUTES[i].num, FAKE_ROUTES[i].name);
        }
        push_statustext(MAV_SEVERITY_INFO, "MLIST END %d", FAKE_ROUTE_COUNT);
        return;
    }

    if (strcmp(param_id, MODULE_PARAM_SELECT_ROUTE) == 0) {
        if (value == 0) {
            return;
        }
        int num = (int)(value + 0.5f);

        if (s_armed) {
            push_statustext(MAV_SEVERITY_ERROR, "MLOAD ERR n=%d armed", num);
            return;
        }
        if (num < 1 || num > FAKE_ROUTE_COUNT) {
            push_statustext(MAV_SEVERITY_ERROR, "MLOAD ERR n=%d range", num);
            return;
        }
        if (s_route_too_far[num]) {
            push_statustext(MAV_SEVERITY_ERROR, "MLOAD ERR n=%d too_far d1=%d",
                             num, (int)(FAKE_ROUTES[num - 1].dist_m * 25));
            return;
        }

        const fake_route_t *r = &FAKE_ROUTES[num - 1];
        push_statustext(MAV_SEVERITY_INFO, "MLOAD OK n=%d wp=%d d1=%d len=%d",
                         num, r->wp, (int)r->dist_m, (int)r->len_m);
        return;
    }

    ESP_LOGW(TAG, "unhandled synthetic PARAM_SET %s=%.2f", param_id, value);
}

void telemetry_source_send_manual_control(int16_t x, int16_t y, int16_t z, int16_t r,
                                           uint16_t buttons)
{
    /* Платформы нет — в синтетическом режиме поток MANUAL_CONTROL
     * намеренно никуда не идёт, только проверяется, что Модуль его
     * формирует (это видно по счётчику вызовов в тестах этапа 9). */
}

void telemetry_synthetic_set_prearm_fail(bool fail, const char *reason_text)
{
    s_prearm_fail = fail;
    if (reason_text) {
        strncpy(s_prearm_fail_reason, reason_text, sizeof(s_prearm_fail_reason) - 1);
        s_prearm_fail_reason[sizeof(s_prearm_fail_reason) - 1] = '\0';
    }
}

void telemetry_synthetic_set_route_too_far(int route_num, bool too_far)
{
    if (route_num >= 1 && route_num <= FAKE_ROUTE_COUNT) {
        s_route_too_far[route_num] = too_far;
    }
}

void telemetry_synthetic_reset(void)
{
    s_armed = false;
    s_custom_mode = ROVER_MODE_HOLD;
    s_prearm_fail = false;
    memset(s_route_too_far, 0, sizeof(s_route_too_far));
    publish_heartbeat_now();
}

#endif /* MODULE_SYNTHETIC_MODE */
