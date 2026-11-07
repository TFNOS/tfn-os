#pragma once
// SERIAL MONITOR: the port (ser_port.c). Two sources with one interface:
//   SER_PINS  UART1 of the board on the pins of rfp_board_uart() (the ESP-IDF UART driver: ABI rev 14)
//   SER_DEMO  a demo device in software: an ESP32 board that prints at 115200 8N1 and answers commands. The demo
//             makes the real bits of each byte and samples them with the settings of the app: a wrong baud gives
//             garbage and frame errors, as on a real line (AUTO finds 115200).
// Call all functions from the app task. The memory: the state is in ser_port.c (file statics and the UART driver).
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int baud;
    uint8_t bits;  // 5 .. 8
    char parity;   // 'N', 'E', 'O'
    uint8_t stop;  // 10 = 1, 15 = 1.5, 20 = 2 stop bits
    bool inv;      // inverted levels (RX and TX)
} ser_cfg_t;

typedef struct {
    uint32_t rx, tx;         // bytes
    uint32_t frame, parity;  // the errors of the receiver
    uint32_t overflow;       // the bytes that the app did not take in time (the buffer was full)
    uint32_t brk;            // the breaks (the line low for a whole frame or more)
    int level;               // the RX pin now: 1 high, 0 low, -1 not known (the demo)
} ser_stats_t;

enum {
    SER_PINS,
    SER_DEMO
};
enum {
    SER_OK = 0,
    SER_ENOPINS = -1,
    SER_EDRIVER = -2,
    SER_ECONFIG = -3,
    SER_EMEM = -4
};

int ser_open(int src, const ser_cfg_t *c);
void ser_close(void);
bool ser_is_open(void);
int ser_source(void);
int ser_config(const ser_cfg_t *c);  // a new baud or format while the port is open
int ser_read(uint8_t *b, int max);   // the bytes that came (no wait)
int ser_write(const void *b, int n);
void ser_stats(ser_stats_t *s);
// THE SELF TEST (SER_PINS): the TX of the UART goes to its RX inside the chip; a test text at the settings now must
// come back the same. 1 = the same, 0 = not the same (msg: what came), < 0 = an error. The TX pin sends the text too.
int ser_selftest(char *msg, int max);
const char *ser_err(int e);
bool ser_has_pins(void);
