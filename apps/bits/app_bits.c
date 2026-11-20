// BITS: how a computer works, from the ground up. A teaching app on the landscape screen
// (640 x 172 on the 3.49, 320 x 240 on the CoreS3).
// The task: the app task of TFN OS calls enter, input, update and render (app_bits at the end of this file).
// The memory: the file statics of this file; no heap.
//
// - BINARY: 8 bit switches. Set a number, read a number, hex, a speed round (FUSE), negative
//   numbers (two's complement) and bit operations. 3 lives in each level. FREE: play.
// - GATES: NOT, AND, OR, XOR, NAND and NOR. Toggle the inputs, watch the lamp, and fill the
//   truth table. MYSTERY: find the hidden gate.
// - BUILD: puzzles in the style of the NAND game. Put a block in each slot and pick the wire of
//   each pin from a list. A solved part becomes a block for the next puzzles: NOT, AND, OR, NOR,
//   XOR, MUX, half adder, full adder, 2-bit adder, SR latch, D latch, counter. LAB: an 8-bit
//   ripple-carry adder to play with.
// - CPU: a small 8-bit CPU with 6502-like instructions (LDA, LDX, STA, ADC, CLC, EOR, ASL, LSR,
//   INX, DEX, CMP, CPX, BNE, BEQ, JMP), 16 bytes of memory, 8 LEDs and an 8 x 8 screen. Write
//   short programs: STEP, RUN, TEST, with the 6502 cycle count.
// - REF: place values, hex, the gates, the blocks and the instructions.
// The stars of each level are kept in NVS. A long touch on a locked card or level opens it.
//
// The page is drawn in horizontal bands (as MORSE does): each line of the frame comes from
// PSRAM one time, which keeps the CoreS3 fast.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"
#include "tfn/settings.h"
#include "tfn/tunes.h"

// ---------------------------------------------------------------- layout

typedef struct {
    int16_t x, y, w, h;
} rc_t;

#define RC(x, y, w, h) ((rc_t){(int16_t)(x), (int16_t)(y), (int16_t)(w), (int16_t)(h)})

#if BOARD_ROUND
// the round view: the logical square of 320 x 320 on the panel at k (see THE ROUND VIEW); the frame of the layout
// starts at its row RV_OY
#define RV_OY 40
static float rv_k(void)
{
    return (float)gfx_w / 320;
}
// the half width of the circle of the square at the row y
static int rchord(int y)
{
    int dy = y - 160;
    return dy * dy >= 160 * 160 ? 0 : (int)sqrtf((float)(160 * 160 - dy * dy));
}
// the same for the row y of the frame, and the narrowest half chord of the rows y .. y + h
static int rch(int y)
{
    return rchord(y + RV_OY);
}
static int rch2(int y, int h)
{
    int a = rch(y), b = rch(y + h);
    return a < b ? a : b;
}
#endif

static bool in_rc(rc_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

#if BOARD_BAR_SCREEN
#define SW      640
#define SH      172
#define TOP_H   34
#define BACK_RC RC(0, 0, 64, 34)
// BINARY: the target panel at the left, 8 switches at the right
#define B_PANEL_RC  RC(0, 34, 176, 138)
#define B_SW_X      176
#define B_SW_Y      34
#define B_SW_W      58
#define B_SW_H      138
#define B_CHOICE(i) RC(4 + ((i) & 1) * 86, 82 + ((i) >> 1) * 45, 82, 42)
// GATES
#define G_IN_RC(i)   RC(6, 40 + (i) * 66, 82, 58)
#define G_CX         200
#define G_CY         103
#define G_SIZE       96
#define G_LAMP_X     306
#define G_TAB_X      344
#define G_TAB_COL    74
#define G_TAB_Y      36
#define G_ROWA_H     26
#define G_Q_RC(i)    RC(G_TAB_X + (i) * G_TAB_COL + 3, 96, G_TAB_COL - 6, 72)
#define G_NAME_RC(i) RC(344 + ((i) % 3) * 99, 40 + ((i) / 3) * 66, 95, 60)
// BUILD: the panels
#define P_PER_PAGE 5
#define P_W        128
#define P_HDR_H    30
#define P_PIN_H    33
#define BT_VIEW_RC RC(262, 0, 88, 34)
#define BT_PREV_RC RC(352, 0, 50, 34)
#define BT_NEXT_RC RC(404, 0, 50, 34)
#define BT_STAT_X  458
// BUILD try view
#define T_IN_RC(i)  RC(6 + (i) * 74, 40, 68, 56)
#define T_OUT_RC(i) RC(6 + (i) * 100, 102, 94, 66)
#define T_CLK_RC    RC(310, 40, 110, 56)
#define T_RST_RC    RC(310, 102, 110, 66)
#define T_GRID_X    430
#define T_GRID_Y    40
#define T_GRID_W    206
#define T_GRID_H    128
// LAB
#define L_LABEL_W 80
#define L_COL_W   70
#define L_ROW_H   46
// CPU
#define C_STEP_RC    RC(254, 0, 80, 34)
#define C_RUN_RC     RC(336, 0, 80, 34)
#define C_RST_RC     RC(418, 0, 80, 34)
#define C_TEST_RC    RC(500, 0, 140, 34)
#define C_LINE_RC(i) RC(((i) / 4) * 190, 34 + ((i) % 4) * 34 + 1, 188, 33)
#define C_OPW        76
#define C_SCR_X      392
#define C_SCR_Y      40
#define C_PIX        8
#define C_LED_Y      116
#define C_REG_X      470
// picker and the result
#define PK_COLS     6
#define R_BTN_RC(i) RC(110 + (i) * 145, 120, 130, 46)
// title cards
#define TC_RC(i) RC(200 + (i) * 88 + 2, 6, 84, 160)
// level tiles
#define LV_COLS     7
#define LV_ROWS     2
#define REF_PREV_RC RC(470, 0, 80, 34)
#define REF_NEXT_RC RC(556, 0, 84, 34)
#else
#if BOARD_UI_TABLET
#define MDX        107  // the Tab5: the logical frame is 427 px wide (x 1.5 = 640)
#define P_PER_PAGE 4
#else
#define MDX        0
#define P_PER_PAGE 3
#endif
#define SW           (320 + MDX)
#define SH           240
#define TOP_H        36
#define BACK_RC      RC(0, 0, 52, 36)
#define B_PANEL_RC   RC(0, 36, SW, 72)
#define B_SW_X       0
#define B_SW_Y       108
#define B_SW_W       (40 + MDX / 8)
#define B_SW_H       132
#define B_CHOICE(i)  RC(2 + (i) * (79 + MDX / 4), 66, 76 + MDX / 4, 40)
#define G_IN_RC(i)   RC(6, 42 + (i) * 54, 72, 48)
#define G_CX         (176 + MDX / 2)
#define G_CY         93
#define G_SIZE       84
#define G_LAMP_X     (290 + MDX)
#define G_TAB_X      0
#define G_TAB_COL    (80 + MDX / 4)
#define G_TAB_Y      150
#define G_ROWA_H     18
#define G_Q_RC(i)    RC(G_TAB_X + (i) * G_TAB_COL + 4, 188, G_TAB_COL - 8, 50)
#define G_NAME_RC(i) RC(2 + ((i) % 3) * (106 + MDX / 3), 150 + ((i) / 3) * 45, 102 + MDX / 3, 42)
#define P_W          106
#define P_HDR_H      36
#define P_PIN_H      40
#define BT_VIEW_RC   RC(54, 0, 64, 36)
#define BT_PREV_RC   RC(120, 0, 44, 36)
#define BT_NEXT_RC   RC(166, 0, 44, 36)
#define BT_STAT_X    212
#define T_IN_RC(i)   RC(4 + (i) * (79 + MDX / 4), 40, 75 + MDX / 4, 54)
#define T_OUT_RC(i)  RC(4 + (i) * (106 + MDX / 3), 98, 102 + MDX / 3, 58)
#define T_CLK_RC     RC(4, 162, 100, 74)
#define T_RST_RC     RC(108, 162, 70, 74)
#define T_GRID_X     4
#define T_GRID_Y     162
#define T_GRID_W     (312 + MDX)
#define T_GRID_H     74
#define L_LABEL_W    48
#define L_COL_W      (34 + MDX / 8)
#define L_ROW_H      68
#define C_STEP_RC    RC(54, 0, 64 + MDX / 4, 36)
#define C_RUN_RC     RC(120 + MDX / 4, 0, 64 + MDX / 4, 36)
#define C_RST_RC     RC(186 + MDX / 2, 0, 56 + MDX / 4, 36)
#define C_TEST_RC    RC(244 + 3 * MDX / 4, 0, 76 + MDX / 4, 36)
#define C_LINE_RC(i) RC(((i) / 4) * (160 + MDX / 2), 36 + ((i) % 4) * 34 + 1, 158 + MDX / 2, 33)
#define C_OPW        64
#define C_SCR_X      4
#define C_SCR_Y      178
#define C_PIX        7
#define C_LED_Y      180
#define C_REG_X      68
#define PK_COLS      4
#define R_BTN_RC(i)  RC(6 + (i) * (104 + MDX / 3), 176, 100 + MDX / 3, 54)
#define TC_RC(i)                                                                                                       \
    ((i) < 3 ? RC(2 + (i) * (106 + MDX / 3), 64, 102 + MDX / 3, 86)                                                    \
             : RC(2 + ((i) - 3) * (159 + MDX / 2), 154, 155 + MDX / 2, 84))
#define LV_COLS     4
#define LV_ROWS     4
#define REF_PREV_RC RC(176 + MDX, 0, 70, 36)
#define REF_NEXT_RC RC(250 + MDX, 0, 70, 36)
#endif

#define BODY_H (SH - TOP_H)

#if BOARD_ROUND
// THE ROUND LAYOUT (the 1.75C 466 x 466, the Watcher 412 x 412: APP_ROUND_HI). The code draws the 320 x 240 frame
// of the layout; the round view puts it in a square of 320 x 320 at the row RV_OY (40) and the square on the panel at
// gfx_w / 320. The circle round the square: the title of each page is in the top cap (above the frame), the bar
// (BACK and the buttons) in the chord of the rows 0 .. 36, the body in the wide middle, the last row narrower.
#undef BACK_RC
#undef B_PANEL_RC
#undef B_SW_X
#undef B_SW_Y
#undef B_SW_W
#undef B_SW_H
#undef B_CHOICE
#undef G_IN_RC
#undef G_CX
#undef G_LAMP_X
#undef G_TAB_X
#undef G_TAB_COL
#undef G_TAB_Y
#undef G_ROWA_H
#undef G_Q_RC
#undef G_NAME_RC
#undef P_W
#undef P_HDR_H
#undef P_PIN_H
#undef BT_VIEW_RC
#undef BT_PREV_RC
#undef BT_NEXT_RC
#undef T_IN_RC
#undef T_OUT_RC
#undef T_CLK_RC
#undef T_RST_RC
#undef T_GRID_X
#undef T_GRID_Y
#undef T_GRID_W
#undef T_GRID_H
#undef L_LABEL_W
#undef L_COL_W
#undef L_ROW_H
#undef C_STEP_RC
#undef C_RUN_RC
#undef C_RST_RC
#undef C_TEST_RC
#undef C_LINE_RC
#undef C_SCR_X
#undef C_SCR_Y
#undef C_PIX
#undef C_LED_Y
#undef C_REG_X
#undef R_BTN_RC
#undef TC_RC
#undef REF_PREV_RC
#undef REF_NEXT_RC
#define BACK_RC      RC(58, 0, 48, 36)
#define BAR_R        264                             // the right end of the bar (the chord)
#define CAP_Y        (-20)                           // the title in the top cap
#define TOP_BAND     in_band(-RV_OY, RV_OY + TOP_H)  // the rows of the cap and the bar
#define B_PANEL_RC   RC(24, 38, 272, 70)
#define B_SW_X       36
#define B_SW_Y       110
#define B_SW_W       31
#define B_SW_H       108
#define B_CHOICE(i)  RC(28 + (i) * 66, 66, 62, 40)
#define G_IN_RC(i)   RC(28, 42 + (i) * 54, 72, 48)
#define G_CX         180
#define G_LAMP_X     274
#define G_TAB_X      40
#define G_TAB_COL    60
#define G_TAB_Y      146
#define G_ROWA_H     16
#define G_Q_RC(i)    RC(G_TAB_X + (i) * G_TAB_COL + 2, 180, G_TAB_COL - 4, 44)
#define G_NAME_RC(i) RC(40 + ((i) % 3) * 80, 148 + ((i) / 3) * 38, 76, 36)
#define P_X0         31
#define P_BODY       184
#define P_W          86
#define P_HDR_H      32
#define P_PIN_H      34
#define BT_VIEW_RC   RC(108, 0, 58, 36)
#define BT_PREV_RC   RC(168, 0, 44, 36)
#define BT_NEXT_RC   RC(214, 0, 44, 36)
#define T_IN_RC(i)   RC(30 + (i) * 66, 40, 62, 54)
#define T_OUT_RC(i)  RC(30 + (i) * 88, 98, 84, 56)
#define T_CLK_RC     RC(40, 160, 88, 60)
#define T_RST_RC     RC(132, 160, 60, 60)
#define T_GRID_X     40
#define T_GRID_Y     160
#define T_GRID_W     240
#define T_GRID_H     60
#define T_GRID_SKIP  156
#define L_LABEL_W    72
#define L_COL_W      26
#define L_ROW_H      60
#define L_TXT_X      30
#define C_STEP_RC    RC(108, 0, 52, 36)
#define C_RUN_RC     RC(162, 0, 50, 36)
#define C_RST_RC     RC(214, 0, 46, 36)
#define C_TEST_RC    RC(110, 228, 100, 34)  // (the bottom cap)
#define C_LINE_RC(i) RC(24 + ((i) / 4) * 137, 36 + ((i) % 4) * 32 + 1, 135, 31)
#define C_SCR_X      52
#define C_SCR_Y      172
#define C_PIX        5
#define C_LED_Y      176
#define C_REG_X      98
#define B_HEX_X      84  // BINARY FREE: the HEX and +/- values
#define B_SGN_X      160
#define C_TY0        188  // the registers, the card, the message
#define C_TY1        200
#define C_TY2        212
#define BYTE_OK_RC   RC(196, 0, 66, 36)
#define BYTE_SW      B_SW_X, TOP_H + 6, B_SW_W, 176
#define R_BTN_RC(i)  RC(40 + (i) * 82, 168, 78, 48)
#define TC_RC(i)     ((i) < 3 ? RC(26 + (i) * 90, 50, 86, 88) : RC(46 + ((i) - 3) * 116, 142, 112, 76))
#define REF_PREV_RC  RC(172, 0, 44, 36)
#define REF_NEXT_RC  RC(218, 0, 44, 36)
#define REF_X0       24
#define REF_W        272
#define REF_SH       226
#else
#define BAR_R       SW
#define P_X0        0
#define P_BODY      BODY_H
#define T_GRID_SKIP 180
#define L_TXT_X     4
#define B_HEX_X     110
#define B_SGN_X     210
#define C_TY0       194
#define C_TY1       208
#define C_TY2       222
#define BYTE_OK_RC  RC(SW - 96, 0, 96, TOP_H)
#define BYTE_SW     0, TOP_H, SW / 8, BODY_H
#define REF_X0      0
#define REF_W       SW
#define REF_SH      SH
#endif
#define BAR_X0 (BACK_RC.x + BACK_RC.w)  // the bar after BACK

// ---------------------------------------------------------------- colours

#define C_BG        rgb(6, 24, 16)  // the circuit board
#define C_PCB       rgb(12, 44, 28)
#define C_PANEL     rgb(16, 56, 36)
#define C_PANEL_HI  rgb(34, 96, 62)
#define C_EDGE      rgb(60, 130, 90)
#define C_TRACE     rgb(150, 104, 40)  // copper
#define C_TRACE_ON  rgb(255, 196, 60)
#define C_SILK      rgb(228, 236, 228)  // the white print on a board
#define C_DIM       rgb(120, 150, 130)
#define C_LAMP_ON   rgb(255, 190, 40)
#define C_LAMP_GLOW rgb(110, 70, 10)
#define C_LAMP_OFF  rgb(44, 40, 22)
#define C_GOOD      rgb(90, 235, 120)
#define C_BAD       rgb(245, 80, 64)
#define C_CHIP      rgb(22, 24, 28)
#define C_GOLD      rgb(255, 210, 80)
#define C_CYAN      rgb(110, 220, 255)
#define C_TOP       rgb(4, 16, 10)

// ---------------------------------------------------------------- band drawing
// bits_render() runs the page code once for each band of rows, and these functions clip all
// the drawing to the band (the same method as app_morse.c).

#ifndef BAND
#if BOARD_BAR_SCREEN
#define BAND 12  // rows: 12 x 1280 bytes = 15 KB
#else
#define BAND 24  // rows: 24 x 640 bytes = 15 KB
#endif
#endif

static int s_by0 = 0, s_by1 = 1 << 30;

#if BOARD_UI_TABLET
// THE TAB5 VIEW: the code draws a logical frame of 427 x 240 (the 320 x 240 layout, wider: see SW and
// MDX). The functions below put it on the 640 x 360 frame of the Tab5 at 1.5 times, the same on the two axes, so a
// circle stays round. The text has a whole scale (1 -> 2, 2 -> 3, 3 -> 4) and keeps its middle point.
// bits_input() takes the touch back to the logical frame.
static int tv_y(int v)
{
    return v >= 0 ? v * 3 / 2 : -((-v * 3 + 1) / 2);
}  // a logical x or y in the frame
static int tv_h(int h)
{
    return (h * 3 + 1) / 2;
}  // a logical width or height (the same at each place: even lines)

// a rectangle in the physical frame, only the rows of the band
static void PR(int x, int y, int w, int h, uint16_t c)
{
    if (y < s_by0) {
        h -= s_by0 - y;
        y = s_by0;
    }
    if (y + h > s_by1) {
        h = s_by1 - y;
    }
    if (h > 0 && w > 0) {
        gfx_fill_rect(x, y, w, h, c);
    }
}

// the span xa .. xb - 1 of the logical ellipse (cx, cy, rx, ry) in the physical row py; false: the row misses it
static bool tv_span(int cx, int cy, int rx, int ry, int py, int *xa, int *xb)
{
    float pcx = 1.5f * (float)cx + 0.75f, pcy = 1.5f * (float)cy + 0.75f;
    float ax = 1.5f * (float)rx + 0.75f, ay = 1.5f * (float)ry + 0.75f;
    float d = ((float)py + 0.5f - pcy) / ay;
    if (d <= -1.0f || d >= 1.0f) {
        return false;
    }
    float dx = ax * sqrtf(1.0f - d * d);
    *xa = (int)lroundf(pcx - dx);
    *xb = (int)lroundf(pcx + dx);
    return *xb > *xa;
}
#endif

#if BOARD_ROUND
// THE ROUND VIEW: the logical frame (moved down by RV_OY in a square of 320 x 320) on the panel at k = gfx_w / 320, the same on the two axes. The
// text has a whole scale (1 -> 2; others: the scale x k); TXT keeps its left end, TXT_IN and TXT_C the middle.
static int rv(int v)
{
    return (int)floorf((float)v * rv_k());
}
static void PR(int x, int y, int w, int h, uint16_t c)
{
    if (y < s_by0) {
        h -= s_by0 - y;
        y = s_by0;
    }
    if (y + h > s_by1) {
        h = s_by1 - y;
    }
    if (h > 0 && w > 0) {
        gfx_fill_rect(x, y, w, h, c);
    }
}
// the span xa .. xb - 1 of the logical ellipse (cx, cy, rx, ry) in the panel row py; false: the row misses it
static bool rv_span(int cx, int cy, int rx, int ry, int py, int *xa, int *xb)
{
    float k = rv_k(), pcx = ((float)cx + 0.5f) * k, pcy = ((float)cy + 0.5f) * k;
    float ax = ((float)rx + 0.5f) * k, ay = ((float)ry + 0.5f) * k;
    float d = ((float)py + 0.5f - pcy) / ay;
    if (d <= -1.0f || d >= 1.0f) {
        return false;
    }
    float dx = ax * sqrtf(1.0f - d * d);
    *xa = (int)lroundf(pcx - dx);
    *xb = (int)lroundf(pcx + dx);
    return *xb > *xa;
}
static bool in_band(int y, int h)
{
    return rv(y + RV_OY) < s_by1 && rv(y + RV_OY + h) > s_by0;
}
static void R(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0) {
        int x0 = rv(x), y0 = rv(y + RV_OY);
        PR(x0, y0, rv(x + w) - x0, rv(y + RV_OY + h) - y0, c);
    }
}
static void FILL(uint16_t c)
{
    PR(0, 0, gfx_w, gfx_h, c);
}
static void FRAME(int x, int y, int w, int h, int t, uint16_t c)
{
    if (!in_band(y, h)) {
        return;
    }
    R(x, y, w, t, c);
    R(x, y + h - t, w, t, c);
    R(x, y + t, t, h - 2 * t, c);
    R(x + w - t, y + t, t, h - 2 * t, c);
}
static void ELL(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0) {
        return;
    }
    cy += RV_OY;
    int y0 = rv(cy - ry) - 1, y1 = rv(cy + ry + 1) + 1;
    if (y0 < s_by0) {
        y0 = s_by0;
    }
    if (y1 > s_by1) {
        y1 = s_by1;
    }
    for (int py = y0; py < y1; py++) {
        int xa, xb;
        if (rv_span(cx, cy, rx, ry, py, &xa, &xb)) {
            gfx_fill_rect(xa, py, xb - xa, 1, c);
        }
    }
}
#elif BOARD_UI_TABLET
static bool in_band(int y, int h)
{
    int y0 = tv_y(y);
    return y0 < s_by1 && y0 + tv_h(h) > s_by0;
}
#else
static bool in_band(int y, int h)
{
    return y < s_by1 && y + h > s_by0;
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void R(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0) {
        PR(tv_y(x), tv_y(y), tv_h(w), tv_h(h), c);
    }
}
#else
static void R(int x, int y, int w, int h, uint16_t c)
{
    if (y < s_by0) {
        h -= s_by0 - y;
        y = s_by0;
    }
    if (y + h > s_by1) {
        h = s_by1 - y;
    }
    if (h > 0 && w > 0) {
        gfx_fill_rect(x, y, w, h, c);
    }
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void FRAME(int x, int y, int w, int h, int t, uint16_t c)
{
    if (!in_band(y, h)) {
        return;
    }
    R(x, y, w, t, c);
    R(x, y + h - t, w, t, c);
    R(x, y + t, t, h - 2 * t, c);
    R(x + w - t, y + t, t, h - 2 * t, c);
}
#else
static void FRAME(int x, int y, int w, int h, int t, uint16_t c)
{
    if (!in_band(y, h)) {
        return;
    }
    R(x, y, w, t, c);
    R(x, y + h - t, w, t, c);
    R(x, y + t, t, h - 2 * t, c);
    R(x + w - t, y + t, t, h - 2 * t, c);
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void ELL(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0) {
        return;
    }
    int y0 = (int)floorf(1.5f * (float)(cy - ry)), y1 = (int)ceilf(1.5f * (float)(cy + ry + 1));
    if (y0 < s_by0) {
        y0 = s_by0;
    }
    if (y1 > s_by1) {
        y1 = s_by1;
    }
    for (int py = y0; py < y1; py++) {
        int xa, xb;
        if (tv_span(cx, cy, rx, ry, py, &xa, &xb)) {
            gfx_fill_rect(xa, py, xb - xa, 1, c);
        }
    }
}
#else
static void ELL(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0 || cy - ry >= s_by1 || cy + ry < s_by0) {
        return;
    }
    if (ry == 0) {
        R(cx - rx, cy, 2 * rx + 1, 1, c);
        return;
    }
    int y0 = -ry, y1 = ry;
    if (cy + y0 < s_by0) {
        y0 = s_by0 - cy;
    }
    if (cy + y1 > s_by1 - 1) {
        y1 = s_by1 - 1 - cy;
    }
    float inv = 1.0f / ((float)ry * (float)ry);
    for (int dy = y0; dy <= y1; dy++) {
        float f = 1.0f - (float)dy * (float)dy * inv;
        int dx = f > 0 ? (int)(rx * sqrtf(f) + 0.5f) : 0;
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
#endif

static void CIRC(int cx, int cy, int r, uint16_t c)
{
    ELL(cx, cy, r, r, c);
}

// The 5 x 7 font of gfx.c, read back from the frame buffer once (as app_morse.c does).
static uint8_t s_glyph[128][7];
static bool s_font_ok;

static void font_capture(void)
{
    if (s_font_ok || !g_fb) {
        return;
    }
    gfx_clip_reset();
    for (int ch = 1; ch < 128; ch++) {
        char t[2] = {(char)ch, 0};
        gfx_fill_rect(0, 0, 6, 7, 0);
        gfx_text(0, 0, t, 1, 0xFFFF);
        for (int r = 0; r < 7; r++) {
            uint8_t bits = 0;
            for (int col = 0; col < 5; col++) {
                if (g_fb[r * gfx_w + col] == 0xFFFF) {
                    bits |= (uint8_t)(0x10 >> col);
                }
            }
            s_glyph[ch][r] = bits;
        }
    }
    s_font_ok = true;
}

// THE ANTI-ALIASED TEXT (the 5 x 7 dot font is rough and hard to read on these screens): the Bungee faces of the
// firmware in
// place of the 5 x 7 dot font. A dot scale k (7 * k px high, 6 * k px a letter) takes the face of about the same width
// (MF_TINY, MF_N16, MF_N24, MF_N34); the capitals keep the middle of the dot line; only the rows of the band show.
// The big faces have capitals only: a small letter shows as its capital.
static const mfont_t *aa_face(int k)
{
#if BOARD_UI_TABLET
    // (the Tab5 text has the scales 2, 3, 4 of its 1.5 view: one step lighter, so a label and the line under it keep
    // their gap)
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_LABEL : k == 3 ? &MF_N20 : k == 4 ? &MF_N24 : &MF_N34;
#else
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_N16 : k == 3 ? &MF_N24 : &MF_N34;
#endif
}
static int aa_cp(int ch, const mfont_t *f)
{
    return (f != &MF_TINY && ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
}
// the advance of a glyph in quarters of a px (a space: 0.3 of the capital height at least)
static int aa_adv4(const mfont_t *f, const mglyph_t *g, int ch)
{
    const int sp = f->asc * 6 / 5;
    return ch == ' ' && g->adv4 < sp ? sp : g->adv4;
}
static int aa_w(const char *s, int k)
{
    const mfont_t *f = aa_face(k);
    int w4 = 0;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, aa_cp((uint8_t)*s, f));
        w4 += g ? aa_adv4(f, g, *s) : 0;
    }
    return (w4 + 2) / 4;
}
// the text from x (the left end) in the dot line from y (its top) of scale k
static void aa_text(int x, int y, const char *s, int k, uint16_t c)
{
    const mfont_t *f = aa_face(k);
    const int base = y + (7 * k + f->asc) / 2;
    int x4 = x * 4;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, aa_cp((uint8_t)*s, f));
        if (!g) {
            continue;
        }
        const int gx = (x4 + 2) / 4 + g->ox, gy = base + g->oy;
        const uint8_t *src = f->a4 + g->off;
        for (int j = 0; j < g->h; j++) {
            const int py = gy + j;
            if (py < s_by0 || py >= s_by1 || py < 0 || py >= gfx_h) {
                continue;
            }
            uint16_t *row = g_fb + (size_t)py * gfx_w;
            for (int i = 0; i < g->w; i++) {
                const int px = gx + i;
                if (px < 0 || px >= gfx_w) {
                    continue;
                }
                const int n = j * g->w + i, v = (n & 1) ? src[n >> 1] >> 4 : src[n >> 1] & 15;
                if (v) {
                    row[px] = v == 15 ? c : ui_mix(row[px], c, v * 17);
                }
            }
        }
        x4 += aa_adv4(f, g, *s);
    }
}
#define gfx_text_width(s, k) aa_w((s), (k))

