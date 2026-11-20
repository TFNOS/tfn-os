#pragma once
// THE COMPANION CORE: the logic of a TFN COMPANION with no hardware (pure C). comp_hw.c gives the radio, the pins, the
// NVS and the clock on the chip; a PC test gives fakes.
#include <stdbool.h>
#include <stdint.h>
#include "tfncomp/tfncomp.h"

// the firmware version (INFO, HELLO, the app image; the CMake file reads this line: keep it one line). 2: the switch
// modules; 3: the update by radio; 4: the noise filter of the RF receivers
#define CC_FW 4

// the state that stays over a restart (NVS)
typedef struct {
    uint8_t magic;  // CC_SAVED_MAGIC
    uint8_t paired;
    uint8_t master[6];
    uint8_t key[TC_KEY_LEN];
    uint8_t master_pub[32];
    uint8_t my_priv[32];
    uint32_t rx_hi;  // the counters of the master up to this one can be old: after a restart the floor starts here
    uint32_t tx_hi;  // the counter of this companion starts here after a restart
    uint8_t home_ch;
    uint8_t nwire;
    tc_wire_t wire[TC_WIRE_MAX];
    char mname[17];  // the name of the master
} cc_saved_t;
#define CC_SAVED_MAGIC 0xC1
#define CC_HI_STEP     4096  // the counters jump this much at a save (one NVS write each CC_HI_STEP frames)

typedef struct {
    // the radio: mac = NULL: a broadcast
    void (*send)(const uint8_t *mac, const uint8_t *d, int n);
    void (*set_channel)(int ch);
    uint32_t (*now_ms)(void);
    void (*random)(uint8_t *p, int n);
    void (*save)(const cc_saved_t *s);
    // the pins. inv: receiver: the active (mark) level is LOW (an IR receiver); transmitter: LOW = on.
    // ir: an IR receiver (the hardware can time it exactly; a RF receiver gives noise with no end)
    bool (*rx_open)(int pin, bool inv, bool ir);
    void (*rx_close)(int pin);
    // The level times since the last read (> 0 a mark, < 0 a space); *idle_us = the time since the last edge,
    // *active = the line is at the mark level now. Returns the count.
    int (*rx_read)(int pin, int32_t *d, int max, int32_t *idle_us, bool *active);
    // Send the level times (> 0 = on); carrier_hz 0 = no carrier. Returns 0 when sent. It can take up to ~200 ms.
    int (*tx_send)(int pin, uint32_t carrier_hz, bool inv, const int32_t *d, int n);
    void (*tx_idle)(int pin, bool inv);  // the pin of a transmitter: an output at the "off" level
    // the switch and analog modules (NULL: the chip has none of them)
    void (*out_set)(int pin, bool level);   // an output pin (opened by tx_idle) to a level
    void (*in_open)(int pin, bool pullup);  // an input pin
    int (*in_read)(int pin);                // its level (0 / 1)
    int (*adc_read)(int pin);               // an analog input: mV (-1: no ADC)
    void (*pin_free)(int pin);              // the pin is not in the wiring any more
    // the update by radio (NULL: none): begin = 0 when the other slot is open; write = 0 when written; end = 0 when
    // the image is good and the next start uses it; valid() = the firmware works (it reached its master: no rollback)
    int (*ota_begin)(uint32_t size);
    int (*ota_write)(const uint8_t *d, int n);
    int (*ota_end)(void);
    void (*ota_abort)(void);
    void (*ota_valid)(void);
    void (*restart)(void);
    void (*led)(bool on);           // NULL: no LED
    void (*log)(const char *line);  // NULL: no log
} cc_io_t;

void cc_init(const cc_io_t *io, const cc_saved_t *saved, const tc_info_t *chip, const uint8_t my_mac[6]);
// One received ESP-NOW frame
void cc_rx(const uint8_t mac[6], const uint8_t *d, int n);
// Each 10 ms (or more often)
void cc_tick(void);
// The BOOT key: a press (the OK of a pairing), a long hold (forget the master)
void cc_boot_press(void);
void cc_boot_hold(void);

// The switch and analog modules: their values now (the console "mods", the tests). Returns the count.
int cc_states(tc_state_t *st, int max);
// A switch output as a SET of the master (the console "set"): false = not a switch output of the wiring
bool cc_set(int pin, int value, int ms);

// The state for the console and the tests
typedef struct {
    bool paired, searching, pair_wait, capturing, repeating;
    int ch;
    uint32_t rx_frames, rx_bad_tag, rx_old, tx_frames, sends, captures;
    uint32_t last_master_ms;
} cc_state_t;
void cc_state(cc_state_t *s);
const cc_saved_t *cc_saved(void);
// The console: a local wiring (as a CONFIG of the master). Returns true when all pins are OK.
bool cc_wire_set(const tc_wire_t *w, int n, uint8_t *err, uint8_t *warn);
void cc_forget(void);
