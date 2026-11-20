#pragma once
// TFNCOMP, THE IR PLAN OF A BUTTON: the level times of one press and of the repeat while the button is held, as a
// remote of that protocol sends them (libs/irdecode makes the frames). The master gives the plan to tcm_send().
#include "irdecode/irdecode.h"
#include "tfncomp.h"

typedef struct {
    int n;            // the level times in d
    int rep_at;       // the repeat sends d[rep_at .. n) (0: all of them)
    int rep_ms;       // from the start of one send to the next while the button is held
    int carrier_khz;  // the carrier of the protocol
} tc_ir_plan_t;

// A code of a known protocol (ird_decode / ird_make). False: no frame (UNKNOWN: use tc_ir_plan_raw).
bool tc_ir_plan(const ird_code_t *c, int32_t *d, int max, tc_ir_plan_t *p);
// The raw level times of a captured frame (an unknown protocol, or the exact copy of a remote)
bool tc_ir_plan_raw(const int32_t *raw, int nraw, int carrier_khz, int32_t *d, int max, tc_ir_plan_t *p);
