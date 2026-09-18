#include "buzzer.h"

bool buzzer_square_on(int64_t now_ms, uint32_t freq_hz)
{
    if (freq_hz == 0 || now_ms < 0) {
        return false;
    }
    uint32_t period_ms = 1000u / freq_hz;
    if (period_ms == 0) {
        return false;
    }
    uint32_t phase_ms = (uint32_t)(now_ms % period_ms);
    return phase_ms < (period_ms / 2);
}
