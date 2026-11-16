// GLASSHOUSE: the screens and the overlays: the title, the shelf of the levels, the HUD of the play, the card at the
// start of a level, the end card, the replay band and the menu icon. All text is English or Hebrew (a Hebrew line is
// laid out from the right; the places of the HUD are mirrored). Included by app_glass.c.

#define SW gfx_w
#define SH gfx_h
static bool s_he;  // Hebrew
#define TX(en, he) (s_he ? (he) : (en))
static inline bool box_frame(void)
{
    return SH > 200;
}
// The round frame (gfx_round(): 466 x 466 on the 1.75C, 412 on the Watcher): the layout of the 320 x 240 frame, RK
// times bigger; the HUD in the top cap and the bottom cap of the circle, the buttons on the bottom arc.
static inline bool rlay(void)
{
    return gfx_round();
}
#define RK (SW / 320.0f)
// the width of the circle at the row y (px)
static int rchord(int y)
{
    float dy = y - SH * 0.5f, h2 = SW * SW * 0.25f - dy * dy;
    return h2 > 0 ? (int)(2 * sqrtf(h2)) : 0;
}

typedef struct {
    int x, y, w, h;
} box_t;
// (a fat finger: the box and m px round it)
static bool in_box_m(box_t b, int x, int y, int m)
{
    return x >= b.x - m && y >= b.y - m && x < b.x + b.w + m && y < b.y + b.h + m;
}
static box_t mirror(box_t b)
{
    if (s_he) {
        b.x = SW - b.x - b.w;
    }
    return b;
}

// a button of glass: a dark fill, a light edge, the text in the middle (down = pressed)
static void glass_button(box_t b, const char *text, bool main, bool down)
{
    uint16_t fill = main ? C3(30, 96, 84) : C3(22, 40, 44);
    if (down) {
        fill = cmix(fill, C3(255, 255, 255), 0.2f);
    }
    tfd_rrect(b.x, b.y, b.w, b.h, b.h / 2 > 14 ? 14 : b.h / 2, C3(4, 10, 12), 140);
    tfd_rrect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 12, fill, 235);
    tfd_line((float)b.x + 12, (float)b.y + 3, (float)(b.x + b.w) - 12, (float)b.y + 3, 1.2f, C3(200, 255, 240),
             main ? 150 : 90);
    tfd_rrect(b.x + 1, b.y + b.h - 4, b.w - 2, 3, 2, C3(0, 0, 0), 70);
    const kfont_t *f = tfd_pick(&KF_BIG, &KF_NAME, text, b.w - 12);
    tfd_text_c(f, b.x + b.w / 2, b.y + b.h / 2 + (f == &KF_BIG ? 9 : 6), text,
               main ? C3(236, 255, 246) : C3(214, 232, 226), b.w - 10);
}
// a text with a soft dark shadow (on the picture of the world)
static void text_sh(const kfont_t *f, int x, int base, const char *s, uint16_t c, int align, int w)
{
    char b[160];
    const char *t = tfd_fit(f, s, w, b, sizeof b);
    int tw = ui_text_w(f, t);
    int cx = align < 0 ? x + tw / 2 : align > 0 ? x - tw + tw / 2 : x;
    ui_text_c(f, cx + 1, base + 1, t, C3(0, 0, 0));
    ui_text_c(f, cx, base, t, c);
}
// a 5-point star (filled, or an outline when off)
static void star(float cx, float cy, float r, bool on, int a)
{
    float p[20];
    for (int k = 0; k < 10; k++) {
        float an = -PI_ / 2 + k * PI_ / 5, rr = k % 2 ? r * 0.45f : r;
        p[2 * k] = cx + fcos(an) * rr;
        p[2 * k + 1] = cy + fsin(an) * rr;
    }
    if (on) {
        tfd_poly(p, 10, C3(255, 214, 102), a);
        tfd_glow(cx, cy, r * 1.6f, C3(255, 200, 90), a / 3);
    } else {
        tfd_poly(p, 10, C3(60, 70, 72), a * 3 / 4);
    }
    for (int k = 0; k < 10; k++) {
        int q = (k + 1) % 10;
        tfd_line(p[2 * k], p[2 * k + 1], p[2 * q], p[2 * q + 1], 1, on ? C3(255, 246, 200) : C3(120, 132, 130), a);
    }
}
// a dark soft band behind text (x0 .. x1)
static void shade_band(int x0, int y0, int x1, int y1, int a)
{
    for (int y = y0; y < y1; y++) {
        float f = (float)(y - y0) / (float)(y1 - y0);
        int aa = (int)(a * (f < 0.25f ? f * 4 : f > 0.75f ? (1 - f) * 4 : 1));
        tfd_hspan(x0, x1, y, C3(6, 12, 16), aa);
    }
}

