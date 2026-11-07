// The layouts of the settings panel (sysui_panel.c) for each frame kind: the boxes of the controls and the chips,
// the mm of a px, the kind of the frame now, the scale of the round frame, the pull zone (sysui.h).
#include "sysui_priv.h"

const pdef_t SUI_PDEF[PL_N] = {
        // the 3.49 landscape 640 x 172 (0.133 mm a px): the sliders side by side; MENU at the left end, RESUME at the right end
        [PL_BAR_L] = {136,
                      176,
                      464,
                      74,
                      126,
                      {{14, 8, 298, 56}, {328, 8, 298, 56}, {476, 74, 150, 52}, {14, 74, 150, 52}},
                      200,
                      440,
                      24,
                      3,
                      3,
                      1,
                      3},
        // the 3.49 portrait 172 x 640: one control a row; MENU at the top, RESUME at the bottom (the sliders between them)
        [PL_BAR_P] = {318,
                      8,
                      164,
                      4,
                      32,
                      {{10, 98, 152, 58}, {10, 164, 152, 58}, {10, 240, 152, 62}, {10, 38, 152, 48}},
                      36,
                      136,
                      28,
                      3,
                      2,
                      1,
                      3},
        // the CoreS3 frame 320 x 240 (0.128 mm; the AMOLED shows it x1.4: 0.111 mm)
        [PL_BOX_L] = {210,
                      12,
                      308,
                      4,
                      30,
                      {{12, 32, 296, 52}, {12, 90, 296, 52}, {160, 152, 148, 50}, {12, 152, 100, 50}},
                      120,
                      200,
                      24,
                      3,
                      2,
                      1,
                      3},
        [PL_BOX_P] = {284,
                      12,
                      228,
                      4,
                      30,
                      {{12, 90, 216, 52}, {12, 150, 216, 52}, {12, 214, 216, 56}, {12, 34, 216, 46}},
                      70,
                      170,
                      26,
                      3,
                      2,
                      1,
                      3},
        // the AMOLED native frame (0.079 mm): 448 x 368 and 368 x 448
        [PL_NAT_L] = {272,
                      18,
                      430,
                      6,
                      42,
                      {{18, 46, 412, 64}, {18, 118, 412, 64}, {206, 196, 224, 62}, {18, 196, 140, 62}},
                      174,
                      274,
                      30,
                      4,
                      3,
                      2,
                      3},
        [PL_NAT_P] = {272,
                      16,
                      352,
                      6,
                      42,
                      {{16, 46, 336, 64}, {16, 118, 336, 64}, {176, 196, 176, 62}, {16, 196, 112, 62}},
                      134,
                      234,
                      30,
                      4,
                      3,
                      2,
                      3},
        // the Watcher, a round layout 412 x 412 (0.089 mm): all in the circle; MENU at the top, RESUME at the bottom
        // (H 412: the sheet covers the whole circle: no straight lower edge across the round panel)
        [PL_ROUND] = {412,
                      0,
                      0,
                      0,
                      0,
                      {{56, 108, 300, 64}, {50, 180, 312, 64}, {86, 258, 240, 72}, {136, 40, 140, 56}},
                      146,
                      266,
                      52,
                      12,
                      3,
                      2,
                      4},
        // the Watcher, the CoreS3 frame zoomed x1.29 (0.115 mm): the controls in the part that the circle shows
        [PL_ZOOM_L] = {226,
                       0,
                       0,
                       0,
                       0,
                       {{44, 56, 232, 48}, {40, 110, 240, 48}, {60, 166, 200, 48}, {110, 4, 100, 44}},
                       113,
                       208,
                       24,
                       3,
                       2,
                       1,
                       3},
        [PL_ZOOM_P] = {276,
                       0,
                       0,
                       0,
                       0,
                       {{16, 96, 208, 48}, {14, 152, 212, 48}, {30, 212, 180, 50}, {50, 40, 140, 46}},
                       74,
                       167,
                       40,
                       24,
                       2,
                       1,
                       3},
        // the Tab5 frame 640 x 360 and 360 x 640 (0.17 mm a px): the controls 12 mm or more high
        [PL_TAB_L] = {232,
                      16,
                      624,
                      6,
                      46,
                      {{16, 54, 296, 72}, {328, 54, 296, 72}, {400, 142, 224, 78}, {16, 142, 180, 78}},
                      200,
                      440,
                      40,
                      4,
                      3,
                      3,
                      4},
        [PL_TAB_P] = {404,
                      12,
                      348,
                      6,
                      48,
                      {{16, 140, 328, 72}, {16, 222, 328, 72}, {16, 310, 328, 80}, {16, 56, 180, 74}},
                      100,
                      260,
                      40,
                      4,
                      3,
                      3,
                      4},
};
// THE LAYOUTS WITH THE ITEMS OF AN APP (stage B): the four controls move (the sliders side by side where the frame is
// wide), and the chips get their slots (in the order of the items; more items than slots: the last slot is MORE). All
// the chips are 5 mm or more on the glass (the round layouts: in the circle). The zone and the grip stay those of PDEF.
const pdefi_t SUI_PDEFI[PL_N] = {
        // the 3.49 landscape: the sliders in the top row, 4 chips, then MENU, 2 chips, RESUME (the whole height)
        [PL_BAR_L] = {172,
                      0,
                      0,
                      0,
                      0,
                      {{8, 6, 308, 52}, {324, 6, 308, 52}, {482, 118, 150, 50}, {8, 118, 120, 50}},
                      3,
                      2,
                      3,
                      6,
                      {{8, 64, 150, 48},
                       {166, 64, 150, 48},
                       {324, 64, 150, 48},
                       {482, 64, 150, 48},
                       {136, 118, 168, 50},
                       {312, 118, 162, 50}}},
        // the 3.49 portrait: one control a row; the chips between BRIGHTNESS and RESUME
        [PL_BAR_P] = {522,
                      4,
                      168,
                      4,
                      32,
                      {{10, 94, 152, 56}, {10, 156, 152, 56}, {10, 448, 152, 60}, {10, 38, 152, 48}},
                      3,
                      2,
                      3,
                      4,
                      {{10, 220, 152, 48}, {10, 276, 152, 48}, {10, 332, 152, 48}, {10, 388, 152, 48}}},
        // the 320 x 240 frame: the sliders side by side, 2 rows of 3 chips, MENU and RESUME at the bottom (the whole height)
        [PL_BOX_L] = {240,
                      0,
                      0,
                      0,
                      0,
                      {{6, 4, 151, 50}, {163, 4, 151, 50}, {164, 176, 150, 56}, {6, 176, 100, 56}},
                      3,
                      1,
                      3,
                      6,
                      {{6, 60, 98, 50},
                       {111, 60, 98, 50},
                       {216, 60, 98, 50},
                       {6, 116, 98, 50},
                       {111, 116, 98, 50},
                       {216, 116, 98, 50}}},
        [PL_BOX_P] = {300,
                      0,
                      0,
                      0,
                      0,
                      {{8, 6, 224, 50}, {8, 62, 224, 50}, {104, 236, 128, 56}, {8, 236, 90, 56}},
                      3,
                      2,
                      3,
                      4,
                      {{8, 120, 110, 50}, {122, 120, 110, 50}, {8, 176, 110, 50}, {122, 176, 110, 50}}},
        // the AMOLED native frame (0.079 mm)
        [PL_NAT_L] = {316,
                      0,
                      0,
                      0,
                      0,
                      {{12, 8, 206, 68}, {230, 8, 206, 68}, {252, 238, 184, 70}, {12, 238, 140, 70}},
                      4,
                      3,
                      3,
                      6,
                      {{12, 86, 136, 66},
                       {156, 86, 136, 66},
                       {300, 86, 136, 66},
                       {12, 160, 136, 66},
                       {156, 160, 136, 66},
                       {300, 160, 136, 66}}},
        [PL_NAT_P] = {430,
                      16,
                      352,
                      6,
                      42,
                      {{16, 46, 336, 64}, {16, 116, 336, 64}, {176, 346, 176, 68}, {16, 346, 112, 68}},
                      4,
                      3,
                      3,
                      4,
                      {{16, 190, 164, 66}, {188, 190, 164, 66}, {16, 262, 164, 66}, {188, 262, 164, 66}}},
        // the Watcher round 412 x 412: MENU at the top, the sliders side by side, 3 + 2 chips, RESUME (all in the circle)
        [PL_ROUND] = {412,
                      0,
                      0,
                      0,
                      0,
                      {{34, 102, 170, 56}, {208, 102, 170, 56}, {106, 290, 200, 60}, {136, 40, 140, 56}},
                      0,
                      2,
                      3,
                      5,
                      {{8, 164, 128, 56},
                       {142, 164, 128, 56},
                       {276, 164, 128, 56},
                       {34, 226, 168, 56},
                       {210, 226, 168, 56}}},
        // the Watcher zoom of the 320 x 240 frame (only the circle of r 160 round (160, 120) shows)
        [PL_ZOOM_L] =
                {238,
                 0,
                 0,
                 0,
                 0,
                 {{30, 50, 128, 42}, {162, 50, 128, 42}, {80, 194, 160, 40}, {112, 4, 96, 42}},
                 0,
                 0,
                 2,
                 5,
                 {{14, 98, 94, 42}, {113, 98, 94, 42}, {212, 98, 94, 42}, {40, 146, 116, 42}, {164, 146, 116, 42}}},
        [PL_ZOOM_P] = {276,
                       0,
                       0,
                       0,
                       0,
                       {{14, 74, 212, 44}, {14, 124, 212, 44}, {50, 226, 140, 44}, {70, 26, 100, 42}},
                       0,
                       1,
                       2,
                       2,
                       {{14, 174, 104, 44}, {122, 174, 104, 44}}},
        // the Tab5 640 x 360: the sliders side by side, 2 rows of 3 chips, MENU and RESUME at the bottom (the whole height)
        [PL_TAB_L] = {360,
                      0,
                      0,
                      0,
                      0,
                      {{12, 10, 302, 72}, {326, 10, 302, 72}, {408, 264, 220, 84}, {12, 264, 180, 84}},
                      3,
                      3,
                      4,
                      6,
                      {{12, 92, 196, 68},
                       {222, 92, 196, 68},
                       {432, 92, 196, 68},
                       {12, 170, 196, 68},
                       {222, 170, 196, 68},
                       {432, 170, 196, 68}}},
        // the Tab5 360 x 640: the sliders one a row, 3 rows of 2 chips, MENU and RESUME at the bottom
        [PL_TAB_P] = {524,
                      0,
                      0,
                      0,
                      0,
                      {{12, 10, 336, 72}, {12, 92, 336, 72}, {164, 424, 184, 86}, {12, 424, 140, 86}},
                      3,
                      3,
                      4,
                      6,
                      {{12, 176, 162, 68},
                       {186, 176, 162, 68},
                       {12, 254, 162, 68},
                       {186, 254, 162, 68},
                       {12, 332, 162, 68},
                       {186, 332, 162, 68}}},
};
// mm of one logical px of the frame now (the thresholds of the pull and the flick are in mm)
float sui_mm_px(void)
{
    // (the values of the board: board_caps.h; 0 = no such frame)
    return BOARD_ROUND_HI_MM_PX > 0 && gfx_round_hi() ? BOARD_ROUND_HI_MM_PX
           : BOARD_NATIVE_MM_PX > 0 && gfx_native()   ? BOARD_NATIVE_MM_PX
                                                      : BOARD_FRAME_MM_PX;
}
int sui_pl_kind(void)
{
    bool land = gfx_w > gfx_h;
#if BOARD_ROUND
    if (gfx_round()) {
        return PL_ROUND;
    }
#if !BOARD_ROUND_ZOOM
    return land ? PL_BOX_L : PL_BOX_P;  // (the 1.75C shows all of the 320 x 240 frame: no zoom)
#endif
    return land ? PL_ZOOM_L : PL_ZOOM_P;
#else
    if (gfx_native()) {
        return land ? PL_NAT_L : PL_NAT_P;
    }
#if BOARD_UI_TABLET
    return land ? PL_TAB_L : PL_TAB_P;
#endif
    if (gfx_w >= 2 * gfx_h) {
        return PL_BAR_L;
    }
    if (gfx_h >= 2 * gfx_w) {
        return PL_BAR_P;
    }
    return land ? PL_BOX_L : PL_BOX_P;
#endif
}
void sysui_zone_logical(int *x0, int *y0, int *x1, int *y1)
{
    const pdef_t *d = sui_pdef_now();
    *x0 = d->zx0;
    *x1 = d->zx1;
    *y0 = 0;
    *y1 = d->zy1;
}

