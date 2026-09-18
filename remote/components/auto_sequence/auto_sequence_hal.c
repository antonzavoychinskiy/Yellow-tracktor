#include "auto_sequence.h"
#include "config.h"
#include "buttons.h"
#include "telemetry_source.h"
#include "mission_ui.h"
#include "ardurover_modes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static auto_sequence_ctx_t s_ctx;
static SemaphoreHandle_t s_mutex;

void auto_sequence_hal_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    auto_sequence_init(&s_ctx);
}

void auto_sequence_hal_tick(bool active)
{
    auto_sequence_inputs_t in = { 0 };
    in.active = active;
    in.now_ms = esp_timer_get_time() / 1000;

    if (active) {
        in.route_loaded_this_cycle = mission_ui_hal_get_route_loaded_this_cycle();
        in.start_pressed = buttons_hal_start_pressed();
        in.start_held = buttons_hal_start_held(); /* читать после start_pressed — см. buttons.h */

        mavlink_telemetry_snapshot_t snap;
        telemetry_source_get_telemetry_snapshot(&snap);
        in.have_heartbeat = snap.have_heartbeat;
        in.armed = snap.heartbeat.armed;
        in.custom_mode = snap.heartbeat.custom_mode;
    }

    auto_sequence_outputs_t out;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    auto_sequence_tick(&s_ctx, &in, &out);
    xSemaphoreGive(s_mutex);

    if (out.cmd_send_arm) {
        telemetry_source_send_arm_disarm(true);
    }
    if (out.cmd_send_mode_auto) {
        telemetry_source_send_set_mode(ROVER_MODE_AUTO);
    }
}

auto_sequence_state_t auto_sequence_hal_get_state(void)
{
    auto_sequence_state_t s;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s = s_ctx.state;
    xSemaphoreGive(s_mutex);
    return s;
}

uint32_t auto_sequence_hal_get_confirm_hold_progress_permille(void)
{
    uint32_t p;
    int64_t now_ms = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    p = auto_sequence_confirm_hold_progress_permille(&s_ctx, now_ms);
    xSemaphoreGive(s_mutex);
    return p;
}

uint32_t auto_sequence_hal_get_confirm_countdown_seconds_left(void)
{
    uint32_t s;
    int64_t now_ms = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s = auto_sequence_confirm_countdown_seconds_left(&s_ctx, now_ms);
    xSemaphoreGive(s_mutex);
    return s;
}
