#pragma once

// VPN: a WireGuard tunnel from this board to the hub (a central manager), so the
// boards and the central manager find each other also out of the LAN. Optional: the board web
// page (VPN) sets it; the manager can set it with one click. The LAN search stays as before.
// - The board makes its WireGuard key pair once; the private key stays in NVS only.
// - The tunnel needs the right time (the WireGuard handshake has a time stamp): SNTP first.
// - Only the tunnel network goes into the tunnel (the WiFi stays the default route).

// An extra (CONFIG_TFN_EXTRA_VPN): when it is off, the board has no tunnel and the functions below give "no VPN".
#include <stdbool.h>
#include <stdint.h>
#include "tfn_extras.h"

typedef struct {
    bool on;
    char host[64];     // the hub: a name (vpn.example.com) or an address
    uint16_t port;     // 51820
    char hub_key[48];  // the public key of the hub (base64)
    char ip[16];       // the tunnel address of this board, in the tunnel network of the hub
    char hub_ip[16];   // the tunnel address of the hub (the manager), the first address of that network
} vpn_cfg_t;

typedef enum {
    VPN_OFF,
    VPN_WAIT_WIFI,
    VPN_WAIT_TIME,
    VPN_CONNECTING,
    VPN_UP,
    VPN_ERROR
} vpn_state_t;

#if CONFIG_TFN_EXTRA_VPN
void vpn_init(void);        // at the start (reads NVS; a board with no key makes it once after the WiFi start)
void vpn_start_task(void);  // after the WiFi start (the task waits for the WiFi and the time)
void vpn_get(vpn_cfg_t *out);
bool vpn_set(const vpn_cfg_t *cfg);  // saves and starts again; false: a bad value
const char *vpn_public_key(void);    // base64
vpn_state_t vpn_state(void);
const char *vpn_state_name(vpn_state_t s);
// The tunnel address of the hub and this board (network byte order; 0 = no tunnel now).
uint32_t vpn_hub_ip(void);
uint32_t vpn_my_ip(void);
// true if ip (network byte order) is in the tunnel network (a /24)
bool vpn_is_tunnel_ip(uint32_t ip);
#else
static inline void vpn_init(void)
{
}
static inline void vpn_start_task(void)
{
}
static inline vpn_state_t vpn_state(void)
{
    return VPN_OFF;
}
static inline uint32_t vpn_hub_ip(void)
{
    return 0;
}
static inline uint32_t vpn_my_ip(void)
{
    return 0;
}
static inline bool vpn_is_tunnel_ip(uint32_t ip)
{
    (void)ip;
    return false;
}
#endif
