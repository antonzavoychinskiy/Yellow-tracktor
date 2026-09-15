#include "buttons.h"

void button_init(button_t *b, bool initial_level, uint32_t stable_delay_us)
{
    debounce_init(&b->db, initial_level, stable_delay_us);
    b->prev_level = initial_level;
}

bool button_update(button_t *b, bool raw_pressed, int64_t now_us)
{
    debounce_update(&b->db, raw_pressed, now_us);
    bool level = debounce_level(&b->db);
    bool rising_edge = level && !b->prev_level;
    b->prev_level = level;
    return rising_edge;
}
