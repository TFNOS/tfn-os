#pragma once
// RFDECODE: the decoders of OOK / ASK / FSK data. Pure C: no pins, no screen (the PC tests use the same
// code). The input: the level times of a receiver (rfpulse.h: > 0 high, < 0 low, in us).
//   THE SLICER     cuts the stream into PACKETS: a packet starts after a long low (a sync or a reset gap) and ends at
//                  the next long low; short glitches join their neighbours; the noise of a receiver (no long low) makes
//                  no packet
//   RCSWITCH       the 12 protocols of the RCSwitch library (the timing of the PT2262 / EV1527 family and others): a
//                  protocol, a value, a bit count, a pulse length
//   GENERIC        the timing of the packet in clusters, then the coding that fits best: PWM (the width of a pulse),
//                  PPM (the width of a gap), MANCHESTER, NRZ (a bit time)
//   THE ENCODERS   the other way (the demo receiver and the tests)
#include <stdbool.h>
#include <stdint.h>

#define RFD_MAX_DUR  600  // the most level times of one packet
#define RFD_MAX_BITS 128

enum {
    RFD_K_NONE,
    RFD_K_RCSWITCH,
    RFD_K_PWM,
    RFD_K_PPM,
    RFD_K_MANCH,
    RFD_K_NRZ,
    RFD_K_RAW,
    RFD_K_N
};
// The decoder modes (rfd_decode)
enum {
    RFD_MODE_AUTO,
    RFD_MODE_RCSWITCH,
    RFD_MODE_GENERIC,
    RFD_MODE_RAW
};

typedef struct {
    int32_t d[RFD_MAX_DUR];  // the level times, the first one high
    int n;
    int32_t gap_before;  // the low before the packet (us; 0 = not known)
    int32_t gap_after;   // the low after it
    int64_t t_us;        // the time of its end
} rfd_packet_t;

typedef struct {
    uint8_t kind;   // RFD_K_*
    uint8_t proto;  // RCSWITCH: the protocol 1..12
    uint16_t nbits;
    uint8_t bits[RFD_MAX_BITS / 8];  // the first bit = the top bit of bits[0]
    uint16_t t_short, t_long;        // the two times of the coding (us): RCSWITCH: the pulse length and 0
    uint32_t t_gap;                  // the sync / reset gap (us)
    float score;                     // 0..1: how well the times fit the coding
    uint8_t nclust_h, nclust_l;      // RAW: the clusters of the high and the low times
    uint16_t ch[4], cl[4];           // RAW: their middles (us)
} rfd_code_t;

// ---------------------------------------------------------------- the slicer
#define RFD_QUEUE 8
typedef struct {
    int min_pulse;  // a level shorter than this is a glitch (us)
    int gap;        // a low at least this long ends a packet (us)
    bool in;        // in a packet
    bool has_prev;
    int32_t prev;  // the level time that waits (the next one can still join it)
    rfd_packet_t cur;
    rfd_packet_t q[RFD_QUEUE];
    int qn, qr;
    uint32_t dropped;  // packets lost: the queue was full, or a packet too long
} rfd_slicer_t;

void rfd_slicer_init(rfd_slicer_t *s, int min_pulse_us, int gap_us);
// The next level times; t_us = the time of the end of the last one (for the time of a packet)
void rfd_slicer_feed(rfd_slicer_t *s, const int32_t *d, int n, int64_t t_us);
// The line is low with no edge for quiet_us: a packet in progress ends (its gap_after = quiet_us)
void rfd_slicer_idle(rfd_slicer_t *s, int32_t quiet_us, int64_t t_us);
bool rfd_slicer_pop(rfd_slicer_t *s, rfd_packet_t *out);

// ---------------------------------------------------------------- the decoders
bool rfd_rcswitch(const rfd_packet_t *p, rfd_code_t *c);
// force: RFD_K_NONE = the coding that fits best; else RFD_K_PWM, RFD_K_PPM, RFD_K_MANCH or RFD_K_NRZ
bool rfd_generic(const rfd_packet_t *p, int force, rfd_code_t *c);
// The clusters of the times only (no bits)
void rfd_raw(const rfd_packet_t *p, rfd_code_t *c);
// One packet in a mode (RFD_MODE_*); coding: as force of rfd_generic. Returns true for a code (RAW: always).
bool rfd_decode(const rfd_packet_t *p, int mode, int coding, rfd_code_t *c);

// ---------------------------------------------------------------- the text of a code
const char *rfd_kind_name(int kind);
int rfd_bit(const rfd_code_t *c, int i);
// The bits as hex (the first bits fill the top of the first digit when nbits is not a multiple of 4: the value)
void rfd_hex(const rfd_code_t *c, char *out, int max);
// The value as a decimal number (only when nbits <= 64; else "")
void rfd_dec(const rfd_code_t *c, char *out, int max);
// The bits as 0/1 in groups of 4 (at most max - 1 characters)
void rfd_binary(const rfd_code_t *c, char *out, int max);
// The tri-state code of the PT2262 family (each bit pair: 00 = 0, 11 = 1, 01 = F); false when a pair is 10
bool rfd_tristate(const rfd_code_t *c, char *out, int max);
// The same code (the same kind, protocol and bits)
bool rfd_same(const rfd_code_t *a, const rfd_code_t *b);

// ---------------------------------------------------------------- the encoders (the demo and the tests)
// One repeat of the code with its sync (RCSWITCH) or its reset gap (the others). Returns the count of level times.
int rfd_enc_rcswitch(int proto, uint64_t value, int nbits, int pulse_us, int32_t *out, int max);
int rfd_enc_pwm(const uint8_t *bits, int nbits, int t_short, int t_long, int gap_us, int32_t *out, int max);
int rfd_enc_ppm(const uint8_t *bits, int nbits, int pulse, int g_short, int g_long, int gap_us, int32_t *out, int max);
int rfd_enc_manch(const uint8_t *bits, int nbits, int t_half, int gap_us, int32_t *out, int max);
