#pragma once
// IRDECODE: the decoders of infrared remote controls, in the style of the Arduino IRremote library. Pure C:
// no pins, no screen (the PC tests use the same code). The input: the level times of an IR receiver module (a
// TSOP38238 or a VS1838B: it removes the carrier) as the rfpulse contract gives them: > 0 = a MARK (the carrier is on),
// < 0 = a SPACE, in us.
//   THE SLICER     cuts the stream into FRAMES: a frame starts with a mark and ends at a space of IRD_GAP_US or more
//                  (the RECORD_GAP_MICROS of IRremote); a glitch shorter than the filter joins its neighbours
//   THE DECODERS   NEC (and ONKYO, APPLE, the NEC repeat), SAMSUNG, SAMSUNG48, LG, SONY (12, 15, 20 bits), RC5, RC6,
//                  PANASONIC and the other KASEIKYO vendors, JVC, DENON, SHARP, BOSEWAVE, WHYNTER; then the universal
//                  decoder (PULSE_DISTANCE, PULSE_WIDTH: the timing of the frame in two lengths); then UNKNOWN with the
//                  hash of IRremote (decodeHash) and the raw times
//   THE ENCODERS   the other way: a code to the level times of one frame (the IR LED, the demo, the tests)
#include <stdbool.h>
#include <stdint.h>

#define IRD_MAX_DUR  600  // the most level times of one frame (an air conditioner sends up to about 300 bits)
#define IRD_MAX_BITS 256
#define IRD_GAP_US   8000  // a space this long ends a frame

enum {
    IRD_P_UNKNOWN,  // no decoder fits: the hash of the times
    IRD_P_NEC,
    IRD_P_ONKYO,
    IRD_P_APPLE,
    IRD_P_SAMSUNG,
    IRD_P_SAMSUNG48,
    IRD_P_LG,
    IRD_P_SONY,
    IRD_P_RC5,
    IRD_P_RC6,
    IRD_P_PANASONIC,
    IRD_P_KASEIKYO,
    IRD_P_DENON_K,
    IRD_P_SHARP_K,
    IRD_P_JVC_K,
    IRD_P_MITSUBISHI_K,
    IRD_P_JVC,
    IRD_P_DENON,
    IRD_P_SHARP,
    IRD_P_BOSEWAVE,
    IRD_P_WHYNTER,
    IRD_P_PULSE_DISTANCE,  // the universal decoder: one mark length, two space lengths
    IRD_P_PULSE_WIDTH,     // the universal decoder: two mark lengths (and one or two space lengths)
    IRD_P_N
};

// ird_code_t.flags
#define IRD_F_REPEAT                                                                                                   \
    0x01                   // a repeat frame (NEC 9 ms + 2.25 ms, SAMSUNG, a JVC frame with no header, the
                           // second DENON frame with the inverted command)
#define IRD_F_TOGGLE 0x02  // RC5 / RC6: the toggle bit is set
#define IRD_F_PARITY 0x04  // a check of the protocol failed (the code is still shown)
#define IRD_F_MSB    0x08  // the protocol sends the most significant bit first (RC5, RC6, LG, DENON, WHYNTER)
#define IRD_F_HEADER 0x10  // the universal decoder: the frame has a header (hdr_mark, hdr_space)
#define IRD_F_STOP   0x20  // the universal decoder: the frame ends with a stop mark

// One level time of a frame: > 0 a mark, < 0 a space (us)
typedef struct {
    int32_t d[IRD_MAX_DUR];  // the first one is a mark, the last one is a mark (the gap after it is not here)
    int n;
    int32_t gap_before;  // the space before the frame (us; IRD_GAP_US or more)
    int64_t t_us;        // the time of its end
} ird_frame_t;

