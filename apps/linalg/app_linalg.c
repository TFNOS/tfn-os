// LINEAR ALGEBRA (UTIL, learning): the basics of linear algebra, then the advanced part (the spectrum and more:
// the tricks to find the determinant, the eigenvalues and the eigenvectors of matrices with a pattern), and the
// beauty of each idea. The app never asks the user to solve: each chapter explains and shows, step by step (a tap =
// the next step), and the user changes the numbers of the example (the chips, + and -); the text, each value and the
// live picture follow.
//
// The parts: la_num.inc (exact fractions, small matrices, eigenvalues), la_type.inc + la_flow.inc (a typesetter of
// the mathematics with the faces of la_font.c, English and Hebrew), la_pic.inc (the parts of the pictures),
// la_ui.inc (the boxes of each frame), la_t1..la_t4.inc (the chapters), this file (the screens, the input, the saves).
// The task: the app task of TFN OS; the effects load in the task of libs/tfsfx. The memory: the file statics only.
// THE TESTS (MANUAL.md, LINEAR ALGEBRA): the pictures of each step on 4 frames, a bot with real touches, a dump of all
// the values for all the numbers that a user can set (a check of each one with numpy and sympy).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/settings.h"
#include "tfn/storage.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "esp_heap_caps.h"
#include "kscroll/kscroll.h"
#include "la_font.h"
#include "la_sfx.h"
#include "tfdraw/tfdraw.h"
#include "tfsfx/tfsfx.h"

#include "la_num.inc"
#include "la_type.inc"
#include "la_flow.inc"
#include "la_pic.inc"
#include "la_ui.inc"

// ---------------------------------------------------------------- the state
static bool s_he;          // Hebrew
static bool s_snd = true;  // the effects
static int s_big;          // TEXT SIZE: 0 normal, 1 large
static int64_t s_now;      // the time of the frame (ms)
static float s_t;          // the time of the pictures (s)

// a number of an example: its name (markup), the range, the start, the shown value = v / den
typedef struct {
    const char *sym;
    int8_t lo, hi, def;
    uint8_t den;
} par_t;
// a chapter
typedef struct {
    const char *name[2];
    uint8_t part;  // 0 BASICS, 1 SPECTRAL, 2 TRICKS, 3 APPLIED
    uint8_t np, ns;
    par_t p[4];
    void (*calc)(void);                        // the values from P[] (once at each change)
    void (*text)(int step);                    // the text of a step (T())
    void (*pic)(rect_t r, int step, float t);  // the live picture
    const char *const *vn[4];                  // names of the values of a number (EN, HE pairs), or NULL
} topic_t;

static int P[4];                 // the numbers of the chapter now
static const topic_t *s_tp_now;  // the chapter now (NULL: the chapters screen)
static int s_hot = -1;           // the number that changed last (its value shows in the accent colour)
static int64_t s_hot_t;

// ---------------------------------------------------------------- the state of the screens
enum {
    SC_HOME,
    SC_TOPIC
};
static int s_scr, s_ti, s_step, s_sel;
static int64_t s_step_t, s_scr_t, s_touch_t;
static int s_dir = 1;       // the slide of the last change of the step
static kscroll_t s_ks;      // the chapters screen
static float s_ek = 1;      // the factor of the eases in this frame (1 = jump)
static bool s_snap = true;  // the next frame: the eases jump to their goals (a new chapter)
static int64_t s_last;
// the touch on the chapter screen
static int s_drag;  // 0 none, 1 a scrub of a chip, 2 a swipe of the text, 3 a held key
static int s_dx0, s_dy0, s_dlast, s_dkey;
static int64_t s_rep_t;
// the pages of a step (la_view.inc)
#define PG_MAX 8
static int s_pg, s_npg, s_pg_at[PG_MAX], s_lay_t, s_lay_m;
static bool s_pg_last;  // the step before was chosen with a "back": show its last page
static bool s_lay_bad;  // a line wider or taller than the box (the PC test fails on it)

static inline void EZ(float *v, float goal)
{
    *v += (goal - *v) * s_ek;
}

// ---------------------------------------------------------------- the text of a step
static char s_tx[3600];
static int s_tn;
// the end of a T(): the length, a full buffer (vsnprintf is not in the ABI of the app files: T is a macro)
static void t_end(int n)
{
    if (n > 0) {
        s_tn += n;
    }
    if (s_tn > (int)sizeof s_tx - 1) {
        s_tn = (int)sizeof s_tx - 1;
        s_overflow = true;
    }
}
#define T(...)     t_end(snprintf(s_tx + s_tn, sizeof s_tx - s_tn, __VA_ARGS__))
#define LG(en, he) (s_he ? (he) : (en))

