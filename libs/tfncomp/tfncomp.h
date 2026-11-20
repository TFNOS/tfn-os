#pragma once
// TFNCOMP: the protocol of a TFN COMPANION (a small ESP32 with no screen: the "hands" of a TFN OS board). Pure C: the
// companion firmware (companion/), the master side on a TFN OS board and the PC tests compile the same code.
//
// THE FRAME (one ESP-NOW packet, 250 bytes at most):
//   'C' 'P' version type seq(2) flags ch | the body | [a counter (4) and a SipHash-2-4 tag (8)]
//   ch = the WiFi channel of the sender (bit 7: the sender is on a WiFi network, so its channel does not change).
//   A frame with TC_FL_TAG has the counter and the tag: tag = SipHash(the pair key, the MAC of the sender || all the
//   bytes before the tag). Only HELLO and PAIR_REQ go with no tag. The receiver takes a tagged frame only when its
//   counter is higher than the last one from that sender.
// THE LEVEL TIMES go as 16-bit values: bit 15 = a MARK (the carrier is on / the line is active), bits 0-14 = the time
// in us (1..32767). A longer time goes as more values of the same level.
// A LONG MESSAGE (FRAME, SEND) goes in PARTS of TC_PART_VALS values; each part has the id, its index and the count.
// THE MODULES: each pin of the wiring has a role, and each role has a class (tc_role_class): a pulse receiver (IR, 315,
// 433: WATCH, CAPTURE), a pulse transmitter (IR LED, 315, 433: PULSE, SEND, HOLD), a switch output (a relay, an LED:
// SET), a switch input (a button, a motion sensor, a door contact) or an analog input (a light or soil sensor, a knob).
// The companion tells the values of the switch and analog modules in TC_STATE (at a change, and after each PONG).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TC_MAGIC0    'C'
#define TC_MAGIC1    'P'
#define TC_VERSION   1
#define TC_FRAME_MAX 250
#define TC_HDR       8
#define TC_TAIL      12  // the counter (4) and the tag (8)
#define TC_BODY_MAX  (TC_FRAME_MAX - TC_HDR - TC_TAIL)
#define TC_KEY_LEN   16
#define TC_PUB_LEN   32

#define TC_CH_WIFI 0x80  // ch: the sender is on a WiFi network

// flags
#define TC_FL_TAG    0x01  // the counter and the tag are at the end
#define TC_FL_PAIRED 0x02  // HELLO: the companion has a master
#define TC_FL_BOOT   0x04  // HELLO: a pairing waits for the BOOT key of the companion

// the frame types
enum {
    TC_HELLO = 1,  // companion, no tag: chip(1) rev(1) fw(2) paired(1) [the MAC of its master(6)]
    TC_PAIR_REQ,   // master, no tag: pub(32) the name of the master (0..16 bytes)
    TC_PAIR_OK,    // companion, tag with the new key: pub(32)
    TC_INFO_REQ,   // master: (no body)
    TC_INFO,       // companion: tc_info_t (tc_info_put / tc_info_get)
    TC_CONFIG,     // master: n(1), n x tc_wire_t (4 bytes); the answer: TC_CFG_RES
    TC_CFG_RES,    // companion: ok(1) n(1) n x (err(1) warn(1))
    TC_WATCH,      // master: pin(1) on(1): the live test of a receiver pin (TC_EDGES 5 times a second, 60 s at most)
    TC_EDGES,      // companion: pin(1) level(1) edges(4) n(1) n x value(2): the last level times
    TC_PULSE,      // master: pin(1) ms(2): the test of a transmitter pin (an IR LED: the carrier)
    TC_CAPTURE,  // master: pin(1) seconds(1) gap_ms(1): wait for one frame; the answer: TC_FRAME, or TC_NAK TC_E_TIMEOUT
    TC_FRAME,    // companion: a part (tc_part_t) of the level times of the captured frame
    TC_SEND,     // master: a part (tc_part_t) of the frame to send
    TC_HOLD,     // master: id(1): the keep-alive of a held button (the companion stops TC_HOLD_MS after the last one)
    TC_STOP,     // master: id(1): stop the repeat at once
    TC_ACK,      // both: seq(2) code(1) arg(4)
    TC_NAK,      // both: seq(2) code(1) arg(4)
    TC_PING,     // master: (no body); the answer: TC_PONG
    TC_PONG,     // companion: uptime_s(4) the wiring hash(4)
    TC_FORGET,   // master: the companion forgets its master (and its wiring stays)
    TC_SET,      // master: pin(1) value(2) ms(2): a switch output on (1) or off (0); ms > 0: back to the old value
                 // after ms (a "press" of a gate key). The answer: TC_ACK (arg = the value now), then a TC_STATE
    TC_STATE,    // companion: n(1) n x tc_state_t (5 bytes): the values of its switch and analog modules
    // THE UPDATE BY RADIO (a companion with firmware 3 or later): BEGIN, the DATA in order, END. Each DATA gets an ACK
    // with arg = the next offset that the companion wants (the master sends a window of frames and goes back to that
    // offset when the ACKs stop). END: the companion checks the size, the CRC and the image, then it starts again.
    TC_OTA_BEGIN,  // master: size(4) crc32(4) fw(2); the answer: TC_ACK (arg 0) or TC_NAK TC_E_OTA
    TC_OTA_DATA,   // master: offset(4) n(1) n bytes (TC_OTA_CHUNK at most)
    TC_OTA_END,  // master: (no body); the answer: TC_ACK (arg = TC_OTA_DONE), then the companion starts the new firmware
    TC_TYPE_N
};

