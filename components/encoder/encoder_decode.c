#include "encoder.h"

/* Состояния table-декодера (классическая устойчивая к дребезгу схема:
 * https://www.buxtronix.net/2011/10/rotary-encoders-done-properly.html —
 * направление отдаётся только на завершённом детенте). */
#define R_START      0x0
#define R_CW_FINAL   0x1
#define R_CW_BEGIN   0x2
#define R_CW_NEXT    0x3
#define R_CCW_BEGIN  0x4
#define R_CCW_FINAL  0x5
#define R_CCW_NEXT   0x6

#define DIR_NONE_BITS 0x00
#define DIR_CW_BITS   0x10
#define DIR_CCW_BITS  0x20

static const uint8_t transition_table[7][4] = {
    /* R_START */    { R_START,     R_CW_BEGIN,  R_CCW_BEGIN, R_START },
    /* R_CW_FINAL */ { R_CW_NEXT,   R_START,     R_CW_FINAL,  R_START | DIR_CW_BITS },
    /* R_CW_BEGIN */ { R_CW_NEXT,   R_CW_BEGIN,  R_START,     R_START },
    /* R_CW_NEXT */  { R_CW_NEXT,   R_CW_BEGIN,  R_CW_FINAL,  R_START },
    /* R_CCW_BEGIN*/ { R_CCW_NEXT,  R_START,     R_CCW_BEGIN, R_START },
    /* R_CCW_FINAL*/ { R_CCW_NEXT,  R_CCW_FINAL, R_START,     R_START | DIR_CCW_BITS },
    /* R_CCW_NEXT */ { R_CCW_NEXT,  R_CCW_FINAL, R_CCW_BEGIN, R_START },
};

void encoder_quad_init(encoder_quad_t *q)
{
    q->state = R_START;
}

encoder_dir_t encoder_quad_step(encoder_quad_t *q, bool a, bool b)
{
    uint8_t pin_state = ((b ? 1 : 0) << 1) | (a ? 1 : 0);
    uint8_t next = transition_table[q->state & 0x7][pin_state];
    q->state = next & 0x0f;

    if (next & DIR_CW_BITS) {
        return ENCODER_DIR_CW;
    }
    if (next & DIR_CCW_BITS) {
        return ENCODER_DIR_CCW;
    }
    return ENCODER_DIR_NONE;
}

void encoder_button_init(encoder_button_t *eb, bool initial_level, uint32_t stable_delay_us)
{
    debounce_init(&eb->db, initial_level, stable_delay_us);
    eb->prev_level = initial_level;
    eb->held = false;
    eb->press_started_us = 0;
    eb->long_fired = false;
}

encoder_btn_event_t encoder_button_update(encoder_button_t *eb, bool raw_pressed,
                                           int64_t now_us, uint32_t longpress_us)
{
    debounce_update(&eb->db, raw_pressed, now_us);
    bool level = debounce_level(&eb->db);

    encoder_btn_event_t event = ENC_BTN_EVENT_NONE;

    if (level && !eb->prev_level) {
        /* Нажатие началось */
        eb->held = true;
        eb->press_started_us = now_us;
        eb->long_fired = false;
    } else if (!level && eb->prev_level) {
        /* Отпускание */
        if (eb->held && !eb->long_fired) {
            event = ENC_BTN_EVENT_SHORT_CLICK;
        }
        eb->held = false;
    } else if (level && eb->held && !eb->long_fired) {
        if ((uint32_t)(now_us - eb->press_started_us) >= longpress_us) {
            eb->long_fired = true;
            event = ENC_BTN_EVENT_LONG_FIRED;
        }
    }

    eb->prev_level = level;
    return event;
}

uint32_t encoder_button_hold_progress_permille(const encoder_button_t *eb, int64_t now_us,
                                                uint32_t longpress_us)
{
    if (!eb->held || eb->long_fired || longpress_us == 0) {
        return 0;
    }
    int64_t elapsed = now_us - eb->press_started_us;
    if (elapsed <= 0) {
        return 0;
    }
    uint64_t permille = ((uint64_t)elapsed * 1000u) / longpress_us;
    return permille > 1000u ? 1000u : (uint32_t)permille;
}
