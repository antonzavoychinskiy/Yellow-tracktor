/*
 * Тесты чистого ядра списка/загрузки маршрутов (mission_ui_core.c).
 * Сборка:
 *   gcc -std=c11 -I ../../components/config/include \
 *       -I ../../components/mission_ui/include \
 *       test_mission_ui.c ../../components/mission_ui/mission_ui_core.c \
 *       -o test_mission_ui && ./test_mission_ui
 */

#include "../test_common/test_framework.h"
#include "mission_ui.h"

/* FR-20/20.1/20.2: вход в OFF запрашивает список, MLIST.../END принимаются. */
static void test_list_happy_path(void)
{
    TEST_CASE("list_happy_path");
    mission_ui_ctx_t ctx;
    mission_ui_init(&ctx);
    mission_ui_outputs_t out;

    mission_ui_tick_inputs_t in = { .in_off = true, .now_ms = 0 };
    mission_ui_tick(&ctx, &in, &out);
    TEST_ASSERT(out.send_request_list); /* FR-20.1 */
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_REQUESTING);

    mission_ui_feed_statustext(&ctx, "MLIST 1 Sklad_A - Angar_B", 10, &out);
    mission_ui_feed_statustext(&ctx, "MLIST 2 Angar_B - Sklad_A", 11, &out);
    mission_ui_feed_statustext(&ctx, "MLIST END 2", 12, &out);

    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LIST);
    TEST_ASSERT_EQ(ctx.route_count, 2);
    TEST_ASSERT_EQ(ctx.routes[0].num, 1);
    TEST_ASSERT_EQ(ctx.routes[1].num, 2);
}

/* FR-20.3: несовпадение числа строк с MLIST END -> повтор, затем ошибка. */
static void test_list_count_mismatch_retries_then_fails(void)
{
    TEST_CASE("list_count_mismatch_retries_then_fails");
    mission_ui_ctx_t ctx;
    mission_ui_init(&ctx);
    mission_ui_outputs_t out;
    mission_ui_tick_inputs_t in = { .in_off = true, .now_ms = 0 };
    mission_ui_tick(&ctx, &in, &out); /* retries_left = MODULE_MLIST_REQUEST_RETRIES */

    int expected_retries = MODULE_MLIST_REQUEST_RETRIES;
    for (int i = 0; i <= expected_retries; i++) {
        mission_ui_feed_statustext(&ctx, "MLIST 1 X", 10, &out);
        /* заявлено 2, реально пришла 1 строка -> несовпадение */
        mission_ui_feed_statustext(&ctx, "MLIST END 2", 11, &out);
        if (i < expected_retries) {
            TEST_ASSERT_EQ(ctx.state, MISSION_UI_REQUESTING); /* повтор */
        }
    }
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LIST_ERROR); /* попытки исчерпаны */
}

/* FR-20.4: полное молчание -> "недоступно" по таймауту. */
static void test_list_timeout(void)
{
    TEST_CASE("list_timeout");
    mission_ui_ctx_t ctx;
    mission_ui_init(&ctx);
    mission_ui_outputs_t out;
    mission_ui_tick_inputs_t in = { .in_off = true, .now_ms = 0 };
    mission_ui_tick(&ctx, &in, &out);

    in.now_ms = MODULE_MLIST_TIMEOUT_MS + 1;
    mission_ui_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LIST_ERROR);
}