// the codes of ACK / NAK and of each pin of TC_CFG_RES
enum {
    TC_E_OK = 0,
    TC_E_NO_PIN,     // the chip has no such GPIO
    TC_E_FLASH,      // the pin belongs to the flash or the PSRAM
    TC_E_IN_ONLY,    // a transmitter on an input-only pin
    TC_E_DUP,        // one pin with two roles
    TC_E_ROLE,       // an unknown role
    TC_E_BUSY,       // no free RMT channel, or a capture or a send runs
    TC_E_USB,        // the pin belongs to the USB of the chip
    TC_E_CONSOLE,    // the pin belongs to the console UART
    TC_E_NOT_WIRED,  // the pin has no such role in the wiring
    TC_E_TIMEOUT,    // CAPTURE: no frame came
    TC_E_COUNTER,    // the counter is not higher than the last one: arg = the last one (the master goes on from there)
    TC_E_BAD,        // a bad body
    TC_E_PARTS,      // SEND: a part is missing (arg = the bit mask of the parts that came)
    TC_E_SEND,       // SEND: the RMT did not send
    TC_E_NO_ADC,     // an analog input on a pin with no ADC (the ESP32: ADC1 only, because the radio uses ADC2)
    TC_E_OTA,        // the update by radio failed (arg: TC_OTA_F_*)
    TC_E_N
};
// the warnings of TC_CFG_RES
#define TC_W_STRAP 0x01  // a strapping pin: a module that pulls it at the start can stop the boot or the flash
#define TC_W_LED   0x02  // the LED of the board is on this pin (the LED stays off)
#define TC_W_PULL  0x04  // an input that needs a pull-up on a pin with no internal pull-up: add a resistor (10 k)

// the roles of a pin
enum {
    TC_R_NONE = 0,
    TC_R_IR_RX,     // an IR receiver module (VS1838B, TSOP38238): the output is LOW while it sees the carrier
    TC_R_IR_TX,     // an IR LED (or an IR LED module): HIGH = the LED is on; the RMT makes the carrier
    TC_R_RF315_RX,  // a 315 MHz receiver (the data pin: HIGH = the carrier)
    TC_R_RF315_TX,  // a 315 MHz transmitter (the data pin)
    TC_R_RF433_RX,
    TC_R_RF433_TX,
    TC_R_RELAY,    // a relay module: HIGH = on (INVERT: the boards with LOW = on). It starts OFF.
    TC_R_OUTPUT,   // a switch output: an LED, a buzzer, a MOSFET (HIGH = on)
    TC_R_BUTTON,   // a button or a switch to GND (the internal pull-up): pressed = LOW = active
    TC_R_MOTION,   // a motion sensor (PIR: HIGH = motion)
    TC_R_CONTACT,  // a door or window contact (a reed switch to GND, the pull-up): closed = LOW = active
    TC_R_ANALOG,   // an analog sensor (light, soil, a knob): the value in mV (0..3300)
    TC_R_N
};
// the classes of the roles
enum {
    TC_C_NONE,
    TC_C_PULSE_RX,
    TC_C_PULSE_TX,
    TC_C_OUT,
    TC_C_IN,
    TC_C_ADC
};
int tc_role_class(int role);
bool tc_role_rx(int role);  // TC_C_PULSE_RX
bool tc_role_tx(int role);  // TC_C_PULSE_TX
// true: a switch input with the internal pull-up (BUTTON, CONTACT: active = LOW); MOTION: active = HIGH, no pull
// (TC_WF_INVERT turns the active level)
bool tc_role_pullup(int role);
bool tc_pin_adc(int chip, int gpio);  // the pin has an ADC that works with the radio on
const char *tc_role_name(int role);   // "IR RECEIVER", ...
const char *tc_err_name(int e);       // a short text in English

