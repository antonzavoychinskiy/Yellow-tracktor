/*
 * Тесты чистого ядра кнопки «Пуск» (auto_sequence_core.c). Сборка:
 *   gcc -std=c11 -I ../../components/config/include \
 *       -I ../../components/auto_sequence/include \
 *       -I ../../components/mavlink_bridge/include \
 *       test_auto_sequence.c ../../components/auto_sequence/auto_sequence_core.c \
 *       -o test_auto_sequence && ./test_auto_sequence
 */

#include "../test_common/test_framework.h"
#include "auto_sequence.h"
#include "config.h"
#include "ardurover_modes.h"

/* FR-40.1: маршрут не загружен -> «Пуск» игнорируется. */
static void test_blocked_ignores_start(void)
{
    TEST_CASE("blocked_ignores_start");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = false,
                                   .start_pressed = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_BLOCKED);
    TEST_ASSERT(!out.cmd_send_arm);
}

/* FR-40: маршрут загружен -> «Пуск» инициирует арм. */
static void test_ready_start_arms(void)
{
    TEST_CASE("ready_start_arms");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);
    TEST_ASSERT(out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_auto); /* FR-40.3: режим ещё не отправлен */
}

/* FR-40.3: SET_MODE AUTO только после подтверждения арминга по HEARTBEAT. */
static void test_mode_auto_gated_on_armed(void)
{
    TEST_CASE("mode_auto_gated_on_armed");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;
    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* -> ARMING */

    in.start_pressed = false; in.now_ms = 10;
    in.have_heartbeat = true; in.armed = false; /* ещё не подтверждено */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);
    TEST_ASSERT(!out.cmd_send_mode_auto);

    in.armed = true; in.now_ms = 20; /* подтверждено */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_SETTING_MODE);
    TEST_ASSERT(out.cmd_send_mode_auto);

    in.custom_mode = ROVER_MODE_AUTO; in.now_ms = 30;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_MOVING);
}

/* FR-40.2 + диаграмма 8А.3: отказ арминга -> ArmFailed, повтор доступен новым «Пуск». */
static void test_arm_failed_retry_via_new_press(void)
{
    TEST_CASE("arm_failed_retry_via_new_press");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;
    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* ARMING */

    in.start_pressed = false;
    in.now_ms = MODULE_ARM_CONFIRM_TIMEOUT_MS + 1; /* таймаут — армing не подтверждён */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARM_FAILED);

    /* без нового нажатия — без изменений (без автоповтора, FR-40.2) */
    in.now_ms += 100;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARM_FAILED);
    TEST_ASSERT(!out.cmd_send_arm);

    /* новое нажатие — новая попытка */
    in.start_pressed = true; in.now_ms += 10;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);
    TEST_ASSERT(out.cmd_send_arm);
}

/* Выход из AUTO и повторный вход — свежий цикл (BLOCKED, если маршрут не перезагружен). */
static void test_leaving_auto_resets(void)
{
    TEST_CASE("leaving_auto_resets");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;
    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* ARMING */

    in.active = false; in.now_ms = 10;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_READY);
}

int main(void)
{
    test_blocked_ignores_start();
    test_ready_start_arms();
    test_mode_auto_gated_on_armed();
    test_arm_failed_retry_via_new_press();
    test_leaving_auto_resets();
    TEST_SUMMARY();
}
