/*
 * Тесты чистого ядра конечного автомата (state_machine_core.c) —
 * без ESP-IDF, без стенда. Сборка:
 *
 *   gcc -std=c11 -I ../../components/config/include \
 *       -I ../../components/state_machine/include \
 *       -I ../../components/mavlink_bridge/include \
 *       -I ../../components/keyswitch/include \
 *       test_state_machine.c ../../components/state_machine/state_machine_core.c \
 *       -o test_state_machine && ./test_state_machine
 *
 * (из каталога test/test_state_machine)
 */

#include "../test_common/test_framework.h"
#include "state_machine.h"
#include "config.h"
#include "ardurover_modes.h"

static sm_inputs_t base_inputs(key_position_t key, int64_t now_ms)
{
    sm_inputs_t in = { 0 };
    in.key = key;
    in.now_ms = now_ms;
    return in;
}

/* FR-1.1: старт с ключом не в OFF -> WaitOff, без команд. */
static void test_boot_not_off(void)
{
    TEST_CASE("boot_not_off");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_LOCAL, 0, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_manual);
    TEST_ASSERT(!out.cmd_send_hold);
    TEST_ASSERT(!out.cmd_send_disarm);
}

/* Диаграмма 8А.1: старт с ключом в OFF -> сразу нормальная работа. */
static void test_boot_off(void)
{
    TEST_CASE("boot_off");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);
    TEST_ASSERT(out.cmd_send_hold); /* FR-5 шаг 1 */
}

/* FR-1.1 -> FR-5: WaitOff, затем ключ -> OFF запускает HOLD. */
static void test_wait_off_to_off(void)
{
    TEST_CASE("wait_off_to_off");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_LOCAL, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_OFF, 100);
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);
    TEST_ASSERT(out.cmd_send_hold);
    /* Ключ не менялся из LOCAL в LOCAL — команд арминга при этом не было
     * отправлено (FR-1.1: никаких команд, пока не будет OFF). */
}

/* FR-5: остановка по скорости -> DISARM -> подтверждение по HEARTBEAT -> OFF_IDLE. */
static void test_off_sequence_success(void)
{
    TEST_CASE("off_sequence_success");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);

    /* скорость ещё высокая — ждём */
    sm_inputs_t in = base_inputs(KEY_POS_OFF, 100);
    in.have_groundspeed = true;
    in.groundspeed_mps = 2.0f;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);
    TEST_ASSERT(!out.cmd_send_disarm);

    /* остановились */
    in.now_ms = 200;
    in.groundspeed_mps = 0.01f;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_DISARM_CONFIRM);
    TEST_ASSERT(out.cmd_send_disarm);

    /* HEARTBEAT подтверждает дизарм */
    in.now_ms = 300;
    in.have_heartbeat = true;
    in.armed = false;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_IDLE);
}

/* FR-5.1: таймаут ожидания остановки -> OFF_FAILED, без автоповтора. */
static void test_off_sequence_timeout(void)
{
    TEST_CASE("off_sequence_timeout");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_OFF, MODULE_STOP_WAIT_TIMEOUT_MS + 1);
    in.have_groundspeed = true;
    in.groundspeed_mps = 5.0f; /* не остановились */
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_FAILED);
    TEST_ASSERT(!out.cmd_send_disarm);

    /* Дальнейшие тики без смены ключа не меняют состояние (без автоповтора). */
    in.now_ms += 1000;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_FAILED);
}

/* FR-6 + диаграмма 8А.2: ARM и SET_MODE MANUAL отправляются вместе на входе в LOCAL. */
static void test_local_entry_compound_command(void)
{
    TEST_CASE("local_entry_compound_command");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ARMING);
    TEST_ASSERT(out.cmd_send_arm);
    TEST_ASSERT(out.cmd_send_mode_manual);
}

/* FR-8.1: отказ арминга -> ARM_FAILED, без автоповтора, пока ключ не сменится. */
static void test_local_arm_failed_no_retry(void)
{
    TEST_CASE("local_arm_failed_no_retry");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out); /* -> LOCAL_ARMING */

    in.now_ms = 200;
    in.arm_ack_received = true;
    in.arm_ack_accepted = false;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ARM_FAILED);

    /* тик спустя — ничего не переотправляется */
    in.now_ms = 300;
    in.arm_ack_received = false;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ARM_FAILED);
    TEST_ASSERT(!out.cmd_send_arm);
}