// tc_wire_t.flags
#define TC_WF_INVERT 0x01  // the module works the other way round (a receiver: HIGH = the carrier; a LED: LOW = on)

#define TC_WIRE_MAX 8
typedef struct {
    uint8_t pin;
    uint8_t role;
    uint8_t flags;
    uint8_t carrier_khz;  // an IR LED: the default carrier when a SEND gives none (0 = 38)
} tc_wire_t;

// THE PINS OF A CHIP (tc_pin_flags)
enum {
    TC_CHIP_ESP32 = 1,
    TC_CHIP_S3,
    TC_CHIP_C3,
    TC_CHIP_C6,
    TC_CHIP_S2,
    TC_CHIP_N
};
#define TC_GPIO_MAX  49
#define TC_P_OK      0x01  // a GPIO that the companion may use
#define TC_P_IN_ONLY 0x02
#define TC_P_STRAP   0x04
#define TC_P_FLASH   0x08  // the flash or the PSRAM (never)
#define TC_P_USB     0x10  // the USB of the chip (never)
#define TC_P_CONSOLE 0x20  // the console UART (never)
#define TC_P_LED     0x40  // the LED of the usual board of this chip
#define TC_P_WARN    0x80  // a pin with a common use (the PSRAM of a WROVER module, the touch of a module ...)
const char *tc_chip_name(int chip);
int tc_chip_gpios(int chip);  // the count of GPIO numbers (the highest + 1)
uint8_t tc_pin_flags(int chip, int gpio);
// The check of one wiring for a chip: err[i] / warn[i] for each entry. Returns true when all entries are OK.
bool tc_wire_check(int chip, const tc_wire_t *w, int n, uint8_t *err, uint8_t *warn);
uint32_t tc_wire_hash(const tc_wire_t *w, int n);

// THE INFO OF A COMPANION
typedef struct {
    uint8_t chip, rev;
    uint16_t fw;
    uint8_t ngpio;
    uint8_t pins[TC_GPIO_MAX];  // TC_P_* for each GPIO of the chip
    uint8_t led;                // the GPIO of the LED of the board (0xFF: none)
    uint8_t nwire;
    tc_wire_t wire[TC_WIRE_MAX];
} tc_info_t;

// ---------------------------------------------------------------- a frame to send
typedef struct {
    uint8_t b[TC_FRAME_MAX];
    int n;
    bool bad;  // a put went past the end
} tc_buf_t;
void tc_begin(tc_buf_t *f, int type, uint16_t seq, uint8_t flags, uint8_t ch);
void tc_put8(tc_buf_t *f, uint32_t v);
void tc_put16(tc_buf_t *f, uint32_t v);
void tc_put32(tc_buf_t *f, uint32_t v);
void tc_putn(tc_buf_t *f, const void *p, int n);
// The counter and the tag (and TC_FL_TAG); src = the MAC of this board. False: no room.
bool tc_seal(tc_buf_t *f, const uint8_t key[TC_KEY_LEN], const uint8_t src[6], uint32_t counter);
int tc_room(const tc_buf_t *f);  // the bytes that a body can still take (with the room of the tag)

// ---------------------------------------------------------------- a received frame
typedef struct {
    uint8_t type, flags, ch;
    uint16_t seq;
    const uint8_t *body;
    int blen;
    uint32_t counter;  // TC_FL_TAG only
    // the reader of the body
    int at;
    bool bad;  // a get went past the end
} tc_msg_t;
// The header (and the place of the body). False: not a frame of this protocol.
bool tc_parse(const uint8_t *d, int n, tc_msg_t *m);
// The tag of a tagged frame (src = the MAC of the sender)
bool tc_check(const uint8_t *d, int n, const uint8_t key[TC_KEY_LEN], const uint8_t src[6]);
uint32_t tc_get8(tc_msg_t *m);
uint32_t tc_get16(tc_msg_t *m);
uint32_t tc_get32(tc_msg_t *m);
bool tc_getn(tc_msg_t *m, void *p, int n);
int tc_left(const tc_msg_t *m);

// ---------------------------------------------------------------- the bodies
void tc_info_put(tc_buf_t *f, const tc_info_t *in);
bool tc_info_get(tc_msg_t *m, tc_info_t *out);

