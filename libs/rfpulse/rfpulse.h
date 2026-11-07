#pragma once
// RFPULSE: THE CONTRACT OF A PULSE RECEIVER. A pulse receiver is a radio module that demodulates by itself
// and gives the data as a logic level on a pin: a super-regenerative or superheterodyne 315 / 433 MHz receiver (OOK and
// ASK: MX-05, RXB6, ...), or later a transceiver chip with its data pin (CC1101 in its asynchronous mode: also FSK).
// The data is a stream of level times: each value is the time of one level in microseconds, > 0 = high, < 0 = low.
// The user of a receiver (the RF DECODER app, libs/rfdecode) knows only this header. A new receiver = one new file
// with one rfp_src_t and one line in rfp_enum() (rfpulse.c). The wiring of each board is in rfp_board.h.
// (The SDR contract of libs/rfsrc is a different job: a spectrum. One chip, for example a CC1101, can give both.)
#include <stdbool.h>
#include <stdint.h>

// caps.mods: the modulations that the receiver can demodulate
#define RFP_MOD_OOK 0x01
#define RFP_MOD_ASK 0x02
#define RFP_MOD_FSK 0x04
// caps.flags
#define RFP_F_DEMO  0x01  // made-up data (not a radio)
#define RFP_F_WIRED 0x02  // a module on the pins of the board (the app shows the wiring first)
#define RFP_F_IR    0x04  // an infrared receiver: > 0 = a mark (the carrier is on), < 0 = a space

#define RFP_MAX_FREQ 4

typedef struct {
    const char *id;       // a short fixed name ("gpio", "demo")
    const char *name[2];  // English, Hebrew
    uint8_t mods;         // RFP_MOD_*
    uint8_t flags;        // RFP_F_*
    uint8_t nfreq;        // the frequencies that the user can choose (the module is made for one of them)
    uint32_t freq_khz[RFP_MAX_FREQ];
} rfp_caps_t;

// The state of the line now (rfp_stats_t.line)
enum {
    RFP_LINE_QUIET = 0,  // no edge, the line low: nothing on the pin, or a module with no power
    RFP_LINE_HIGH = 1,   // no edge, the line high: check the wires (DATA on VCC?)
    RFP_LINE_ALIVE = 2,  // edges: a receiver gives its noise or data
};

typedef struct {
    uint32_t edges;        // all edges since the open
    uint32_t lost;         // the values that the buffer lost (the reader was late)
    int level;             // the level of the line now
    int64_t last_edge_us;  // the time of the last edge (the clock of esp_timer_get_time)
    int64_t now_us;
    int line;  // RFP_LINE_* (from the last 0.5 s)
} rfp_stats_t;

typedef struct {
    int (*open)(void *ctx);                         // 0 = ok, < 0 = an error
    int (*read)(void *ctx, int32_t *dur, int max);  // the level times since the last read (at most max)
    void (*stats)(void *ctx, rfp_stats_t *s);
    void (*close)(void *ctx);
    const char *(*error)(int e);
} rfp_ops_t;

typedef struct {
    const rfp_caps_t *caps;
    const rfp_ops_t *ops;
    void *ctx;
} rfp_src_t;

// The receivers of this board: the one on the data pin (when the board has a free pin), then the demo. tests = true:
// also the receivers of the tests. Returns the count.
int rfp_enum(rfp_src_t *out, int max, bool tests);
bool rfp_gpio(rfp_src_t *out);  // rfp_gpio.c: a receiver module on the data pin of this board (rfp_board.h)
bool rfp_demo(rfp_src_t *out);  // rfp_demo.c: made-up remotes and sensors, and the noise of a receiver

// THE INFRARED RECEIVER (the IR DECODER app): an IR receiver module (TSOP38238, VS1838B: it removes the
// 36..40 kHz carrier) on the IR pin of the board (rfp_board_ir()). Its output is LOW while it sees the carrier: this
// receiver gives the levels the other way round (> 0 = a mark), so the same contract applies. The pin has the pull-up
// (the output of the module is high when there is no light). The state of the line: QUIET = no light (or no
// module), HIGH = the mark stays on (the pin stays low: check the wires).
int rfp_enum_ir(rfp_src_t *out, int max);  // the IR receiver on the IR pin (when the board has one), then the demo
bool rfp_ir(rfp_src_t *out);               // rfp_gpio.c
bool rfp_ir_demo(rfp_src_t *out);          // rfp_irdemo.c: made-up remote controls (the encoders of libs/irdecode)
// THE INFRARED LED (rfp_irtx.c): the level times (> 0 = the carrier on, < 0 = off) on the TX pin of rfp_board_ir(),
// with the carrier (carrier_hz, a duty of 33 %), by the RMT peripheral. It waits for the end (at most 3 s).
// 0 = sent; < 0 = an error (rfp_ir_send_error)
int rfp_ir_send(const int32_t *d, int n, uint32_t carrier_hz);
const char *rfp_ir_send_error(int e);
