#include "mission_ui.h"

#include <string.h>
#include <stdio.h>

/* ============ Разбор STATUSTEXT (Приложение А) — чистые функции ============ */

static bool parse_mlist_entry(const char *text, int *num, char *name_out, size_t name_out_sz)
{
    int consumed = 0;
    if (sscanf(text, "MLIST %d%n", num, &consumed) != 1 || consumed == 0) {
        return false; /* также отсекает "MLIST END ..." и "MLIST ERR ..." — %d не матчится на "END"/"ERR" */
    }
    const char *name_start = text + consumed;
    while (*name_start == ' ') {
        name_start++;
    }
    if (*name_start == '\0') {
        return false;
    }
    strncpy(name_out, name_start, name_out_sz - 1);
    name_out[name_out_sz - 1] = '\0';
    return true;
}

static bool parse_mlist_end(const char *text, int *count)
{
    return sscanf(text, "MLIST END %d", count) == 1;
}

static bool parse_mlist_err(const char *text)
{
    return strncmp(text, "MLIST ERR", 9) == 0;
}

static bool parse_mload(const char *text, mission_ui_load_result_t *out)
{
    memset(out, 0, sizeof(*out));
    int num;

    if (sscanf(text, "MLOAD OK n=%d wp=%d d1=%d len=%d",
               &num, &out->load_wp_count, &out->load_dist_to_wp1_m, &out->load_len_m) == 4) {
        out->have_load_result = true;
        out->load_ok = true;
        out->load_route_num = num;
        return true;
    }

    int dist;
    if (sscanf(text, "MLOAD ERR n=%d too_far d1=%d", &num, &dist) == 2) {
        out->have_load_result = true;
        out->load_ok = false;
        out->load_route_num = num;
        out->load_dist_to_wp1_m = dist;
        strncpy(out->load_err_code, "too_far", sizeof(out->load_err_code) - 1);
        return true;
    }

    char code[16] = { 0 };
    if (sscanf(text, "MLOAD ERR n=%d %15s", &num, code) == 2) {
        out->have_load_result = true;
        out->load_ok = false;
        out->load_route_num = num;
        strncpy(out->load_err_code, code, sizeof(out->load_err_code) - 1);
        return true;
    }

    return false;
}

/* ============ Ядро состояний ============ */

void mission_ui_init(mission_ui_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = MISSION_UI_IDLE;
}

static void start_request(mission_ui_ctx_t *ctx, int64_t now_ms, mission_ui_outputs_t *out)
{
    ctx->route_count = 0;
    ctx->received_count = 0;
    ctx->expected_count = -1;
    ctx->state = MISSION_UI_REQUESTING;
    ctx->state_entered_ms = now_ms;
    out->send_request_list = true;
}

void mission_ui_feed_statustext(mission_ui_ctx_t *ctx, const char *text, int64_t now_ms,
                                 mission_ui_outputs_t *out)
{
    memset(out, 0, sizeof(*out));

    if (ctx->state == MISSION_UI_REQUESTING) {
        int num;
        char name[MODULE_ROUTE_NAME_MAX_LEN + 1];
        int end_count;

        if (parse_mlist_entry(text, &num, name, sizeof(name))) {
            ctx->received_count++;
            if (ctx->route_count < MODULE_MAX_ROUTES_UI) {
                ctx->routes[ctx->route_count].num = num;
                strncpy(ctx->routes[ctx->route_count].name, name, MODULE_ROUTE_NAME_MAX_LEN);
                ctx->routes[ctx->route_count].name[MODULE_ROUTE_NAME_MAX_LEN] = '\0';
                ctx->route_count++;
            }
            return;
        }

        if (parse_mlist_end(text, &end_count)) {
            ctx->expected_count = end_count;
            if (ctx->received_count == end_count) {
                /* FR-20.3: число совпало — список достоверен. */
                ctx->state = MISSION_UI_LIST;
                ctx->state_entered_ms = now_ms;
                ctx->cursor = 0;
            } else if (ctx->retries_left > 0) {
                ctx->retries_left--;
                start_request(ctx, now_ms, out);
            } else {
                ctx->state = MISSION_UI_LIST_ERROR;
                ctx->state_entered_ms = now_ms;
            }
            return;
        }

        if (parse_mlist_err(text)) {
            /* Явная ошибка чтения манифеста на Pixhawk — та же логика
             * повторов, что и при несовпадении счётчика (FR-20.3):
             * транзиентный сбой не должен выглядеть как окончательный
             * отказ раньше исчерпания попыток. */
            if (ctx->retries_left > 0) {
                ctx->retries_left--;
                start_request(ctx, now_ms, out);
            } else {
                ctx->state = MISSION_UI_LIST_ERROR;
                ctx->state_entered_ms = now_ms;
            }
            return;
        }
        return;
    }

    if (ctx->state == MISSION_UI_LOADING) {
        mission_ui_load_result_t result;
        if (parse_mload(text, &result) && result.load_route_num == ctx->card_route_num) {
            ctx->last_result = result;
            if (result.load_ok) {
                ctx->route_loaded_this_cycle = true; /* FR-24.1 */
            }
            ctx->state = MISSION_UI_LOAD_RESULT;
            ctx->state_entered_ms = now_ms;
        }
        return;
    }

    /* В остальных состояниях STATUSTEXT для этого модуля не ожидается. */
}