// THE ROUND FRAME AT THE PANEL RESOLUTION (the 1.75C: 466 x 466): the round boxes are for 412 x 412; they
// scale to the frame (rs: a length, rs_box: a box)
int sui_rs(int v)
{
#if BOARD_ROUND
    return gfx_round() && gfx_w != BOARD_NATIVE_W ? (v * gfx_w + BOARD_NATIVE_W / 2) / BOARD_NATIVE_W : v;
#else
    return v;  // (no round frame on this board)
#endif
}
void sui_rs_box(pbox_t *b)
{
    b->x = (int16_t)sui_rs(b->x);
    b->y = (int16_t)sui_rs(b->y);
    b->w = (int16_t)sui_rs(b->w);
    b->h = (int16_t)sui_rs(b->h);
}
void sui_rs_def(pdef_t *d)
{
    d->H = (int16_t)sui_rs(d->H);
    d->tx0 = (int16_t)sui_rs(d->tx0);
    d->tx1 = (int16_t)sui_rs(d->tx1);
    d->ty0 = (int16_t)sui_rs(d->ty0);
    d->ty1 = (int16_t)sui_rs(d->ty1);
    for (int i = 0; i < PC_N; i++) {
        sui_rs_box(&d->c[i]);
    }
    d->zx0 = (int16_t)sui_rs(d->zx0);
    d->zx1 = (int16_t)sui_rs(d->zx1);
    d->zy1 = (int16_t)sui_rs(d->zy1);
    d->grip_y = (int16_t)sui_rs(d->grip_y);
}
// the layout of the frame now with no items (the pull zone, the grip)
static pdef_t s_P;
const pdef_t *sui_pdef_now(void)
{
    s_P = SUI_PDEF[sui_pl_kind()];
    if (sui_rs(BOARD_NATIVE_W) != BOARD_NATIVE_W) {
        sui_rs_def(&s_P);
    }
    return &s_P;
}
