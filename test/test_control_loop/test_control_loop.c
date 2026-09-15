/*
 * Тесты чистого ядра локального управления (control_loop_core.c).
 * Сборка:
 *   gcc -std=c11 -I ../../components/config/include \
 *       -I ../../components/control_loop/include \
 *       test_control_loop.c ../../components/control_loop/control_loop_core.c \
 *       -o test_control_loop && ./test_control_loop
 */

#include "../test_common/test_framework.h"
#include "control_loop.h"
#include "config.h"

/* FR-15/FR-17: нейтраль, пока джойстик не пройдёт через нейтраль хотя бы раз. */
static void test_initial_neutral_gate(void)
{
    TEST_CASE("initial_neutral_gate");
    control_loop_state_t st;
    control_loop_reset(&st);

    control_loop_joystick_input_t in = { .valid = true, .raw_x = 80, .raw_y = 0, .dead_man_held = true };
    control_loop_output_t out;
    control_loop_tick(&st, &in, &out);

    TEST_ASSERT(!out.movement_allowed);
    TEST_ASSERT(out.waiting_initial_neutral);
    TEST_ASSERT_EQ(out.out_x, 0);

    /* возврат в нейтраль подтверждает готовность; движение разрешается
     * со следующего тика (см. control_loop_core.c) */
    in.raw_x = 0;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT(!out.movement_allowed);

    in.raw_x = 50; /* теперь можно двигаться (мёртвая рука уже удержана) */
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT(out.movement_allowed);
}

/* FR-13/14: без мёртвой руки — нейтраль; отпускание — немедленно, без сглаживания. */
static void test_dead_man_gating(void)
{
    TEST_CASE("dead_man_gating");
    control_loop_state_t st;
    control_loop_reset(&st);
    control_loop_output_t out;

    /* пройти начальную нейтраль */
    control_loop_joystick_input_t in = { .valid = true, .raw_x = 0, .raw_y = 0, .dead_man_held = false };
    control_loop_tick(&st, &in, &out);

    /* без мёртвой руки движение запрещено, даже если джойстик отклонён */
    in.raw_x = 100;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT(!out.movement_allowed);
    TEST_ASSERT_EQ(out.out_x, 0);

    /* мёртвая рука нажата — движение разрешено, нарастает от нуля (slew) */
    in.dead_man_held = true;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT(out.movement_allowed);
    TEST_ASSERT(out.out_x > 0);
    TEST_ASSERT(out.out_x <= MODULE_MANUAL_CONTROL_SLEW_PER_TICK);

    /* отпускание — мгновенно 0, а не плавный спад */
    in.dead_man_held = false;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT_EQ(out.out_x, 0);
}

/* FR-18: показания внутри дедбенда дают нулевую команду. */
static void test_deadband(void)
{
    TEST_CASE("deadband");
    control_loop_state_t st;
    control_loop_reset(&st);
    control_loop_output_t out;

    control_loop_joystick_input_t in = { .valid = true, .raw_x = 0, .raw_y = 0, .dead_man_held = true };
    control_loop_tick(&st, &in, &out); /* начальная нейтраль пройдена */

    in.raw_x = MODULE_JOYSTICK_DEADBAND_COUNTS; /* на границе — всё ещё 0 */
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT_EQ(out.out_x, 0);

    in.raw_x = MODULE_JOYSTICK_DEADBAND_COUNTS + 1;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT(out.out_x >= 0); /* уже не обязательно 0, но не отрицательно */
}

/* FR-19: скорость нарастания команды ограничена конфигурацией. */
static void test_slew_limit(void)
{
    TEST_CASE("slew_limit");
    control_loop_state_t st;
    control_loop_reset(&st);
    control_loop_output_t out;

    control_loop_joystick_input_t in = { .valid = true, .raw_x = 0, .raw_y = 0, .dead_man_held = true };
    control_loop_tick(&st, &in, &out); /* нейтраль пройдена */

    in.raw_x = 127; /* максимум оси — резкий скачок */
    int16_t prev = 0;
    for (int i = 0; i < 5; i++) {
        control_loop_tick(&st, &in, &out);
        int16_t step = out.out_x - prev;
        TEST_ASSERT(step <= MODULE_MANUAL_CONTROL_SLEW_PER_TICK);
        prev = out.out_x;
    }
}

/* FR-37.1/NFR-8: невалидное показание -> немедленная нейтраль, без сглаживания. */
static void test_invalid_reading_forces_neutral(void)
{
    TEST_CASE("invalid_reading_forces_neutral");
    control_loop_state_t st;
    control_loop_reset(&st);
    control_loop_output_t out;

    control_loop_joystick_input_t in = { .valid = true, .raw_x = 0, .raw_y = 0, .dead_man_held = true };
    control_loop_tick(&st, &in, &out);
    in.raw_x = 127;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT(out.out_x > 0);

    in.valid = false;
    control_loop_tick(&st, &in, &out);
    TEST_ASSERT_EQ(out.out_x, 0);
    TEST_ASSERT(!out.movement_allowed);
}

int main(void)
{
    test_initial_neutral_gate();
    test_dead_man_gating();
    test_deadband();
    test_slew_limit();
    test_invalid_reading_forces_neutral();
    TEST_SUMMARY();
}
