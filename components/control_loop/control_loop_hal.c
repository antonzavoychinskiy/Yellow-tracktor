#include "control_loop.h"
#include "config.h"
#include "nunchuk.h"
#include "telemetry_source.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static control_loop_state_t s_state;
static bool s_was_active_last_tick = false;

static SemaphoreHandle_t s_status_mutex;
static bool s_pub_dead_man_engaged;
static bool s_pub_movement_allowed;
static bool s_pub_waiting_initial_neutral;

void control_loop_hal_init(void)
{
    s_status_mutex = xSemaphoreCreateMutex();
}

static void publish_status(const control_loop_output_t *out)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_pub_dead_man_engaged = out->dead_man_engaged;
    s_pub_movement_allowed = out->movement_allowed;
    s_pub_waiting_initial_neutral = out->waiting_initial_neutral;
    xSemaphoreGive(s_status_mutex);
}

void control_loop_hal_tick(bool active)
{
    if (active && !s_was_active_last_tick) {
        control_loop_reset(&s_state); /* FR-15: новый вход в LOCAL_ACTIVE */
    }
    s_was_active_last_tick = active;

    if (!active) {
        /* Нейтраль на выходе из LOCAL уже обеспечена state_machine
         * (cmd_send_neutral_once) — здесь просто ничего не делаем. */
        return;
    }

    nunchuk_sample_t sample;
    esp_err_t err = nunchuk_hal_read(&sample);

    control_loop_joystick_input_t in = { 0 };
    in.valid = (err == ESP_OK);
    if (in.valid) {
        in.raw_x = sample.x;
        in.raw_y = sample.y;
        in.dead_man_held = sample.btn_z;
    }

    control_loop_output_t out;
    control_loop_tick(&s_state, &in, &out);

    telemetry_source_send_manual_control(out.out_x, out.out_y, 0, 0, 0);
    publish_status(&out);
}

bool control_loop_hal_get_dead_man_engaged(void)
{
    bool v;
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    v = s_pub_dead_man_engaged;
    xSemaphoreGive(s_status_mutex);
    return v;
}

bool control_loop_hal_get_movement_allowed(void)
{
    bool v;
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    v = s_pub_movement_allowed;
    xSemaphoreGive(s_status_mutex);
    return v;
}

bool control_loop_hal_get_waiting_initial_neutral(void)
{
    bool v;
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    v = s_pub_waiting_initial_neutral;
    xSemaphoreGive(s_status_mutex);
    return v;
}
