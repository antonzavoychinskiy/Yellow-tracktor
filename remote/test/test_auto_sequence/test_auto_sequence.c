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

/* FR-40.1: маршрут не загружен -> «Пуск» игнорируется, удержание не запускается. */
static void test_blocked_ignores_start(void)
{
    TEST_CASE("blocked_ignores_start");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = false,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_BLOCKED);
    TEST_ASSERT(!out.cmd_send_arm);
}

/* FR-40.5: нажатие при загруженном маршруте запускает фазу удержания,
 * а не арм немедленно. */
static void test_ready_press_enters_confirm_hold(void)
{
    TEST_CASE("ready_press_enters_confirm_hold");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_HOLD);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT_EQ(auto_sequence_confirm_hold_progress_permille(&ctx, 0), 0u);
}

/* FR-40.5: полоса растёт линейно и отпускание до заполнения отменяет
 * попытку без арма — новая попытка требует нового нажатия. */
static void test_confirm_hold_release_early_cancels(void)
{
    TEST_CASE("confirm_hold_release_early_cancels");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_HOLD, entered at t=0 */

    in.start_pressed = false; in.now_ms = MODULE_START_CONFIRM_HOLD_MS / 2;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_HOLD);
    uint32_t mid_progress = auto_sequence_confirm_hold_progress_permille(&ctx, in.now_ms);
    TEST_ASSERT(mid_progress > 400 && mid_progress < 600); /* ~500‰ на середине */

    in.start_held = false; /* отпустили раньше времени */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_READY);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT_EQ(auto_sequence_confirm_hold_progress_permille(&ctx, in.now_ms), 0u);

    /* Без нового нажатия — остаёмся в READY, повторного арма нет. */
    in.now_ms += MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_READY);
}

/* FR-40.5/40.6: полное удержание -> отсчёт -> по истечении отсчёта, и
 * только тогда, отправляется арм. */
static void test_full_confirm_gesture_arms_after_countdown(void)
{
    TEST_CASE("full_confirm_gesture_arms_after_countdown");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_HOLD at t=0 */

    in.start_pressed = false; in.now_ms = MODULE_START_CONFIRM_HOLD_MS - 1;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_HOLD); /* ещё не набрали полный порог */

    in.now_ms = MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_COUNTDOWN); /* полоса заполнена */
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT_EQ(auto_sequence_confirm_countdown_seconds_left(&ctx, in.now_ms), 5u);

    in.now_ms += MODULE_START_CONFIRM_COUNTDOWN_MS - 1;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_COUNTDOWN); /* ещё не истёк */
    TEST_ASSERT(!out.cmd_send_arm);

    in.now_ms += 1;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);
    TEST_ASSERT(out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_auto); /* FR-40.3: режим ещё не отправлен */
}

/* FR-40.6: отсчёт необратим — отпускание «Пуск» во время отсчёта не
 * отменяет его. */
static void test_countdown_ignores_release(void)
{
    TEST_CASE("countdown_ignores_release");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;

    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out);
    in.start_pressed = false; in.now_ms = MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_COUNTDOWN */
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_COUNTDOWN);

    in.start_held = false; /* отпустили сразу по входу в отсчёт */
    in.now_ms += MODULE_START_CONFIRM_COUNTDOWN_MS;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING); /* всё равно арм — отсчёт необратим */
    TEST_ASSERT(out.cmd_send_arm);
}

/* FR-40.3: SET_MODE AUTO только после подтверждения арминга по HEARTBEAT. */
static void test_mode_auto_gated_on_armed(void)
{
    TEST_CASE("mode_auto_gated_on_armed");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;
    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_HOLD */
    in.start_pressed = false;
    in.now_ms = MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_COUNTDOWN */
    in.now_ms += MODULE_START_CONFIRM_COUNTDOWN_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> ARMING */
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);

    in.start_held = false; in.now_ms += 10;
    in.have_heartbeat = true; in.armed = false; /* ещё не подтверждено */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);
    TEST_ASSERT(!out.cmd_send_mode_auto);

    in.armed = true; in.now_ms += 10; /* подтверждено */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_SETTING_MODE);
    TEST_ASSERT(out.cmd_send_mode_auto);

    in.custom_mode = ROVER_MODE_AUTO; in.now_ms += 10;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_MOVING);
}

/* FR-40.2 + диаграмма 8А.3: отказ арминга -> ArmFailed, повтор доступен
 * новым полным жестом подтверждения (FR-40.5), не просто нажатием. */
static void test_arm_failed_retry_via_new_confirm_gesture(void)
{
    TEST_CASE("arm_failed_retry_via_new_confirm_gesture");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;
    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_HOLD */
    in.start_pressed = false;
    in.now_ms = MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_COUNTDOWN */
    in.now_ms += MODULE_START_CONFIRM_COUNTDOWN_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> ARMING */
    in.start_held = false;

    in.now_ms += MODULE_ARM_CONFIRM_TIMEOUT_MS + 1; /* таймаут — арминг не подтверждён */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARM_FAILED);

    /* без нового нажатия — без изменений (без автоповтора, FR-40.2) */
    in.now_ms += 100;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARM_FAILED);
    TEST_ASSERT(!out.cmd_send_arm);

    /* новая попытка — снова полный жест, не одно нажатие */
    in.start_pressed = true; in.start_held = true; in.now_ms += 10;
    int64_t retry_started_ms = in.now_ms;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_HOLD);
    TEST_ASSERT(!out.cmd_send_arm);

    in.start_pressed = false; in.now_ms = retry_started_ms + MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_COUNTDOWN */
    in.now_ms += MODULE_START_CONFIRM_COUNTDOWN_MS;
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_ARMING);
    TEST_ASSERT(out.cmd_send_arm);
}

/* Выход из AUTO во время отсчёта — единственный способ прервать
 * необратимую фазу (FR-40.6); повторный вход — свежий цикл. */
static void test_leaving_auto_during_countdown_resets(void)
{
    TEST_CASE("leaving_auto_during_countdown_resets");
    auto_sequence_ctx_t ctx;
    auto_sequence_init(&ctx);
    auto_sequence_outputs_t out;
    auto_sequence_inputs_t in = { .active = true, .route_loaded_this_cycle = true,
                                   .start_pressed = true, .start_held = true, .now_ms = 0 };
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_HOLD */
    in.start_pressed = false;
    in.now_ms = MODULE_START_CONFIRM_HOLD_MS;
    auto_sequence_tick(&ctx, &in, &out); /* -> CONFIRM_COUNTDOWN */
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_CONFIRM_COUNTDOWN);

    in.active = false; in.now_ms += 10; /* ключ увели из AUTO */
    auto_sequence_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, AUTO_SEQ_READY);
    TEST_ASSERT(!out.cmd_send_arm);
}

int main(void)
{
    test_blocked_ignores_start();
    test_ready_press_enters_confirm_hold();
    test_confirm_hold_release_early_cancels();
    test_full_confirm_gesture_arms_after_countdown();
    test_countdown_ignores_release();
    test_mode_auto_gated_on_armed();
    test_arm_failed_retry_via_new_confirm_gesture();
    test_leaving_auto_during_countdown_resets();
    TEST_SUMMARY();
}
