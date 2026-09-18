#include "joystick_adc.h"
#include "config.h"

static int16_t center_axis(int raw, int center, int invert)
{
    int centered = raw - center;
    if (invert) {
        centered = -centered;
    }
    if (centered > INT16_MAX) {
        centered = INT16_MAX;
    } else if (centered < INT16_MIN) {
        centered = INT16_MIN;
    }
    return (int16_t)centered;
}

void joystick_adc_center(int raw_x, int raw_y, joystick_adc_sample_t *out)
{
    out->x = center_axis(raw_x, MODULE_JOYSTICK_ADC_CENTER_X, MODULE_JOYSTICK_X_INVERT);
    out->y = center_axis(raw_y, MODULE_JOYSTICK_ADC_CENTER_Y, MODULE_JOYSTICK_Y_INVERT);
}
