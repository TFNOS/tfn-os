// The link test: a small copy of the VPN API of the OS (main/vpn.h). A tunnel address = 10.0.x.x (fake).
#pragma once
#include <stdbool.h>
#include <stdint.h>

uint32_t vpn_hub_ip(void);
bool vpn_is_tunnel_ip(uint32_t ip);
