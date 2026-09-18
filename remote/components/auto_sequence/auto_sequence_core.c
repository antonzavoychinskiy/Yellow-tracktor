#include "auto_sequence.h"
#include "config.h"
#include "ardurover_modes.h"

#include <string.h>

void auto_sequence_init(auto_sequence_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = AUTO_SEQ_READY;
}

static void try_start_confirm(auto_sequence_ctx_t *ctx, const auto_sequence_inputs_t *in)
{
    if (in->start_pressed && in->route_loaded_this_cycle) {
        /* FR-40.5: нажатие запускает фазу удержания-подтверждения, а
         * не сам арм — тот отправляется только по истечении отсчёта
         * (см. AUTO_SEQ_CONFIRM_COUNTDOWN ниже). */
        ctx->state = AUTO_SEQ_CONFIRM_HOLD;
        ctx->state_entered_ms = in->now_ms;
    }
    /* FR-40.1: нажатие при !route_loaded_this_cycle просто
     * игнорируется — состояние (BLOCKED) уже само по себе показывает
     * причину недоступности пуска. */
}

void auto_sequence_tick(auto_sequence_ctx_t *ctx, const auto_sequence_inputs_t *in,
                         auto_sequence_outputs_t *out)
{
    memset(out, 0, sizeof(*out));

    if (!in->active) {
        ctx->state = AUTO_SEQ_READY; /* свежий цикл при следующем входе в AUTO */
        return;
    }

    switch (ctx->state) {
    case AUTO_SEQ_READY:
    case AUTO_SEQ_BLOCKED:
        ctx->state = in->route_loaded_this_cycle ? AUTO_SEQ_READY : AUTO_SEQ_BLOCKED;
        try_start_confirm(ctx, in);
        break;

    case AUTO_SEQ_CONFIRM_HOLD:
        if (!in->start_held) {
            /* FR-40.5: раннее отпускание — сброс, без арма. Оператор
             * начинает заново новым нажатием, свежим фронтом. */
            ctx->state = AUTO_SEQ_READY;
            ctx->state_entered_ms = in->now_ms;
        } else if (in->now_ms - ctx->state_entered_ms >= MODULE_START_CONFIRM_HOLD_MS) {
            ctx->state = AUTO_SEQ_CONFIRM_COUNTDOWN;
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case AUTO_SEQ_CONFIRM_COUNTDOWN:
        /* FR-40.6: необратимо — состояние удержания/отпускания «Пуск»
         * дальше не проверяется. Единственный выход раньше срабатывания
         * — верхнеуровневый !in->active (смена ключа из AUTO), уже
         * обработанный выше. */
        if (in->now_ms - ctx->state_entered_ms >= MODULE_START_CONFIRM_COUNTDOWN_MS) {
            out->cmd_send_arm = true; /* FR-40, FR-7.1: без форсирования */
            ctx->state = AUTO_SEQ_ARMING;
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case AUTO_SEQ_ARM_FAILED:
        /* Диаграмма 8А.3: "ArmFailed --> Ready: доступен повтор" —
         * новое удержание «Пуск» (FR-40.5) является новым действием
         * оператора, поэтому не противоречит "без автоматического
         * повтора" из FR-40.2/FR-8.1: сама прошивка не повторяет
         * команду, повторяет оператор — тем же полным жестом
         * подтверждения, что и первый раз. */
        try_start_confirm(ctx, in);
        break;

    case AUTO_SEQ_ARMING:
        if (in->have_heartbeat && in->armed) {
            /* FR-40.3: SET_MODE AUTO — только после подтверждённого арминга. */
            out->cmd_send_mode_auto = true;
            ctx->state = AUTO_SEQ_SETTING_MODE;
            ctx->state_entered_ms = in->now_ms;
        } else if (in->now_ms - ctx->state_entered_ms > MODULE_ARM_CONFIRM_TIMEOUT_MS) {
            ctx->state = AUTO_SEQ_ARM_FAILED; /* FR-40.2 */
            ctx->state_entered_ms = in->now_ms;
        }
        break;

    case AUTO_SEQ_SETTING_MODE:
        if (in->have_heartbeat && in->custom_mode == ROVER_MODE_AUTO) {
            ctx->state = AUTO_SEQ_MOVING;
            ctx->state_entered_ms = in->now_ms;
        }
        /* Таймаут подтверждения режима не описан отдельным FR — при
         * отсутствии подтверждения остаёмся здесь; MANUAL_CONTROL в
         * AUTO не отправляется в любом случае (модуль не двигает
         * платформу напрямую), новое движение без исполненной команды
         * не запустится — NFR-8 соблюдён без дополнительных действий. */
        break;

    case AUTO_SEQ_MOVING:
        /* Завершение обрабатывается верхним уровнем (ключ -> LOCAL
         * через !active выше, либо естественное завершение маршрута —
         * вне зоны ответственности прошивки, раздел 1.3). */
        break;
    }
}

uint32_t auto_sequence_confirm_hold_progress_permille(const auto_sequence_ctx_t *ctx, int64_t now_ms)
{
    if (ctx->state != AUTO_SEQ_CONFIRM_HOLD) {
        return 0;
    }
    int64_t elapsed = now_ms - ctx->state_entered_ms;
    if (elapsed <= 0) {
        return 0;
    }
    uint64_t permille = ((uint64_t)elapsed * 1000u) / MODULE_START_CONFIRM_HOLD_MS;
    return permille > 1000u ? 1000u : (uint32_t)permille;
}

uint32_t auto_sequence_confirm_countdown_seconds_left(const auto_sequence_ctx_t *ctx, int64_t now_ms)
{
    if (ctx->state != AUTO_SEQ_CONFIRM_COUNTDOWN) {
        return 0;
    }
    int64_t elapsed = now_ms - ctx->state_entered_ms;
    if (elapsed < 0) {
        elapsed = 0;
    }
    int64_t remaining_ms = (int64_t)MODULE_START_CONFIRM_COUNTDOWN_MS - elapsed;
    if (remaining_ms <= 0) {
        return 0;
    }
    /* Округление вверх: «5» держится всю первую секунду отсчёта. */
    return (uint32_t)((remaining_ms + 999) / 1000);
}
