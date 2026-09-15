#include "debounce.h"

void debounce_init(debounce_t *d, bool initial_level, uint32_t stable_delay_us)
{
    d->stable_level = initial_level;
    d->candidate_level = initial_level;
    d->candidate_since_us = 0;
    d->have_candidate = false;
    d->stable_delay_us = stable_delay_us;
}

bool debounce_update(debounce_t *d, bool raw_level, int64_t now_us)
{
    if (raw_level == d->stable_level) {
        /* Совпадает со стабильным значением — кандидат на смену снят. */
        d->have_candidate = false;
        return false;
    }

    if (!d->have_candidate || d->candidate_level != raw_level) {
        d->have_candidate = true;
        d->candidate_level = raw_level;
        d->candidate_since_us = now_us;
        return false;
    }

    if ((uint32_t)(now_us - d->candidate_since_us) >= d->stable_delay_us) {
        d->stable_level = raw_level;
        d->have_candidate = false;
        return true;
    }

    return false;
}