/* FR-9: расхождение режима в LOCAL_ACTIVE -> нейтраль + HOLD + предупреждение. */
static void test_local_mode_mismatch(void)
{
    TEST_CASE("local_mode_mismatch");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out); /* ARMING */

    in.now_ms = 200;
    in.have_heartbeat = true;
    in.armed = true;
    in.custom_mode = ROVER_MODE_MANUAL;
    sm_tick(&ctx, &in, &out); /* -> ACTIVE */
    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ACTIVE);

    in.now_ms = 300;
    in.custom_mode = ROVER_MODE_HOLD; /* диспетчер сменил режим извне */
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_MODE_LOST);
    TEST_ASSERT(out.cmd_send_neutral_once);
    TEST_ASSERT(out.cmd_send_hold);
    TEST_ASSERT(out.warn_mode_mismatch);
}

/* FR-9.1: то же расхождение в OFF/AUTO НЕ вызывает реакции. */
static void test_off_auto_passive_to_mode(void)
{
    TEST_CASE("off_auto_passive_to_mode");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_OFF, 100);
    in.have_heartbeat = true;
    in.armed = true;         /* диспетчер заармил платформу извне */
    in.custom_mode = ROVER_MODE_AUTO;
    sm_tick(&ctx, &in, &out);

    /* Модуль остаётся в своей OFF-последовательности, не реагирует. */
    TEST_ASSERT(!out.warn_mode_mismatch);
    TEST_ASSERT(!out.cmd_send_hold || ctx.state == SM_STATE_OFF_WAIT_DISARM_CONFIRM);
}

/* FR-35: недопустимое положение ключа -> FAULT, нейтраль (если ехали), без команд арминга/режима. */
static void test_invalid_key_fault(void)
{
    TEST_CASE("invalid_key_fault");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out);
    in.now_ms = 200; in.have_heartbeat = true; in.armed = true; in.custom_mode = ROVER_MODE_MANUAL;
    sm_tick(&ctx, &in, &out); /* -> LOCAL_ACTIVE */

    in.now_ms = 300;
    in.key = KEY_POS_INVALID;
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_FAULT);
    TEST_ASSERT(out.warn_invalid_key);
    TEST_ASSERT(out.cmd_send_neutral_once); /* были в LOCAL_ACTIVE */
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_hold);
}

/* FR-10.1: выход из LOCAL_ACTIVE в OFF — нейтраль в тот же тик, что и HOLD. */
static void test_local_to_off_neutral_first(void)
{
    TEST_CASE("local_to_off_neutral_first");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out);
    in.now_ms = 200; in.have_heartbeat = true; in.armed = true; in.custom_mode = ROVER_MODE_MANUAL;
    sm_tick(&ctx, &in, &out); /* -> LOCAL_ACTIVE */

    in.now_ms = 300;
    in.key = KEY_POS_OFF;
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);
    TEST_ASSERT(out.cmd_send_neutral_once);
    TEST_ASSERT(out.cmd_send_hold);
}

/* FR-1.2: старт в AUTO, OFF «проскочен» при повороте в LOCAL — без
 * арминга, ожидание OFF (СЦ-11). */
static void test_wait_off_auto_to_local_no_arm(void)
{
    TEST_CASE("wait_off_auto_to_local_no_arm");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_AUTO, 0, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);

    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_manual);
}

/* FR-1.2: старт в LOCAL, OFF «проскочен» при повороте в AUTO — не
 * входим в AUTO, остаёмся в ожидании OFF. */
static void test_wait_off_local_to_auto_stays_waiting(void)
{
    TEST_CASE("wait_off_local_to_auto_stays_waiting");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_LOCAL, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_AUTO, 100);
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_hold);
}

/* FR-1.2 + FR-8.1: после отказа арминга быстрый поворот LOCAL -> AUTO ->
 * LOCAL без зафиксированного OFF не повторяет арминг. */
