// The link test: the MAC of the fake board (fk_mac)
#pragma once
#include <stdint.h>
#include <string.h>
#include "fake_board.h"

static inline void arc_mac(uint8_t mac[6])
{
    memcpy(mac, fk_mac, 6);
}