// the short texts of the numbers (16 buffers in turn)
static char s_nb[20][420];
static int s_nbi;
static char *nb(void)
{
    s_nbi = (s_nbi + 1) % 20;
    return s_nb[s_nbi];
}
static const char *I(int64_t v)
{
    char *b = nb();
    s_int(b, 420, v);
    return b;
}
static const char *Q(q_t q)  // inline: 3/4
{
    char *b = nb();
    s_q(b, 420, q, false);
    return b;
}
static const char *QD(q_t q)  // display: a fraction
{
    char *b = nb();
    s_q(b, 420, q, true);
    return b;
}
static const char *F(float v, int p)
{
    char *b = nb();
    s_f(b, 420, v, p);
    return b;
}
static const char *CX(cx_t z, int p)
{
    char *b = nb();
    s_cx(b, 420, z, p);
    return b;
}
static const char *MQ(const qm_t *m)
{
    char *b = nb();
    s_qm(b, 420, m, false, true);
    return b;
}
static const char *DQ(const qm_t *m)  // the bars of a determinant
{
    char *b = nb();
    s_qm(b, 420, m, true, true);
    return b;
}
// a square root in its simple form: "2", "@r{13}", "3@r{2}", "@r{−3}"
static const char *ROOT(int64_t v)
{
    char *b = nb();
    if (v < 0) {
        snprintf(b, 420, "@r{" MINUS "%lld}", (long long)-v);
        return b;
    }
    int64_t k, r;
    surd(v, &k, &r);
    if (r == 1 || v == 0) {
        snprintf(b, 420, "%lld", (long long)(v == 0 ? 0 : k));
    } else if (k == 1) {
        snprintf(b, 420, "@r{%lld}", (long long)r);
    } else {
        snprintf(b, 420, "%lld@r{%lld}", (long long)k, (long long)r);
    }
    return b;
}
// a value that comes from the number i of the example: in the accent colour while that number is new
static bool hot(int i)
{
    return i == s_hot && s_now - s_hot_t < 2500;
}
static const char *HV(int i, int64_t v)
{
    char *b = nb(), t[32];
    s_int(t, sizeof t, v);
    snprintf(b, 420, hot(i) ? "@h{%s}" : "%s", t);
    return b;
}
// the text of the value of the number i now: "3", "3/2", "0.7" (tenths), or its name
static void val_txt(const topic_t *tp, int i, char *o, int max)
{
    const par_t *p = &tp->p[i];
    if (tp->vn[i]) {
        snprintf(o, max, "%s", tp->vn[i][2 * (P[i] - p->lo) + (s_he ? 1 : 0)]);
    } else if (p->den == 10) {
        s_f(o, max, P[i] / 10.0f, 1);
    } else {
        s_q(o, max, qn(P[i], p->den ? p->den : 1), false);
    }
}
// a number of the example with the accent colour while it is new
static const char *PV(int i)
{
    char *b = nb(), v[40];
    val_txt(s_tp_now, i, v, sizeof v);
    if (hot(i)) {
        snprintf(b, 420, "@h{%s}", v);
    } else {
        snprintf(b, 420, "%s", v);
    }
    return b;
}
// a value in brackets when it is negative (for a product: 3 · (−2))
static const char *IP(int64_t v)
{
    char *b = nb();
    if (v < 0) {
        snprintf(b, 420, "(" MINUS "%lld)", (long long)-v);
    } else {
        snprintf(b, 420, "%lld", (long long)v);
    }
    return b;
}
static const char *PVP(int i)  // PV in brackets when it is negative
{
    if (P[i] >= 0) {
        return PV(i);
    }
    char *b = nb();
    snprintf(b, 420, "(%s)", PV(i));
    return b;
}

#include "la_t1.inc"
#include "la_t2.inc"
#include "la_t3.inc"
#include "la_t4.inc"

static const topic_t *const TOPICS[] = {
        &TP_VEC,  &TP_MAP,  &TP_DET,   &TP_RANK,                              // the basics
        &TP_EIG,  &TP_CHAR, &TP_DIAG,  &TP_SYM,                               // the spectral part
        &TP_2X2,  &TP_TRI,  &TP_BLOCK, &TP_CIRC, &TP_TOEP, &TP_R1, &TP_VAND,  // the tricks
        &TP_MIRR, &TP_KRON, &TP_GERS,  &TP_CH,                                //
        &TP_SVD,  &TP_QF,   &TP_POW,   &TP_MARK, &TP_LAP,                     // beyond
};
#define NT ((int)(sizeof TOPICS / sizeof TOPICS[0]))

#include "la_main.inc"
#include "la_view.inc"