void mission_ui_tick(mission_ui_ctx_t *ctx, const mission_ui_tick_inputs_t *in,
                      mission_ui_outputs_t *out)
{
    memset(out, 0, sizeof(*out));

    bool off_edge = in->in_off && !ctx->prev_in_off;
    ctx->prev_in_off = in->in_off;

    if (!in->in_off) {
        ctx->state = MISSION_UI_IDLE;
        return;
    }

    if (off_edge) {
        /* FR-20 + FR-24.1: каждый вход в OFF — свежий запрос списка и
         * сброс признака "маршрут загружен". */
        ctx->route_loaded_this_cycle = false;
        ctx->retries_left = MODULE_MLIST_REQUEST_RETRIES;
        start_request(ctx, in->now_ms, out);
        return;
    }

    switch (ctx->state) {
    case MISSION_UI_IDLE:
        /* Вход в OFF обрабатывается веткой off_edge выше. */
        break;

    case MISSION_UI_REQUESTING:
        if (in->now_ms - ctx->state_entered_ms > MODULE_MLIST_TIMEOUT_MS) {
            /* FR-20.4: полное молчание — не тратим попытки из
             * MODULE_MLIST_REQUEST_RETRIES (та переменная — про
             * несовпадение счётчика при частичном ответе, FR-20.3),
             * сразу отдаём управление оператору. */
            ctx->state = MISSION_UI_LIST_ERROR;
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case MISSION_UI_LIST_ERROR:
        if (in->encoder_short_click) {
            /* FR-20.4: "предоставить возможность повторного запроса". */
            ctx->retries_left = MODULE_MLIST_REQUEST_RETRIES;
            start_request(ctx, in->now_ms, out);
        }
        break;

    case MISSION_UI_LIST:
        if (ctx->route_count > 0 && in->encoder_delta != 0) {
            int c = ctx->cursor + in->encoder_delta;
            if (c < 0) c = 0;
            if (c > ctx->route_count - 1) c = ctx->route_count - 1;
            ctx->cursor = c;
        }
        if (ctx->route_count > 0 && in->encoder_short_click) {
            ctx->card_route_num = ctx->routes[ctx->cursor].num; /* FR-22 */
            ctx->state = MISSION_UI_CARD;
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case MISSION_UI_CARD:
        if (in->back_pressed) {
            ctx->state = MISSION_UI_LIST; /* FR-22.1 */
            ctx->state_entered_ms = in->now_ms;
        } else if (in->encoder_long_fired) {
            out->send_select_route = true; /* FR-23, FR-24 */
            out->selected_route_num = ctx->card_route_num;
            ctx->state = MISSION_UI_LOADING;
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case MISSION_UI_LOADING:
        if (in->now_ms - ctx->state_entered_ms > MODULE_MLOAD_TIMEOUT_MS) {
            /* FR-26: "отсутствие ответа в пределах таймаута" — третья,
             * отдельная от OK/ERR категория результата. */
            memset(&ctx->last_result, 0, sizeof(ctx->last_result));
            ctx->last_result.have_load_result = true;
            ctx->last_result.load_ok = false;
            ctx->last_result.load_route_num = ctx->card_route_num;
            strncpy(ctx->last_result.load_err_code, "timeout",
                    sizeof(ctx->last_result.load_err_code) - 1);
            ctx->state = MISSION_UI_LOAD_RESULT;
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case MISSION_UI_LOAD_RESULT:
        if (in->back_pressed) {
            ctx->state = MISSION_UI_LIST;
            ctx->state_entered_ms = in->now_ms;
        }
        break;
    }
}