// ---------------------------------------------------------------- the texts of the moves
static const char *move_word(int kind)
{
    switch (kind) {
    case MV_CUT:
        return TX("CUT", "חתוך");
    case MV_PULL:
        return TX("PULL", "שלוף");
    case MV_CRACK:
        return TX("CRACK", "סדוק");
    case MV_KICK:
        return TX("PUSH", "דחוף");
    case MV_OPEN:
        return TX("OPEN", "פתח");
    default:
        return TX("START", "הפעל");
    }
}

// ---------------------------------------------------------------- the title
static box_t title_play(void)
{
    if (rlay()) {
        return mirror((box_t){SW / 2 - SW * 36 / 100, SH * 70 / 100, SW * 38 / 100, SH * 13 / 100});
    }
    return box_frame() ? mirror((box_t){24, SH - 70, 150, 54}) : mirror((box_t){26, 104, 156, 54});
}
static box_t title_levels(void)
{
    if (rlay()) {
        return mirror((box_t){SW / 2 + SW * 4 / 100, SH * 71 / 100, SW * 30 / 100, SH * 11 / 100});
    }
    return box_frame() ? mirror((box_t){188, SH - 66, 112, 46}) : mirror((box_t){196, 108, 120, 46});
}
static void title_draw(int pressed, int li, const char *lvname)
{
    if (rlay()) {
        shade_band(0, SH * 8 / 100, SW, SH * 30 / 100, 190);
        shade_band(0, SH * 58 / 100, SW, SH * 90 / 100, 170);
    } else if (box_frame()) {
        shade_band(0, 18, SW, 104, 190);
    } else {
        // (a dark band behind the text: full at the side of the text, then a fade in 12 steps; spans, not pixels)
        for (int y = 0; y < SH; y++) {
            if (s_he) {
                tfd_hspan(SW - 240, SW, y, C3(6, 12, 16), 150);
            } else {
                tfd_hspan(0, 240, y, C3(6, 12, 16), 150);
            }
            for (int k = 0; k < 12; k++) {
                int a = (int)(150 * (1 - (k + 0.5f) / 12)), x0 = 240 + k * 10;
                if (s_he) {
                    tfd_hspan(SW - x0 - 10, SW - x0, y, C3(6, 12, 16), a);
                } else {
                    tfd_hspan(x0, x0 + 10, y, C3(6, 12, 16), a);
                }
            }
        }
    }
    const char *t = TX("GLASSHOUSE", "חממה");
    const char *sub = TX("A PUZZLE OF GLASS AND TIMBER", "פאזל של זכוכית ועץ");
    if (rlay()) {
        text_sh(&KF_TITLE, SW / 2, SH * 18 / 100, t, C3(222, 248, 240), 0, SW * 70 / 100);
        text_sh(&KF_NAME, SW / 2, SH * 24 / 100, sub, C3(255, 214, 160), 0, SW * 76 / 100);
    } else if (box_frame()) {
        text_sh(&KF_TITLE, SW / 2, 60, t, C3(222, 248, 240), 0, SW - 20);
        text_sh(&KF_NAME, SW / 2, 86, sub, C3(255, 214, 160), 0, SW - 20);
    } else {
        int x = s_he ? SW - 26 : 26, al = s_he ? 1 : -1;
        text_sh(&KF_TITLE, x, 52, t, C3(222, 248, 240), al, 320);
        text_sh(&KF_NAME, x, 78, sub, C3(255, 214, 160), al, 320);
    }
    char line[80];
    snprintf(line, sizeof line, "%d  %s", li + 1, lvname);
    box_t p = title_play(), l = title_levels();
    glass_button(p, TX("PLAY", "שחק"), true, pressed == 0);
    glass_button(l, TX("LEVELS", "שלבים"), false, pressed == 1);
    if (rlay()) {
        text_sh(&KF_NAME, SW / 2, SH * 66 / 100, line, C3(200, 220, 214), 0, SW * 80 / 100);
    } else if (box_frame()) {
        text_sh(&KF_LABEL, SW / 2, SH - 82, line, C3(200, 220, 214), 0, SW - 20);
    } else {
        text_sh(&KF_LABEL, s_he ? SW - 28 : 28, 96, line, C3(200, 220, 214), s_he ? 1 : -1, 300);
    }
}

