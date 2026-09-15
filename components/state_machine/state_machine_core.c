#include "state_machine.h"
#include "config.h"
#include "ardurover_modes.h"

#include <string.h>

static bool is_local_driving_state(sm_state_t s)
{
    return s == SM_STATE_LOCAL_ARMING || s == SM_STATE_LOCAL_ACTIVE ||
           s == SM_STATE_LOCAL_MODE_LOST || s == SM_STATE_LOCAL_ARM_FAILED;
}

static void enter_off_sequence(sm_context_t *ctx, int64_t now_ms, sm_outputs_t *out)
{
    ctx->state = SM_STATE_OFF_WAIT_STOP;
    ctx->state_entered_ms = now_ms;
    out->cmd_send_hold = true; /* FR-5 шаг 1 */
}

static void enter_local_sequence(sm_context_t *ctx, int64_t now_ms, sm_outputs_t *out)
{
    ctx->state = SM_STATE_LOCAL_ARMING;
    ctx->state_entered_ms = now_ms;
    /* FR-6 + диаграмма 8А.2: ARM и SET_MODE MANUAL — единое компаунд-
     * действие входа в LOCAL, не гейтится подтверждением (в отличие от
     * AUTO/«Пуск», см. FR-40.3 и обоснование в разделе 1.3/5.8: MANUAL
     * без арминга не создаёт движения, дополнительная осторожность не
     * требуется). */
    out->cmd_send_arm = true;
    out->cmd_send_mode_manual = true;
}

static void enter_auto(sm_context_t *ctx, int64_t now_ms)
{
    ctx->state = SM_STATE_AUTO;
    ctx->state_entered_ms = now_ms;
    /* FR-7: команды не отправляются. */
}

void sm_init(sm_context_t *ctx, key_position_t initial_key, int64_t now_ms, sm_outputs_t *out)
{
    memset(ctx, 0, sizeof(*ctx));
    memset(out, 0, sizeof(*out));
    ctx->last_key = initial_key;
    ctx->state_entered_ms = now_ms;

    switch (initial_key) {
    case KEY_POS_OFF:
        /* FR-1.1 + диаграмма 8А.1: старт с ключом в OFF — сразу
         * нормальная работа, без промежуточного WaitOff. */
        enter_off_sequence(ctx, now_ms, out);
        break;
    case KEY_POS_INVALID:
        ctx->state = SM_STATE_FAULT;
        out->warn_invalid_key = true;
        break;
    default: /* LOCAL или AUTO */
        /* FR-1.1: ключ не в OFF на старте — состояние ожидания, без
         * арминга и смены режима. */
        ctx->state = SM_STATE_WAIT_OFF;
        break;
    }
}

