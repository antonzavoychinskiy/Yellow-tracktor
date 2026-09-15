#include "control_loop.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>

static int16_t apply_deadband(int16_t v, int16_t deadband)
{
    if (v > deadband) {
        return v - deadband;
    }
    if (v < -deadband) {
        return v + deadband;
    }
    return 0;
}

static int16_t clamp16(int32_t v, int16_t lo, int16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return (int16_t)v;
}

/* Дедбенд + масштабирование до -1000..1000 с непрерывностью на
 * границе зоны нечувствительности + программный лимит скорости
 * (FR-16, FR-18). */
static int16_t scale_axis(int16_t raw)
{
    int16_t deadbanded = apply_deadband(raw, MODULE_JOYSTICK_DEADBAND_COUNTS);
    int16_t full_scale = MODULE_JOYSTICK_RAW_FULL_SCALE - MODULE_JOYSTICK_DEADBAND_COUNTS;
    if (full_scale <= 0) {
        full_scale = 1;
    }

    int32_t scaled = ((int32_t)deadbanded * 1000) / full_scale;
    scaled = (scaled * MODULE_LOCAL_SPEED_LIMIT_PERCENT) / 100;
    return clamp16(scaled, -1000, 1000);
}

static int16_t slew_limit(int16_t last, int16_t target, int16_t max_step)
{
    int16_t diff = (int16_t)(target - last);
    if (diff > max_step) {
        diff = max_step;
    } else if (diff < -max_step) {
        diff = -max_step;
    }
    return (int16_t)(last + diff);
}

void control_loop_reset(control_loop_state_t *st)
{
    memset(st, 0, sizeof(*st));
}

void control_loop_tick(control_loop_state_t *st, const control_loop_joystick_input_t *in,
                        control_loop_output_t *out)
{
    memset(out, 0, sizeof(*out));

    if (!in->valid) {
        /* FR-37.1 / NFR-8: нет достоверного показания в этом тике —
         * безопасное значение — нейтраль, без сглаживания. */
        st->last_sent_x = 0;
        st->last_sent_y = 0;
        out->waiting_initial_neutral = !st->initial_neutral_confirmed;
        return;
    }

    int16_t deadbanded_x = apply_deadband(in->raw_x, MODULE_JOYSTICK_DEADBAND_COUNTS);
    int16_t deadbanded_y = apply_deadband(in->raw_y, MODULE_JOYSTICK_DEADBAND_COUNTS);

    if (!st->initial_neutral_confirmed) {
        st->have_first_reading = true; /* FR-15 */

        bool within_neutral = (deadbanded_x == 0 && deadbanded_y == 0);
        if (within_neutral) {
            st->initial_neutral_confirmed = true; /* разрешение — со следующего тика */
        }

        st->last_sent_x = 0;
        st->last_sent_y = 0;
        out->waiting_initial_neutral = !st->initial_neutral_confirmed;
        out->dead_man_engaged = in->dead_man_held; /* информативно */
        return;
    }

    if (!in->dead_man_held) {
        /* FR-14: немедленно, без ограничения скорости нарастания. */
        st->last_sent_x = 0;
        st->last_sent_y = 0;
        return;
    }

    int16_t target_x = scale_axis(in->raw_x);
    int16_t target_y = scale_axis(in->raw_y);

    st->last_sent_x = slew_limit(st->last_sent_x, target_x, MODULE_MANUAL_CONTROL_SLEW_PER_TICK);
    st->last_sent_y = slew_limit(st->last_sent_y, target_y, MODULE_MANUAL_CONTROL_SLEW_PER_TICK);

    out->out_x = st->last_sent_x;
    out->out_y = st->last_sent_y;
    out->movement_allowed = true;
    out->dead_man_engaged = true;
}