// ---------------------------------------------------------------- the shelf of the levels
static int shelf_cols(void)
{
    return box_frame() ? 3 : 4;
}
static box_t shelf_tile(int i, int n)
{
    int cols = shelf_cols(), rows = (n + cols - 1) / cols;
    int col = i % cols, row = i / cols;
    if (s_he) {
        col = cols - 1 - col;
    }
    if (rlay()) {
        // (the round frame: 3 x 4 bigger tiles in the middle of the circle; the last tile is BACK)
        int w = SW * 25 / 100, gx = SW / 80, gy = SH / 120, h = SH * 125 / 1000;
        int x0 = (SW - cols * w - (cols - 1) * gx) / 2, y0 = SH * 55 / 100 - (4 * h + 3 * gy) / 2;
        return (box_t){x0 + col * (w + gx), y0 + row * (h + gy), w, h};
    }
    if (box_frame()) {
        // (3 x 4 tiles of 46 px: 42 px and more is the aim for a thumb on the AMOLED; the last tile is BACK)
        (void)rows;
        int w = 98, gx = 5, gy = 2, y0 = 38, h = (SH - y0 - 6 - 3 * gy) / 4;
        if (h > 52) {
            h = 52;
        }
        int x0 = (SW - cols * w - (cols - 1) * gx) / 2;
        return (box_t){x0 + col * (w + gx), y0 + row * (h + gy), w, h};
    }
    int w = 150, gx = 8, gy = rows > 2 ? 4 : 8, y0 = 38;
    int h = (SH - y0 - 4 - (rows - 1) * gy) / (rows ? rows : 1);
    if (h > 58) {
        h = 58;
    }
    int x0 = (SW - cols * w - (cols - 1) * gx) / 2;
    return (box_t){x0 + col * (w + gx), y0 + row * (h + gy), w, h};
}
static box_t shelf_back(void)
{
    return box_frame() ? shelf_tile(11, 12) : mirror((box_t){4, 2, 56, 36});
}
static void shelf_draw(int n, const uint8_t *stars, int open_to, int pressed, const char *const *names)
{
    // the walnut of the shelf and the glass of a cabinet
    for (int y = 0; y < SH; y++) {
        float f = (float)y / SH;
        tfd_hspan(0, SW, y, cmix(HX(0x3d2617), HX(0x1a0f08), f), 256);
    }
    for (int k = 0; k < 18; k++) {
        float y0 = (float)(k * 37 % SH);
        for (int x = 0; x < SW; x += 16) {
            tfd_line((float)x, y0 + fsin(x * 0.02f + k) * 1.4f, (float)x + 16, y0 + fsin((x + 16) * 0.02f + k) * 1.4f,
                     0.6f, HX(0x140a05), 90);
        }
    }
    // (the 320 x 240 frame: the tiles have no name; the band shows the name of the tile under the finger)
    int hy = rlay() ? shelf_tile(0, n).y - 14 : 0;
    if (box_frame() && pressed >= 0 && pressed < n && pressed <= open_to) {
        text_sh(&KF_NAME, SW / 2, rlay() ? hy : 26, names[pressed], C3(242, 213, 139), 0,
                rlay() ? rchord(hy - 10) - 30 : SW - 16);
    } else {
        text_sh(&KF_BIG, SW / 2, rlay() ? hy : 30, TX("THE SHELF", "המדף"), C3(242, 213, 139), 0, SW - 140);
    }
    box_t bk = shelf_back();
    glass_button(bk, box_frame() ? TX("BACK", "חזרה") : s_he ? ">" : "<", false, pressed == 99);
    for (int i = 0; i < n; i++) {
        box_t t = shelf_tile(i, n);
        bool open = i <= open_to;
        tfd_rrect(t.x, t.y, t.w, t.h, 8, C3(10, 22, 24), 230);
        tfd_rrect(t.x + 2, t.y + 2, t.w - 4, t.h - 4, 7, open ? C3(30, 58, 56) : C3(26, 30, 32), 230);
        tfd_line((float)t.x + 8, (float)t.y + 4, (float)(t.x + t.w) - 8, (float)t.y + 4, 1, C3(200, 255, 240),
                 open ? 110 : 40);
        if (pressed == i) {
            tfd_rrect(t.x, t.y, t.w, t.h, 8, C3(255, 255, 255), 50);
        }
        if (box_frame()) {
            // a big number on brass and the stars under it, or a lock
            char nb[8];
            snprintf(nb, sizeof nb, "%d", i + 1);
            int cx = t.x + t.w / 2;
            if (open) {
                tfd_rrect(cx - 18, t.y + 5, 36, 24, 5, C3(196, 150, 70), 256);
                tfd_rrect(cx - 17, t.y + 6, 34, 11, 4, C3(242, 213, 139), 160);
                tfd_text_c(&KF_NAME, cx, t.y + 23, nb, C3(58, 38, 10), 34);
                for (int k = 0; k < 3; k++) {
                    star((float)(cx - 14 + (s_he ? 2 - k : k) * 14), (float)(t.y + t.h - 9), 5, k < stars[i], 230);
                }
            } else {
                tfd_text_c(&KF_NAME, cx, t.y + 21, nb, C3(110, 116, 112), 34);
                tfd_rrect(cx - 6, t.y + t.h - 16, 12, 10, 2, C3(110, 116, 112), 256);
                tfd_arc((float)cx, (float)(t.y + t.h - 16), 4, 2, PI_, TAU_, C3(110, 116, 112), 256);
            }
            continue;
        }
        // the brass plate with the number
        int pw = 28, ph = t.h < 44 ? 22 : 26;
        int px = s_he ? t.x + t.w - pw - 6 : t.x + 6, py = t.y + (t.h - ph) / 2;
        tfd_rrect(px, py, pw, ph, 4, open ? C3(196, 150, 70) : C3(90, 84, 70), 256);
        tfd_rrect(px + 1, py + 1, pw - 2, ph / 2, 3, open ? C3(242, 213, 139) : C3(120, 112, 96), 160);
        char num[8];
        snprintf(num, sizeof num, "%d", i + 1);
        tfd_text_c(&KF_NAME, px + pw / 2, py + ph / 2 + 6, num, C3(58, 38, 10), pw);
        int tx0 = s_he ? t.x + 6 : px + pw + 6, tx1 = s_he ? px - 6 : t.x + t.w - 6;
        if (open) {
            const char *nm = names[i];
            if (!strncmp(nm, "THE ", 4)) {
                nm += 4;
            }
            bool two = t.h >= 40;
            tfd_text_s(&KF_LABEL, tx0, tx1, two ? t.y + t.h / 2 - 2 : t.y + t.h / 2 + 5, nm, C3(222, 240, 234), s_he);
            if (two) {
                for (int k = 0; k < 3; k++) {
                    float sx = s_he ? (float)tx1 - 6 - k * 14 : (float)tx0 + 6 + k * 14;
                    star(sx, (float)(t.y + t.h - 10), 5, k < stars[i], 230);
                }
            } else {
                for (int k = 0; k < 3; k++) {
                    float sx = s_he ? (float)tx0 + 6 + k * 12 : (float)tx1 - 6 - k * 12;
                    star(sx, (float)(t.y + 9), 4, k < stars[i], 230);
                }
            }
        } else {
            // a small lock
            float lx = (tx0 + tx1) / 2.0f, ly = t.y + t.h / 2.0f;
            tfd_rrect((int)lx - 7, (int)ly - 2, 14, 11, 2, C3(110, 116, 112), 256);
            tfd_arc(lx, ly - 2, 4.5f, 2, PI_, TAU_, C3(110, 116, 112), 256);
        }
    }
}