static void test_local_arm_failed_fast_turn_no_rearm(void)
{
    TEST_CASE("local_arm_failed_fast_turn_no_rearm");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out); /* -> LOCAL_ARMING */
    in.now_ms = 200;
    in.arm_ack_received = true;
    in.arm_ack_accepted = false;
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ARM_FAILED);

    in = base_inputs(KEY_POS_AUTO, 300);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);

    in = base_inputs(KEY_POS_LOCAL, 400);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_manual);
}

/* FR-1.2 + FR-10.1: из LOCAL_ACTIVE ключ сразу в AUTO (OFF не
 * зафиксирован) — нейтраль и ожидание OFF, без команд режима. */
static void test_local_active_fast_turn_to_auto(void)
{
    TEST_CASE("local_active_fast_turn_to_auto");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_LOCAL, 100);
    sm_tick(&ctx, &in, &out);
    in.now_ms = 200; in.have_heartbeat = true; in.armed = true; in.custom_mode = ROVER_MODE_MANUAL;
    sm_tick(&ctx, &in, &out); /* -> LOCAL_ACTIVE */
    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ACTIVE);

    in.now_ms = 300;
    in.key = KEY_POS_AUTO;
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
    TEST_ASSERT(out.cmd_send_neutral_once);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_manual);
}

/* FR-1.2: восстановление после FAULT с ключом в LOCAL — без арминга,
 * ожидание OFF. */
static void test_fault_to_local_no_arm(void)
{
    TEST_CASE("fault_to_local_no_arm");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);
    sm_inputs_t in = base_inputs(KEY_POS_INVALID, 100);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_FAULT);

    in = base_inputs(KEY_POS_LOCAL, 200);
    sm_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
    TEST_ASSERT(!out.cmd_send_arm);
    TEST_ASSERT(!out.cmd_send_mode_manual);
    TEST_ASSERT(!out.warn_invalid_key);
}

/* FR-1.2: старт с недопустимым ключом, затем AUTO — ожидание OFF. */
static void test_boot_fault_to_auto_waits(void)
{
    TEST_CASE("boot_fault_to_auto_waits");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_INVALID, 0, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_FAULT);

    sm_inputs_t in = base_inputs(KEY_POS_AUTO, 100);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_WAIT_OFF);
}

/* Диаграмма 8А.1: FAULT -> OFF — нормальная OFF-последовательность. */
static void test_fault_to_off(void)
{
    TEST_CASE("fault_to_off");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_INVALID, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_OFF, 100);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);
    TEST_ASSERT(out.cmd_send_hold);
}

/* 4.1: штатный путь OFF -> AUTO -> OFF -> LOCAL работает как раньше. */
static void test_normal_off_auto_off_local(void)
{
    TEST_CASE("normal_off_auto_off_local");
    sm_context_t ctx;
    sm_outputs_t out;
    sm_init(&ctx, KEY_POS_OFF, 0, &out);

    sm_inputs_t in = base_inputs(KEY_POS_AUTO, 100);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_AUTO);
    TEST_ASSERT(!out.cmd_send_arm); /* FR-7 */

    in = base_inputs(KEY_POS_OFF, 200);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_OFF_WAIT_STOP);

    in = base_inputs(KEY_POS_LOCAL, 300);
    sm_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, SM_STATE_LOCAL_ARMING);
    TEST_ASSERT(out.cmd_send_arm);
}

int main(void)
{
    test_boot_not_off();
    test_boot_off();
    test_wait_off_to_off();
    test_off_sequence_success();
    test_off_sequence_timeout();
    test_local_entry_compound_command();
    test_local_arm_failed_no_retry();
    test_local_mode_mismatch();
    test_off_auto_passive_to_mode();
    test_invalid_key_fault();
    test_local_to_off_neutral_first();
    test_wait_off_auto_to_local_no_arm();
    test_wait_off_local_to_auto_stays_waiting();
    test_local_arm_failed_fast_turn_no_rearm();
    test_local_active_fast_turn_to_auto();
    test_fault_to_local_no_arm();
    test_boot_fault_to_auto_waits();
    test_fault_to_off();
    test_normal_off_auto_off_local();
    TEST_SUMMARY();
}
