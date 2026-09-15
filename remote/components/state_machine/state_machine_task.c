#include "state_machine_task.h"
#include "config.h"
#include "keyswitch.h"
#include "telemetry_source.h"
#include "control_loop.h"
#include "ardurover_modes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "common/mavlink.h" /* MAV_CMD_COMPONENT_ARM_DISARM, MAV_RESULT_* */

/*
 * Проверено сборкой на ESP-IDF 6.0.1 (framework-espidf 4.60001.0):
 * esp_task_wdt_init(const esp_task_wdt_config_t*) — компонент esp_system.
 */

#define STATUSTEXT_BROADCAST_QUEUE_LEN 16
#define RT_TASK_STACK      4096
#define RT_TASK_PRIO        (tskIDLE_PRIORITY + 5) /* NFR-1: приоритет выше уровня интерфейса */
#define RT_TASK_CORE         0                       /* NFR-3 */
#define RT_TICK_PERIOD_MS   MODULE_MANUAL_CONTROL_PERIOD_MS /* стыкуется с 20 Гц control_loop, этап 5 */

static const char *TAG = "state_machine";

static sm_context_t s_ctx;
static SemaphoreHandle_t s_state_mutex;
static sm_state_t s_public_state;
static bool s_public_warn_mode_mismatch;
static bool s_public_warn_invalid_key;

static QueueHandle_t s_statustext_broadcast;

static bool s_awaiting_arm_ack = false;
static bool s_awaiting_disarm_ack = false;

static void apply_outputs(const sm_outputs_t *out)
{
    /* Порядок важен: нейтраль — раньше остальных команд (FR-10.1). */
    if (out->cmd_send_neutral_once) {
        telemetry_source_send_manual_control(0, 0, 0, 0, 0);
    }
    if (out->cmd_send_hold) {
        telemetry_source_send_set_mode(ROVER_MODE_HOLD);
    }
    if (out->cmd_send_disarm) {
        telemetry_source_send_arm_disarm(false);
        s_awaiting_disarm_ack = true;
    }
    if (out->cmd_send_arm) {
        telemetry_source_send_arm_disarm(true);
        s_awaiting_arm_ack = true;
    }
    if (out->cmd_send_mode_manual) {
        telemetry_source_send_set_mode(ROVER_MODE_MANUAL);
    }
}

static void update_public_state(const sm_outputs_t *out)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_public_state = s_ctx.state;
    s_public_warn_mode_mismatch = out->warn_mode_mismatch;
    s_public_warn_invalid_key = out->warn_invalid_key;
    xSemaphoreGive(s_state_mutex);
}

static void drain_events_and_build_inputs(sm_inputs_t *in)
{
    mavlink_bridge_event_t ev;
    while (telemetry_source_poll_event(&ev)) {
        switch (ev.type) {
        case MAVLINK_EVENT_COMMAND_ACK:
            if (ev.data.command_ack.command == MAV_CMD_COMPONENT_ARM_DISARM) {
                bool accepted = (ev.data.command_ack.result == MAV_RESULT_ACCEPTED);
                if (s_awaiting_arm_ack) {
                    in->arm_ack_received = true;
                    in->arm_ack_accepted = accepted;
                    s_awaiting_arm_ack = false;
                } else if (s_awaiting_disarm_ack) {
                    in->disarm_ack_received = true;
                    in->disarm_ack_accepted = accepted;
                    s_awaiting_disarm_ack = false;
                }
            }
            break;
        case MAVLINK_EVENT_STATUSTEXT:
            /* Ретрансляция для UI/mission_ui — см. комментарий в
             * state_machine_task.h про единственного потребителя
             * общей очереди событий. */
            if (xQueueSend(s_statustext_broadcast, &ev, 0) != pdTRUE) {
                ESP_LOGW(TAG, "statustext broadcast queue full, dropping");
            }
            break;
        case MAVLINK_EVENT_HEARTBEAT:
        default:
            break; /* актуальное состояние берём из снимка телеметрии */
        }
    }
}

