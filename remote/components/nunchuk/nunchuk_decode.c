#include "nunchuk.h"

void nunchuk_decode(const uint8_t raw[6], nunchuk_sample_t *out)
{
    out->x = (int16_t)raw[0] - 128;
    out->y = (int16_t)raw[1] - 128;
    out->btn_z = (raw[5] & 0x01) == 0; /* активна в 0 */
    out->btn_c = (raw[5] & 0x02) == 0;
}
