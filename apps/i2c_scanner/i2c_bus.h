#pragma once
// I2C SCANNER: the bus (i2c_bus.c). One task of the app does all the work on the bus (a probe can wait for a busy
// bus; the app task must not wait): the scans (each address 0x08 .. 0x77), the reads of the ID registers and of the
// register map of one device. The app gives requests and reads the results; no lock (the task writes, the app reads
// with a sequence number). The memory: the state is in i2c_bus.c (file statics); ib_get copies it to the app.
#include <stdbool.h>
#include <stdint.h>
#include "rfpulse/rfp_board.h"

enum {
    IB_IDLE,
    IB_SCAN,
    IB_IDENT,
    IB_REGS,
    IB_FAIL
};
typedef struct {
    int state;          // IB_*: what the task does now
    int err;            // the last fault of the bus (IB_E_*)
    uint32_t scans;     // the scans done since the open
    int64_t t_scan;     // ms: the end of the last scan
    int scan_ms;        // the time of the last scan
    uint8_t found[16];  // the bits of the addresses that answered (bit a = address a)
    // the result of the last IDENT / REGS request (for the address req_addr)
    uint8_t req_addr;
    uint32_t req_done;  // +1 at the end of each request
    uint8_t regs[256];
    uint8_t regs_ok[32];  // the bits of the registers that answered
    int ident;            // the index of the chip of the data base that the ID registers gave (-1 = none)
} ib_state_t;

enum {
    IB_E_OK = 0,
    IB_E_BUS = -1,
    IB_E_TASK = -2,
    IB_E_STUCK = -3
};

bool ib_open(const rfp_i2c_t *bus, int khz);  // the bus of the system (its handle) or a new bus on its pins
void ib_close(void);
void ib_scan(void);           // one scan now
void ib_live(bool on);        // a scan each 1.5 s
void ib_ident(uint8_t addr);  // read the ID registers of the candidates of addr (a write of one
                              // register address byte, then a read)
void ib_regs(uint8_t addr);   // read the registers 0x00 .. 0xFF of addr
void ib_get(ib_state_t *o);
const char *ib_err(int e);
#ifndef ESP_PLATFORM
void ib_pc_round(void);       // the PC: the work of the task (the app calls it each frame)
void ib_pc_modules(bool on);  // the PC: the three modules of the fake bus on or off (the PC tests)
#endif