static void rt_task(void *arg)
{
    esp_task_wdt_add(NULL);

    key_position_t initial_key = keyswitch_hal_read();
    sm_outputs_t out;
    int64_t now_ms = esp_timer_get_time() / 1000;
    sm_init(&s_ctx, initial_key, now_ms, &out);
    apply_outputs(&out);
    update_public_state(&out);

    int64_t last_hb_sent_ms = 0;

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        esp_task_wdt_reset();

        sm_inputs_t in = { 0 };
        in.key = keyswitch_hal_read();
        in.now_ms = esp_timer_get_time() / 1000;

        /* Собственный HEARTBEAT Модуля, 1 Гц. Обязателен: Модуль
         * представляется наземной станцией (MAV_TYPE_GCS, sysid =
         * MAV_GCS_SYSID, см. FR-4 и раздел 3.4) — без периодического
         * HEARTBEAT ArduPilot не считает канал активной GCS. */
        if (in.now_ms - last_hb_sent_ms >= MODULE_HEARTBEAT_PERIOD_MS) {
            telemetry_source_send_heartbeat();
            last_hb_sent_ms = in.now_ms;
        }

        mavlink_telemetry_snapshot_t snap;
        telemetry_source_get_telemetry_snapshot(&snap);
        in.have_heartbeat = snap.have_heartbeat;
        in.armed = snap.heartbeat.armed;
        in.custom_mode = snap.heartbeat.custom_mode;
        in.have_groundspeed = snap.have_vfr_hud;
        in.groundspeed_mps = snap.groundspeed_mps;

        drain_events_and_build_inputs(&in);

        sm_outputs_t out2 = { 0 };
        sm_tick(&s_ctx, &in, &out2);
        apply_outputs(&out2);
        update_public_state(&out2);

        /* Локальное ручное управление — тот же тик, тот же период
         * (NFR-4: минимальный джиттер 20 Гц потока MANUAL_CONTROL). */
        control_loop_hal_tick(s_ctx.state == SM_STATE_LOCAL_ACTIVE);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(RT_TICK_PERIOD_MS));
    }
}

esp_err_t state_machine_start(void)
{
    s_state_mutex = xSemaphoreCreateMutex();
    s_statustext_broadcast = xQueueCreate(STATUSTEXT_BROADCAST_QUEUE_LEN, sizeof(mavlink_bridge_event_t));
    if (s_state_mutex == NULL || s_statustext_broadcast == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = MODULE_WATCHDOG_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    /* При CONFIG_ESP_TASK_WDT_EN=y (см. sdkconfig.defaults) TWDT уже
     * поднят системой до app_main, поэтому сначала пробуем применить
     * свои параметры к работающему таймеру. Порядок важен именно так:
     * esp_task_wdt_init() на уже инициализированном TWDT сам печатает
     * строку уровня ERROR ("TWDT already initialized") до возврата
     * кода — она попадала бы в загрузочный лог и сбивала с толку при
     * приёмке по docs/flashing_and_verification.md. */
    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    if (err == ESP_ERR_INVALID_STATE) {
        /* Автоматическая инициализация отключена в конфигурации —
         * поднимаем TWDT сами (FR-36). */
        err = esp_task_wdt_init(&wdt_config);
    }
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(rt_task, "sm_rt", RT_TASK_STACK, NULL,
                                             RT_TASK_PRIO, NULL, RT_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

sm_state_t state_machine_get_state(void)
{
    sm_state_t s;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s = s_public_state;
    xSemaphoreGive(s_state_mutex);
    return s;
}

bool state_machine_get_warn_mode_mismatch(void)
{
    bool w;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    w = s_public_warn_mode_mismatch;
    xSemaphoreGive(s_state_mutex);
    return w;
}

bool state_machine_get_warn_invalid_key(void)
{
    bool w;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    w = s_public_warn_invalid_key;
    xSemaphoreGive(s_state_mutex);
    return w;
}

bool state_machine_poll_statustext(mavlink_statustext_info_t *out)
{
    mavlink_bridge_event_t ev;
    if (xQueueReceive(s_statustext_broadcast, &ev, 0) == pdTRUE) {
        *out = ev.data.statustext;
        return true;
    }
    return false;
}
