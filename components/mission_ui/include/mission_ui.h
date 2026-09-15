#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

/*
 * Список, выбор и загрузка маршрута (раздел 5.4, FR-20..28). Активен,
 * пока верхний автомат — в одном из состояний OFF (ключ в OFF, FR-27);
 * решение "мы сейчас в OFF" принимает вызывающая сторона (HAL), этот
 * модуль сам ключ не читает.
 *
 * Чистое ядро (mission_ui_core.c) — тестируется на хосте (этап 9).
 */

typedef enum {
    MISSION_UI_IDLE,          /* вне OFF — список не запрашивается и не показывается */
    MISSION_UI_REQUESTING,    /* FR-20.1: SCR_USER2=1 отправлен, ждём MLIST/END */
    MISSION_UI_LIST_ERROR,    /* FR-20.3/20.4: повторы исчерпаны либо MLIST ERR */
    MISSION_UI_LIST,          /* FR-21: список получен, выбор энкодером */
    MISSION_UI_CARD,          /* FR-22: карточка выбранного маршрута */
    MISSION_UI_LOADING,       /* FR-23: удержание энкодера, прогресс */
    MISSION_UI_LOAD_RESULT,   /* FR-25/26: результат MLOAD OK/ERR */
} mission_ui_state_t;

typedef struct {
    int num;                                    /* номер из MLIST — не назначается прошивкой (FR-24) */
    char name[MODULE_ROUTE_NAME_MAX_LEN + 1];
} mission_ui_route_t;

typedef struct {
    /* Результат последней загрузки (для экрана "Результат загрузки"). */
    bool have_load_result;
    bool load_ok;
    int load_route_num;
    int load_wp_count;
    int load_dist_to_wp1_m;
    int load_len_m;
    char load_err_code[16];
} mission_ui_load_result_t;

typedef struct {
    mission_ui_state_t state;
    int64_t state_entered_ms;

    mission_ui_route_t routes[MODULE_MAX_ROUTES_UI];
    int route_count;
    int expected_count;      /* из MLIST END, для сверки FR-20.3 */
    int received_count;
    int retries_left;

    int cursor;               /* индекс в routes[] — FR-21 */
    int card_route_num;       /* номер маршрута, открытого в карточке */

    bool route_loaded_this_cycle; /* FR-24.1 */

    mission_ui_load_result_t last_result;

    bool prev_in_off;          /* для детекции входа в OFF */
} mission_ui_ctx_t;

typedef struct {
    bool send_request_list;    /* PARAM_SET SCR_USER2=1 */
    bool send_select_route;    /* PARAM_SET SCR_USER1=selected_route_num */
    int selected_route_num;
} mission_ui_outputs_t;

typedef struct {
    bool in_off;                 /* FR-27: ключ в одном из состояний OFF */
    int32_t encoder_delta;       /* FR-21 */
    bool encoder_short_click;    /* FR-22 */
    bool encoder_long_fired;     /* FR-23 */
    bool back_pressed;           /* FR-22.1 */
    int64_t now_ms;
} mission_ui_tick_inputs_t;

void mission_ui_init(mission_ui_ctx_t *ctx);

/* Подать одну строку STATUSTEXT (MLIST.../MLOAD...), если пришла в
 * этом цикле опроса. HAL вызывает это для каждой строки из очереди
 * ПЕРЕД mission_ui_tick(). */
void mission_ui_feed_statustext(mission_ui_ctx_t *ctx, const char *text, int64_t now_ms,
                                 mission_ui_outputs_t *out);

/* Основной шаг: обрабатывает вход в/выход из OFF (FR-20/24.1),
 * энкодер, кнопку "Назад", таймауты запроса списка (FR-20.4). */
void mission_ui_tick(mission_ui_ctx_t *ctx, const mission_ui_tick_inputs_t *in,
                      mission_ui_outputs_t *out);

/* ---- HAL (ESP-IDF) ---- */

/* Вызвать один раз в app_main() до старта UI-задачи. */
void mission_ui_hal_init(void);

void mission_ui_hal_tick(bool in_off);

/* Потокобезопасные геттеры для UI-отрисовки (этап 8). */
mission_ui_state_t mission_ui_hal_get_state(void);
int mission_ui_hal_get_route_count(void);
bool mission_ui_hal_get_route(int index, mission_ui_route_t *out);
int mission_ui_hal_get_cursor(void);
int mission_ui_hal_get_card_route_num(void);
uint32_t mission_ui_hal_get_hold_progress_permille(void);
bool mission_ui_hal_get_load_result(mission_ui_load_result_t *out);
bool mission_ui_hal_get_route_loaded_this_cycle(void); /* используется auto_sequence, этап 7 */
