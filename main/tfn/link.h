#pragma once

// A small radio link between boards (ESP-NOW): broadcast packets on the WiFi channel now.
// The link puts the two boards on one channel by itself (link_recv): a board on a WiFi network stays on the channel of
// its network, a board with no network searches for the other board (link_meet.h in the OS).
// Each packet has a magic number and a version, so other ESP-NOW traffic does not come in.

#include <stdbool.h>
#include <stdint.h>

#define LINK_DATA_MAX 230  // bytes of app data in one packet (ESP-NOW: 250 at most, with the 9-byte header)

typedef struct {
    uint8_t mac[6];  // the board that sent it
    uint8_t app;     // the app that sent it (for example 'M' = MORSE)
    uint8_t kind;    // set by the app
    uint8_t len;     // bytes in data
    uint8_t data[LINK_DATA_MAX];
    int8_t rssi;    // dBm (0 = not known)
    uint8_t ch;     // the channel of the sender (bit 7: it is on a WiFi network; 0 = not known)
    int64_t rx_ms;  // local time of the receive (esp_timer, ms)
} link_msg_t;

// Start ESP-NOW (it starts the WiFi driver if it is off). Returns true when the link is
// ready. While the link is on, the WiFi power save is off, so that broadcasts come in fast.
bool link_start(void);
// Stop ESP-NOW and put the WiFi power save back.
void link_stop(void);

// Send a broadcast. Returns false if the link is off or the data is too long.
bool link_send(uint8_t app, uint8_t kind, const void *data, int len);
// The next received packet (no wait). Returns false if there is none.
bool link_recv(link_msg_t *m);

// The link finds the channel of the other board by itself: this call changes nothing (an app of an older firmware
// searched with it). Returns false if the link is off or ch is not 1..13.
bool link_set_channel(int ch);

// The WiFi channel now (0 = not known), and the MAC address of this board.
int link_channel(void);
void link_my_mac(uint8_t mac[6]);

// RAW FRAMES: the frames of another protocol on the same radio, for example a TFN COMPANION (libs/tfncomp: 'C' 'P').
// A raw frame starts with its 2 magic bytes and has up to LINK_RAW_MAX bytes; it does not go by the LAN, it has no
// pool tag (its protocol has its own), and the link does not search for a channel for it.
#define LINK_RAW_MAX 250
typedef struct {
    uint8_t mac[6];  // the sender
    uint8_t len;
    uint8_t data[LINK_RAW_MAX];
    int8_t rssi;
    int64_t rx_ms;
} link_raw_t;
// Start the link (link_start) and take the frames that start with these 2 bytes into a queue. False: no radio.
bool link_raw_open(uint8_t magic0, uint8_t magic1);
// Stop the raw frames and the link (link_stop)
void link_raw_close(void);
// Send one raw frame: mac = NULL is a broadcast, else a unicast (the radio sends it again when no ACK comes).
bool link_raw_send(const uint8_t *mac, const void *data, int len);
// The next raw frame (no wait). False: none.
bool link_raw_recv(link_raw_t *m);