#if BOARD_ROUND
// the text at the panel point px (the left end) and the logical row y (its middle stays); the panel scale ps
static void rtxt(int px, int y, const char *s, int scale, int ps, uint16_t c)
{
    float k = rv_k();
    int py = (int)(((float)(y + RV_OY) + 3.5f * (float)scale) * k) - 7 * ps / 2;
    // (the AA glyphs reach about 2 scale px over the dot line and 4 scale px under it)
    if (py - 2 * ps - 2 >= s_by1 || py + 11 * ps + 4 <= s_by0) {
        return;
    }
    aa_text(px, py, s, ps, c);
}
static int rps(int scale)
{
    return scale == 1 ? 2 : (int)((float)scale * rv_k() + 0.5f);
}
// a text that keeps its left end (the text on the panel is wider than in the logical square)
static void TXT(int x, int y, const char *s, int scale, uint16_t c)
{
    rtxt(rv(x), y, s, scale, rps(scale), c);
}
// a text that keeps its middle (cx: logical)
static void TXT_MID(int cx, int y, const char *s, int scale, uint16_t c)
{
    int ps = rps(scale);
    rtxt((int)((float)cx * rv_k()) - gfx_text_width(s, ps) / 2, y, s, scale, ps, c);
}
#elif BOARD_UI_TABLET
static void TXT(int x, int y, const char *s, int scale, uint16_t c)
{
    if (!in_band(y - 2 * scale - 2, 13 * scale + 6)) {  // (the AA glyphs are taller than the dot line)
        return;
    }
    int ps = scale == 1 ? 2 : scale == 3 ? 4 : scale * 3 / 2;  // (a whole scale)
    int px = (int)lroundf(1.5f * (float)x + 0.75f * (float)gfx_text_width(s, scale) -
                          0.5f * (float)gfx_text_width(s, ps));
    int py = (int)lroundf(1.5f * (float)y + 5.25f * (float)scale - 3.5f * (float)ps);
    int wp = gfx_text_width(s, ps);
    if (px + wp > gfx_w) {
        px = gfx_w - wp;  // (the bigger text stays in the frame)
    }
    if (px < 0) {
        px = 0;
    }
    aa_text(px, py, s, ps, c);
}
#else
static void TXT(int x, int y, const char *s, int scale, uint16_t c)
{
    // (the AA glyphs reach about 2 scale px over the dot line and 4 scale px under it)
    if (y - 2 * scale - 2 >= s_by1 || y + 11 * scale + 4 <= s_by0) {
        return;
    }
    aa_text(x, y, s, scale, c);
}
#endif

#if BOARD_ROUND
// the width of a text in the logical frame (the text on the panel has a whole scale)
static int rtw(const char *s, int scale)
{
    return (int)((float)gfx_text_width(s, rps(scale)) / rv_k()) + 1;
}
#endif

static void TXT_IN(int x, int w, int y, const char *s, int scale, uint16_t c)
{
#if BOARD_ROUND
    TXT_MID(x + w / 2, y, s, scale, c);
    return;
#endif
    TXT(x + (w - gfx_text_width(s, scale)) / 2, y, s, scale, c);
}

static void TXT_C(int y, const char *s, int scale, uint16_t c)
{
#if BOARD_UI_TABLET || BOARD_ROUND
    TXT_IN(0, SW, y, s, scale, c);
#else
    TXT_IN(0, gfx_w, y, s, scale, c);
#endif
}

// the largest scale (up to `scale`) that fits in w
static int fit_scale(const char *s, int w, int scale)
{
#if BOARD_ROUND
    // (the round view: the text on the panel has a whole scale, wider than in the logical frame)
    while (scale > 1 && gfx_text_width(s, rps(scale)) > (int)((float)w * rv_k())) {
        scale--;
    }
    return scale;
#endif
    while (scale > 1 && gfx_text_width(s, scale) > w) {
        scale--;
    }
    return scale;
}

static void text_fit(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    scale = fit_scale(s, w, scale);
    TXT_IN(x, w, y, s, scale, c);
}

// ---------------------------------------------------------------- widgets

static int s_px = -1, s_py = -1;  // the finger now (logical), -1 = none

static bool pressed(rc_t r)
{
    return s_px >= 0 && in_rc(r, s_px, s_py);
}

// a panel with cut corners
static void panel(rc_t r, uint16_t fill, uint16_t edge)
{
    if (!in_band(r.y, r.h)) {
        return;
    }
    R(r.x + 1, r.y + 1, r.w - 2, r.h - 2, fill);
    R(r.x + 2, r.y, r.w - 4, 1, edge);
    R(r.x + 2, r.y + r.h - 1, r.w - 4, 1, edge);
    R(r.x, r.y + 2, 1, r.h - 4, edge);
    R(r.x + r.w - 1, r.y + 2, 1, r.h - 4, edge);
}

// a button: the label at scale 2 (or less to fit); sub: a small second line
static void button(rc_t r, const char *label, const char *sub, uint16_t accent, bool on)
{
    if (!in_band(r.y, r.h)) {
        return;
    }
    bool p = pressed(r);
    panel(r, p ? C_PANEL_HI : (on ? rgb(30, 80, 52) : C_PANEL), on ? accent : C_EDGE);
    int sc = fit_scale(label, r.w - 6, r.h >= 44 ? 2 : (r.h >= 30 ? 2 : 1));
    int th = 7 * sc + (sub ? 10 : 0);
    int y = r.y + (r.h - th) / 2;
    TXT_IN(r.x, r.w, y, label, sc, on ? accent : C_SILK);
    if (sub) {
        TXT_IN(r.x, r.w, y + 7 * sc + 3, sub, 1, C_DIM);
    }
}

static void lamp(int cx, int cy, int r, bool on)
{
    if (!in_band(cy - r - 3, 2 * r + 7)) {
        return;
    }
    if (on) {
        CIRC(cx, cy, r + 3, C_LAMP_GLOW);
        CIRC(cx, cy, r, C_LAMP_ON);
        CIRC(cx - r / 3, cy - r / 3, r / 3 > 0 ? r / 3 : 1, rgb(255, 245, 200));
    } else {
        CIRC(cx, cy, r, C_LAMP_OFF);
        CIRC(cx, cy, r - 2 > 0 ? r - 2 : 1, rgb(30, 28, 16));
    }
}