// ---------------------------------------------------------------- the HUD of the play
// the round frame: the level and the state of the object in the top cap, the moves and the goal in the bottom cap
static void hud_draw_round(const char *name, int li, int moves_left, int moves, float goal, bool goal_break, int panes,
                           int need, int obj_state, const char *he_safe, const char *he_broke)
{
    char line[96];
    int y1 = SH * 13 / 100, y2 = SH * 19 / 100;
    shade_band(0, SH * 4 / 100, SW, SH * 23 / 100, 170);
    snprintf(line, sizeof line, "%d  %s", li + 1, name);
    text_sh(&KF_NAME, SW / 2, y1, line, C3(246, 234, 210), 0, rchord(y1 - 12) - 30);
    const char *st = obj_state == 0   ? TX("SAFE", he_safe)
                     : obj_state == 1 ? TX("AT RISK", "בסכנה")
                                      : TX("BROKEN", he_broke);
    uint16_t sc = obj_state == 0   ? C_HUD
                  : obj_state == 1 ? (fsin(s_clock * 14) > 0 ? C_WARN : cmul(C_WARN, 0.65f))
                                   : C3(255, 90, 80);
    text_sh(&KF_LABEL, SW / 2, y2, st, sc, 0, rchord(y2 - 10) - 30);
    // the bottom cap: the moves (diamonds) above, the goal (a bar and its text) below
    int by1 = SH * 86 / 100, by2 = SH * 925 / 1000;
    shade_band(0, SH * 80 / 100, SW, SH, 190);
    const char *mv = TX("MOVES", "מהלכים");
    int mw = ui_text_w(&KF_LABEL, mv), dw = moves * 15, tw = mw + 10 + dw, x0 = SW / 2 - tw / 2;
    int mx = s_he ? x0 + tw : x0;
    text_sh(&KF_LABEL, mx, by1, mv, C3(246, 234, 210), s_he ? 1 : -1, 100);
    for (int i = 0; i < moves; i++) {
        float cx = s_he ? (float)(x0 + dw - 7 - i * 15) : (float)(x0 + mw + 17 + i * 15), cy = (float)by1 - 5;
        float p[8] = {cx, cy - 6, cx + 6, cy, cx, cy + 6, cx - 6, cy};
        tfd_poly(p, 4, i < moves_left ? C_HUD : C3(110, 120, 116), i < moves_left ? 256 : 120);
    }
    const char *gl = goal_break ? TX("GLASS", "זכוכית") : TX("DOWN", "הופל");
    if (goal_break) {
        snprintf(line, sizeof line, "%s %d/%d", gl, panes, need);
    } else {
        snprintf(line, sizeof line, "%s %d%%", gl, (int)(clampf(goal, 0, 1) * 100));
    }
    int lw = ui_text_w(&KF_LABEL, line), bw = SW * 18 / 100, gw = lw + 10 + bw, g0 = SW / 2 - gw / 2;
    int bx = s_he ? g0 : g0 + lw + 10;
    tfd_rrect(bx, by2 - 9, bw, 8, 4, C3(10, 20, 22), 200);
    tfd_rrect(bx + 1, by2 - 8, (int)((bw - 2) * clampf(goal, 0, 1)), 6, 3, goal >= 1 ? C3(255, 214, 102) : C_HUD, 256);
    text_sh(&KF_LABEL, s_he ? g0 + gw : g0, by2, line, C3(246, 234, 210), s_he ? 1 : -1, 140);
}
static void hud_draw(const char *name, int li, int moves_left, int moves, float goal, bool goal_break, int panes,
                     int need, int obj_state, const char *obj_name, const char *he_safe, const char *he_broke)
{
    if (rlay()) {
        hud_draw_round(name, li, moves_left, moves, goal, goal_break, panes, need, obj_state, he_safe, he_broke);
        return;
    }
    bool bx = box_frame();
    char line[96];
    snprintf(line, sizeof line, "%d  %s", li + 1, name);
    int top = bx ? 20 : 17;
    if (bx) {
        shade_band(0, 0, SW, 28, 160);
    }
    // the fragile object: SAFE, AT RISK (it blinks), BROKEN (Hebrew: a whole phrase of each object, with the gender of
    // its noun: "הביצה שלמה"); the 320 x 240 frame has room only for the word of the state
    const char *st = obj_state == 0   ? TX("SAFE", he_safe)
                     : obj_state == 1 ? TX("AT RISK", "בסכנה")
                                      : TX("BROKEN", he_broke);
    if ((s_he && obj_state != 1) || bx) {
        obj_name = "";
    }
    uint16_t sc = obj_state == 0   ? C_HUD
                  : obj_state == 1 ? (fsin(s_clock * 14) > 0 ? C_WARN : cmul(C_WARN, 0.65f))
                                   : C3(255, 90, 80);
    int sw_ = ui_text_w(&KF_LABEL, st), on_w = obj_name[0] ? ui_text_w(&KF_LABEL, obj_name) + 6 : 0;
    if (on_w > (bx ? 110 : 220)) {
        on_w = bx ? 110 : 220;
    }
    text_sh(&KF_LABEL, s_he ? 8 : SW - 8, top - 1, st, sc, s_he ? -1 : 1, 150);
    if (obj_name[0]) {
        text_sh(&KF_LABEL, s_he ? 14 + sw_ : SW - 14 - sw_, top - 1, obj_name, C3(225, 215, 200), s_he ? -1 : 1, on_w);
    }
    // the level: its number and its name, in the room that is left
    int room = SW - 16 - sw_ - on_w - 14;
    text_sh(bx ? &KF_LABEL : &KF_NAME, s_he ? SW - 8 : 8, top, line, C3(246, 234, 210), s_he ? 1 : -1,
            room > 300 ? 300 : room);
    // the moves (diamonds) and the goal (a bar)
    int by = bx ? SH - 12 : SH - 9;
    if (bx) {
        shade_band(0, SH - 30, SW, SH, 150);
    }
    const char *mv = TX("MOVES", "מהלכים");
    int mw = ui_text_w(&KF_LABEL, mv);
    int mx = s_he ? SW - 8 : 8;
    text_sh(&KF_LABEL, mx, by, mv, C3(246, 234, 210), s_he ? 1 : -1, 80);
    for (int i = 0; i < moves; i++) {
        float cx = s_he ? (float)(mx - mw - 12 - i * 13) : (float)(mx + mw + 12 + i * 13), cy = (float)by - 5;
        float p[8] = {cx, cy - 5, cx + 5, cy, cx, cy + 5, cx - 5, cy};
        tfd_poly(p, 4, i < moves_left ? C_HUD : C3(110, 120, 116), i < moves_left ? 256 : 120);
    }
    const char *gl = goal_break ? TX("GLASS", "זכוכית") : TX("DOWN", "הופל");
    int bw = bx ? 70 : 96;
    int gx1 = s_he ? 8 + bw : SW - 8, gx0 = gx1 - bw;
    tfd_rrect(gx0, by - 9, bw, 8, 4, C3(10, 20, 22), 200);
    tfd_rrect(gx0 + 1, by - 8, (int)((bw - 2) * clampf(goal, 0, 1)), 6, 3, goal >= 1 ? C3(255, 214, 102) : C_HUD, 256);
    if (goal_break) {
        snprintf(line, sizeof line, "%s %d/%d", gl, panes, need);
    } else {
        snprintf(line, sizeof line, "%s %d%%", gl, (int)(clampf(goal, 0, 1) * 100));
    }
    text_sh(&KF_LABEL, s_he ? gx1 + 8 : gx0 - 8, by, line, C3(246, 234, 210), s_he ? -1 : 1, 120);
}
// the pill of the selected handle: the word of the move, a big target near the handle
static box_t pill_box(float hx, float hy)
{
    int w = box_frame() ? 104 : 112, h = 44;
    if (rlay()) {
        w = 120;
        h = 50;
    }
    int x = (int)hx - w / 2, y = (int)hy + 18;
    if (y + h > SH - (box_frame() ? 32 : 4)) {
        y = (int)hy - 18 - h;
    }
    x = clampi(x, 6, SW - w - 6);
    y = clampi(y, box_frame() ? 30 : 4, SH - h - 4);
    if (rlay()) {
        // (in the circle: between the caps of the HUD, and inside the width of the circle at its rows)
        y = clampi(y, SH * 24 / 100, SH * 74 / 100 - h);
        int cw = rchord(y < SH / 2 ? y : y + h) / 2 - 8;
        x = clampi(x, SW / 2 - cw, SW / 2 + cw - w);
    }
    return (box_t){x, y, w, h};
}
// the card at the start of a level: the name and the idea
// a text on one line, or on two lines (cut at the space nearest to the middle) when it is longer than w
static void text_wrap2(const kfont_t *f, int cx, int base, int line, const char *s, uint16_t c, int w)
{
    if (ui_text_w(f, s) <= w) {
        tfd_text_c(f, cx, base, s, c, w);
        return;
    }
    int n = (int)strlen(s), best = -1;
    for (int i = 0; i < n; i++) {
        if (s[i] == ' ' && (best < 0 || abs(i - n / 2) < abs(best - n / 2))) {
            best = i;
        }
    }
    if (best < 0) {
        tfd_text_c(f, cx, base, s, c, w);
        return;
    }
    char a[160], b[160];
    snprintf(a, sizeof a, "%.*s", best, s);
    snprintf(b, sizeof b, "%s", s + best + 1);
    tfd_text_c(f, cx, base, a, c, w);
    tfd_text_c(f, cx, base + line, b, c, w);
}
static void intro_card(const char *name, const char *idea, float a)
{
    if (a <= 0) {
        return;
    }
    bool bx = box_frame();
    int y0 = rlay() ? SH * 21 / 100 : 0, h = bx ? 96 : 58;
    for (int y = y0; y < y0 + h; y++) {
        float f = (float)(y - y0) / h, e = f > 0.7f ? (1 - f) / 0.3f : 1;
        tfd_hspan(0, SW, y, C3(6, 12, 16), (int)(215 * a * e));
    }
    uint16_t c1 = cmix(C3(6, 12, 16), C3(242, 213, 139), a), c2 = cmix(C3(6, 12, 16), C3(230, 240, 236), a);
    int tw = rlay() ? rchord(y0 + 10) - 40 : SW - 16;
    tfd_text_c(&KF_BIG, SW / 2, y0 + 25, name, c1, tw);
    if (bx) {
        text_wrap2(&KF_NAME, SW / 2, y0 + 50, 20, idea, c2, rlay() ? rchord(y0 + 40) - 40 : SW - 16);
    } else {
        tfd_text_c(&KF_NAME, SW / 2, y0 + 46, idea, c2, SW - 16);
    }
}