void sm_tick(sm_context_t *ctx, const sm_inputs_t *in, sm_outputs_t *out)
{
    memset(out, 0, sizeof(*out));

    /* FR-35 — высший приоритет, проверяется независимо от текущего состояния. */
    if (in->key == KEY_POS_INVALID) {
        if (ctx->state != SM_STATE_FAULT) {
            if (is_local_driving_state(ctx->state)) {
                out->cmd_send_neutral_once = true;
            }
            ctx->state = SM_STATE_FAULT;
            ctx->state_entered_ms = in->now_ms;
        }
        out->warn_invalid_key = true;
        ctx->last_key = in->key;
        return;
    }

    bool key_changed = (in->key != ctx->last_key);

    if (key_changed) {
        if (is_local_driving_state(ctx->state) && in->key != KEY_POS_LOCAL) {
            /* FR-10.1: нейтраль перед командами нового положения ключа. */
            out->cmd_send_neutral_once = true;
        }

        switch (in->key) {
        case KEY_POS_OFF:
            enter_off_sequence(ctx, in->now_ms, out);
            break;
        case KEY_POS_LOCAL:
            enter_local_sequence(ctx, in->now_ms, out);
            break;
        case KEY_POS_AUTO:
            enter_auto(ctx, in->now_ms);
            break;
        default:
            break;
        }

        ctx->last_key = in->key;
        return;
    }

    ctx->last_key = in->key;

    switch (ctx->state) {
    case SM_STATE_WAIT_OFF:
        /* Ждём перевода ключа в OFF — обрабатывается веткой key_changed выше. */
        break;

    case SM_STATE_OFF_WAIT_STOP: {
        bool stopped = in->have_groundspeed && in->groundspeed_mps < MODULE_STOP_SPEED_THRESHOLD_MPS;
        if (stopped) {
            ctx->state = SM_STATE_OFF_WAIT_DISARM_CONFIRM;
            ctx->state_entered_ms = in->now_ms;
            out->cmd_send_disarm = true; /* FR-5 шаг 3 */
        } else if (in->now_ms - ctx->state_entered_ms > MODULE_STOP_WAIT_TIMEOUT_MS) {
            ctx->state = SM_STATE_OFF_FAILED; /* FR-5.1 */
            ctx->state_entered_ms = in->now_ms;
        }
        break;
    }

    case SM_STATE_OFF_WAIT_DISARM_CONFIRM: {
        if (in->disarm_ack_received && !in->disarm_ack_accepted) {
            ctx->state = SM_STATE_OFF_FAILED;
            ctx->state_entered_ms = in->now_ms;
        } else if (in->have_heartbeat && !in->armed) {
            ctx->state = SM_STATE_OFF_IDLE; /* FR-5 шаг 4 */
            ctx->state_entered_ms = in->now_ms;
        } else if (in->now_ms - ctx->state_entered_ms > MODULE_DISARM_CONFIRM_TIMEOUT_MS) {
            ctx->state = SM_STATE_OFF_FAILED;
            ctx->state_entered_ms = in->now_ms;
        }
        break;
    }

    case SM_STATE_OFF_IDLE:
    case SM_STATE_OFF_FAILED:
        /* Терминальны в рамках текущего пребывания в OFF — FR-5.1
         * запрещает автоповтор. */
        break;

    case SM_STATE_LOCAL_ARMING: {
        if (in->arm_ack_received && !in->arm_ack_accepted) {
            ctx->state = SM_STATE_LOCAL_ARM_FAILED; /* FR-8.1 */
            ctx->state_entered_ms = in->now_ms;
        } else if (in->have_heartbeat && in->armed) {
            ctx->state = SM_STATE_LOCAL_ACTIVE;
            ctx->state_entered_ms = in->now_ms;
        } else if (in->now_ms - ctx->state_entered_ms > MODULE_ARM_CONFIRM_TIMEOUT_MS) {
            ctx->state = SM_STATE_LOCAL_ARM_FAILED;
            ctx->state_entered_ms = in->now_ms;
        }
        break;
    }

    case SM_STATE_LOCAL_ARM_FAILED:
        /* FR-8.1: без автоповтора; выход — только сменой положения
         * ключа (обрабатывается веткой key_changed). */
        break;

    case SM_STATE_LOCAL_ACTIVE: {
        /* FR-9 (расхождение режима) расширено до общего "небезопасно
         * продолжать": потеря связи или неожиданный дизарм — тоже
         * основание уйти в нейтраль+HOLD, а не только смена режима.
         * Обоснование: NFR-8 требует нейтрали при любой
         * неопределённости/отказе, не только при явном расхождении
         * режима. */
        bool unsafe = !in->have_heartbeat || in->custom_mode != ROVER_MODE_MANUAL || !in->armed;
        if (unsafe) {
            out->cmd_send_neutral_once = true;
            out->cmd_send_hold = true;
            out->warn_mode_mismatch = true;
            ctx->state = SM_STATE_LOCAL_MODE_LOST;
            ctx->state_entered_ms = in->now_ms;
        }
        break;
    }

    case SM_STATE_LOCAL_MODE_LOST:
        out->warn_mode_mismatch = true; /* держим предупреждение до смены ключа */
        break;

    case SM_STATE_AUTO:
        /* FR-7, FR-9.1: полностью пассивно. Подпоследовательность
         * «Пуск» — вне этого модуля (auto_sequence, этап 7). */
        break;

    case SM_STATE_FAULT:
        out->warn_invalid_key = true;
        break;
    }
}
