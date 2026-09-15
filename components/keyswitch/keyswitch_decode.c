#include "keyswitch.h"

key_position_t keyswitch_decode(bool local_contact, bool auto_contact)
{
    if (local_contact && auto_contact) {
        return KEY_POS_INVALID;
    }
    if (local_contact) {
        return KEY_POS_LOCAL;
    }
    if (auto_contact) {
        return KEY_POS_AUTO;
    }
    return KEY_POS_OFF;
}
