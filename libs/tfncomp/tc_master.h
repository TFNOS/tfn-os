#pragma once
// TFNCOMP, THE MASTER SIDE: a TFN OS board that controls its companions (tfncomp.h). Pure C: the app gives the radio
// (link_raw_* of tfn/link.h) and the clock in tcm_io_t, calls tcm_rx() for each frame and tcm_tick() often (each
// frame of the app). Each request is non-blocking: it returns at once, and tcm_req() tells its state later.
#include "tfncomp.h"

#define TCM_MAX       8     // paired companions
#define TCM_FOUND_MAX 8     // companions that the search hears
#define TCM_LIVE_MS   6000  // a companion is "online" when it answered in this time

typedef struct {
    bool (*send)(const uint8_t *mac, const uint8_t *d, int n);  // mac = NULL: a broadcast
    uint32_t (*now_ms)(void);
    void (*random)(uint8_t *p, int n);
    int (*channel)(void);   // the channel of this board (1..13; bit 7: on a WiFi network)
    void (*changed)(void);  // the list of the paired companions changed: save it (tcm_save)
} tcm_io_t;

// one paired companion
typedef struct {
    uint8_t mac[6];
    char name[17];
    uint8_t key[TC_KEY_LEN];
    uint8_t chip;
    uint32_t tx_ctr;  // the counter of this board to it (saved)
    // live (not saved)
    uint32_t rx_ctr;
    uint32_t seen_ms;  // the last good frame (0: never)
    int rssi;
    bool has_info;  // the info and the wiring (saved: they stay when the companion is off)
    tc_info_t info;
    bool info_fresh;   // the info came after the start (tcm_init)
    uint32_t info_ms;  // the last INFO_REQ of the clock
    uint32_t ping_ms;
    // the live test of a pin (TC_EDGES)
    int watch_pin;
    bool watch_level;
    uint32_t watch_edges, watch_ms;
    int32_t watch_d[200];  // the last level times
    int watch_n;
    // the values of the switch and analog modules (TC_STATE; live)
    int nst;
    tc_state_t st[TC_WIRE_MAX];
    uint32_t st_ms;  // the last TC_STATE (0: none)
} tcm_comp_t;

// a companion that the search hears (its HELLO)
typedef struct {
    uint8_t mac[6];
    uint8_t chip, rev;
    uint16_t fw;
    bool paired;
    uint8_t master[6];
    uint32_t seen_ms;
    int rssi;
} tcm_found_t;

void tcm_init(const tcm_io_t *io, const uint8_t my_mac[6], const char *my_name);
void tcm_rx(const uint8_t mac[6], const uint8_t *d, int n, int rssi);
void tcm_tick(void);

// the paired companions
int tcm_count(void);
tcm_comp_t *tcm_get(int i);
int tcm_find(const uint8_t mac[6]);
bool tcm_online(int i);
void tcm_rename(int i, const char *name);
void tcm_forget(int i);  // FORGET to the companion, and out of the list
int tcm_save(uint8_t *buf, int max);
bool tcm_load(const uint8_t *buf, int n);
#define TCM_REC      (48 + 4 + 1 + TC_GPIO_MAX + 2 + 4 * TC_WIRE_MAX)  // one companion in the file
#define TCM_SAVE_MAX (4 + TCM_MAX * TCM_REC)

// the search and the pairing
int tcm_found(tcm_found_t *out, int max);  // the companions heard in the last 10 s (paired to this board: not here)
bool tcm_pair_start(const uint8_t mac[6], const char *name);
// 0: none; 1: the request goes out (the companion did not answer yet); 3: the companion waits for its BOOT key;
// 2: paired (tcm_pair_index); -1: no answer in time
int tcm_pair_state(void);
int tcm_pair_index(void);
void tcm_pair_cancel(void);

// THE REQUESTS: one at a time for each companion; a new one ends the old one
enum {
    TCM_R_NONE,
    TCM_R_INFO,
    TCM_R_CONFIG,
    TCM_R_WATCH,
    TCM_R_PULSE,
    TCM_R_CAPTURE,
    TCM_R_SEND,
    TCM_R_PING,
    TCM_R_SET,
    TCM_R_OTA
};
enum {
    TCM_BUSY = 0,  // it waits for the answer
    TCM_DONE = 1,  // the answer came (a capture: the frame is in tcm_frame)
    TCM_FAIL = -1  // tcm_req_code() has the TC_E_* code (TC_E_TIMEOUT: no answer)
};
bool tcm_info(int i);
bool tcm_config(int i, const tc_wire_t *w, int n);
bool tcm_watch(int i, int pin, bool on);
bool tcm_pulse(int i, int pin, int ms);
bool tcm_capture(int i, int pin, int secs, int gap_ms);
bool tcm_capture_ready(int i);  // the companion listens now (show "press a key of the remote")
// Send level times (> 0 = on). carrier_khz: an IR LED (0: the wiring value); rep_ms > 0: while tcm_hold() comes, the
// companion sends again each rep_ms the level times from d[rep_at] (rep_at = n or 0: all of them).
bool tcm_send(int i, int pin, int carrier_khz, const int32_t *d, int n, int rep_at, int rep_ms);
void tcm_hold(int i);  // call it each 100 ms while the button is held
// A switch output (a relay, an LED) on (1) or off (0); ms > 0: a "press" (back to the old value after ms)
bool tcm_set(int i, int pin, int value, int ms);
// THE UPDATE BY RADIO: send a firmware image (img stays in memory until the request ends; tc_img_info checks it
// first). The companion starts again with it at the end (TCM_DONE). tcm_ota_done() = the bytes that the companion has.
bool tcm_ota(int i, const uint8_t *img, int n, int fw);
int tcm_ota_done(int i);
// The last value of a switch or analog module (TC_STATE): *v (a switch: 1 = active; analog: mV), *cnt (the count of
// the activations of a switch input). False: no value yet (or the companion is offline).
bool tcm_value(int i, int pin, int *v, int *cnt);
void tcm_stop(int i);
int tcm_req(int i);       // TCM_BUSY / TCM_DONE / TCM_FAIL (TCM_DONE when no request runs)
int tcm_req_type(int i);  // TCM_R_*
int tcm_req_code(int i);  // TC_E_* of a failed request
// the answer of TCM_R_CONFIG (also of a failed one): the result of each pin; true = all pins OK
bool tcm_cfg_result(int i, uint8_t *err, uint8_t *warn, int *n);
// the frame of TCM_R_CAPTURE: the level times (> 0 a mark), the receiver pin, the gap that ended it (ms)
int tcm_frame(int32_t *d, int max, int *pin);