// one part of a FRAME / SEND
#define TC_PART_VALS 100
#define TC_VALS_MAX  800  // the values of one message at most (8 parts)
typedef struct {
    uint8_t id;  // the message (a new id for each new FRAME / SEND)
    uint8_t part, nparts;
    uint16_t total;   // the values of the whole message
    uint8_t pin;      // the pin (FRAME: the receiver; SEND: the transmitter)
    uint8_t carrier;  // SEND: kHz (0: no carrier: a RF transmitter); FRAME: the gap that ended it (ms)
    uint16_t rep_ms;  // SEND: the time from the start of one send to the next while the HOLD comes (0: one time)
    uint16_t rep_at;  // SEND: the repeat sends the values from here (a NEC repeat frame); = total: all of them
    uint8_t n;        // the values in this part
    uint16_t v[TC_PART_VALS];
} tc_part_t;
void tc_part_put(tc_buf_t *f, const tc_part_t *p);
bool tc_part_get(tc_msg_t *m, tc_part_t *p);
// Cut the values of a message into its parts: p[] gets nparts parts (the fields of p[0] go to all). Returns nparts.
int tc_parts_make(const tc_part_t *head, const uint16_t *v, int n, tc_part_t *p, int max_parts);

// the assembly of the parts of a message
typedef struct {
    uint8_t id;
    bool on;
    uint8_t nparts;
    uint16_t total;
    uint8_t got;     // bit i: part i came
    tc_part_t head;  // the fields of the first part that came
    uint16_t v[TC_VALS_MAX];
} tc_asm_t;
// Returns 1 when the message is complete, 0 when parts are missing, -1 for a bad part
int tc_asm_add(tc_asm_t *a, const tc_part_t *p);

// the reasons of TC_E_OTA
enum {
    TC_OTA_F_NONE,
    TC_OTA_F_NO,     // this firmware cannot update by radio (no second app slot)
    TC_OTA_F_BEGIN,  // the slot did not open (too big)
    TC_OTA_F_WRITE,  // a write to the flash failed
    TC_OTA_F_SIZE,   // END before all the bytes came
    TC_OTA_F_CRC,    // the CRC-32 is not the same
    TC_OTA_F_IMAGE,  // the image is not a good firmware for this chip
    TC_OTA_F_STATE   // a DATA or an END with no BEGIN
};
#define TC_OTA_CHUNK 200
#define TC_OTA_DONE  0xFFFFFFFFu  // the arg of the ACK of TC_OTA_END (a DATA ACK has an offset)
uint32_t tc_crc32(uint32_t crc, const uint8_t *p, int n);  // start with 0
// The facts of a companion firmware file (an ESP-IDF app image): its chip (TC_CHIP_*) and its version (the number of
// the version string). False: not an app image.
bool tc_img_info(const uint8_t *img, int n, int *chip, int *fw);

// the value of a switch or analog module (TC_STATE)
typedef struct {
    uint8_t pin;
    uint16_t v;    // a switch: 1 = active (on, pressed, motion, closed), 0 = not; an analog input: mV
    uint16_t cnt;  // a switch input: the count of its activations (mod 65536: a short press between frames counts)
} tc_state_t;
void tc_states_put(tc_buf_t *f, const tc_state_t *st, int n);
int tc_states_get(tc_msg_t *m, tc_state_t *st, int max);

// ---------------------------------------------------------------- the level times
// > 0 a mark, < 0 a space (us) -> the 16-bit values. Returns the count of values (at most max).
int tc_vals_from_levels(const int32_t *d, int n, uint16_t *v, int max);
// The other way: the values of one level are one time again. Returns the count of level times.
int tc_levels_from_vals(const uint16_t *v, int n, int32_t *d, int max);

// ---------------------------------------------------------------- the keys
uint64_t tc_siphash(const uint8_t key[16], const uint8_t *a, size_t an, const uint8_t *b, size_t bn);
// X25519 (RFC 7748): out = k * u. Pure C (about 30 ms on an ESP32).
void tc_x25519(uint8_t out[32], const uint8_t k[32], const uint8_t u[32]);
void tc_x25519_base(uint8_t pub[32], const uint8_t priv[32]);
// The pair key from the shared X25519 value and the two public keys (the master first)
void tc_pair_key(uint8_t key[TC_KEY_LEN], const uint8_t shared[32], const uint8_t pub_master[32],
                 const uint8_t pub_comp[32]);
