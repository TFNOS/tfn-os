// The math faces of INTEGRALS (made by apps/integral/tools/mk_font.py; do not edit).
#pragma once
#include <stdint.h>
#include "tfn/mfont.h"

// the em of each level (px): level 0 = the display size of the 3.49, a script goes 2 levels down
#define IGF_EM_LIST {30, 24, 19, 15, 12}
#define IGF_LEVELS 5
// the kinds of the tall glyphs
enum { IGT_LP, IGT_RP, IGT_LB, IGT_RB, IGT_LC, IGT_RC, IGT_BAR, IGT_INT, IGT_SUM, IGT_KINDS };
typedef struct {
    uint8_t kind, h, w;
    uint32_t off;
} ig_tall_t;
#define IG_TALL_N 189
extern const mfont_t IGF_R[5], IGF_I[5];
extern const ig_tall_t IG_TALL[IG_TALL_N];
extern const uint8_t *const IG_TALL_DATA;