typedef struct {
    uint8_t proto;     // IRD_P_*
    uint8_t flags;     // IRD_F_*
    uint16_t nbits;    // the data bits (no header, no stop mark)
    uint32_t address;  // the fields of the protocol (0 for the universal decoder and UNKNOWN)
    uint32_t command;
    uint32_t extra;                  // KASEIKYO: the vendor id; DENON / SHARP: the two frame bits
    uint8_t bits[IRD_MAX_BITS / 8];  // the data bits in the order on the air: the first bit = the top bit of bits[0]
    // the timing (us): the nominal times of a known protocol, or the times of the frame (the universal decoder)
    uint16_t hdr_mark, hdr_space;  // 0: no header
    uint16_t one_mark, one_space, zero_mark, zero_space;
    uint32_t carrier_hz;  // the carrier of the protocol (the IR LED): 36 / 37 / 38 / 40 kHz
    uint32_t hash;        // the hash of the times (IRremote decodeHash: FNV-1 of the compare of each time with
                          // the time two places after it); for each frame, also a known one
    uint16_t nraw;        // the level times of the frame
} ird_code_t;

// ---------------------------------------------------------------- the slicer
#define IRD_QUEUE 6
typedef struct {
    int min_pulse;  // a level shorter than this is a glitch (us)
    bool in;        // a gap came: the next mark starts a frame
    bool has_prev;
    int32_t prev;  // the level time that waits (the next one can still join it)
    ird_frame_t cur;
    ird_frame_t q[IRD_QUEUE];
    int qn, qr;
    uint32_t dropped;  // frames lost: the queue was full, or a frame too long
} ird_slicer_t;

void ird_slicer_init(ird_slicer_t *s, int min_pulse_us);
// The next level times; t_us = the time of the end of the last one
void ird_slicer_feed(ird_slicer_t *s, const int32_t *d, int n, int64_t t_us);
// No edge for quiet_us and the line is a space: a frame in progress ends
void ird_slicer_idle(ird_slicer_t *s, int32_t quiet_us, int64_t t_us);
bool ird_slicer_pop(ird_slicer_t *s, ird_frame_t *out);

// ---------------------------------------------------------------- the decoders
// One frame: a known protocol, else the universal decoder, else UNKNOWN. raw_only: always UNKNOWN (the hash only).
// Returns false only for a frame with less than 3 level times (no code).
bool ird_decode(const int32_t *d, int n, bool raw_only, ird_code_t *c);
uint32_t ird_hash(const int32_t *d, int n);

// ---------------------------------------------------------------- the text of a code
const char *ird_name(int proto);
int ird_bit(const ird_code_t *c, int i);
// The data as IRremote prints it (decodedRawData): the first bit on the air is bit 0 (the LSB first protocols) or the
// top bit (IRD_F_MSB); a code of more than 64 bits: 64-bit words, the first word first ("0x... 0x...")
void ird_raw_hex(const ird_code_t *c, char *out, int max);
// The bits as 0/1 in the order on the air, in groups of 8
void ird_binary(const ird_code_t *c, char *out, int max);
// The same code: the same protocol, address, command and bits (the repeat flag and the toggle bit do not count)
bool ird_same(const ird_code_t *a, const ird_code_t *b);
// A protocol with an address and a command (not the universal decoder, not UNKNOWN)
bool ird_has_fields(int proto);

// ---------------------------------------------------------------- the encoders (the IR LED, the demo and the tests)
// One frame of the code (no repeat frame, no gap after it). Returns the count of level times (0: UNKNOWN, or no room).
int ird_encode(const ird_code_t *c, int32_t *out, int max);
// One key press as a remote sends it: the frames of the protocol with their gaps (SONY 3 frames, DENON / SHARP the
// frame and the inverted frame, RC5 / RC6 the toggle bit as in c), the spaces between them as level times. Returns the
// count (0: UNKNOWN).
int ird_encode_press(const ird_code_t *c, int32_t *out, int max);
// A code of a protocol from its fields (the demo and the tests): the bits, the timing, the carrier
void ird_make(int proto, uint32_t address, uint32_t command, uint32_t extra, int nbits, ird_code_t *c);