/* FR-21/22/22.1/23/24: выбор, карточка, назад, долгое нажатие -> запрос загрузки. */
static void test_select_card_load_flow(void)
{
    TEST_CASE("select_card_load_flow");
    mission_ui_ctx_t ctx;
    mission_ui_init(&ctx);
    mission_ui_outputs_t out;
    mission_ui_tick_inputs_t in = { .in_off = true, .now_ms = 0 };
    mission_ui_tick(&ctx, &in, &out);
    mission_ui_feed_statustext(&ctx, "MLIST 5 Test Route", 5, &out);
    mission_ui_feed_statustext(&ctx, "MLIST END 1", 6, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LIST);

    in.now_ms = 10; in.encoder_short_click = true;
    mission_ui_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_CARD);
    TEST_ASSERT_EQ(ctx.card_route_num, 5);

    in.encoder_short_click = false; in.back_pressed = true; in.now_ms = 20;
    mission_ui_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LIST); /* FR-22.1 */

    in.back_pressed = false; in.encoder_short_click = true; in.now_ms = 30;
    mission_ui_tick(&ctx, &in, &out); /* обратно в карточку */
    in.encoder_short_click = false; in.encoder_long_fired = true; in.now_ms = 40;
    mission_ui_tick(&ctx, &in, &out);

    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LOADING);
    TEST_ASSERT(out.send_select_route);
    TEST_ASSERT_EQ(out.selected_route_num, 5);
}

/* FR-25/26/24.1: три различных исхода загрузки. */
static void test_load_outcomes(void)
{
    TEST_CASE("load_outcomes_ok");
    mission_ui_ctx_t ctx;
    mission_ui_init(&ctx);
    ctx.state = MISSION_UI_LOADING;
    ctx.card_route_num = 3;
    mission_ui_outputs_t out;

    mission_ui_feed_statustext(&ctx, "MLOAD OK n=3 wp=10 d1=4 len=240", 0, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LOAD_RESULT);
    TEST_ASSERT(ctx.last_result.load_ok);
    TEST_ASSERT_EQ(ctx.last_result.load_wp_count, 10);
    TEST_ASSERT(ctx.route_loaded_this_cycle); /* FR-24.1 */

    TEST_CASE("load_outcomes_err");
    mission_ui_init(&ctx);
    ctx.state = MISSION_UI_LOADING;
    ctx.card_route_num = 3;
    mission_ui_feed_statustext(&ctx, "MLOAD ERR n=3 too_far d1=1240", 0, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LOAD_RESULT);
    TEST_ASSERT(!ctx.last_result.load_ok);
    TEST_ASSERT(!ctx.route_loaded_this_cycle);

    TEST_CASE("load_outcomes_timeout");
    mission_ui_init(&ctx);
    ctx.state = MISSION_UI_LOADING;
    ctx.card_route_num = 3;
    ctx.prev_in_off = true; /* иначе tick() решит, что это свежий вход в OFF, и сбросит state */
    mission_ui_tick_inputs_t in = { .in_off = true, .now_ms = MODULE_MLOAD_TIMEOUT_MS + 1 };
    mission_ui_tick(&ctx, &in, &out);
    TEST_ASSERT_EQ(ctx.state, MISSION_UI_LOAD_RESULT);
    TEST_ASSERT(!ctx.last_result.load_ok);
}

/* FR-24.1: признак "маршрут загружен" сбрасывается при каждом новом входе в OFF. */
static void test_route_loaded_flag_resets_on_off_entry(void)
{
    TEST_CASE("route_loaded_flag_resets_on_off_entry");
    mission_ui_ctx_t ctx;
    mission_ui_init(&ctx);
    mission_ui_outputs_t out;
    mission_ui_tick_inputs_t in = { .in_off = true, .now_ms = 0 };
    mission_ui_tick(&ctx, &in, &out);
    ctx.route_loaded_this_cycle = true;

    /* выход из OFF и повторный вход — новый цикл */
    in.in_off = false; in.now_ms = 100;
    mission_ui_tick(&ctx, &in, &out);
    in.in_off = true; in.now_ms = 200;
    mission_ui_tick(&ctx, &in, &out);

    TEST_ASSERT(!ctx.route_loaded_this_cycle);
}

int main(void)
{
    test_list_happy_path();
    test_list_count_mismatch_retries_then_fails();
    test_list_timeout();
    test_select_card_load_flow();
    test_load_outcomes();
    test_route_loaded_flag_resets_on_off_entry();
    TEST_SUMMARY();
}
