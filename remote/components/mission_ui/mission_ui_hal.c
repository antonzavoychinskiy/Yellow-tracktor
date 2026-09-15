#include "mission_ui.h"
#include "encoder.h"
#include "buttons.h"
#include "telemetry_source.h"
#include "state_machine_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

/*
 * Однозадачная модель: mission_ui_hal_tick() и геттеры ниже
 * вызываются из одной и той же UI-задачи (см. ui_task, этап 6/8) —
 * мьютекс защищает не от параллелизма нескольких писателей, а просто
 * даёт согласованный снимок для геттеров, если когда-нибудь появится
 * второй читатель.
 */

static mission_ui_ctx_t s_ctx;
static SemaphoreHandle_t s_mutex;

static void send_outputs(const mission_ui_outputs_t *out)
{
    if (out->send_request_list) {
        telemetry_source_send_param_set_float(MODULE_PARAM_REQUEST_LIST, 1.0f);
    }
    if (out->send_select_route) {
        telemetry_source_send_param_set_float(MODULE_PARAM_SELECT_ROUTE,
                                               (float)out->selected_route_num);
    }
}

void mission_ui_hal_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    mission_ui_init(&s_ctx);
}

void mission_ui_hal_tick(bool in_off)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    mission_ui_outputs_t out;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    mavlink_statustext_info_t st;
    while (state_machine_poll_statustext(&st)) {
        mission_ui_feed_statustext(&s_ctx, st.text, now_ms, &out);
        send_outputs(&out);
    }

    mission_ui_tick_inputs_t in = { 0 };
    in.in_off = in_off;
    in.encoder_delta = encoder_hal_take_delta();
    encoder_btn_event_t ev = encoder_hal_button_update();
    in.encoder_short_click = (ev == ENC_BTN_EVENT_SHORT_CLICK);
    in.encoder_long_fired = (ev == ENC_BTN_EVENT_LONG_FIRED);
    in.back_pressed = buttons_hal_back_pressed();
    in.now_ms = now_ms;

    mission_ui_tick(&s_ctx, &in, &out);
    send_outputs(&out);

    xSemaphoreGive(s_mutex);
}

mission_ui_state_t mission_ui_hal_get_state(void)
{
    mission_ui_state_t s;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s = s_ctx.state;
    xSemaphoreGive(s_mutex);
    return s;
}

int mission_ui_hal_get_route_count(void)
{
    int n;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    n = s_ctx.route_count;
    xSemaphoreGive(s_mutex);
    return n;
}

bool mission_ui_hal_get_route(int index, mission_ui_route_t *out)
{
    bool ok = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (index >= 0 && index < s_ctx.route_count) {
        *out = s_ctx.routes[index];
        ok = true;
    }
    xSemaphoreGive(s_mutex);
    return ok;
}

int mission_ui_hal_get_cursor(void)
{
    int c;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    c = s_ctx.cursor;
    xSemaphoreGive(s_mutex);
    return c;
}

int mission_ui_hal_get_card_route_num(void)
{
    int n;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    n = s_ctx.card_route_num;
    xSemaphoreGive(s_mutex);
    return n;
}

uint32_t mission_ui_hal_get_hold_progress_permille(void)
{
    return encoder_hal_hold_progress_permille(); /* FR-23.1 */
}

bool mission_ui_hal_get_load_result(mission_ui_load_result_t *out)
{
    bool have;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    have = s_ctx.last_result.have_load_result;
    if (have) {
        *out = s_ctx.last_result;
    }
    xSemaphoreGive(s_mutex);
    return have;
}

bool mission_ui_hal_get_route_loaded_this_cycle(void)
{
    bool v;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    v = s_ctx.route_loaded_this_cycle;
    xSemaphoreGive(s_mutex);
    return v;
}