// ---------------------------------------------------------------- the end card
enum {
    EB_NEXT,
    EB_REPLAY,
    EB_AGAIN,
    EB_N
};
static box_t s_end_btn[3];
static int s_end_id[3], s_end_nb;
static void end_layout(bool won, bool lost, bool has_next, bool has_replay)
{
    int n = 0;
    if (has_replay) {
        s_end_id[n++] = EB_REPLAY;
    }
    s_end_id[n++] = EB_AGAIN;
    if (won && has_next) {
        s_end_id[n++] = EB_NEXT;
    }
    (void)lost;
    s_end_nb = n;
    int bw = box_frame() ? 94 : 104, bh = 44, gap = 8;
    int ch = box_frame() ? 180 : 156, cy = box_frame() ? (SH - ch) / 2 + 6 : (SH - ch) / 2;
    if (rlay()) {
        bw = 104;
        bh = 52;
        ch = 204;
        cy = (SH - ch) / 2;
    }
    int x0 = (SW - n * bw - (n - 1) * gap) / 2, y = cy + ch - bh - 10;
    for (int i = 0; i < n; i++) {
        int k = s_he ? n - 1 - i : i;
        s_end_btn[i] = (box_t){x0 + k * (bw + gap), y, bw, bh};
    }
}
static void end_card(int kind, int stars, const char *title, const char *line, float t, int pressed)
{
    float a = clampf(t / 0.35f, 0, 1);
    bool bx = box_frame();
    int w = bx ? 300 : 360, h = bx ? 180 : 156, x = (SW - w) / 2, y = bx ? (SH - h) / 2 + 6 : (SH - h) / 2;
    if (rlay()) {
        w = 344;
        h = 204;
        x = (SW - w) / 2;
        y = (SH - h) / 2;
    }
    // (one box of the panel and a thin dark edge: two full boxes with alpha cost too much in each frame)
    tfd_rrect(x, y, w, h, 14, C3(12, 26, 28), (int)(225 * a));
    tfd_line((float)x + 14, (float)(y + h) - 1, (float)(x + w) - 14, (float)(y + h) - 1, 2, C3(0, 0, 0),
             (int)(120 * a));
    tfd_line((float)x + 16, (float)y + 4, (float)(x + w) - 16, (float)y + 4, 1.2f, C3(200, 255, 240), (int)(110 * a));
    if (a < 1) {
        return;
    }
    uint16_t tc = kind == 0 ? C3(255, 226, 150) : kind == 1 ? C3(255, 140, 120) : C3(220, 230, 226);
    tfd_text_c(&KF_BIG, SW / 2, y + 30, title, tc, w - 16);
    if (kind == 0) {
        for (int k = 0; k < 3; k++) {
            float at = 0.5f + k * 0.35f, s = clampf((t - at) / 0.18f, 0, 1);
            float r = bx ? 12 : 11;
            float sx = SW / 2.0f + (k - 1) * (r * 2.8f), sy = (float)y + (bx ? 62 : 54);
            if (s > 0 && k < stars) {
                star(sx, sy, r * (1 + 0.4f * (1 - s)), true, (int)(256 * s));
            } else {
                star(sx, sy, r, false, 220);
            }
        }
    }
    tfd_text_c(&KF_LABEL, SW / 2, y + (kind == 0 ? (bx ? 92 : 80) : (bx ? 66 : 58)), line, C3(214, 230, 224), w - 20);
    for (int i = 0; i < s_end_nb; i++) {
        int id = s_end_id[i];
        const char *bt = id == EB_NEXT     ? TX("NEXT", "הבא")
                         : id == EB_REPLAY ? TX("REPLAY", "שידור חוזר")
                                           : TX("AGAIN", "שוב");
        glass_button(s_end_btn[i], bt, id == EB_NEXT || (id == EB_AGAIN && kind != 0), pressed == i);
    }
}
// the replay: the bands of a film, a red light, REPLAY
static void replay_band(float t)
{
    int bh = box_frame() ? 22 : 12;
    if (rlay()) {
        bh = SH * 13 / 100;  // (the round frame: the bands of the film cover the top cap and the bottom cap)
    }
    for (int y = 0; y < bh; y++) {
        tfd_hspan(0, SW, y, C3(0, 0, 0), 230);
        tfd_hspan(0, SW, SH - 1 - y, C3(0, 0, 0), 230);
    }
    bool on = fmodf(t, 1.0f) < 0.6f;
    int x = rlay() ? SW / 2 + (s_he ? 40 : -40) : s_he ? SW - 14 : 14;
    if (on) {
        tfd_disc((float)x, (float)bh + 12, 4, C3(230, 50, 40), 256);
    }
    text_sh(&KF_LABEL, s_he ? x - 10 : x + 10, bh + 17, TX("REPLAY", "שידור חוזר"), C3(246, 234, 210), s_he ? 1 : -1,
            160);
}
