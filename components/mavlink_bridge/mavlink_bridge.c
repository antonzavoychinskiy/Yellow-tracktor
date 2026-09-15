#include "mavlink_bridge.h"
#include "config.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

/* -Waddress-of-packed-member из этого заголовка (вендоренный код,
 * mavlink/VENDORED.md) подавлен глобально в platformio.ini — см.
 * комментарий там. */
#include "common/mavlink.h"

static const char *TAG = "mavlink_bridge";

#define MAVLINK_CHAN            MAVLINK_COMM_0
#define UART_RX_BUF_SIZE        1024
#define UART_READ_CHUNK         64
#define EVENT_QUEUE_LEN         16
#define RX_TASK_STACK           4096
#define RX_TASK_PRIO            (tskIDLE_PRIORITY + 3)

static QueueHandle_t s_event_queue;
static SemaphoreHandle_t s_telemetry_mutex;
static mavlink_telemetry_snapshot_t s_telemetry;
static volatile int64_t s_last_heartbeat_time_us = -1;

static void push_event(const mavlink_bridge_event_t *ev)
{
    if (xQueueSend(s_event_queue, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event type=%d", ev->type);
    }
}

static void handle_heartbeat(const mavlink_message_t *msg)
{
    mavlink_heartbeat_t hb;
    mavlink_msg_heartbeat_decode(msg, &hb);

    mavlink_bridge_event_t ev = { .type = MAVLINK_EVENT_HEARTBEAT };
    ev.data.heartbeat.base_mode = hb.base_mode;
    ev.data.heartbeat.custom_mode = hb.custom_mode;
    ev.data.heartbeat.system_status = hb.system_status;
    ev.data.heartbeat.armed = (hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;

    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    s_telemetry.heartbeat = ev.data.heartbeat;
    s_telemetry.have_heartbeat = true;
    xSemaphoreGive(s_telemetry_mutex);
    s_last_heartbeat_time_us = esp_timer_get_time();

    push_event(&ev);
}

static void handle_vfr_hud(const mavlink_message_t *msg)
{
    mavlink_vfr_hud_t vfr;
    mavlink_msg_vfr_hud_decode(msg, &vfr);

    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    s_telemetry.groundspeed_mps = vfr.groundspeed;
    s_telemetry.have_vfr_hud = true;
    xSemaphoreGive(s_telemetry_mutex);
}

static void handle_sys_status(const mavlink_message_t *msg)
{
    mavlink_sys_status_t sys;
    mavlink_msg_sys_status_decode(msg, &sys);

    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    s_telemetry.battery_voltage_v = sys.voltage_battery / 1000.0f;
    s_telemetry.battery_current_a = sys.current_battery / 100.0f;
    s_telemetry.have_sys_status = true;
    xSemaphoreGive(s_telemetry_mutex);
}

static void handle_gps_raw_int(const mavlink_message_t *msg)
{
    mavlink_gps_raw_int_t gps;
    mavlink_msg_gps_raw_int_decode(msg, &gps);

    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    s_telemetry.gps_fix_type = gps.fix_type;
    s_telemetry.gps_satellites_visible = gps.satellites_visible;
    s_telemetry.have_gps = true;
    xSemaphoreGive(s_telemetry_mutex);
}

static void handle_mission_current(const mavlink_message_t *msg)
{
    mavlink_mission_current_t mc;
    mavlink_msg_mission_current_decode(msg, &mc);

    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    s_telemetry.mission_current_seq = mc.seq;
    s_telemetry.have_mission_current = true;
    xSemaphoreGive(s_telemetry_mutex);
}

static void handle_command_ack(const mavlink_message_t *msg)
{
    mavlink_command_ack_t ack;
    mavlink_msg_command_ack_decode(msg, &ack);

    mavlink_bridge_event_t ev = { .type = MAVLINK_EVENT_COMMAND_ACK };
    ev.data.command_ack.command = ack.command;
    ev.data.command_ack.result = ack.result;
    push_event(&ev);
}

static void handle_statustext(const mavlink_message_t *msg)
{
    mavlink_bridge_event_t ev = { .type = MAVLINK_EVENT_STATUSTEXT };
    memset(ev.data.statustext.text, 0, sizeof(ev.data.statustext.text));
    ev.data.statustext.severity = mavlink_msg_statustext_get_severity(msg);
    /* text[50] в протоколе не гарантированно завершён нулём — буфер
     * события на 1 байт больше и заранее обнулён. */
    mavlink_msg_statustext_get_text(msg, ev.data.statustext.text);
    push_event(&ev);
}

static void rx_task(void *arg)
{
    uint8_t chunk[UART_READ_CHUNK];
    mavlink_message_t msg;
    mavlink_status_t status;

    /* Диагностика физического уровня связи. Раз в 5 с в лог уходит
     * сводка: сколько сырых байт пришло по UART, сколько собралось
     * корректных MAVLink-пакетов, сколько было ошибок разбора и первый
     * увиденный байт. Это разделяет три разных отказа, которые снаружи
     * выглядят одинаково ("СВЯЗЬ ПОТЕРЯНА"):
     *   bytes=0            — от Pixhawk не приходит ничего (провода,
     *                        порт, SERIAL2_PROTOCOL, управление потоком);
     *   bytes>0, msgs=0    — байты идут, но не разбираются (скорость
     *                        не 115200, либо помеха/инверсия);
     *   msgs>0             — линия исправна, разбираться дальше на
     *                        уровне логики.
     * Оставлено в боевой сборке намеренно: это единственный способ
     * диагностировать линию на объекте, где нет осциллографа. */
    uint32_t stat_bytes = 0;
    uint32_t stat_msgs = 0;
    uint32_t stat_parse_err = 0;
    int first_byte = -1;
    int64_t last_report_ms = 0;

    while (1) {
        int len = uart_read_bytes(MODULE_UART_PORT_NUM, chunk, sizeof(chunk), pdMS_TO_TICKS(20));

        if (len > 0) {
            stat_bytes += (uint32_t)len;
            if (first_byte < 0) {
                first_byte = chunk[0];
            }
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_report_ms >= 5000) {
            last_report_ms = now_ms;
            ESP_LOGI(TAG, "UART RX: bytes=%u msgs=%u parse_err=%u first_byte=0x%02X",
                     (unsigned)stat_bytes, (unsigned)stat_msgs,
                     (unsigned)stat_parse_err,
                     first_byte < 0 ? 0 : (unsigned)first_byte);
        }

        for (int i = 0; i < len; i++) {
            if (mavlink_parse_char(MAVLINK_CHAN, chunk[i], &msg, &status)) {
                stat_msgs++;
                switch (msg.msgid) {
                case MAVLINK_MSG_ID_HEARTBEAT:
                    handle_heartbeat(&msg);
                    break;
                case MAVLINK_MSG_ID_COMMAND_ACK:
                    handle_command_ack(&msg);
                    break;
                case MAVLINK_MSG_ID_STATUSTEXT:
                    handle_statustext(&msg);
                    break;
                case MAVLINK_MSG_ID_VFR_HUD:
                    handle_vfr_hud(&msg);
                    break;
                case MAVLINK_MSG_ID_SYS_STATUS:
                    handle_sys_status(&msg);
                    break;
                case MAVLINK_MSG_ID_GPS_RAW_INT:
                    handle_gps_raw_int(&msg);
                    break;
                case MAVLINK_MSG_ID_MISSION_CURRENT:
                    handle_mission_current(&msg);
                    break;
                default:
                    break; /* остальные сообщения диалекта не используются */
                }
            } else if (status.parse_error) {
                stat_parse_err = status.parse_error;
            }
        }
    }
}

esp_err_t mavlink_bridge_init(void)
{
    s_event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(mavlink_bridge_event_t));
    if (s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_telemetry_mutex = xSemaphoreCreateMutex();
    if (s_telemetry_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_telemetry, 0, sizeof(s_telemetry));

    uart_config_t uart_config = {
        .baud_rate = MODULE_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Порядок вызовов важен: сначала параметры, ЗАТЕМ переназначение
     * выводов, и только после этого установка драйвера.
     *
     * Причина: до uart_set_pin() UART1 сидит на своих выводах по
     * умолчанию — GPIO9 (RX) и GPIO10 (TX). На модулях ESP32-WROOM
     * выводы GPIO6..11 заняты микросхемой флеш-памяти, и включение
     * GPIO10 на выход, пусть даже на доли миллисекунды, направляет
     * сигнал прямо в линию флеша. Обратный порядок (установка драйвера
     * до set_pin) встречается в примерах ESP-IDF, но там UART обычно
     * остаётся на выводах по умолчанию, где такого конфликта нет. */
    esp_err_t err = uart_param_config(MODULE_UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(MODULE_UART_PORT_NUM, MODULE_UART_TX_GPIO, MODULE_UART_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_driver_install(MODULE_UART_PORT_NUM, UART_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreate(rx_task, "mavlink_rx", RX_TASK_STACK, NULL, RX_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "MAVLink bridge started (sysid=%d, target sysid=%d)",
             MODULE_MAVLINK_SYSID, MODULE_MAVLINK_TARGET_SYSID);
    return ESP_OK;
}

bool mavlink_bridge_poll_event(mavlink_bridge_event_t *out_event)
{
    return xQueueReceive(s_event_queue, out_event, 0) == pdTRUE;
}

bool mavlink_bridge_get_last_heartbeat(mavlink_heartbeat_info_t *out)
{
    bool have;
    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    have = s_telemetry.have_heartbeat;
    if (have) {
        *out = s_telemetry.heartbeat;
    }
    xSemaphoreGive(s_telemetry_mutex);
    return have;
}

void mavlink_bridge_get_telemetry_snapshot(mavlink_telemetry_snapshot_t *out)
{
    xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY);
    *out = s_telemetry;
    xSemaphoreGive(s_telemetry_mutex);
}

int64_t mavlink_bridge_last_heartbeat_age_ms(void)
{
    if (s_last_heartbeat_time_us < 0) {
        return -1;
    }
    return (esp_timer_get_time() - s_last_heartbeat_time_us) / 1000;
}

static void send_message(mavlink_message_t *msg)
{
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len = mavlink_msg_to_send_buffer(buf, msg);
    uart_write_bytes(MODULE_UART_PORT_NUM, (const char *)buf, len);
}

void mavlink_bridge_send_heartbeat(void)
{
    mavlink_message_t msg;
    /* Модуль представляется GCS-подобным узлом (MAV_TYPE_GCS),
     * autopilot=MAV_AUTOPILOT_INVALID — по конвенции для не-автопилотных
     * компонентов. */
    mavlink_msg_heartbeat_pack(MODULE_MAVLINK_SYSID, MODULE_MAVLINK_COMPID, &msg,
                                MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
    send_message(&msg);
}

void mavlink_bridge_send_arm_disarm(bool arm)
{
    mavlink_message_t msg;
    /* FR-7.1: param2 всегда 0 — принудительный (форсированный) арминг
     * запрещён требованиями. */
    mavlink_msg_command_long_pack(MODULE_MAVLINK_SYSID, MODULE_MAVLINK_COMPID, &msg,
                                   MODULE_MAVLINK_TARGET_SYSID, MODULE_MAVLINK_TARGET_COMPID,
                                   MAV_CMD_COMPONENT_ARM_DISARM, 0,
                                   arm ? 1.0f : 0.0f, 0.0f, 0, 0, 0, 0, 0);
    send_message(&msg);
}

void mavlink_bridge_send_set_mode(uint32_t custom_mode)
{
    mavlink_message_t msg;
    float base_mode = (float)MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
    mavlink_msg_command_long_pack(MODULE_MAVLINK_SYSID, MODULE_MAVLINK_COMPID, &msg,
                                   MODULE_MAVLINK_TARGET_SYSID, MODULE_MAVLINK_TARGET_COMPID,
                                   MAV_CMD_DO_SET_MODE, 0,
                                   base_mode, (float)custom_mode, 0, 0, 0, 0, 0);
    send_message(&msg);
}

void mavlink_bridge_send_param_set_float(const char *param_id, float value)
{
    mavlink_message_t msg;
    mavlink_msg_param_set_pack(MODULE_MAVLINK_SYSID, MODULE_MAVLINK_COMPID, &msg,
                                MODULE_MAVLINK_TARGET_SYSID, MODULE_MAVLINK_TARGET_COMPID,
                                param_id, value, MAV_PARAM_TYPE_REAL32);
    send_message(&msg);
}

void mavlink_bridge_send_manual_control(int16_t x, int16_t y, int16_t z, int16_t r,
                                         uint16_t buttons)
{
    mavlink_message_t msg;
    mavlink_msg_manual_control_pack(MODULE_MAVLINK_SYSID, MODULE_MAVLINK_COMPID, &msg,
                                     (uint8_t)MODULE_MAVLINK_TARGET_SYSID,
                                     x, y, z, r, buttons,
                                     /* buttons2, enabled_extensions, s, t, aux1..aux6 —
                                      * расширения не используются */
                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    send_message(&msg);
}