static void star(int cx, int cy, int r, bool on)
{
    uint16_t c = on ? C_GOLD : rgb(50, 70, 56);
    // a simple 5-point star from rows: a diamond and a bar
    for (int dy = -r; dy <= r; dy++) {
        int w = dy < 0 ? (r + dy) / 2 : r - dy;
        if (dy == -r / 4 || dy == -r / 4 + 1) {
            w = r;
        }
        if (w < 0) {
            w = 0;
        }
        R(cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}

static void stars_row(int cx, int cy, int n, int r)
{
    for (int i = 0; i < 3; i++) {
        star(cx + (i - 1) * (2 * r + 6), cy, r, i < n);
    }
}

static void lock_icon(int cx, int cy, uint16_t c)
{
    R(cx - 7, cy - 2, 14, 11, c);
    FRAME(cx - 5, cy - 9, 10, 9, 2, c);
}

static void bin8(char *out, int v)
{
    for (int i = 0; i < 8; i++) {
        out[i + (i >= 4)] = (v & (0x80 >> i)) ? '1' : '0';
    }
    out[4] = ' ';
    out[9] = 0;
}

// ---------------------------------------------------------------- sounds

static void snd_click(void)
{
    sfx_tone(3200, 1800, 7, W_NOISE, 34);  // a relay
}

static void snd_good(void)
{
    sfx_tone(1320, 1320, 60, W_SQUARE, 22);
    sfx_tone(1760, 1760, 90, W_SQUARE, 18);
}

static void snd_bad(void)
{
    sfx_tone(150, 110, 220, W_SQUARE, 30);  // a buzz
}

// ---------------------------------------------------------------- the state

typedef enum {
    ST_TITLE,
    ST_LEVELS,
    ST_BIN,
    ST_GATE,
    ST_BUILD,
    ST_LAB,
    ST_CPU,
    ST_RESULT,
    ST_REF,
} st_t;

enum {
    M_BIN,
    M_GATE,
    M_BUILD,
    M_CPU,
    M_REF,
    N_MODES
};

static const char *const MODE_NAME[N_MODES] = {"BINARY", "GATES", "BUILD", "CPU", "REF"};
static const char *const MODE_SUB[N_MODES] = {"0 AND 1", "LOGIC", "CIRCUITS", "6502", "CARDS"};
#define N_BIN_T   9   // FREE + 8 levels
#define N_GATE_T  7   // 6 gates + MYSTERY
#define N_BUILD_P 12  // puzzles (the LAB is tile 12)
#define N_BUILD_T 13
#define N_CPU_T   8
static const uint8_t N_TILES[4] = {N_BIN_T, N_GATE_T, N_BUILD_T, N_CPU_T};
static const char *const KEY_STARS[4] = {"bits_bin", "bits_gate", "bits_build", "bits_cpu"};

static st_t s_state;
static int s_mode, s_tile;
static int64_t s_now;
static uint32_t s_stars[4];  // 2 bits for each tile
static uint32_t s_open;      // bit m: mode m open; bit 8 + m: all levels of mode m open
static int s_lv_page;        // the page of the level tiles (CoreS3: 16 tiles)

// a short text in the middle of the play area
static char s_pop[40];
static uint16_t s_pop_c;
static int64_t s_pop_at;

static void pop(const char *t, uint16_t c)
{
    snprintf(s_pop, sizeof(s_pop), "%s", t);
    s_pop_c = c;
    s_pop_at = s_now;
}

static int stars_get(int m, int t)
{
    return (int)((s_stars[m] >> (2 * t)) & 3);
}

static void stars_set(int m, int t, int n)
{
    if (n <= stars_get(m, t)) {
        return;
    }
    s_stars[m] = (s_stars[m] & ~(3u << (2 * t))) | ((uint32_t)n << (2 * t));
    store_set(KEY_STARS[m], s_stars[m]);
}

static int stars_sum(int m)
{
    int n = 0;
    for (int t = 0; t < N_TILES[m]; t++) {
        n += stars_get(m, t);
    }
    return n;
}

static int solved_count(int m)
{
    int n = 0;
    for (int t = 0; t < N_TILES[m]; t++) {
        n += stars_get(m, t) > 0;
    }
    return n;
}

static bool mode_open(int m)
{
    if (m == M_BIN || m == M_REF || (s_open & (1u << m))) {
        return true;
    }
    switch (m) {
    case M_GATE:
        return stars_get(M_BIN, 2) > 0;  // after A BYTE
    case M_BUILD:
        return solved_count(M_GATE) >= 3;
    case M_CPU:
        return solved_count(M_BUILD) >= 3;
    default:
        return false;
    }
}

static const char *mode_lock_text(int m)
{
    switch (m) {
    case M_GATE:
        return "PASS A BYTE";
    case M_BUILD:
        return "3 GATES";
    case M_CPU:
        return "3 BUILDS";
    default:
        return "";
    }
}

static bool tile_open(int m, int t)
{
    if (s_open & (1u << (8 + m))) {
        return true;
    }
    if (m == M_BIN) {
        return t <= 1 || stars_get(m, t - 1) > 0;
    }
    if (m == M_BUILD && t == N_BUILD_P) {
        return stars_get(m, 8) > 0;  // the LAB: after the 2-bit adder
    }
    return t == 0 || stars_get(m, t - 1) > 0;
}

// ================================================================ BINARY

enum {
    BK_FREE,
    BK_SET,
    BK_READ,
    BK_FUSE,
    BK_OPS
};
enum {
    F_DEC,
    F_HEX,
    F_SIGNED,
    F_MIX
};

typedef struct {
    const char *name, *help;
    uint8_t kind, fmt, nq;
    uint16_t time_s;
    uint8_t mask;
} binlev_t;

static const binlev_t BIN_LEV[N_BIN_T] = {
        {"FREE", "PLAY WITH THE SWITCHES", BK_FREE, F_DEC, 0, 0, 0xFF},
        {"FOUR BITS", "SET THE NUMBER: 8 4 2 1", BK_SET, F_DEC, 10, 20, 0x0F},
        {"A BYTE", "8 BITS: 0 TO 255", BK_SET, F_DEC, 10, 25, 0xFF},
        {"READ", "WHAT NUMBER IS THIS?", BK_READ, F_DEC, 10, 15, 0xFF},
        {"HEX", "$ = HEX: 4 BITS = 1 DIGIT", BK_SET, F_HEX, 10, 20, 0xFF},
        {"HEX READ", "WHICH HEX IS THIS?", BK_READ, F_HEX, 10, 12, 0xFF},
        {"FUSE", "FAST! THE FUSE GETS SHORT", BK_FUSE, F_MIX, 15, 12, 0xFF},
        {"NEGATIVE", "TOP BIT = -128", BK_SET, F_SIGNED, 10, 25, 0xFF},
        {"BIT OPS", "SHIFT, AND, OR, XOR, NOT", BK_OPS, F_HEX, 10, 25, 0xFF},
};

static uint8_t s_sw;     // the switches (bit 7 = the left switch)
static int s_target;     // the value to set (can be negative)
static int s_tfmt;       // the format of the target now
static uint8_t s_shown;  // READ: the bits on the switches
static int s_choice[4], s_choice_ok;
static int s_q, s_lives, s_score, s_streak, s_best_streak;
static int64_t s_q_at;        // the start of the question
static int s_q_ms;            // the time for the question
static int64_t s_wait_until;  // feedback: no input until this time
static bool s_reveal;         // show the right answer (after a time-out)
static char s_op[2][24];      // BIT OPS: the two lines of the task

static int rnd(int n)
{
    return n > 0 ? (int)(esp_random() % (uint32_t)n) : 0;
}

static void fmt_value(char *out, int len, int v, int fmt)
{
    if (fmt == F_HEX) {
        snprintf(out, len, "$%02X", v & 0xFF);
    } else if (fmt == F_SIGNED) {
        snprintf(out, len, "%+d", v);  // the sign is the point here
    } else {
        snprintf(out, len, "%d", v);
    }
}

// BK_READ: a wrong answer near the shown number (one bit, a small step, the nibbles turned, or random)
static int bin_wrong(void)
{
    int v;
    switch (rnd(4)) {
    case 0:
        v = s_shown ^ (1 << rnd(8));
        break;
    case 1:
        v = s_shown + (rnd(2) ? 1 : -1) * (1 + rnd(3));
        break;
    case 2:
        v = ((s_shown << 4) | (s_shown >> 4)) & 0xFF;
        break;
    default:
        v = rnd(256);
        break;
    }
    v &= 0xFF;
    return v;
}

// BK_READ: the answer of card i (right: the card of the shown number); not the same as a card before it
static int bin_choice(int i, int right)
{
    int v;
    bool dup;
    do {
        if (i == right) {
            v = s_shown;
        } else {
            v = bin_wrong();
        }
        dup = (i != right && v == s_shown);
        for (int j = 0; j < i; j++) {
            dup |= s_choice[j] == v;
        }
    } while (dup);
    return v;
}

static void bin_question(void)
{
    const binlev_t *L = &BIN_LEV[s_tile];
    s_q_at = s_now;
    s_reveal = false;
    s_q_ms = L->time_s * 1000;
    s_tfmt = L->fmt;
    switch (L->kind) {
    case BK_SET:
    case BK_FUSE:
        s_sw = 0;
        if (L->kind == BK_FUSE) {
            s_q_ms = 12000 - s_q * 600;
            if (s_q_ms < 4500) {
                s_q_ms = 4500;
            }
            s_tfmt = rnd(2) ? F_HEX : F_DEC;
        }
        do {
            if (L->fmt == F_SIGNED) {
                s_target = rnd(4) ? -1 - rnd(128) : 1 + rnd(127);
            } else {
                s_target = 1 + rnd(L->mask);
            }
        } while (s_target == 0);
        break;
    case BK_READ: {
        s_shown = (uint8_t)(1 + rnd(255));
        s_sw = s_shown;
        int right = rnd(4);
        for (int i = 0; i < 4; i++) {
            s_choice[i] = bin_choice(i, right);
        }
        s_choice_ok = right;
        break;
    }
    case BK_OPS: {
        s_sw = 0;
        int a, m, op, r;
    again:
        a = 1 + rnd(254);
        m = 1 + rnd(254);
        op = rnd(6);
        char ab[12], mb[12];
        bin8(ab, a);
        bin8(mb, m);
        snprintf(s_op[0], sizeof(s_op[0]), "A = %s", ab);
        switch (op) {
        case 0:
            r = a << 1;
            snprintf(s_op[1], sizeof(s_op[1]), "A << 1  (ASL)");
            break;
        case 1:
            r = a >> 1;
            snprintf(s_op[1], sizeof(s_op[1]), "A >> 1  (LSR)");
            break;
        case 2:
            r = a & m;
            snprintf(s_op[1], sizeof(s_op[1]), "AND %s", mb);
            break;
        case 3:
            r = a | m;
            snprintf(s_op[1], sizeof(s_op[1]), "OR  %s", mb);
            break;
        case 4:
            r = a ^ m;
            snprintf(s_op[1], sizeof(s_op[1]), "XOR %s", mb);
            break;
        default:
            r = ~a;
            snprintf(s_op[1], sizeof(s_op[1]), "NOT A");
            break;
        }
        s_target = r & 0xFF;
        if (s_target == 0) {
            goto again;  // the switches start at 0: a zero result is no task
        }
        break;
    }
    default:
        break;
    }
}

static void level_start_tune(void)
{
    tune_play(TUNE_BITS);
}

static void bin_start(void)
{
    s_state = ST_BIN;
    s_q = 0;
    s_lives = 3;
    s_score = 0;
    s_streak = s_best_streak = 0;
    s_sw = 0;
    s_wait_until = 0;
    if (BIN_LEV[s_tile].kind != BK_FREE) {
        bin_question();
        level_start_tune();
    }
}

static void result_show(bool ok, int stars, const char *detail);
static char s_res_detail[48];

static void bin_next(void)
{
    s_q++;
    if (s_q >= BIN_LEV[s_tile].nq) {
        snprintf(s_res_detail, sizeof(s_res_detail), "SCORE %d  BEST ROW %d", s_score, s_best_streak);
        result_show(true, s_lives, s_res_detail);
        return;
    }
    bin_question();
}

static void bin_lose(void)
{
    snd_bad();
    s_lives--;
    s_streak = 0;
    if (s_lives <= 0) {
        snprintf(s_res_detail, sizeof(s_res_detail), "SCORE %d", s_score);
        result_show(false, 0, s_res_detail);
    }
}

static void bin_right(void)
{
    snd_good();
    s_streak++;
    if (s_streak > s_best_streak) {
        s_best_streak = s_streak;
    }
    int left = s_q_ms - (int)(s_now - s_q_at);
    s_score += 10 + (left > 0 ? left / 500 : 0) + s_streak;
    pop(s_streak >= 3 ? "STREAK!" : "RIGHT", C_GOOD);
    s_wait_until = s_now + 450;
}

static bool bin_match(void)
{
    const binlev_t *L = &BIN_LEV[s_tile];
    if (L->fmt == F_SIGNED && L->kind == BK_SET) {
        return (int8_t)s_sw == s_target;
    }
    return s_sw == (uint8_t)s_target;
}

static void bin_update(void)
{
    const binlev_t *L = &BIN_LEV[s_tile];
    if (L->kind == BK_FREE || s_state != ST_BIN) {
        return;
    }
    if (s_wait_until) {
        if (s_now < s_wait_until) {
            return;
        }
        s_wait_until = 0;
        bin_next();
        return;
    }
    if (s_now - s_q_at > s_q_ms) {
        // the time is out: show the answer, then go on
        s_reveal = true;
        if (L->kind != BK_READ) {
            s_sw = (uint8_t)s_target;
        }
        pop("TIME!", C_BAD);
        bin_lose();
        if (s_state == ST_BIN) {
            s_wait_until = s_now + 1300;
        }
    }
}

static int bin_switch_at(int x, int y)
{
    if (y < B_SW_Y || x < B_SW_X) {
        return -1;
    }
    int i = (x - B_SW_X) / B_SW_W;
    return i >= 0 && i < 8 ? i : -1;
}

static void bin_input(const input_event_t *ev)
{
    const binlev_t *L = &BIN_LEV[s_tile];
    if (s_wait_until) {
        return;
    }
    if (L->kind == BK_READ) {
        if (ev->kind != IN_TAP) {
            return;
        }
        for (int i = 0; i < 4; i++) {
            if (!in_rc(B_CHOICE(i), ev->x, ev->y)) {
                continue;
            }
            if (i == s_choice_ok) {
                bin_right();
            } else {
                pop("NO", C_BAD);
                s_reveal = true;
                bin_lose();
                if (s_state == ST_BIN) {
                    s_wait_until = s_now + 1300;
                }
            }
            return;
        }
        return;
    }
    if (ev->kind != IN_DOWN) {
        return;
    }
    int i = bin_switch_at(ev->x, ev->y);
    if (i < 0) {
        return;
    }
    uint8_t bit = (uint8_t)(0x80 >> i);
    if (!(L->mask & bit)) {
        return;  // a locked switch (FOUR BITS)
    }
    s_sw ^= bit;
    snd_click();
    if (L->kind != BK_FREE && bin_match()) {
        bin_right();
    }
}

// one switch: the lamp, the lever and the weight (a short switch has no top lamp)
static void draw_switch(int x, int y, int w, int h, bool on, bool locked, const char *weight)
{
    if (!in_band(y, h)) {
        return;
    }
    rc_t r = RC(x + 2, y + 2, w - 4, h - 4);
    panel(r, locked ? rgb(10, 30, 20) : C_PCB, locked ? rgb(30, 60, 44) : C_EDGE);
    int cx = x + w / 2;
    int top = y + 5, bottom = y + h - (weight[0] ? 16 : 6);
    if (h >= 90) {
        int lr = w / 6 < 9 ? w / 6 : 9;
        if (lr < 4) {
            lr = 4;
        }
        if (!locked) {
            lamp(cx, top + lr + 2, lr, on);
        }
        top += 2 * lr + 10;
    }
    int sh = bottom - top;
    if (sh < 10) {
        sh = 10;
    }
    int sw = w / 3 < 18 ? w / 3 : 18;
    R(cx - sw / 2, top, sw, sh, C_CHIP);
    int kh = sh / 2 - 2;
    if (kh < 3) {
        kh = 3;
    }
    int ky = on ? top + 2 : top + sh - kh - 2;
    uint16_t kc = locked ? rgb(40, 50, 44) : (on ? (h >= 90 ? rgb(230, 230, 220) : C_LAMP_ON) : rgb(150, 156, 150));
    R(cx - sw / 2 - 3, ky, sw + 6, kh, kc);
    R(cx - sw / 2 - 3, ky + kh / 2, sw + 6, 1, rgb(90, 96, 90));
    if (weight[0]) {
        TXT_IN(x, w, y + h - 14, weight, 1, locked ? rgb(50, 80, 60) : (on ? C_GOLD : C_DIM));
    }
}

static void draw_switches(uint8_t v, uint8_t mask, bool signed_w, int x0, int y0, int w, int h)
{
    static const char *const WT[8] = {"128", "64", "32", "16", "8", "4", "2", "1"};
    for (int i = 0; i < 8; i++) {
        const char *wt = (i == 0 && signed_w) ? "-128" : WT[i];
        draw_switch(x0 + i * w, y0, w, h, v & (0x80 >> i), !(mask & (0x80 >> i)), wt);
    }
    // a gap mark between the two nibbles (a hex digit is 4 bits)
    R(x0 + 4 * w - 1, y0 + 6, 2, h - 12, C_TRACE);
}

#if BOARD_ROUND
// the round bar: the cap above the frame in the colour of the bar, the title in it (scale 2 or 1: the chord at the
// row of the title is about 150)
static void draw_cap(const char *title)
{
    R(0, -RV_OY, SW, RV_OY, C_TOP);
    TXT_MID(160, CAP_Y, title, fit_scale(title, 2 * rch(CAP_Y) - 12, 2), C_GOLD);
}
#endif

static void draw_top(const char *title, const char *right)
{
#if BOARD_ROUND
    if (!in_band(-RV_OY, RV_OY + TOP_H)) {
        return;
    }
    draw_cap(title);
    R(0, 0, SW, TOP_H, C_TOP);
    R(0, TOP_H - 1, SW, 1, C_EDGE);
    button(BACK_RC, "<", NULL, C_SILK, false);
    if (right) {
        TXT(BAR_R - 4 - rtw(right, 1), (TOP_H - 7) / 2, right, 1, C_SILK);
    }
    return;
#endif
    if (!in_band(0, TOP_H)) {
        return;
    }
    R(0, 0, SW, TOP_H, C_TOP);
    R(0, TOP_H - 1, SW, 1, C_EDGE);
    button(BACK_RC, "<", NULL, C_SILK, false);
    int x = BACK_RC.w + 6;
    int sc = fit_scale(title, SW - x - 4 - (right ? gfx_text_width(right, 1) + 8 : 0), 2);
    TXT(x, (TOP_H - 7 * sc) / 2, title, sc, C_GOLD);
    if (right) {
        TXT(SW - 4 - gfx_text_width(right, 1), (TOP_H - 7) / 2, right, 1, C_SILK);
    }
}

static void draw_lives(int x, int y)
{
    for (int i = 0; i < 3; i++) {
        lamp(x + i * 16, y, 5, i < s_lives);
    }
}

static void render_bin(void)
{
    const binlev_t *L = &BIN_LEV[s_tile];
    char t[48], v[24];
    snprintf(t, sizeof(t), "%s", L->name);
    char right[48] = "";
    if (L->kind != BK_FREE) {
        snprintf(right, sizeof(right), "%d/%d  %d", s_q + 1 > L->nq ? L->nq : s_q + 1, L->nq, s_score);
    }
    draw_top(t, right[0] ? right : NULL);
    R(0, TOP_H, SW, BODY_H, C_BG);
    rc_t p = B_PANEL_RC;
    if (in_band(p.y, p.h)) {
        panel(RC(p.x + 2, p.y + 2, p.w - 4, p.h - 4), C_PCB, C_EDGE);
        if (L->kind == BK_FREE) {
            snprintf(v, sizeof(v), "%d", s_sw);
            TXT(p.x + 10, p.y + 8, "DEC", 1, C_DIM);
            TXT(p.x + 40, p.y + 6, v, 2, C_SILK);
            snprintf(v, sizeof(v), "$%02X", s_sw);
            TXT(p.x + 10, p.y + 8 + (BOARD_BAR_SCREEN ? 26 : 0), BOARD_BAR_SCREEN ? "HEX" : "", 1, C_DIM);
#if BOARD_BAR_SCREEN
            TXT(p.x + 40, p.y + 32, v, 2, C_CYAN);
            snprintf(v, sizeof(v), "%d", (int8_t)s_sw);
            TXT(p.x + 10, p.y + 60, "+/-", 1, C_DIM);
            TXT(p.x + 40, p.y + 58, v, 2, C_GOLD);
            TXT(p.x + 10, p.y + 90, "TAP THE", 1, C_DIM);
            TXT(p.x + 10, p.y + 102, "SWITCHES", 1, C_DIM);
#else
            TXT(p.x + B_HEX_X, p.y + 6, "HEX", 1, C_DIM);
            TXT(p.x + B_HEX_X + 26, p.y + 4, v, 2, C_CYAN);
            snprintf(v, sizeof(v), "%d", (int8_t)s_sw);
            TXT(p.x + B_SGN_X, p.y + 6, "+/-", 1, C_DIM);
            TXT(p.x + B_SGN_X + 26, p.y + 4, v, 2, C_GOLD);
            char b[12];
            bin8(b, s_sw);
            TXT_IN(p.x, p.w, p.y + 34, b, 3, C_SILK);
#endif
        } else {
            // the target
            int ty = p.y + 6;
            if (L->kind == BK_OPS) {
                TXT_IN(p.x, p.w, ty, s_op[0], 1, C_DIM);
                TXT_IN(p.x, p.w, ty + 12, s_op[1], 1, C_CYAN);
                TXT_IN(p.x, p.w, ty + 24, "= ?", 1, C_SILK);
            } else if (L->kind == BK_READ) {
                TXT_IN(p.x, p.w, ty, s_tfmt == F_HEX ? "WHICH HEX?" : "WHICH NUMBER?", BOARD_BAR_SCREEN ? 2 : 1,
                       C_SILK);
            } else {
                fmt_value(v, sizeof(v), s_target, s_tfmt);
                TXT_IN(p.x, p.w, ty, "SET", 1, C_DIM);
                TXT_IN(p.x, p.w, ty + 10, v, BOARD_BAR_SCREEN ? 4 : 3, C_GOLD);
            }
            if (s_tile == 1 && L->kind == BK_SET) {
                snprintf(v, sizeof(v), "NOW %d", s_sw);
                TXT_IN(p.x, p.w, ty + (BOARD_BAR_SCREEN ? 46 : 36), v, 1, C_DIM);
            }
            if (L->kind == BK_READ) {
                for (int i = 0; i < 4; i++) {
                    char c[12];
                    fmt_value(c, sizeof(c), s_choice[i], s_tfmt);
                    bool show = s_reveal && i == s_choice_ok;
                    button(B_CHOICE(i), c, NULL, show ? C_GOOD : C_SILK, show);
                }
            }
            // the fuse: the time left
            int left = s_q_ms - (int)(s_now - s_q_at);
            if (left < 0 || s_wait_until) {
                left = s_wait_until ? left : 0;
            }
            if (left < 0) {
                left = 0;
            }
            int fw = (p.w - 16) * left / (s_q_ms > 0 ? s_q_ms : 1);
            int fy = p.y + p.h - 12;
            if (L->kind == BK_READ && BOARD_BAR_SCREEN) {
                fy = p.y + 38;
            }
            if (L->kind == BK_READ && !BOARD_BAR_SCREEN) {
                fy = p.y + 16;
            }
            R(p.x + 8, fy, p.w - 16, 4, rgb(40, 30, 14));
            R(p.x + 8, fy, fw, 4, left < 3000 ? C_BAD : C_TRACE_ON);
            if (fw > 2) {
                CIRC(p.x + 8 + fw, fy + 2, 3 + (int)((s_now / 60) & 1), rgb(255, 240, 160));
            }

            if (s_reveal && L->kind != BK_READ) {
                char b[12];
                bin8(b, s_target);
                TXT_IN(p.x, p.w, fy - 12, b, 1, C_BAD);
            }
        }
    }
    if (L->kind != BK_FREE && in_band(0, TOP_H)) {
#if BOARD_ROUND
        draw_lives(BAR_X0 + 12, TOP_H / 2);
#else
        draw_lives(SW - 10 - gfx_text_width(right, 1) - 46, TOP_H / 2);
#endif
    }
    bool signed_w = L->fmt == F_SIGNED;
    draw_switches(s_sw, L->mask, signed_w, B_SW_X, B_SW_Y, B_SW_W, B_SW_H);
}

// ================================================================ GATES and BUILD blocks

enum {
    B_NONE,
    B_NOT,
    B_AND,
    B_OR,
    B_XOR,
    B_NAND,
    B_NOR,
    B_HA,
    B_FA,
    B_SR,
    B_DFF,
    N_BLOCKS
};

typedef struct {
    const char *name;
    uint8_t nin, nout;
    const char *pin[3];
    const char *out[2];
    const char *about;
} block_t;

static const block_t BLOCK[N_BLOCKS] = {
        {"-", 0, 0, {0}, {0}, ""},
        {"NOT", 1, 1, {"IN"}, {""}, "1 IF IN IS 0"},
        {"AND", 2, 1, {"A", "B"}, {""}, "1 IF A AND B ARE 1"},
        {"OR", 2, 1, {"A", "B"}, {""}, "1 IF A OR B IS 1"},
        {"XOR", 2, 1, {"A", "B"}, {""}, "1 IF A AND B ARE NOT THE SAME"},
        {"NAND", 2, 1, {"A", "B"}, {""}, "NOT AND: 0 ONLY IF BOTH ARE 1"},
        {"NOR", 2, 1, {"A", "B"}, {""}, "NOT OR: 1 ONLY IF BOTH ARE 0"},
        {"HALF+", 2, 2, {"A", "B"}, {"S", "C"}, "HALF ADDER: S = A XOR B, C = A AND B"},
        {"FULL+", 3, 2, {"A", "B", "CI"}, {"S", "CO"}, "FULL ADDER: A + B + CARRY IN"},
        {"SR", 2, 1, {"S", "R"}, {"Q"}, "LATCH: S SETS Q TO 1, R RESETS IT"},
        {"DFF", 1, 1, {"D"}, {"Q"}, "FLIP-FLOP: Q TAKES D AT EACH CLOCK"},
};

#define BITN(b) (1u << (b))

static int gate_eval(int type, int a, int b)
{
    switch (type) {
    case B_NOT:
        return !a;
    case B_AND:
        return a & b;
    case B_OR:
        return a | b;
    case B_XOR:
        return a ^ b;
    case B_NAND:
        return !(a & b);
    case B_NOR:
        return !(a | b);
    default:
        return 0;
    }
}

// The row span of a gate shape at row dy (-h/2 .. h/2) of a gate of width w and height h, from
// the left edge x0. Returns false if the row is empty.
static bool gate_span(int type, int dy, int w, int h, int *xa, int *xb)
{
    float t = (float)dy / (float)(h / 2);
    if (t < -1 || t > 1) {
        return false;
    }
    float c = sqrtf(1.0f - t * t);
    switch (type) {
    case B_NOT:
        *xa = 0;
        *xb = (int)(w * 0.82f * (1.0f - fabsf(t)));
        return *xb > 0;
    case B_AND:
    case B_NAND:
        *xa = 0;
        *xb = (int)(w * 0.42f + w * 0.42f * c);
        return true;
    default:  // OR, XOR, NOR: a curved back and a pointed nose
        *xa = (int)(w * 0.16f * c);
        *xb = (int)(w * 0.84f * (0.25f + 0.75f * sqrtf(c)));
        if (fabsf(t) > 0.97f) {
            *xb = *xa + 2;
        }
        return true;
    }
}

// a gate symbol, w x h, at (x, y); the output bubble for NOT, NAND, NOR
static void gate_draw(int type, int x, int y, int w, int h, uint16_t body, uint16_t edge)
{
    if (!in_band(y - 2, h + 4)) {
        return;
    }
    bool bubble = type == B_NOT || type == B_NAND || type == B_NOR;
    int cy = y + h / 2;
    for (int dy = -h / 2; dy <= h / 2; dy++) {
        int yy = cy + dy;
#if BOARD_UI_TABLET || BOARD_ROUND
        if (!in_band(yy, 1)) {
            continue;
        }
        int xa, xb;
        if (!gate_span(type, dy, w, h, &xa, &xb)) {
            continue;
        }
        R(x + xa, yy, xb - xa + 1, 1, edge);
        if (xb - xa > 4 && dy > -h / 2 + 1 && dy < h / 2 - 1) {
            R(x + xa + 2, yy, xb - xa - 3, 1, body);
        }
        if (type == B_XOR) {
            int xe = (int)(w * 0.16f * sqrtf(1.0f - ((float)dy / (float)(h / 2)) * ((float)dy / (float)(h / 2)))) - 6;
            R(x + xe, yy, 2, 1, edge);
        }
#else
        if (yy < s_by0 || yy >= s_by1) {
            continue;
        }
        int xa, xb;
        if (!gate_span(type, dy, w, h, &xa, &xb)) {
            continue;
        }
        gfx_fill_rect(x + xa, yy, xb - xa + 1, 1, edge);
        if (xb - xa > 4 && dy > -h / 2 + 1 && dy < h / 2 - 1) {
            gfx_fill_rect(x + xa + 2, yy, xb - xa - 3, 1, body);
        }
        if (type == B_XOR) {
            int xe = (int)(w * 0.16f * sqrtf(1.0f - ((float)dy / (float)(h / 2)) * ((float)dy / (float)(h / 2)))) - 6;
            gfx_fill_rect(x + xe, yy, 2, 1, edge);
        }
#endif
    }
    if (bubble) {
        int xa, xb;
        gate_span(type, 0, w, h, &xa, &xb);
        int br = h / 10 < 3 ? 3 : h / 10;
        CIRC(x + xb + br + 1, cy, br, edge);
        CIRC(x + xb + br + 1, cy, br - 2 > 0 ? br - 2 : 1, body);
    }
}

// ---------------------------------------------------------------- GATES mode

static int s_gin[2];       // the inputs A, B
static int8_t s_gtab[4];   // the output column: -1 = not filled
static int s_gmiss;        // wrong checks
static int s_gtype;        // the gate of the level (or of the MYSTERY round)
static int64_t s_gbad_at;  // the time of the last wrong check
static uint8_t s_gbad;     // the wrong rows
static int s_mround, s_morder[6];
static const uint8_t GATE_OF[6] = {B_NOT, B_AND, B_OR, B_XOR, B_NAND, B_NOR};

// the 0 button (v = 0) or the 1 button (v = 1) of output row r
static rc_t gate_qbtn(int r, int v)
{
    rc_t q = G_Q_RC(r);
    int w = q.w / 2;
    return RC(q.x + v * w, q.y + 14, w - 2, q.h - 14);
}

static int gate_rows(void)
{
    return s_gtype == B_NOT ? 2 : 4;
}

static int row_a(int r)
{
    return s_gtype == B_NOT ? r : r >> 1;
}

static int row_b(int r)
{
    return s_gtype == B_NOT ? 0 : r & 1;
}

static void gate_start(void)
{
    s_state = ST_GATE;
    s_gin[0] = s_gin[1] = 0;
    memset(s_gtab, -1, sizeof(s_gtab));
    s_gmiss = 0;
    s_gbad = 0;
    s_wait_until = 0;
    s_lives = 3;
    if (s_tile < 6) {
        s_gtype = GATE_OF[s_tile];
    } else {
        for (int i = 0; i < 6; i++) {
            s_morder[i] = i;
        }
        for (int i = 5; i > 0; i--) {
            int j = rnd(i + 1), t = s_morder[i];
            s_morder[i] = s_morder[j];
            s_morder[j] = t;
        }
        s_mround = 0;
        s_gtype = GATE_OF[s_morder[0]];
    }
    level_start_tune();
}

static void gate_check(void)
{
    int n = gate_rows();
    for (int r = 0; r < n; r++) {
        if (s_gtab[r] < 0) {
            return;
        }
    }
    s_gbad = 0;
    for (int r = 0; r < n; r++) {
        if (s_gtab[r] != gate_eval(s_gtype, row_a(r), row_b(r))) {
            s_gbad |= (uint8_t)BITN(r);
        }
    }
    if (s_gbad) {
        snd_bad();
        s_gmiss++;
        s_gbad_at = s_now;
        pop("NOT YET: TRY THE INPUTS", C_BAD);
        for (int r = 0; r < n; r++) {
            if (s_gbad & BITN(r)) {
                s_gtab[r] = -1;
            }
        }
    } else {
        snd_good();
        int st = s_gmiss == 0 ? 3 : (s_gmiss == 1 ? 2 : 1);
        snprintf(s_res_detail, sizeof(s_res_detail), "%s: %s", BLOCK[s_gtype].name, BLOCK[s_gtype].about);
        pop("RIGHT!", C_GOOD);
        s_wait_until = s_now + 700;
        s_gmiss = -st;  // remember the stars until the result shows
    }
}

static void gate_update(void)
{
    if (!s_wait_until || s_now < s_wait_until) {
        return;
    }
    s_wait_until = 0;
    if (s_tile < 6) {
        result_show(true, -s_gmiss, s_res_detail);
        return;
    }
    // MYSTERY: the next round
    s_mround++;
    if (s_mround >= 6) {
        snprintf(s_res_detail, sizeof(s_res_detail), "%d OF 6 GATES FOUND", 6 - (3 - s_lives));
        result_show(true, s_lives, s_res_detail);
        return;
    }
    s_gtype = GATE_OF[s_morder[s_mround]];
    s_gin[0] = s_gin[1] = 0;
}

static void gate_input(const input_event_t *ev)
{
    if (s_wait_until) {
        return;
    }
    if (ev->kind == IN_DOWN) {
        for (int i = 0; i < (s_gtype == B_NOT ? 1 : 2); i++) {
            if (in_rc(G_IN_RC(i), ev->x, ev->y)) {
                s_gin[i] ^= 1;
                snd_click();
                return;
            }
        }
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (s_tile < 6) {
        for (int r = 0; r < gate_rows(); r++) {
            for (int v = 0; v < 2; v++) {
                if (in_rc(gate_qbtn(r, v), ev->x, ev->y)) {
                    s_gtab[r] = (int8_t)v;
                    snd_click();
                    gate_check();
                    return;
                }
            }
        }
    } else {
        for (int i = 0; i < 6; i++) {
            if (!in_rc(G_NAME_RC(i), ev->x, ev->y)) {
                continue;
            }
            if (GATE_OF[i] == s_gtype) {
                snd_good();
                pop("RIGHT!", C_GOOD);
                s_wait_until = s_now + 700;
            } else {
                snd_bad();
                s_lives--;
                pop("NO", C_BAD);
                if (s_lives <= 0) {
                    snprintf(s_res_detail, sizeof(s_res_detail), "IT WAS %s", BLOCK[s_gtype].name);
                    result_show(false, 0, s_res_detail);
                }
            }
            return;
        }
    }
}

// a copper trace: lit when 1
static void trace(int x0, int y0, int x1, int y1, bool on)
{
    uint16_t c = on ? C_TRACE_ON : C_TRACE;
    if (y0 == y1) {
        R(x0 < x1 ? x0 : x1, y0 - 1, abs(x1 - x0) + 1, 3, c);
    } else {
        int xm = (x0 + x1) / 2;
        R(x0 < xm ? x0 : xm, y0 - 1, abs(xm - x0) + 1, 3, c);
        R(xm - 1, y0 < y1 ? y0 : y1, 3, abs(y1 - y0) + 1, c);
        R(xm < x1 ? xm : x1, y1 - 1, abs(x1 - xm) + 1, 3, c);
    }
}

static void render_gate(void)
{
    char t[40];
    bool mystery = s_tile >= 6;
    if (mystery) {
        snprintf(t, sizeof(t), "MYSTERY %d/6", s_mround + 1 > 6 ? 6 : s_mround + 1);
    } else {
        snprintf(t, sizeof(t), "GATE: %s", BLOCK[s_gtype].name);
    }
    draw_top(t, mystery ? NULL : "FILL THE Q ROW");
    R(0, TOP_H, SW, BODY_H, C_BG);
    int nin = s_gtype == B_NOT ? 1 : 2;
    int gw = G_SIZE, gh = G_SIZE * 3 / 4;
    int gx = G_CX - gw / 2, gy = G_CY - gh / 2;
    int out = gate_eval(s_gtype, s_gin[0], s_gin[1]);
    // the inputs and their traces
    for (int i = 0; i < nin; i++) {
        rc_t r = G_IN_RC(i);
        if (nin == 1) {
            r = RC(r.x, G_CY - r.h / 2, r.w, r.h);
        }
        int py = nin == 1 ? G_CY : gy + gh / 4 + i * gh / 2;
        trace(r.x + r.w, r.y + r.h / 2, gx + 4, py, s_gin[i]);
        char l[16];
        snprintf(l, sizeof(l), "%s=%d", i ? "B" : "A", s_gin[i]);
        button(r, l, "TAP", s_gin[i] ? C_LAMP_ON : C_SILK, s_gin[i]);
    }
    trace(gx + gw - 4, G_CY, G_LAMP_X, G_CY, out);
    if (mystery) {
        panel(RC(gx, gy, gw, gh), C_CHIP, C_EDGE);
        TXT_IN(gx, gw, G_CY - 10, "?", 3, C_GOLD);
    } else {
        gate_draw(s_gtype, gx, gy, gw - 10, gh, C_PANEL, C_SILK);
        TXT_IN(gx, gw - 14, G_CY - 4, BLOCK[s_gtype].name, 1, C_SILK);
    }
    lamp(G_LAMP_X, G_CY, 12, out);
    TXT_IN(G_LAMP_X - 20, 40, G_CY + 18, "Q", 1, C_DIM);
    if (mystery) {
        for (int i = 0; i < 6; i++) {
            button(G_NAME_RC(i), BLOCK[GATE_OF[i]].name, NULL, C_SILK, false);
        }
        draw_lives(G_LAMP_X - 16, TOP_H + 10);
        return;
    }
    // the truth table: one column for each row of inputs
    int n = gate_rows();
    int cur = s_gtype == B_NOT ? s_gin[0] : (s_gin[0] << 1 | s_gin[1]);
    for (int r = 0; r < n; r++) {
        int x = G_TAB_X + r * G_TAB_COL;
        bool here = r == cur;
        if (here) {
            R(x + 1, G_TAB_Y, G_TAB_COL - 2, G_ROWA_H * 2 + 2, rgb(30, 70, 48));
        }
        char l[16];
        snprintf(l, sizeof(l), "A %d", row_a(r));
        TXT_IN(x, G_TAB_COL, G_TAB_Y + (G_ROWA_H - 7) / 2, l, 1, here ? C_GOLD : C_DIM);
        if (nin == 2) {
            snprintf(l, sizeof(l), "B %d", row_b(r));
            TXT_IN(x, G_TAB_COL, G_TAB_Y + G_ROWA_H + (G_ROWA_H - 7) / 2, l, 1, here ? C_GOLD : C_DIM);
        }
        rc_t q = G_Q_RC(r);
        bool bad = (s_gbad & BITN(r)) && s_now - s_gbad_at < 900;
        TXT_IN(q.x, q.w, q.y + 3, bad ? "Q: NO" : (s_gtab[r] < 0 ? "Q = ?" : "Q"), 1, bad ? C_BAD : C_CYAN);
        for (int v = 0; v < 2; v++) {
            bool on = s_gtab[r] == v;
            button(gate_qbtn(r, v), v ? "1" : "0", NULL, v ? C_LAMP_ON : C_SILK, on);
        }
    }
}

// ================================================================ BUILD

#define MAX_SLOTS   6
#define SRC_NONE    (-1)
#define SRC_ZERO    4
#define SRC_ONE     5
#define SRC_G(k, o) (8 + (k) * 2 + (o))

typedef struct {
    uint8_t type;
    int8_t src[3];
} slot_t;

typedef struct {
    uint8_t val[MAX_SLOTS];  // the outputs of each slot (bit 0, bit 1)
    uint8_t q[MAX_SLOTS];    // the memory of SR and DFF blocks
} sim_t;

typedef struct {
    uint8_t in, clk, out;
} step_t;

typedef struct {
    const char *name, *goal;
    uint8_t ni, no;
    const char *in_name[4];
    const char *out_name[3];
    uint16_t palette;
    uint8_t slots, best;
    uint8_t (*f)(uint8_t in);  // the wanted outputs (combinational puzzles)
    const step_t *seq;         // the steps (memory puzzles)
    uint8_t nseq;
} puzzle_t;

static uint8_t f_not(uint8_t i)
{
    return !(i & 1);
}
static uint8_t f_and(uint8_t i)
{
    return (i & 1) & (i >> 1);
}
static uint8_t f_or(uint8_t i)
{
    return (i & 1) | ((i >> 1) & 1);
}
static uint8_t f_nor(uint8_t i)
{
    return !f_or(i);
}
static uint8_t f_xor(uint8_t i)
{
    return (i & 1) ^ ((i >> 1) & 1);
}
static uint8_t f_mux(uint8_t i)
{
    return (i & 4) ? ((i >> 1) & 1) : (i & 1);
}
static uint8_t f_ha(uint8_t i)
{
    return (uint8_t)(f_xor(i) | (f_and(i) << 1));
}
static uint8_t f_fa(uint8_t i)
{
    return (uint8_t)((i & 1) + ((i >> 1) & 1) + ((i >> 2) & 1));
}
static uint8_t f_add2(uint8_t i)
{
    return (uint8_t)((i & 3) + ((i >> 2) & 3));
}

// SR latch: in bit 0 = S, bit 1 = R
static const step_t SEQ_SR[] = {
        {2, 0, 0}, {0, 0, 0}, {1, 0, 1}, {0, 0, 1}, {2, 0, 0}, {0, 0, 0}, {1, 0, 1}, {0, 0, 1},
};
// D latch: in bit 0 = D, bit 1 = E
static const step_t SEQ_D[] = {
        {3, 0, 1}, {0, 0, 1}, {2, 0, 0}, {1, 0, 0}, {3, 0, 1}, {1, 0, 1}, {0, 0, 1}, {2, 0, 0},
};
// counter: a clock at each step after the first
static const step_t SEQ_CNT[] = {
        {0, 0, 0}, {0, 1, 1}, {0, 1, 2}, {0, 1, 3}, {0, 1, 0}, {0, 1, 1},
};

#define PAL(b) BITN(b)
static const puzzle_t PUZ[N_BUILD_P] = {
        {"NOT", "MAKE NOT FROM NAND", 1, 1, {"A"}, {"Q"}, PAL(B_NAND), 2, 1, f_not, NULL, 0},
        {"AND", "MAKE AND", 2, 1, {"A", "B"}, {"Q"}, PAL(B_NAND) | PAL(B_NOT), 3, 2, f_and, NULL, 0},
        {"OR", "MAKE OR (TIP: NOT THE INPUTS)", 2, 1, {"A", "B"}, {"Q"}, PAL(B_NAND) | PAL(B_NOT), 4, 3, f_or, NULL, 0},
        {"NOR",
         "MAKE NOR",
         2,
         1,
         {"A", "B"},
         {"Q"},
         PAL(B_NAND) | PAL(B_NOT) | PAL(B_AND) | PAL(B_OR),
         4,
         2,
         f_nor,
         NULL,
         0},
        {"XOR",
         "1 IF A AND B DIFFER",
         2,
         1,
         {"A", "B"},
         {"Q"},
         PAL(B_NAND) | PAL(B_NOT) | PAL(B_AND) | PAL(B_OR),
         5,
         3,
         f_xor,
         NULL,
         0},
        {"MUX",
         "S=0: Q=A, S=1: Q=B",
         3,
         1,
         {"A", "B", "S"},
         {"Q"},
         PAL(B_NAND) | PAL(B_NOT) | PAL(B_AND) | PAL(B_OR),
         5,
         4,
         f_mux,
         NULL,
         0},
        {"HALF ADDER",
         "ADD A + B: SUM S, CARRY C",
         2,
         2,
         {"A", "B"},
         {"S", "C"},
         PAL(B_NAND) | PAL(B_NOT) | PAL(B_AND) | PAL(B_OR) | PAL(B_XOR),
         4,
         2,
         f_ha,
         NULL,
         0},
        {"FULL ADDER",
         "ADD A + B + CARRY IN",
         3,
         2,
         {"A", "B", "CI"},
         {"S", "CO"},
         PAL(B_AND) | PAL(B_OR) | PAL(B_XOR) | PAL(B_HA),
         5,
         3,
         f_fa,
         NULL,
         0},
        {"2-BIT ADDER",
         "A1A0 + B1B0 = C S1 S0",
         4,
         3,
         {"A0", "A1", "B0", "B1"},
         {"S0", "S1", "C"},
         PAL(B_AND) | PAL(B_OR) | PAL(B_XOR) | PAL(B_HA) | PAL(B_FA),
         4,
         2,
         f_add2,
         NULL,
         0},
        {"SR LATCH",
         "MEMORY! S: Q=1, R: Q=0, ELSE HOLD",
         2,
         1,
         {"S", "R"},
         {"Q"},
         PAL(B_NOR) | PAL(B_NAND) | PAL(B_NOT),
         4,
         2,
         NULL,
         SEQ_SR,
         8},
        {"D LATCH",
         "E=1: Q=D, E=0: HOLD",
         2,
         1,
         {"D", "E"},
         {"Q"},
         PAL(B_NOT) | PAL(B_AND) | PAL(B_SR),
         5,
         4,
         NULL,
         SEQ_D,
         8},
        {"COUNTER",
         "COUNT 0 1 2 3 AT EACH CLOCK",
         0,
         2,
         {""},
         {"Q0", "Q1"},
         PAL(B_NOT) | PAL(B_XOR) | PAL(B_AND) | PAL(B_DFF),
         5,
         4,
         NULL,
         SEQ_CNT,
         6},
};

static int s_pz;  // the puzzle now
static slot_t s_slot[MAX_SLOTS];
static int8_t s_osrc[3];  // the source of each output
static sim_t s_live;      // the live state (TRY view and the lamps)
static uint8_t s_try_in;  // the inputs of the TRY view
static bool s_view_try;
static int s_page;
static int s_rows_ok, s_rows_n;  // the test result now
static uint32_t s_pass_map;      // the rows or steps that pass
static int64_t s_solved_at;      // 0, or the time the puzzle passed

static int read_src(const sim_t *S, int src, uint8_t in)
{
    if (src < 0) {
        return 0;
    }
    if (src < 4) {
        return (in >> src) & 1;
    }
    if (src == SRC_ZERO) {
        return 0;
    }
    if (src == SRC_ONE) {
        return 1;
    }
    int k = (src - 8) / 2, o = (src - 8) & 1;
    if (k < 0 || k >= MAX_SLOTS) {
        return 0;
    }
    return (S->val[k] >> o) & 1;
}

static uint8_t eval_slot(sim_t *S, int k, uint8_t in)
{
    const slot_t *sl = &s_slot[k];
    int a = read_src(S, sl->src[0], in), b = read_src(S, sl->src[1], in), c = read_src(S, sl->src[2], in);
    switch (sl->type) {
    case B_NOT:
    case B_AND:
    case B_OR:
    case B_XOR:
    case B_NAND:
    case B_NOR:
        return (uint8_t)gate_eval(sl->type, a, b);
    case B_HA:
        return (uint8_t)((a ^ b) | ((a & b) << 1));
    case B_FA:
        return (uint8_t)(a + b + c);
    case B_SR:
        if (a) {
            S->q[k] = 1;
        } else if (b) {
            S->q[k] = 0;
        }
        return S->q[k];
    case B_DFF:
        return S->q[k];
    default:
        return 0;
    }
}

// the network settles (feedback loops too); false if it does not settle
static bool settle(sim_t *S, uint8_t in)
{
    for (int pass = 0; pass < 16; pass++) {
        bool changed = false;
        for (int k = 0; k < PUZ[s_pz].slots; k++) {
            uint8_t v = eval_slot(S, k, in);
            if (v != S->val[k]) {
                S->val[k] = v;
                changed = true;
            }
        }
        if (!changed) {
            return true;
        }
    }
    return false;
}

static void clock_tick(sim_t *S, uint8_t in)
{
    settle(S, in);
    uint8_t d[MAX_SLOTS] = {0};
    for (int k = 0; k < PUZ[s_pz].slots; k++) {
        if (s_slot[k].type == B_DFF) {
            d[k] = (uint8_t)read_src(S, s_slot[k].src[0], in);
        }
    }
    for (int k = 0; k < PUZ[s_pz].slots; k++) {
        if (s_slot[k].type == B_DFF) {
            S->q[k] = d[k];
        }
    }
    settle(S, in);
}

static uint8_t outputs(const sim_t *S, uint8_t in)
{
    uint8_t o = 0;
    for (int i = 0; i < PUZ[s_pz].no; i++) {
        o |= (uint8_t)(read_src(S, s_osrc[i], in) << i);
    }
    return o;
}

static int blocks_used(void)
{
    int n = 0;
    for (int k = 0; k < PUZ[s_pz].slots; k++) {
        n += s_slot[k].type != B_NONE;
    }
    return n;
}

// run all the tests: the rows of the truth table, or the steps
static void build_test(void)
{
    const puzzle_t *P = &PUZ[s_pz];
    uint8_t mask = (uint8_t)((1 << P->no) - 1);
    s_rows_ok = 0;
    s_pass_map = 0;
    sim_t S;
    if (P->f) {
        s_rows_n = 1 << P->ni;
        for (int r = 0; r < s_rows_n; r++) {
            memset(&S, 0, sizeof(S));
            bool ok = settle(&S, (uint8_t)r) && (outputs(&S, (uint8_t)r) & mask) == (P->f((uint8_t)r) & mask);
            if (ok) {
                s_rows_ok++;
                s_pass_map |= BITN(r);
            }
        }
    } else {
        s_rows_n = P->nseq;
        memset(&S, 0, sizeof(S));
        for (int i = 0; i < P->nseq; i++) {
            const step_t *st = &P->seq[i];
            bool stable = true;
            if (st->clk) {
                clock_tick(&S, st->in);
            } else {
                stable = settle(&S, st->in);
            }
            if (stable && (outputs(&S, st->in) & mask) == st->out) {
                s_rows_ok++;
                s_pass_map |= BITN(i);
            }
        }
    }
}

static int build_stars(void)
{
    int n = blocks_used(), best = PUZ[s_pz].best;
    return n <= best ? 3 : (n <= best + 1 ? 2 : 1);
}

static void build_changed(void)
{
    build_test();
    settle(&s_live, s_try_in);
    if (s_rows_ok == s_rows_n && blocks_used() > 0) {
        if (!s_solved_at) {
            s_solved_at = s_now;
            snd_good();
            pop("IT WORKS!", C_GOOD);
        }
    } else {
        s_solved_at = 0;
    }
}

static void build_start(void)
{
    s_state = ST_BUILD;
    s_pz = s_tile;
    memset(s_slot, 0, sizeof(s_slot));
    for (int k = 0; k < MAX_SLOTS; k++) {
        s_slot[k].src[0] = s_slot[k].src[1] = s_slot[k].src[2] = SRC_NONE;
    }
    memset(s_osrc, SRC_NONE, sizeof(s_osrc));
    memset(&s_live, 0, sizeof(s_live));
    s_try_in = 0;
    s_view_try = false;
    s_page = 0;
    s_solved_at = 0;
    build_changed();
    level_start_tune();
}

static void src_name(char *out, int len, int src)
{
    const puzzle_t *P = &PUZ[s_pz];
    if (src < 0) {
        snprintf(out, len, "-");
    } else if (src < 4) {
        snprintf(out, len, "%s", P->in_name[src]);
    } else if (src == SRC_ZERO) {
        snprintf(out, len, "0");
    } else if (src == SRC_ONE) {
        snprintf(out, len, "1");
    } else {
        int k = (src - 8) / 2, o = (src - 8) & 1;
        if (BLOCK[s_slot[k].type].nout > 1) {
            snprintf(out, len, "G%d.%s", k + 1, BLOCK[s_slot[k].type].out[o]);
        } else {
            snprintf(out, len, "G%d", k + 1);
        }
    }
}

// ---------------------------------------------------------------- the picker
// A list of choices on the body of the screen (the block of a slot, the wire of a pin, an
// instruction, an address, a line). The top bar shows the title, or the card of the choice
// under the finger.

enum {
    PK_NONE,
    PK_BLOCK,
    PK_SRC,
    PK_OUT,
    PK_OP,
    PK_ADDR,
    PK_LINE,
    PK_BYTE
};

typedef struct {
    int kind, a, b;
    int n;
    int16_t val[24];
    char label[24][16];
    char title[40];
} picker_t;

static picker_t s_pk;

static void pk_add(int v, const char *label)
{
    if (s_pk.n >= 24) {
        return;
    }
    s_pk.val[s_pk.n] = (int16_t)v;
    snprintf(s_pk.label[s_pk.n], sizeof(s_pk.label[0]), "%s", label);
    s_pk.n++;
}

static int pk_rows(void)
{
    int r = (s_pk.n + PK_COLS - 1) / PK_COLS;
    return r < 1 ? 1 : r;
}

static rc_t pk_cell(int i)
{
    int rows = pk_rows();
    int ch = BODY_H / rows;
    if (ch > 60) {
        ch = 60;
    }
#if BOARD_ROUND
    // (the round frame: the rows from the bar to the row 226, each as wide as its chord)
    ch = (226 - TOP_H) / rows;
    if (ch > 56) {
        ch = 56;
    }
    int y = TOP_H + (i / PK_COLS) * ch, half = rch2(y, ch) - 4;
    int x0 = 160 - half + (i % PK_COLS) * 2 * half / PK_COLS, x1 = 160 - half + (i % PK_COLS + 1) * 2 * half / PK_COLS;
    return RC(x0 + 1, y + 1, x1 - x0 - 2, ch - 2);
#endif
    int cw = SW / PK_COLS;
    return RC((i % PK_COLS) * cw + 1, TOP_H + (i / PK_COLS) * ch + 1, cw - 2, ch - 2);
}

static void pk_open_src(int kind, int slot, int pin)
{
    memset(&s_pk, 0, sizeof(s_pk));
    s_pk.kind = kind;
    s_pk.a = slot;
    s_pk.b = pin;
    const puzzle_t *P = &PUZ[s_pz];
    char l[12];
    pk_add(SRC_NONE, "-");
    for (int i = 0; i < P->ni; i++) {
        pk_add(i, P->in_name[i]);
    }
    pk_add(SRC_ZERO, "0");
    pk_add(SRC_ONE, "1");
    for (int k = 0; k < P->slots; k++) {
        if (s_slot[k].type == B_NONE) {
            continue;
        }
        for (int o = 0; o < BLOCK[s_slot[k].type].nout; o++) {
            src_name(l, sizeof(l), SRC_G(k, o));
            pk_add(SRC_G(k, o), l);
        }
    }
    if (kind == PK_SRC) {
        snprintf(s_pk.title, sizeof(s_pk.title), "G%d %s: WIRE FROM", slot + 1, BLOCK[s_slot[slot].type].pin[pin]);
    } else {
        snprintf(s_pk.title, sizeof(s_pk.title), "OUTPUT %s: WIRE FROM", P->out_name[pin]);
    }
}

static void pk_open_block(int slot)
{
    memset(&s_pk, 0, sizeof(s_pk));
    s_pk.kind = PK_BLOCK;
    s_pk.a = slot;
    pk_add(B_NONE, "EMPTY");
    for (int b = 1; b < N_BLOCKS; b++) {
        if (PUZ[s_pz].palette & BITN(b)) {
            pk_add(b, BLOCK[b].name);
        }
    }
    snprintf(s_pk.title, sizeof(s_pk.title), "SLOT G%d: BLOCK", slot + 1);
}

// ---------------------------------------------------------------- BUILD input and drawing

static int n_panels(void)
{
    return PUZ[s_pz].slots + 1;  // the slots and OUT
}

static int n_pages(void)
{
    return (n_panels() + P_PER_PAGE - 1) / P_PER_PAGE;
}

static rc_t panel_rc(int p)
{
    return RC(P_X0 + (p % P_PER_PAGE) * P_W, TOP_H, P_W, P_BODY);
}

static rc_t hdr_rc(int p)
{
    rc_t r = panel_rc(p);
    return RC(r.x + 2, r.y + 2, r.w - 4, P_HDR_H);
}

static rc_t pin_rc(int p, int j)
{
    rc_t r = panel_rc(p);
    return RC(r.x + 2, r.y + 4 + P_HDR_H + j * (P_PIN_H + 2), r.w - 4, P_PIN_H);
}

// BUILD, the PARTS view: a tap on the page buttons, a block header or a pin opens its picker
static void build_input_parts(const puzzle_t *P, const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    if (in_rc(BT_PREV_RC, ev->x, ev->y) && s_page > 0) {
        s_page--;
        return;
    }
    if (in_rc(BT_NEXT_RC, ev->x, ev->y) && s_page < n_pages() - 1) {
        s_page++;
        return;
    }
    for (int i = 0; i < P_PER_PAGE; i++) {
        int p = s_page * P_PER_PAGE + i;
        if (p >= n_panels()) {
            break;
        }
        if (p < P->slots) {
            if (in_rc(hdr_rc(p), ev->x, ev->y)) {
                pk_open_block(p);
                snd_click();
                return;
            }
            for (int j = 0; j < BLOCK[s_slot[p].type].nin; j++) {
                if (in_rc(pin_rc(p, j), ev->x, ev->y)) {
                    pk_open_src(PK_SRC, p, j);
                    snd_click();
                    return;
                }
            }
        } else {
            for (int j = 0; j < P->no; j++) {
                if (in_rc(pin_rc(p, j), ev->x, ev->y)) {
                    pk_open_src(PK_OUT, 0, j);
                    snd_click();
                    return;
                }
            }
        }
    }
}

static void build_input(const input_event_t *ev)
{
    const puzzle_t *P = &PUZ[s_pz];
    if (ev->kind == IN_TAP && in_rc(BT_VIEW_RC, ev->x, ev->y)) {
        s_view_try = !s_view_try;
        snd_click();
        return;
    }
    if (!s_view_try) {
        build_input_parts(P, ev);
        return;
    }
    // TRY view
    if (ev->kind == IN_DOWN) {
        for (int i = 0; i < P->ni; i++) {
            if (in_rc(T_IN_RC(i), ev->x, ev->y)) {
                s_try_in ^= (uint8_t)BITN(i);
                snd_click();
                settle(&s_live, s_try_in);
                return;
            }
        }
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (!P->f && in_rc(T_CLK_RC, ev->x, ev->y) && P->ni == 0) {
        clock_tick(&s_live, s_try_in);
        snd_click();
        return;
    }
    if (!P->f && in_rc(T_RST_RC, ev->x, ev->y)) {
        memset(&s_live, 0, sizeof(s_live));
        settle(&s_live, s_try_in);
        snd_click();
        return;
    }
}

static void pk_choose(int i);

static void render_build_top(void)
{
    const puzzle_t *P = &PUZ[s_pz];
#if BOARD_ROUND
    if (!TOP_BAND) {
        return;
    }
    // (the cap: the name of the puzzle, the rows that pass and the blocks under it)
    R(0, -RV_OY, SW, RV_OY, C_TOP);
    TXT_MID(160, -33, P->name, 1, C_GOLD);
#else
    if (!in_band(0, TOP_H)) {
        return;
    }
#endif
    R(0, 0, SW, TOP_H, C_TOP);
    R(0, TOP_H - 1, SW, 1, C_EDGE);
    button(BACK_RC, "<", NULL, C_SILK, false);
#if BOARD_BAR_SCREEN
    TXT(68, 4, P->name, fit_scale(P->name, 190, 2), C_GOLD);
    text_fit(68, 190, 22, P->goal, 1, C_DIM);
#endif
    button(BT_VIEW_RC, s_view_try ? "WIRE" : "TRY", NULL, C_CYAN, s_view_try);
    if (!s_view_try && n_pages() > 1) {
        button(BT_PREV_RC, "<", NULL, s_page > 0 ? C_SILK : C_DIM, false);
        button(BT_NEXT_RC, ">", NULL, s_page < n_pages() - 1 ? C_SILK : C_DIM, false);
    }
    char t[40];
    snprintf(t, sizeof(t), "%d/%d OK", s_rows_ok, s_rows_n);
    bool ok = s_rows_ok == s_rows_n;
#if !BOARD_ROUND
    TXT(BT_STAT_X, 5, t, BOARD_BAR_SCREEN ? 2 : 1, ok ? C_GOOD : C_SILK);
#endif
    snprintf(t, sizeof(t), "BLOCKS %d (BEST %d)", blocks_used(), P->best);
#if BOARD_BAR_SCREEN
    TXT(BT_STAT_X, 23, t, 1, C_DIM);
#elif BOARD_ROUND
    snprintf(t, sizeof(t), "%d/%d OK  BLK %d/%d", s_rows_ok, s_rows_n, blocks_used(), P->best);
    TXT_MID(160, -19, t, 1, ok ? C_GOOD : C_DIM);
#else
    TXT(BT_STAT_X, 16, P->name, 1, C_GOLD);
    snprintf(t, sizeof(t), "BLK %d/%d", blocks_used(), P->best);
    TXT(BT_STAT_X, 26, t, 1, C_DIM);
#endif
}

static void render_wire(void)
{
    const puzzle_t *P = &PUZ[s_pz];
    char l[24], s[16];
    for (int i = 0; i < P_PER_PAGE; i++) {
        int p = s_page * P_PER_PAGE + i;
        rc_t pr = panel_rc(p);
        if (p >= n_panels()) {
            if (p == n_panels() && !BOARD_BAR_SCREEN) {
                TXT_IN(pr.x, pr.w, pr.y + 20, P->goal, 1, C_DIM);
            }
            continue;
        }
        R(pr.x + pr.w - 1, pr.y, 1, pr.h, rgb(20, 60, 40));
        if (p < P->slots) {
            const slot_t *sl = &s_slot[p];
            rc_t h = hdr_rc(p);
            snprintf(l, sizeof(l), "G%d %s", p + 1, sl->type ? BLOCK[sl->type].name : "+");
            bool hp = pressed(h);
            panel(h, hp ? C_PANEL_HI : (sl->type ? C_CHIP : C_PANEL), sl->type ? C_GOLD : C_EDGE);
            int nout = BLOCK[sl->type].nout;
            TXT(h.x + 5, h.y + (h.h - 14) / 2, l, fit_scale(l, h.w - 10 - nout * 16, 2), sl->type ? C_SILK : C_DIM);
            for (int o = 0; o < nout; o++) {
                lamp(h.x + h.w - 9 - o * 16, h.y + h.h / 2, 5, (s_live.val[p] >> o) & 1);
            }
            if (!sl->type) {
                TXT_IN(h.x, h.w, h.y + h.h + 12, "TAP + FOR", 1, C_DIM);
            }
            if (!sl->type) {
                TXT_IN(h.x, h.w, h.y + h.h + 24, "A BLOCK", 1, C_DIM);
            }
            for (int j = 0; j < BLOCK[sl->type].nin; j++) {
                rc_t r = pin_rc(p, j);
                src_name(s, sizeof(s), sl->src[j]);
                bool v = read_src(&s_live, sl->src[j], s_try_in);
                panel(r, pressed(r) ? C_PANEL_HI : C_PCB, sl->src[j] < 0 ? C_EDGE : C_TRACE);
                TXT(r.x + 5, r.y + 4, BLOCK[sl->type].pin[j], 1, C_DIM);
                TXT(r.x + 5, r.y + r.h - 18, s, fit_scale(s, r.w - 24, 2), sl->src[j] < 0 ? C_DIM : C_SILK);
                lamp(r.x + r.w - 10, r.y + r.h / 2, 4, v);
            }
        } else {
            rc_t h = hdr_rc(p);
            panel(h, C_CHIP, C_CYAN);
            TXT_IN(h.x, h.w, h.y + (h.h - 14) / 2, "OUT", 2, C_CYAN);
            for (int j = 0; j < P->no; j++) {
                rc_t r = pin_rc(p, j);
                src_name(s, sizeof(s), s_osrc[j]);
                panel(r, pressed(r) ? C_PANEL_HI : C_PCB, s_osrc[j] < 0 ? C_EDGE : C_TRACE);
                TXT(r.x + 5, r.y + 4, P->out_name[j], 1, C_CYAN);
                TXT(r.x + 5, r.y + r.h - 18, s, fit_scale(s, r.w - 24, 2), s_osrc[j] < 0 ? C_DIM : C_SILK);
                lamp(r.x + r.w - 10, r.y + r.h / 2, 4, read_src(&s_live, s_osrc[j], s_try_in));
            }
        }
    }
}

static void render_try(void)
{
    const puzzle_t *P = &PUZ[s_pz];
    char l[24];
    for (int i = 0; i < P->ni; i++) {
        bool on = (s_try_in >> i) & 1;
        snprintf(l, sizeof(l), "%s=%d", P->in_name[i], on);
        button(T_IN_RC(i), l, "TAP", on ? C_LAMP_ON : C_SILK, on);
    }
    if (P->ni == 0 && in_band(T_IN_RC(0).y, 20)) {
        TXT(T_IN_RC(0).x, T_IN_RC(0).y + 4, "NO INPUTS: TAP CLOCK", 1, C_DIM);
    }
    uint8_t out = outputs(&s_live, s_try_in);
    uint8_t want = P->f ? P->f(s_try_in) : 0;
    for (int o = 0; o < P->no; o++) {
        rc_t r = T_OUT_RC(o);
        if (!in_band(r.y, r.h)) {
            continue;
        }
        panel(r, C_PCB, C_EDGE);
        TXT(r.x + 6, r.y + 6, P->out_name[o], 2, C_CYAN);
        bool v = (out >> o) & 1;
        lamp(r.x + r.w - 18, r.y + 16, 8, v);
        if (P->f) {
            bool w = (want >> o) & 1;
            snprintf(l, sizeof(l), "WANT %d", w);
            TXT(r.x + 6, r.y + r.h - 14, l, 1, v == w ? C_GOOD : C_BAD);
        }
    }
    if (!P->f) {
        if (P->ni == 0) {
            button(T_CLK_RC, "CLOCK", "TAP", C_CYAN, false);
        } else if (in_band(T_CLK_RC.y, T_CLK_RC.h)) {
            TXT_IN(T_CLK_RC.x, T_CLK_RC.w, T_CLK_RC.y + 10, "MEMORY:", 1, C_DIM);
            TXT_IN(T_CLK_RC.x, T_CLK_RC.w, T_CLK_RC.y + 24, "Q HOLDS", 1, C_DIM);
        }
        button(T_RST_RC, "RESET", NULL, C_SILK, false);
    }
    // the test map: one cell for each row or step
    int n = s_rows_n, cols = n <= 8 ? n : 8, rows = (n + cols - 1) / cols;
    int gx = T_GRID_X + (!P->f ? (BOARD_BAR_SCREEN ? 0 : T_GRID_SKIP) : 0),
        gw = T_GRID_W - (!P->f && !BOARD_BAR_SCREEN ? T_GRID_SKIP : 0);
    int cw = gw / cols, chh = (T_GRID_H - 12) / rows;
    if (chh > 40) {
        chh = 40;
    }
    TXT(gx, T_GRID_Y, P->f ? "TRUTH TABLE" : "TEST STEPS", 1, C_DIM);
    for (int i = 0; i < n; i++) {
        int x = gx + (i % cols) * cw, y = T_GRID_Y + 12 + (i / cols) * chh;
        bool ok = (s_pass_map >> i) & 1;
        bool here = P->f && i == s_try_in;
        R(x + 1, y + 1, cw - 2, chh - 2, ok ? rgb(30, 110, 60) : rgb(120, 34, 28));
        if (here) {
            FRAME(x, y, cw, chh, 2, C_GOLD);
        }
        if (cw >= 22) {
            snprintf(l, sizeof(l), "%d", i);
            TXT_IN(x, cw, y + (chh - 7) / 2, l, 1, C_SILK);
        }
    }
}

static void render_picker(void)
{
#if BOARD_ROUND
    if (in_band(-RV_OY, RV_OY)) {
        R(0, -RV_OY, SW, RV_OY, C_TOP);
    }
#endif
    if (in_band(0, TOP_H)) {
        R(0, 0, SW, TOP_H, C_TOP);
        R(0, TOP_H - 1, SW, 1, C_GOLD);
    }
    R(0, TOP_H, SW, BODY_H, rgb(4, 14, 10));
    for (int i = 0; i < s_pk.n; i++) {
        rc_t r = pk_cell(i);
        button(r, s_pk.label[i], NULL, C_SILK, false);
    }
}

static void pk_title(void);

// ================================================================ LAB: an 8-bit adder

static uint8_t s_la, s_lb;
static int64_t s_lab_at;  // the time of the last change: the carry ripples from it

static void lab_start(void)
{
    s_state = ST_LAB;
    s_la = 23;
    s_lb = 200;
    s_lab_at = s_now;
}

static rc_t lab_cell(int row, int col)
{
    return RC(L_LABEL_W + col * L_COL_W, TOP_H + row * L_ROW_H, L_COL_W, L_ROW_H);
}

static void lab_input(const input_event_t *ev)
{
    if (ev->kind != IN_DOWN) {
        return;
    }
    for (int row = 0; row < 2; row++) {
        for (int c = 0; c < 8; c++) {
            if (in_rc(lab_cell(row, c), ev->x, ev->y)) {
                uint8_t bit = (uint8_t)(0x80 >> c);
                if (row == 0) {
                    s_la ^= bit;
                } else {
                    s_lb ^= bit;
                }
                s_lab_at = s_now;
                snd_click();
                return;
            }
        }
    }
}

static void render_lab(void)
{
    char t[48];
    int sum = s_la + s_lb;
    snprintf(t, sizeof(t), "%d + %d = %d", s_la, s_lb, sum);
    draw_top("8-BIT ADDER", t);
    R(0, TOP_H, SW, BODY_H, C_BG);
    // the ripple: the carry moves one bit each 90 ms from bit 0
    int done = (int)((s_now - s_lab_at) / 90);
    int carry = 0;
    uint8_t s = 0;
    uint16_t cmask = 0;
    for (int b = 0; b < 8; b++) {
        int a = (s_la >> b) & 1, bb = (s_lb >> b) & 1;
        int v = a + bb + carry;
        if (b <= done) {
            s |= (uint8_t)((v & 1) << b);
            if (v >> 1) {
                cmask |= (uint16_t)BITN(b);
            }
        }
        carry = v >> 1;
    }
    static const char *const ROWN[3] = {"A", "B", "SUM"};
    for (int row = 0; row < 3; row++) {
        rc_t c0 = lab_cell(row, 0);
        if (!in_band(c0.y, c0.h)) {
            continue;
        }
        TXT(L_TXT_X, c0.y + 6, ROWN[row], 2, row == 2 ? C_GOLD : C_SILK);
        int v = row == 0 ? s_la : (row == 1 ? s_lb : (done >= 8 ? sum & 0xFF : s));
        snprintf(t, sizeof(t), "%d", v);
        TXT(L_TXT_X, c0.y + c0.h - 14, t, 1, C_DIM);
        for (int c = 0; c < 8; c++) {
            rc_t r = lab_cell(row, c);
            int bitn = 7 - c;
            bool on = row == 0 ? (s_la >> bitn) & 1 : (row == 1 ? (s_lb >> bitn) & 1 : (s >> bitn) & 1);
            if (row < 2) {
                draw_switch(r.x, r.y, r.w, r.h, on, false, "");
            } else {
                panel(RC(r.x + 2, r.y + 2, r.w - 4, r.h - 4), C_CHIP, bitn <= done ? C_EDGE : rgb(40, 50, 44));
                lamp(r.x + r.w / 2, r.y + r.h / 2 - 4, r.w / 5 > 10 ? 10 : r.w / 5, on);
                // the carry into the next bit (to the left)
                if (bitn < 7 && ((cmask >> bitn) & 1)) {
                    R(r.x - 3, r.y + r.h - 10, 6, 6, C_CYAN);
                }
                TXT_IN(r.x, r.w, r.y + r.h - 12, "FA", 1, C_DIM);
            }
        }
    }
    // the carry out of bit 7
    rc_t r = lab_cell(2, 0);
    if (done >= 8 && sum > 255 && in_band(r.y, r.h)) {
        TXT(L_TXT_X, r.y + r.h / 2 - 3, "C!", 1, C_CYAN);
    }
}

// ================================================================ CPU

enum {
    O_NONE,
    O_LDA_I,
    O_LDA_M,
    O_LDX_I,
    O_LDX_M,
    O_STA_M,
    O_STA_MX,
    O_ADC_I,
    O_ADC_M,
    O_CLC,
    O_EOR_I,
    O_ASL,
    O_LSR,
    O_INX,
    O_DEX,
    O_CMP_I,
    O_CPX_I,
    O_BNE,
    O_BEQ,
    O_JMP,
    N_OPS,
};
enum {
    AM_IMP,
    AM_IMM,
    AM_MEM,
    AM_MEMX,
    AM_LINE
};

typedef struct {
    const char *mn;  // the mnemonic as the picker shows it
    uint8_t mode, cycles;
    const char *card;
} opinfo_t;

static const opinfo_t OP[N_OPS] = {
        {"", AM_IMP, 0, ""},
        {"LDA #", AM_IMM, 2, "LDA #N: A = N"},
        {"LDA M", AM_MEM, 3, "LDA M: A = THE BYTE IN M"},
        {"LDX #", AM_IMM, 2, "LDX #N: X = N"},
        {"LDX M", AM_MEM, 3, "LDX M: X = THE BYTE IN M"},
        {"STA M", AM_MEM, 3, "STA M: PUT A INTO M"},
        {"STA M,X", AM_MEMX, 4, "STA M,X: PUT A INTO M + X"},
        {"ADC #", AM_IMM, 2, "ADC #N: A = A + N + C"},
        {"ADC M", AM_MEM, 3, "ADC M: A = A + M + C"},
        {"CLC", AM_IMP, 2, "CLC: C = 0 (DO IT BEFORE ADC)"},
        {"EOR #", AM_IMM, 2, "EOR #N: A = A XOR N"},
        {"ASL", AM_IMP, 2, "ASL: A = A << 1 (A x 2)"},
        {"LSR", AM_IMP, 2, "LSR: A = A >> 1 (A / 2)"},
        {"INX", AM_IMP, 2, "INX: X = X + 1"},
        {"DEX", AM_IMP, 2, "DEX: X = X - 1"},
        {"CMP #", AM_IMM, 2, "CMP #N: Z = 1 IF A = N"},
        {"CPX #", AM_IMM, 2, "CPX #N: Z = 1 IF X = N"},
        {"BNE", AM_LINE, 3, "BNE L: GO TO L IF Z = 0"},
        {"BEQ", AM_LINE, 3, "BEQ L: GO TO L IF Z = 1"},
        {"JMP", AM_LINE, 3, "JMP L: GO TO L"},
};

#define N_LINES  8
#define MEM_LEDS 7
#define MEM_SCR  8

typedef struct {
    uint8_t op, arg;
} ins_t;

typedef struct {
    uint8_t a, x, c, z, pc;
    uint8_t mem[16];
    uint32_t cycles, steps;
    bool halt, err;
} cpu_t;

typedef struct {
    const char *name, *goal, *hint;
    uint8_t best, ntests;
    void (*init)(int t, uint8_t *mem);
    bool (*check)(int t, const uint8_t *mem);
} cpuz_t;

static const uint8_t ADD_T[3][2] = {{2, 3}, {100, 27}, {77, 99}};
static const uint8_t MUL_T[3][2] = {{3, 4}, {7, 5}, {12, 9}};

static void ci_none(int t, uint8_t *m)
{
    (void)t;
    (void)m;
}
static void ci_add(int t, uint8_t *m)
{
    m[0] = ADD_T[t][0];
    m[1] = ADD_T[t][1];
}
static void ci_dbl(int t, uint8_t *m)
{
    static const uint8_t V[3] = {5, 60, 100};
    m[0] = V[t];
}
static void ci_mul(int t, uint8_t *m)
{
    m[0] = MUL_T[t][0];
    m[1] = MUL_T[t][1];
}
static bool cc_ff(int t, const uint8_t *m)
{
    (void)t;
    return m[MEM_LEDS] == 0xFF;
}
static bool cc_aa(int t, const uint8_t *m)
{
    (void)t;
    return m[MEM_LEDS] == 0xAA;
}
static bool cc_add(int t, const uint8_t *m)
{
    return m[MEM_LEDS] == (uint8_t)(ADD_T[t][0] + ADD_T[t][1]);
}
static bool cc_dbl(int t, const uint8_t *m)
{
    static const uint8_t V[3] = {5, 60, 100};
    return m[MEM_LEDS] == (uint8_t)(V[t] * 2);
}
static bool cc_mul(int t, const uint8_t *m)
{
    return m[MEM_LEDS] == (uint8_t)(MUL_T[t][0] * MUL_T[t][1]);
}
static bool cc_fill(int t, const uint8_t *m)
{
    (void)t;
    for (int i = 0; i < 8; i++) {
        if (m[MEM_SCR + i] != 0xFF) {
            return false;
        }
    }
    return true;
}
static bool cc_check(int t, const uint8_t *m)
{
    (void)t;
    for (int i = 0; i < 8; i++) {
        if (m[MEM_SCR + i] != ((i & 1) ? 0x55 : 0xAA)) {
            return false;
        }
    }
    return true;
}
static bool cc_diag(int t, const uint8_t *m)
{
    (void)t;
    for (int i = 0; i < 8; i++) {
        if (m[MEM_SCR + i] != (0x80 >> i)) {
            return false;
        }
    }
    return true;
}

static const cpuz_t CPUZ[N_CPU_T] = {
        {"LIGHTS ON", "ALL 8 LEDS ON", "LDA #$FF, THEN STA LEDS", 2, 1, ci_none, cc_ff},
        {"PATTERN", "LEDS = 10101010", "1010 1010 = $AA", 2, 1, ci_none, cc_aa},
        {"ADD", "LEDS = M0 + M1", "LDA, ADC, STA", 3, 3, ci_add, cc_add},
        {"DOUBLE", "LEDS = M0 x 2", "A SHIFT LEFT IS x 2", 3, 3, ci_dbl, cc_dbl},
        {"FILL", "ALL SCREEN ROWS ON", "A LOOP: STA SCR,X  INX  CPX #8  BNE", 6, 1, ci_none, cc_fill},
        {"CHECKER", "ROWS $AA, $55, $AA ...", "EOR #$FF FLIPS ALL BITS", 7, 1, ci_none, cc_check},
        {"DIAGONAL", "A LINE FROM CORNER TO CORNER", "LSR MOVES THE DOT", 7, 1, ci_none, cc_diag},
        {"MULTIPLY", "LEDS = M0 x M1", "ADD M0, M1 TIMES (DEX, BNE)", 7, 3, ci_mul, cc_mul},
};

static ins_t s_prog[N_LINES];
static cpu_t s_cpu;
static bool s_running;
static int64_t s_run_at;
static int s_sel = -1;  // the line last tapped (its card shows)
static char s_cpu_msg[48];
static uint16_t s_cpu_msg_c;
static uint8_t s_byte;  // PK_BYTE: the value on the switches

static void cpu_reset(cpu_t *C, int test)
{
    memset(C, 0, sizeof(*C));
    CPUZ[s_tile].init(test, C->mem);
}

static void cpu_step(cpu_t *C)
{
    if (C->halt) {
        return;
    }
    if (C->pc >= N_LINES || s_prog[C->pc].op == O_NONE) {
        C->halt = true;
        return;
    }
    const ins_t *I = &s_prog[C->pc];
    uint8_t n = I->arg, next = (uint8_t)(C->pc + 1);
    uint8_t m = C->mem[n & 15];
    int v;
    C->cycles += OP[I->op].cycles;
    C->steps++;
    switch (I->op) {
    case O_LDA_I:
        C->a = n;
        C->z = !C->a;
        break;
    case O_LDA_M:
        C->a = m;
        C->z = !C->a;
        break;
    case O_LDX_I:
        C->x = n;
        C->z = !C->x;
        break;
    case O_LDX_M:
        C->x = m;
        C->z = !C->x;
        break;
    case O_STA_M:
        C->mem[n & 15] = C->a;
        break;
    case O_STA_MX:
        C->mem[(n + C->x) & 15] = C->a;
        C->cycles += 0;
        break;
    case O_ADC_I:
        v = C->a + n + C->c;
        C->c = v > 255;
        C->a = (uint8_t)v;
        C->z = !C->a;
        break;
    case O_ADC_M:
        v = C->a + m + C->c;
        C->c = v > 255;
        C->a = (uint8_t)v;
        C->z = !C->a;
        break;
    case O_CLC:
        C->c = 0;
        break;
    case O_EOR_I:
        C->a ^= n;
        C->z = !C->a;
        break;
    case O_ASL:
        C->c = C->a >> 7;
        C->a = (uint8_t)(C->a << 1);
        C->z = !C->a;
        break;
    case O_LSR:
        C->c = C->a & 1;
        C->a >>= 1;
        C->z = !C->a;
        break;
    case O_INX:
        C->x++;
        C->z = !C->x;
        break;
    case O_DEX:
        C->x--;
        C->z = !C->x;
        break;
    case O_CMP_I:
        C->c = C->a >= n;
        C->z = C->a == n;
        break;
    case O_CPX_I:
        C->c = C->x >= n;
        C->z = C->x == n;
        break;
    case O_BNE:
        if (!C->z) {
            next = n;
        } else {
            C->cycles--;
        }
        break;
    case O_BEQ:
        if (C->z) {
            next = n;
        } else {
            C->cycles--;
        }
        break;
    case O_JMP:
        next = n;
        break;
    default:
        break;
    }
    C->pc = next;
}

static int prog_lines(void)
{
    int n = 0;
    for (int i = 0; i < N_LINES; i++) {
        n += s_prog[i].op != O_NONE;
    }
    return n;
}

static void cpu_msg(const char *t, uint16_t c)
{
    snprintf(s_cpu_msg, sizeof(s_cpu_msg), "%s", t);
    s_cpu_msg_c = c;
}

static void cpu_start(void)
{
    s_state = ST_CPU;
    memset(s_prog, 0, sizeof(s_prog));
    s_running = false;
    s_sel = -1;
    cpu_reset(&s_cpu, 0);
    cpu_msg(CPUZ[s_tile].goal, C_GOLD);
    level_start_tune();
}

// run all the tests at full speed
static void cpu_test(void)
{
    const cpuz_t *Z = &CPUZ[s_tile];
    int pass = 0;
    uint32_t cyc = 0;
    cpu_t C;
    bool loop = false;
    for (int t = 0; t < Z->ntests; t++) {
        cpu_reset(&C, t);
        int guard = 0;
        while (!C.halt && guard < 3000) {
            cpu_step(&C);
            guard++;
        }
        if (!C.halt) {
            loop = true;
        }
        if (C.halt && Z->check(t, C.mem)) {
            pass++;
        }
        cyc += C.cycles;
        if (t == 0) {
            s_cpu = C;  // show the first test on the machine
        }
    }
    char m[48];
    if (pass == Z->ntests) {
        snd_good();
        int lines = prog_lines();
        int st = lines <= Z->best ? 3 : (lines <= Z->best + 2 ? 2 : 1);
        snprintf(s_res_detail, sizeof(s_res_detail), "%d LINES  %lu CYCLES", lines, (unsigned long)cyc);
        result_show(true, st, s_res_detail);
        return;
    }
    snd_bad();
    if (loop) {
        snprintf(m, sizeof(m), "IT NEVER ENDS: CHECK THE LOOP");
    } else {
        snprintf(m, sizeof(m), "TEST %d/%d PASS", pass, Z->ntests);
    }
    cpu_msg(m, C_BAD);
}

static const char *addr_name(int a)
{
    static const char *const N[16] = {"M0",  "M1",   "M2",   "M3",   "M4",   "M5",   "M6",   "LEDS",
                                      "SCR", "SCR1", "SCR2", "SCR3", "SCR4", "SCR5", "SCR6", "SCR7"};
    return N[a & 15];
}

static void arg_text(char *out, int len, const ins_t *I)
{
    switch (OP[I->op].mode) {
    case AM_IMM:
        snprintf(out, len, "#$%02X", I->arg);
        break;
    case AM_MEM:
        snprintf(out, len, "%s", addr_name(I->arg));
        break;
    case AM_MEMX:
        snprintf(out, len, "%s,X", addr_name(I->arg));
        break;
    case AM_LINE:
        snprintf(out, len, "L%d", I->arg + 1);
        break;
    default:
        out[0] = 0;
        break;
    }
}

static void pk_open_op(int line)
{
    memset(&s_pk, 0, sizeof(s_pk));
    s_pk.kind = PK_OP;
    s_pk.a = line;
    for (int o = 1; o < N_OPS; o++) {
        pk_add(o, OP[o].mn);
    }
    pk_add(O_NONE, "DELETE");
    snprintf(s_pk.title, sizeof(s_pk.title), "LINE %d: INSTRUCTION", line + 1);
}

static void pk_open_arg(int line)
{
    int mode = OP[s_prog[line].op].mode;
    memset(&s_pk, 0, sizeof(s_pk));
    s_pk.a = line;
    if (mode == AM_IMM) {
        s_pk.kind = PK_BYTE;
        s_byte = s_prog[line].arg;
        snprintf(s_pk.title, sizeof(s_pk.title), "LINE %d: THE NUMBER", line + 1);
    } else if (mode == AM_MEM || mode == AM_MEMX) {
        s_pk.kind = PK_ADDR;
        for (int a = 0; a < 16; a++) {
            pk_add(a, addr_name(a));
        }
        snprintf(s_pk.title, sizeof(s_pk.title), "LINE %d: THE ADDRESS", line + 1);
    } else if (mode == AM_LINE) {
        s_pk.kind = PK_LINE;
        char l[16];
        for (int i = 0; i < N_LINES; i++) {
            snprintf(l, sizeof(l), "L%d", i + 1);
            pk_add(i, l);
        }
        snprintf(s_pk.title, sizeof(s_pk.title), "LINE %d: GO TO LINE", line + 1);
    }
}

static void cpu_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    if (in_rc(C_STEP_RC, ev->x, ev->y)) {
        s_running = false;
        if (s_cpu.halt) {
            cpu_reset(&s_cpu, 0);
        }
        cpu_step(&s_cpu);
        snd_click();
        cpu_msg(s_cpu.halt ? "END" : "STEP", s_cpu.halt ? C_GOOD : C_SILK);
        return;
    }
    if (in_rc(C_RUN_RC, ev->x, ev->y)) {
        s_running = !s_running;
        if (s_running && s_cpu.halt) {
            cpu_reset(&s_cpu, 0);
        }
        s_run_at = s_now;
        snd_click();
        return;
    }
    if (in_rc(C_RST_RC, ev->x, ev->y)) {
        s_running = false;
        cpu_reset(&s_cpu, 0);
        snd_click();
        cpu_msg(CPUZ[s_tile].goal, C_GOLD);
        return;
    }
    if (in_rc(C_TEST_RC, ev->x, ev->y)) {
        s_running = false;
        cpu_test();
        return;
    }
    for (int i = 0; i < N_LINES; i++) {
        rc_t r = C_LINE_RC(i);
        if (!in_rc(r, ev->x, ev->y)) {
            continue;
        }
        s_sel = i;
        s_running = false;
        snd_click();
        if (ev->x < r.x + C_OPW - 4 || s_prog[i].op == O_NONE || OP[s_prog[i].op].mode == AM_IMP) {
            pk_open_op(i);
        } else {
            pk_open_arg(i);
        }
        return;
    }
}

static void cpu_update(void)
{
    if (!s_running) {
        return;
    }
    while (s_now - s_run_at >= 150) {
        s_run_at += 150;
        cpu_step(&s_cpu);
        if (s_cpu.halt) {
            s_running = false;
            cpu_msg("END: TAP TEST TO CHECK", C_GOOD);
            break;
        }
        if (s_cpu.steps > 400) {
            s_running = false;
            cpu_msg("STOPPED: 400 STEPS", C_BAD);
            break;
        }
    }
}

static void render_cpu(void)
{
    const cpuz_t *Z = &CPUZ[s_tile];
    char t[48], a[16];
#if BOARD_ROUND
    if (in_band(-RV_OY, RV_OY)) {
        draw_cap(Z->name);
    }
#endif
    if (in_band(0, TOP_H)) {
        R(0, 0, SW, TOP_H, C_TOP);
        R(0, TOP_H - 1, SW, 1, C_EDGE);
        button(BACK_RC, "<", NULL, C_SILK, false);
#if BOARD_BAR_SCREEN
        TXT(68, 4, Z->name, 2, C_GOLD);
        TXT(68, 22, Z->hint, 1, C_DIM);
#endif
        button(C_STEP_RC, "STEP", NULL, C_SILK, false);
        button(C_RUN_RC, s_running ? "STOP" : "RUN", NULL, C_CYAN, s_running);
        button(C_RST_RC, BOARD_BAR_SCREEN ? "RESET" : "RST", NULL, C_SILK, false);
#if !BOARD_ROUND
        button(C_TEST_RC, "TEST", NULL, C_GOOD, false);
#endif
    }
#if BOARD_ROUND
    button(C_TEST_RC, "TEST", NULL, C_GOOD, false);
#endif
    R(0, TOP_H, SW, BODY_H, C_BG);
    for (int i = 0; i < N_LINES; i++) {
        rc_t r = C_LINE_RC(i);
        if (!in_band(r.y, r.h)) {
            continue;
        }
        bool pc = !s_cpu.halt && s_cpu.pc == i && (s_cpu.steps > 0 || s_running);
        panel(r, pressed(r) ? C_PANEL_HI : (pc ? rgb(70, 60, 10) : C_PCB), i == s_sel ? C_GOLD : C_EDGE);
        snprintf(t, sizeof(t), "%d", i + 1);
        TXT(r.x + 4, r.y + 4, t, 1, pc ? C_GOLD : C_DIM);
        const ins_t *I = &s_prog[i];
        if (I->op == O_NONE) {
            TXT(r.x + 18, r.y + (r.h - 7) / 2, "TAP: ADD", 1, rgb(70, 100, 80));
            continue;
        }
        const char *mn = OP[I->op].mn;
        char m3[4] = {mn[0], mn[1], mn[2], 0};
        TXT(r.x + 18, r.y + (r.h - 14) / 2, m3, 2, C_SILK);
        arg_text(a, sizeof(a), I);
        TXT(r.x + 18 + C_OPW - 20, r.y + (r.h - 14) / 2, a, fit_scale(a, r.w - C_OPW + 2, 2), C_CYAN);
    }
    // the machine: the screen, the LEDs, the registers
    if (in_band(C_SCR_Y - 4, 8 * C_PIX + 8)) {
        R(C_SCR_X - 2, C_SCR_Y - 2, 8 * C_PIX + 4, 8 * C_PIX + 4, rgb(60, 70, 64));
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                bool on = (s_cpu.mem[MEM_SCR + y] >> (7 - x)) & 1;
                R(C_SCR_X + x * C_PIX, C_SCR_Y + y * C_PIX, C_PIX - 1, C_PIX - 1,
                  on ? rgb(120, 255, 140) : rgb(10, 30, 16));
            }
        }
    }
#if BOARD_BAR_SCREEN
    int lx = C_SCR_X + 4, ly = C_LED_Y, lstep = 9;
#else
    int lx = C_REG_X + 6, ly = C_LED_Y, lstep = 15;
#endif
    for (int i = 0; i < 8; i++) {
        lamp(lx + i * lstep, ly, BOARD_BAR_SCREEN ? 3 : 5, (s_cpu.mem[MEM_LEDS] >> (7 - i)) & 1);
    }
#if BOARD_BAR_SCREEN
    TXT(C_SCR_X, ly + 8, "LEDS", 1, C_DIM);
    int ry = C_SCR_Y;
    snprintf(t, sizeof(t), "A $%02X %3d", s_cpu.a, s_cpu.a);
    TXT(C_REG_X, ry, t, 1, C_SILK);
    snprintf(t, sizeof(t), "X $%02X %3d", s_cpu.x, s_cpu.x);
    TXT(C_REG_X, ry + 11, t, 1, C_SILK);
    snprintf(t, sizeof(t), "Z %d  C %d", s_cpu.z, s_cpu.c);
    TXT(C_REG_X, ry + 22, t, 1, C_SILK);
    snprintf(t, sizeof(t), "PC L%d", s_cpu.pc + 1);
    TXT(C_REG_X, ry + 33, t, 1, C_DIM);
    snprintf(t, sizeof(t), "CYC %lu", (unsigned long)s_cpu.cycles);
    TXT(C_REG_X, ry + 44, t, 1, C_GOLD);
    snprintf(t, sizeof(t), "M0 %02X M1 %02X", s_cpu.mem[0], s_cpu.mem[1]);
    TXT(C_REG_X, ry + 55, t, 1, C_DIM);
    snprintf(t, sizeof(t), "LEDS %02X", s_cpu.mem[MEM_LEDS]);
    TXT(C_REG_X, ry + 66, t, 1, C_DIM);
    const char *card = s_sel >= 0 && s_prog[s_sel].op ? OP[s_prog[s_sel].op].card : Z->goal;
    TXT(C_SCR_X, 136, card, 1, s_sel >= 0 && s_prog[s_sel].op ? C_CYAN : C_GOLD);
    TXT(C_SCR_X, 152, s_cpu_msg, 1, s_cpu_msg_c);
#else
    snprintf(t, sizeof(t), "A%02X X%02X Z%d C%d CYC%lu", s_cpu.a, s_cpu.x, s_cpu.z, s_cpu.c,
             (unsigned long)s_cpu.cycles);
    TXT(C_REG_X, C_TY0, t, 1, C_SILK);
    const char *card = s_sel >= 0 && s_prog[s_sel].op ? OP[s_prog[s_sel].op].card : Z->name;
    TXT(C_REG_X, C_TY1, card, 1, s_sel >= 0 && s_prog[s_sel].op ? C_CYAN : C_GOLD);
    TXT(C_REG_X, C_TY2, s_cpu_msg, 1, s_cpu_msg_c);
#endif
}

// ---------------------------------------------------------------- the picker: choose

static void render_byte(void)
{
    char t[40];
    if (in_band(0, TOP_H)) {
        R(0, 0, SW, TOP_H, C_TOP);
        R(0, TOP_H - 1, SW, 1, C_GOLD);
        button(BACK_RC, "X", NULL, C_BAD, false);
        snprintf(t, sizeof(t), "$%02X = %d", s_byte, s_byte);
        TXT(BAR_X0 + 8, (TOP_H - 14) / 2, t, 2, C_GOLD);
        button(BYTE_OK_RC, "OK", NULL, C_GOOD, true);
    }
    R(0, TOP_H, SW, BODY_H, C_BG);
    draw_switches(s_byte, 0xFF, false, BYTE_SW);
}

static void pk_title(void)
{
#if BOARD_ROUND
    if (!TOP_BAND) {
#else
    if (!in_band(0, TOP_H)) {
#endif
        return;
    }
    const char *t = s_pk.title;
    uint16_t c = C_GOLD;
    // the card of the instruction under the finger
    if (s_pk.kind == PK_OP && s_px >= 0) {
        for (int i = 0; i < s_pk.n; i++) {
            if (in_rc(pk_cell(i), s_px, s_py) && s_pk.val[i] != O_NONE) {
                t = OP[s_pk.val[i]].card;
                c = C_CYAN;
            }
        }
    }
    if (s_pk.kind == PK_BLOCK && s_px >= 0) {
        for (int i = 0; i < s_pk.n; i++) {
            if (in_rc(pk_cell(i), s_px, s_py) && s_pk.val[i] != B_NONE) {
                t = BLOCK[s_pk.val[i]].about;
                c = C_CYAN;
            }
        }
    }
    button(BACK_RC, "X", NULL, C_BAD, false);
#if BOARD_ROUND
    // (the round bar: the long card in the top cap and the bar, the title in the bar)
    text_fit(BAR_X0 + 4, BAR_R - BAR_X0 - 8, (TOP_H - 14) / 2, s_pk.title, 2, C_GOLD);
    if (t != s_pk.title) {
        text_fit(40, 240, CAP_Y + 6, t, 1, c);
    }
    return;
#endif
    text_fit(BACK_RC.w + 4, SW - BACK_RC.w - 8, (TOP_H - 14) / 2, t, 2, c);
}

// PK_BLOCK: the slot s_pk.a gets the block v; its inputs go, and the wires from a removed output go away
static void pk_set_block(slot_t *sl, int v)
{
    sl->type = (uint8_t)v;
    sl->src[0] = sl->src[1] = sl->src[2] = SRC_NONE;
    // wires from a removed output go away
    for (int k = 0; k < MAX_SLOTS; k++) {
        for (int j = 0; j < 3; j++) {
            int s = s_slot[k].src[j];
            if (s >= 8 && (s - 8) / 2 == s_pk.a && ((s - 8) & 1) >= BLOCK[v].nout) {
                s_slot[k].src[j] = SRC_NONE;
            }
        }
    }
    for (int j = 0; j < 3; j++) {
        int s = s_osrc[j];
        if (s >= 8 && (s - 8) / 2 == s_pk.a && ((s - 8) & 1) >= BLOCK[v].nout) {
            s_osrc[j] = SRC_NONE;
        }
    }
    s_live.q[s_pk.a] = 0;
}

static void pk_choose(int i)
{
    int v = s_pk.val[i];
    switch (s_pk.kind) {
    case PK_BLOCK: {
        slot_t *sl = &s_slot[s_pk.a];
        if (sl->type != v) {
            pk_set_block(sl, v);
        }
        build_changed();
        break;
    }
    case PK_SRC:
        s_slot[s_pk.a].src[s_pk.b] = (int8_t)v;
        build_changed();
        break;
    case PK_OUT:
        s_osrc[s_pk.b] = (int8_t)v;
        build_changed();
        break;
    case PK_OP: {
        int line = s_pk.a;
        int old = OP[s_prog[line].op].mode;
        s_prog[line].op = (uint8_t)v;
        if (OP[v].mode != old) {
            s_prog[line].arg = 0;
        }
        if (OP[v].mode == AM_MEM && (s_prog[line].op == O_STA_M)) {
            s_prog[line].arg = MEM_LEDS;
        }
        if (OP[v].mode == AM_MEMX) {
            s_prog[line].arg = MEM_SCR;
        }
        s_running = false;
        cpu_reset(&s_cpu, 0);
        memset(&s_pk, 0, sizeof(s_pk));
        if (v != O_NONE && OP[v].mode != AM_IMP) {
            pk_open_arg(line);  // the operand next
            snd_click();
            return;
        }
        break;
    }
    case PK_ADDR:
    case PK_LINE:
        s_prog[s_pk.a].arg = (uint8_t)v;
        cpu_reset(&s_cpu, 0);
        break;
    default:
        break;
    }
    memset(&s_pk, 0, sizeof(s_pk));
    snd_click();
}

static void pk_input(const input_event_t *ev)
{
    if (s_pk.kind == PK_BYTE) {
        if (ev->kind == IN_DOWN && ev->y >= TOP_H) {
            int i = ev->x / (SW / 8);
            if (i >= 0 && i < 8) {
                s_byte ^= (uint8_t)(0x80 >> i);
                snd_click();
            }
            return;
        }
        if (ev->kind != IN_TAP) {
            return;
        }
        if (in_rc(BACK_RC, ev->x, ev->y)) {
            memset(&s_pk, 0, sizeof(s_pk));
            return;
        }
        if (in_rc(BYTE_OK_RC, ev->x, ev->y)) {
            s_prog[s_pk.a].arg = s_byte;
            cpu_reset(&s_cpu, 0);
            memset(&s_pk, 0, sizeof(s_pk));
            snd_click();
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (in_rc(BACK_RC, ev->x, ev->y)) {
        memset(&s_pk, 0, sizeof(s_pk));
        snd_click();
        return;
    }
    for (int i = 0; i < s_pk.n; i++) {
        if (in_rc(pk_cell(i), ev->x, ev->y)) {
            pk_choose(i);
            return;
        }
    }
}

// ================================================================ REF

#define N_REF 5
static int s_ref;

static void ref_open(void)
{
    s_state = ST_REF;
    s_ref = 0;
}

static void render_ref(void)
{
    static const char *const TITLE[N_REF] = {"PLACE VALUES", "GATES", "BLOCKS", "6502 (1/2)", "6502 (2/2)"};
    char t[64];
#if BOARD_ROUND
    if (TOP_BAND) {
        draw_cap(TITLE[s_ref]);
    }
#endif
    if (in_band(0, TOP_H)) {
        R(0, 0, SW, TOP_H, C_TOP);
        R(0, TOP_H - 1, SW, 1, C_EDGE);
        button(BACK_RC, "<", NULL, C_SILK, false);
#if !BOARD_ROUND
        TXT(BACK_RC.w + 6, (TOP_H - 14) / 2, TITLE[s_ref], fit_scale(TITLE[s_ref], REF_PREV_RC.x - BACK_RC.w - 10, 2),
            C_GOLD);
#endif
        button(REF_PREV_RC, "<", NULL, s_ref > 0 ? C_SILK : C_DIM, false);
        button(REF_NEXT_RC, ">", NULL, s_ref < N_REF - 1 ? C_SILK : C_DIM, false);
    }
    R(0, TOP_H, SW, BODY_H, C_BG);
    int y0 = TOP_H + 6;
    switch (s_ref) {
    case 0: {
        static const char *const WT[8] = {"128", "64", "32", "16", "8", "4", "2", "1"};
        int cw = REF_W / 8;
        for (int i = 0; i < 8; i++) {
            text_fit(REF_X0 + i * cw, cw, y0, WT[i], 2, C_GOLD);
            snprintf(t, sizeof(t), "%s%d", BOARD_ROUND ? "B" : "BIT ", 7 - i);
            TXT_IN(REF_X0 + i * cw, cw, y0 + 18, t, 1, C_DIM);
        }
        int cols = BOARD_BAR_SCREEN ? 8 : 4, rows = 16 / cols;
        int hw = REF_W / cols, hy = y0 + 34, hh = (REF_SH - hy - 4) / rows;
        for (int d = 0; d < 16; d++) {
            int x = REF_X0 + (d % cols) * hw, y = hy + (d / cols) * hh;
            snprintf(t, sizeof(t), "%X=%d%d%d%d", d, (d >> 3) & 1, (d >> 2) & 1, (d >> 1) & 1, d & 1);
            TXT_IN(x, hw, y + (hh - 7) / 2, t, hh >= 30 && !BOARD_ROUND ? 2 : 1, d < 10 ? C_SILK : C_CYAN);
        }
        break;
    }
    case 1: {
        int cols = BOARD_BAR_SCREEN ? 6 : 3, rows = 6 / cols;
        int cw = REF_W / cols, ch = (REF_SH - TOP_H) / rows;
        for (int i = 0; i < 6; i++) {
            int g = GATE_OF[i];
            int x = REF_X0 + (i % cols) * cw, y = TOP_H + (i / cols) * ch;
            TXT_IN(x, cw, y + 4, BLOCK[g].name, 2, C_GOLD);
            gate_draw(g, x + cw / 2 - 26, y + 22, 44, 30, C_PANEL, C_SILK);
            if (g == B_NOT) {
                TXT_IN(x, cw, y + 58, "0>1 1>0", 1, C_SILK);
            } else {
                snprintf(t, sizeof(t), "00>%d 01>%d", gate_eval(g, 0, 0), gate_eval(g, 0, 1));
                TXT_IN(x, cw, y + 58, t, 1, C_SILK);
                snprintf(t, sizeof(t), "10>%d 11>%d", gate_eval(g, 1, 0), gate_eval(g, 1, 1));
                TXT_IN(x, cw, y + 70, t, 1, C_SILK);
            }
        }
        break;
    }
    case 2: {
        static const uint8_t B4[4] = {B_HA, B_FA, B_SR, B_DFF};
        int lh = (REF_SH - TOP_H) / 4;
        for (int i = 0; i < 4; i++) {
            int y = TOP_H + i * lh + 4;
            TXT(REF_X0 + 6, y, BLOCK[B4[i]].name, 2, C_GOLD);
            text_fit(BOARD_BAR_SCREEN ? 90 : REF_X0 + 6, BOARD_BAR_SCREEN ? SW - 96 : REF_W - 12,
                     y + (BOARD_BAR_SCREEN ? 4 : 17), BLOCK[B4[i]].about, 1, C_SILK);
        }
        break;
    }
    default: {
        int from = s_ref == 3 ? 1 : 11, to = s_ref == 3 ? 10 : N_OPS - 1;
        int n = to - from + 1;
        int cols = BOARD_BAR_SCREEN ? 2 : 1, rows = (n + cols - 1) / cols;
        int cw = REF_W / cols, lh = (REF_SH - TOP_H - 4) / rows;
        for (int k = 0; k < n; k++) {
            int o = from + k;
            int x = REF_X0 + (k / rows) * cw + 6, y = TOP_H + 3 + (k % rows) * lh;
            snprintf(t, sizeof(t), "%s  %dC", OP[o].card, OP[o].cycles);
            TXT(x, y + (lh - 7) / 2, t, 1, k & 1 ? C_SILK : C_CYAN);
        }
        break;
    }
    }
}

static void ref_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    if (in_rc(REF_PREV_RC, ev->x, ev->y) && s_ref > 0) {
        s_ref--;
    } else if (in_rc(REF_NEXT_RC, ev->x, ev->y) && s_ref < N_REF - 1) {
        s_ref++;
    } else if (ev->y > TOP_H) {
        s_ref = (s_ref + 1) % N_REF;
    } else {
        return;
    }
    snd_click();
}

// ================================================================ title, levels, result

static bool s_res_ok;
static int s_res_stars;
static int64_t s_res_at;

static void result_show(bool ok, int stars, const char *detail)
{
    s_res_ok = ok;
    s_res_stars = stars;
    s_res_at = s_now;
    if (detail != s_res_detail) {
        snprintf(s_res_detail, sizeof(s_res_detail), "%s", detail);
    }
    if (ok && stars > 0) {
        stars_set(s_mode, s_tile, stars);
    }
    s_state = ST_RESULT;
    s_running = false;
    memset(&s_pk, 0, sizeof(s_pk));
    if (ok) {
        sfx_play(SFX_WIN);
    } else {
        sfx_play(SFX_GAME_OVER);
    }
}

static const char *tile_name(int m, int t)
{
    switch (m) {
    case M_BIN:
        return BIN_LEV[t].name;
    case M_GATE:
        return t < 6 ? BLOCK[GATE_OF[t]].name : "MYSTERY";
    case M_BUILD:
        return t < N_BUILD_P ? PUZ[t].name : "LAB";
    case M_CPU:
        return CPUZ[t].name;
    default:
        return "";
    }
}

static void start_tile(int t)
{
    s_tile = t;
    s_pop_at = 0;
    switch (s_mode) {
    case M_BIN:
        bin_start();
        break;
    case M_GATE:
        gate_start();
        break;
    case M_BUILD:
        if (t == N_BUILD_P) {
            lab_start();
        } else {
            build_start();
        }
        break;
    case M_CPU:
        cpu_start();
        break;
    default:
        break;
    }
}

static int lv_per_page(void)
{
    return LV_COLS * LV_ROWS;
}

static rc_t lv_rc(int i)
{
    int k = i % lv_per_page();
#if BOARD_ROUND
    {
        // (the round frame: 4 rows from the bar to the row 222, each as wide as its chord)
        int ch = (222 - TOP_H) / LV_ROWS, y = TOP_H + (k / LV_COLS) * ch, half = rch2(y, ch) - 4;
        int x0 = 160 - half + (k % LV_COLS) * 2 * half / LV_COLS;
        int x1 = 160 - half + (k % LV_COLS + 1) * 2 * half / LV_COLS;
        return RC(x0 + 2, y + 2, x1 - x0 - 4, ch - 4);
    }
#endif
    int cw = SW / LV_COLS, ch = BODY_H / LV_ROWS;
    return RC((k % LV_COLS) * cw + 2, TOP_H + (k / LV_COLS) * ch + 2, cw - 4, ch - 4);
}

static void render_levels(void)
{
    char t[40];
    snprintf(t, sizeof(t), "%s  %d STARS", MODE_NAME[s_mode], stars_sum(s_mode));
    draw_top(t, NULL);
    R(0, TOP_H, SW, BODY_H, C_BG);
    int n = N_TILES[s_mode];
    for (int i = s_lv_page * lv_per_page(); i < n && i < (s_lv_page + 1) * lv_per_page(); i++) {
        rc_t r = lv_rc(i);
        if (!in_band(r.y, r.h)) {
            continue;
        }
        bool open = tile_open(s_mode, i);
        bool free = (s_mode == M_BIN && i == 0) || (s_mode == M_BUILD && i == N_BUILD_P);
        panel(r, pressed(r) && open ? C_PANEL_HI : (open ? C_PCB : rgb(8, 26, 18)), open ? C_EDGE : rgb(30, 50, 40));
        const char *nm = tile_name(s_mode, i);
        snprintf(t, sizeof(t), "%d", i + (s_mode == M_BIN ? 0 : 1));
        if (!free) {
            TXT(r.x + 4, r.y + 3, t, 1, open ? C_DIM : rgb(50, 70, 60));
        }
        int sc = fit_scale(nm, r.w - 6, 2);
        TXT_IN(r.x, r.w, r.y + r.h / 2 - 7 * sc / 2 - 4, nm, sc, open ? C_SILK : rgb(60, 84, 70));
        if (!open) {
            lock_icon(r.x + r.w / 2, r.y + r.h - 12, rgb(70, 90, 80));
        } else if (!free) {
            stars_row(r.x + r.w / 2, r.y + r.h - 9, stars_get(s_mode, i), 5);
        }
    }
    int pages = (n + lv_per_page() - 1) / lv_per_page();
    if (pages > 1 && in_band(TOP_H, 10)) {
        snprintf(t, sizeof(t), "PAGE %d/%d >", s_lv_page + 1, pages);
#if BOARD_ROUND
        TXT(BAR_R - rtw(t, 1) - 6, (TOP_H - 7) / 2, t, 1, C_CYAN);
#else
        TXT(SW - gfx_text_width(t, 1) - 6, (TOP_H - 7) / 2, t, 1, C_CYAN);
#endif
    }
}

static void levels_input(const input_event_t *ev)
{
    int n = N_TILES[s_mode];
    int pages = (n + lv_per_page() - 1) / lv_per_page();
    if (ev->kind == IN_TAP && ev->y < TOP_H && ev->x > SW / 2 && pages > 1) {
        s_lv_page = (s_lv_page + 1) % pages;
        snd_click();
        return;
    }
    for (int i = s_lv_page * lv_per_page(); i < n && i < (s_lv_page + 1) * lv_per_page(); i++) {
        if (!in_rc(lv_rc(i), ev->x, ev->y)) {
            continue;
        }
        if (ev->kind == IN_LONG && !tile_open(s_mode, i)) {
            s_open |= 1u << (8 + s_mode);
            store_set("bits_open", s_open);
            pop("ALL LEVELS OPEN", C_GOLD);
            snd_good();
            return;
        }
        if (ev->kind != IN_TAP) {
            return;
        }
        if (!tile_open(s_mode, i)) {
            snd_bad();
            pop("SOLVE THE ONE BEFORE (OR HOLD)", C_BAD);
            return;
        }
        snd_click();
        start_tile(i);
        return;
    }
}

#if BOARD_ROUND
// the title of the round frame: BITS in the top cap, the cards in the circle
static void render_title_round(void)
{
    TXT_MID(160, -30, "BITS", 4, C_GOLD);
    TXT_MID(160, 6, "HOW A COMPUTER WORKS,", 1, C_SILK);
    TXT_MID(160, 18, "BIT BY BIT", 1, C_SILK);
    TXT_MID(160, 30, "HOLD A LOCK: OPEN", 1, rgb(70, 100, 80));
}
#endif

static void render_title(int64_t now)
{
    R(0, 0, SW, SH, C_BG);
    // circuit traces in the background
    for (int i = 0; i < 6; i++) {
        int y = (BOARD_BAR_SCREEN ? 20 : 8) + i * (BOARD_BAR_SCREEN ? 26 : 9);
        if (!in_band(y - 1, 3)) {
            continue;
        }
        R(0, y, BOARD_BAR_SCREEN ? 196 : SW, 1, rgb(24, 60, 40));
        int px = (int)((now / 12 + i * 53) % (BOARD_BAR_SCREEN ? 196 : SW));
        R(px, y - 1, 6, 3, C_TRACE_ON);
    }
#if BOARD_BAR_SCREEN
    TXT(16, 30, "BITS", 6, C_GOLD);
    TXT(18, 82, "HOW A COMPUTER", 1, C_SILK);
    TXT(18, 94, "WORKS, BIT BY BIT", 1, C_SILK);
    TXT(18, 120, "0 1 > GATES >", 1, C_DIM);
    TXT(18, 132, "CIRCUITS > CPU", 1, C_DIM);
    TXT(18, 150, "HOLD A LOCK: OPEN", 1, rgb(70, 100, 80));
#elif BOARD_ROUND
    render_title_round();
#else
    TXT(10, 12, "BITS", 5, C_GOLD);
    TXT(140 + MDX / 4, 14, "HOW A COMPUTER", 1, C_SILK);
    TXT(140 + MDX / 4, 26, "WORKS, BIT BY BIT", 1, C_SILK);
    TXT(140 + MDX / 4, 44, "HOLD A LOCK: OPEN", 1, rgb(70, 100, 80));
#endif
    for (int m = 0; m < N_MODES; m++) {
        rc_t r = TC_RC(m);
        if (!in_band(r.y, r.h)) {
            continue;
        }
        bool open = mode_open(m);
        panel(r, pressed(r) && open ? C_PANEL_HI : (open ? C_PCB : rgb(8, 26, 18)), open ? C_GOLD : rgb(30, 50, 40));
        int sc = fit_scale(MODE_NAME[m], r.w - 6, 2);
        TXT_IN(r.x, r.w, r.y + 8, MODE_NAME[m], sc, open ? C_SILK : rgb(60, 84, 70));
        TXT_IN(r.x, r.w, r.y + 26, MODE_SUB[m], 1, open ? C_CYAN : rgb(50, 70, 60));
        // a small picture of the mode
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2 + (BOARD_BAR_SCREEN ? 4 : 2);
        switch (m) {
        case M_BIN:
            for (int i = 0; i < 4; i++) {
                lamp(cx - 21 + i * 14, cy, 5, (0xA >> (3 - i)) & 1);
            }
            break;
        case M_GATE:
            gate_draw(B_AND, cx - 18, cy - 11, 30, 22, C_PANEL, open ? C_SILK : rgb(60, 84, 70));
            break;
        case M_BUILD:
            R(cx - 16, cy - 10, 32, 20, C_CHIP);
            for (int i = 0; i < 4; i++) {
                R(cx - 13 + i * 8, cy - 14, 3, 4, C_DIM);
                R(cx - 13 + i * 8, cy + 10, 3, 4, C_DIM);
            }
            break;
        case M_CPU:
            TXT_IN(r.x, r.w, cy - 7, "LDA", 2, open ? C_CYAN : rgb(50, 70, 60));
            break;
        default:
            TXT_IN(r.x, r.w, cy - 7, "?", 2, open ? C_DIM : rgb(50, 70, 60));
            break;
        }
        if (m == M_REF) {
            continue;
        }
        if (!open) {
            lock_icon(cx, r.y + r.h - 26, rgb(70, 90, 80));
            TXT_IN(r.x, r.w, r.y + r.h - 12, mode_lock_text(m), 1, rgb(70, 90, 80));
        } else {
            char t[32];
            int max = 3 * (N_TILES[m] - (m == M_BIN ? 1 : 0) - (m == M_BUILD ? 1 : 0));
            snprintf(t, sizeof(t), "%d/%d", stars_sum(m), max);
            star(cx - gfx_text_width(t, 1) / 2 - 8, r.y + r.h - 12, 4, true);
            TXT(cx - gfx_text_width(t, 1) / 2 + 2, r.y + r.h - 15, t, 1, C_GOLD);
        }
    }
}

static void title_input(const input_event_t *ev)
{
    for (int m = 0; m < N_MODES; m++) {
        if (!in_rc(TC_RC(m), ev->x, ev->y)) {
            continue;
        }
        if (ev->kind == IN_LONG && !mode_open(m)) {
            s_open |= 1u << m;
            store_set("bits_open", s_open);
            pop("OPEN", C_GOLD);
            snd_good();
            return;
        }
        if (ev->kind != IN_TAP) {
            return;
        }
        if (!mode_open(m)) {
            snd_bad();
            pop(mode_lock_text(m), C_BAD);
            return;
        }
        snd_click();
        s_mode = m;
        if (m == M_REF) {
            ref_open();
        } else {
            s_state = ST_LEVELS;
            s_lv_page = 0;
        }
        return;
    }
}

static void render_result(void)
{
    char t[48];
    snprintf(t, sizeof(t), "%s", tile_name(s_mode, s_tile));
    draw_top(t, NULL);
    R(0, TOP_H, SW, BODY_H, C_BG);
    int y = TOP_H + (BOARD_BAR_SCREEN ? 6 : 18);
    TXT_C(y, s_res_ok ? (s_mode == M_BUILD ? "IT WORKS!" : "SOLVED!") : "TRY AGAIN", 3, s_res_ok ? C_GOOD : C_BAD);
    stars_row(SW / 2, y + (BOARD_BAR_SCREEN ? 40 : 52), s_res_stars, BOARD_BAR_SCREEN ? 9 : 12);
    text_fit(0, SW, y + (BOARD_BAR_SCREEN ? 58 : 76), s_res_detail, 1, C_SILK);
    bool next = s_res_ok && s_tile + 1 < N_TILES[s_mode] && tile_open(s_mode, s_tile + 1);
    button(R_BTN_RC(0), "AGAIN", NULL, C_SILK, false);
    button(R_BTN_RC(1), "NEXT", NULL, next ? C_GOOD : C_DIM, next);
    button(R_BTN_RC(2), "LEVELS", NULL, C_SILK, false);
}

static void result_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP || s_now - s_res_at < 400) {
        return;
    }
    if (in_rc(R_BTN_RC(0), ev->x, ev->y)) {
        start_tile(s_tile);
    } else if (in_rc(R_BTN_RC(1), ev->x, ev->y)) {
        if (s_res_ok && s_tile + 1 < N_TILES[s_mode] && tile_open(s_mode, s_tile + 1)) {
            start_tile(s_tile + 1);
        } else {
            return;
        }
    } else if (in_rc(R_BTN_RC(2), ev->x, ev->y)) {
        s_state = ST_LEVELS;
    } else {
        return;
    }
    snd_click();
}

// ================================================================ the app

static void go_back(void)
{
    snd_click();
    s_running = false;
    switch (s_state) {
    case ST_TITLE:
        app_open(&app_menu);
        break;
    case ST_LEVELS:
    case ST_REF:
        s_state = ST_TITLE;
        break;
    default:
        s_state = ST_LEVELS;
        break;
    }
}

static void bits_init(void)
{
}

static void bits_enter(int64_t now)
{
    s_now = now;
    for (int m = 0; m < 4; m++) {
        s_stars[m] = store_get(KEY_STARS[m], 0);
    }
    s_open = store_get("bits_open", 0);
    s_state = ST_TITLE;
    s_px = s_py = -1;
    memset(&s_pk, 0, sizeof(s_pk));
    s_running = false;
    s_pop_at = 0;
}

static void bits_input(const input_event_t *ev)
{
#if BOARD_UI_TABLET
    input_event_t tv = *ev;  // (the Tab5 view: the touch in the logical frame)
    tv.x = ev->x * 2 / 3;
    tv.y = ev->y * 2 / 3;
    ev = &tv;
#elif BOARD_ROUND
    input_event_t tv = *ev;  // (the round view: the touch in the logical frame)
    tv.x = (int16_t)((float)ev->x / rv_k());
    tv.y = (int16_t)((float)ev->y / rv_k() - RV_OY);
    ev = &tv;
#endif
    if (ev->kind == IN_DOWN || ev->kind == IN_MOVE) {
        s_px = ev->x;
        s_py = ev->y;
    } else if (ev->kind == IN_UP) {
        s_px = s_py = -1;
    }
    if (s_pk.kind != PK_NONE) {
        pk_input(ev);
        return;
    }
    if (ev->kind == IN_TAP && in_rc(BACK_RC, ev->x, ev->y)) {
        go_back();
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        title_input(ev);
        break;
    case ST_LEVELS:
        levels_input(ev);
        break;
    case ST_BIN:
        bin_input(ev);
        break;
    case ST_GATE:
        gate_input(ev);
        break;
    case ST_BUILD:
        build_input(ev);
        break;
    case ST_LAB:
        lab_input(ev);
        break;
    case ST_CPU:
        cpu_input(ev);
        break;
    case ST_RESULT:
        result_input(ev);
        break;
    case ST_REF:
        ref_input(ev);
        break;
    }
}

static void bits_update(int64_t now)
{
    s_now = now;
    switch (s_state) {
    case ST_BIN:
        bin_update();
        break;
    case ST_GATE:
        gate_update();
        break;
    case ST_CPU:
        cpu_update();
        break;
    case ST_BUILD:
        if (s_solved_at && now - s_solved_at > 900 && s_pk.kind == PK_NONE) {
            snprintf(s_res_detail, sizeof(s_res_detail), "%d BLOCKS (BEST %d)", blocks_used(), PUZ[s_pz].best);
            int st = build_stars();
            s_solved_at = 0;
            result_show(true, st, s_res_detail);
        }
        break;
    default:
        break;
    }
}

static void render_pop(void)
{
    if (!s_pop_at || s_now - s_pop_at > 900) {
        return;
    }
    int sc = fit_scale(s_pop, SW - 20, 2);
    int w = gfx_text_width(s_pop, sc) + 16, h = 7 * sc + 12;
    int y = TOP_H + 4;
    if (s_state == ST_BIN && !BOARD_BAR_SCREEN) {
        y = B_SW_Y - h - 2;
    }
    rc_t r = RC((SW - w) / 2, y, w, h);
    if (!in_band(r.y, r.h)) {
        return;
    }
    panel(r, C_TOP, s_pop_c);
    TXT_IN(r.x, r.w, r.y + 6, s_pop, sc, s_pop_c);
}

static void render_page(int64_t now)
{
#if BOARD_ROUND
    FILL(C_BG);  // (the round frame: the caps out of the frame too)
#endif
    if (s_pk.kind == PK_BYTE) {
        render_byte();
        return;
    }
    if (s_pk.kind != PK_NONE) {
        render_picker();
        pk_title();
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        render_title(now);
        break;
    case ST_LEVELS:
        render_levels();
        break;
    case ST_BIN:
        render_bin();
        break;
    case ST_GATE:
        render_gate();
        break;
    case ST_BUILD:
        R(0, TOP_H, SW, BODY_H, C_BG);
        render_build_top();
        if (s_view_try) {
            render_try();
        } else {
            render_wire();
        }
        break;
    case ST_LAB:
        render_lab();
        break;
    case ST_CPU:
        render_cpu();
        break;
    case ST_RESULT:
        render_result();
        break;
    case ST_REF:
        render_ref();
        break;
    }
    render_pop();
}

static void bits_render(int64_t now)
{
    font_capture();
    for (int y0 = 0; y0 < gfx_h; y0 += BAND) {
        s_by0 = y0;
        s_by1 = y0 + BAND < gfx_h ? y0 + BAND : gfx_h;
        render_page(now);
    }
    s_by0 = 0;
    s_by1 = 1 << 30;
}

#include "tficon/tficon.h"
// the menu icon: a chip; four lights on it count in binary (0 to 15 in 2 s), the digits under them follow
static void bits_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x2ee67a), tfi_hex(0x006b3c));
    float u = s / 100.0f, ph = tfi_ph(2);
    int n = (int)(ph * 16) & 15;
    // the pins and the chip
    for (int k = 0; k < 5; k++) {
        int p = x + (int)((26 + k * 12) * u);
        tfd_rect(p, y + (int)(16 * u), (int)(4 * u), (int)(70 * u), tfi_hex(0xe0e4ec), 256);
        tfd_rect(x + (int)(16 * u), p - (int)(2 * u), (int)(70 * u), (int)(4 * u), tfi_hex(0xe0e4ec), 256);
    }
    int cx = x + (int)(22 * u), cy = y + (int)(22 * u), cw = (int)(56 * u);
    tfi_drop(cx, cy, cw, cw, (int)(6 * u), (int)(3 * u), 90);
    tfi_gloss_box(cx, cy, cw, cw, (int)(6 * u), tfi_hex(0x3a3f4e), tfi_hex(0x101218));
    for (int k = 0; k < 4; k++) {
        bool on = n >> (3 - k) & 1;
        float lx = cx + (9 + k * 12.5f) * u, ly = cy + 20 * u;
        if (on) {
            tfi_glow(lx, ly, 9 * u, tfi_hex(0x6dff8a), 220);
        }
        tfi_disc(lx, ly, 4.5f * u, on ? tfi_hex(0xd0ffd8) : tfi_hex(0x2a3a30),
                 on ? tfi_hex(0x3ad870) : tfi_hex(0x1a2420), 256);
        // the digit: 1 = a bar, 0 = a ring
        uint16_t dc = on ? tfi_hex(0x6dff8a) : tfi_hex(0x5a6a62);
        if (on) {
            tfd_rect((int)(lx - 1 * u), (int)(cy + 32 * u), (int)(2.4f * u) > 1 ? (int)(2.4f * u) : 1, (int)(10 * u),
                     dc, 256);
        } else {
            tfi_ring(lx, cy + 37 * u, 3.6f * u, 2 * u, dc, 256);
        }
    }
    tfi_end();
}

const app_t app_bits = {
        .name = "BITS",
        .landscape = true,
        .round = APP_ROUND_HI,  // (a round panel: the round frame, see THE ROUND LAYOUT)
        .needs = 0,
        .init = bits_init,
        .enter = bits_enter,
        .input = bits_input,
        .update = bits_update,
        .render = bits_render,
        .draw_icon = bits_icon,
};
