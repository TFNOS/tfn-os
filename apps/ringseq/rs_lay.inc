// RING SEQUENCER: the layouts. ROUND (the AMOLED-1.75C 466 x 466, the Watcher 412 x 412): the rings fill the circle,
// the controls are arcs at the rim and pads round the centre. SIDE (the 320 x 240 frame, the Tab5 640 x 360): the
// circle at the left, the controls in a column at the right. BAR (the 3.49, 640 x 172): a small circle at the left,
// ring keys and controls at the right; a ring opens as a long lane of steps. (Part of app_ringseq.c.)

enum {
    L_ROUND,
    L_SIDE,
    L_BAR
};
enum {
    V_MAIN,
    V_RING,
    V_KEY,
    V_SLOTS
};
enum {
    SH_RECT,
    SH_SECT,
    SH_DISC
};
// the touch zones (the elements of a screen)
enum {
    E_NONE,
    E_CENTRE,  // V_MAIN: play / tempo; V_RING: back (a swipe: the next ring); V_SLOTS: back
    E_RINGS,   // V_MAIN: a tap opens the ring at that radius
    E_BAND,    // V_RING: the steps on the big ring
    E_LANE,    // V_RING (BAR): the steps on the lane
    B_KEY,
    B_SWING,
    B_MODE,
    B_UNDO,
    B_SLOTS,
    B_VARY,
    B_EXPORT,
    B_PLAY,
    B_RING,  // arg = the ring
    P_HITS,
    P_STEPS,
    P_SND,
    P_LVL,
    B_MINUS,  // arg = 0..3: HITS, STEPS, SOUND, LEVEL
    B_PLUS,
    B_BACK,
    B_PREV,
    B_NEXT,
    K_KEY,   // arg = the key
    K_MODE,  // arg = the mode
    K_DONE,
    S_SLOT,  // arg = the slot
    B_MUTE,
    B_SOLO,
};
typedef struct {
    uint8_t sh, id, arg;
    float a, b, c, d, e, f;  // RECT: x y w h; SECT: cx cy r0 r1 a0 a1 (radians); DISC: cx cy r
} hit_t;
#define NHIT 48
static hit_t s_hit[NHIT];
static int s_nhit;

typedef struct {
    int kind;
    float cx, cy, R, s;          // the circle; s = R / 233
    float rc, g0, gd;            // V_MAIN: the centre disc, ring 0, the space of the rings
    float rim0;                  // ROUND V_MAIN: the inside of the rim controls
    float zc, zi, zr, zo;        // V_RING: the core, the inside of the band, the steps, the outside of the band
    float pin, pout;             // ROUND V_RING: the pads
    float lx, ly, lw, lh;        // BAR V_RING: the lane
    float mm;                    // mm for one px
    const font_t *fl, *fv, *fb;  // the faces: a label, a value, a big value
} lay_t;
static lay_t L;
static int s_view, s_sel;  // the screen, the ring of V_RING
static bool s_he;          // Hebrew

static float mm_px(void)
{
    if (gfx_round()) {
        return BOARD_ROUND_HI_MM_PX > 0 ? BOARD_ROUND_HI_MM_PX : BOARD_NATIVE_MM_PX;
    }
    return BOARD_FRAME_MM_PX;
}
static float rad(float deg)
{
    return deg * 0.017453293f;
}
static void hit_rect(int id, int arg, float x, float y, float w, float h)
{
    if (s_nhit < NHIT) {
        s_hit[s_nhit++] = (hit_t){SH_RECT, (uint8_t)id, (uint8_t)arg, x, y, w, h, 0, 0};
    }
}
static void hit_sect(int id, int arg, float r0, float r1, float a0, float a1)
{
    if (s_nhit < NHIT) {
        s_hit[s_nhit++] = (hit_t){SH_SECT, (uint8_t)id, (uint8_t)arg, L.cx, L.cy, r0, r1, rad(a0), rad(a1)};
    }
}
static void hit_disc(int id, int arg, float r)
{
    if (s_nhit < NHIT) {
        s_hit[s_nhit++] = (hit_t){SH_DISC, (uint8_t)id, (uint8_t)arg, L.cx, L.cy, r, 0, 0, 0};
    }
}
// a row of rect zones: ids/args with weights, from x0 to x1, gap g
static void hit_row(const uint8_t *id, const uint8_t *arg, const float *wt, int n, float x0, float x1, float y, float h,
                    float g)
{
    float sum = 0;
    for (int i = 0; i < n; i++) {
        sum += wt[i];
    }
    float u = (x1 - x0 - g * (n - 1)) / sum, x = x0;
    for (int i = 0; i < n; i++) {
        hit_rect(id[i], arg[i], x, y, u * wt[i], h);
        x += u * wt[i] + g;
    }
}
// the angle of a point (radians, 0 = up, with the clock, -pi..pi)
static float fatan2(float y, float x)
{
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax < 1e-6f && ay < 1e-6f) {
        return 0;
    }
    float a = ax < ay ? ax / ay : ay / ax, q = a * a;
    float r = ((-0.0464964749f * q + 0.15931422f) * q - 0.327622764f) * q * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = 3.14159274f - r;
    }
    return y < 0 ? -r : r;
}
static float ang_of(float x, float y)
{
    return fatan2(x - L.cx, -(y - L.cy));
}
static float dist_of(float x, float y)
{
    float dx = x - L.cx, dy = y - L.cy;
    return fsq(dx * dx + dy * dy);
}
static bool hit_in(const hit_t *h, float x, float y)
{
    if (h->sh == SH_RECT) {
        return x >= h->a && x < h->a + h->c && y >= h->b && y < h->b + h->d;
    }
    float dx = x - h->a, dy = y - h->b, d = fsq(dx * dx + dy * dy);
    if (h->sh == SH_DISC) {
        return d <= h->c;
    }
    if (d < h->c || d > h->d) {
        return false;
    }
    float t = fatan2(dx, -dy) - h->e;
    while (t < 0) {
        t += 6.2831853f;
    }
    while (t >= 6.2831853f) {
        t -= 6.2831853f;
    }
    return t <= h->f - h->e;
}
static const hit_t *hit_at(float x, float y)
{
    for (int i = 0; i < s_nhit; i++) {
        if (hit_in(&s_hit[i], x, y)) {
            return &s_hit[i];
        }
    }
    return NULL;
}
static const hit_t *hit_find(int id, int arg)
{
    for (int i = 0; i < s_nhit; i++) {
        if (s_hit[i].id == id && s_hit[i].arg == arg) {
            return &s_hit[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------- the three layouts

static void lay_round(void)
{
    float s = L.s, R = L.R;
    L.rc = 50 * s;
    L.g0 = 68 * s;
    L.gd = 17 * s;
    L.rim0 = 184 * s;
    L.zc = 54 * s;
    L.zi = 146 * s;
    L.zr = 190 * s;
    L.zo = 228 * s;
    L.pin = 60 * s;
    L.pout = 138 * s;
    L.fl = &KF_LABEL;
    L.fv = &KF_NAME;
    L.fb = &KF_BIG;
    if (s_view == V_MAIN) {
        hit_disc(E_CENTRE, 0, L.rc + 6 * s);
        hit_disc(E_RINGS, 0, L.rim0 - 2 * s);
        // the rim: a free middle at the top (the pull zone of the settings panel)
        static const struct {
            uint8_t id;
            int16_t a0, a1;
        } RIM[] = {{B_KEY, -70, -22},  {B_SWING, 22, 70},   {B_MODE, 73, 107}, {B_EXPORT, 110, 151},
                   {B_VARY, 154, 206}, {B_SLOTS, 209, 250}, {B_UNDO, 253, 287}};
        for (int i = 0; i < 7; i++) {
            hit_sect(RIM[i].id, 0, L.rim0, R + 40, RIM[i].a0, RIM[i].a1);
        }
    } else if (s_view == V_RING) {
        hit_disc(E_CENTRE, 0, L.zc + 4 * s);
        hit_sect(P_HITS, 0, L.pin, L.pout + 3 * s, -44, 44);
        hit_sect(P_STEPS, 0, L.pin, L.pout + 3 * s, 46, 134);
        hit_sect(P_SND, 0, L.pin, L.pout + 3 * s, 136, 224);
        hit_sect(P_LVL, 0, L.pin, L.pout + 3 * s, 226, 314);
        hit_disc(E_BAND, 0, R + 40);
    } else if (s_view == V_KEY) {
        for (int i = 0; i < 12; i++) {
            hit_sect(K_KEY, (uint8_t)((i * 7) % 12), 150 * s, R + 40, i * 30 - 15, i * 30 + 15);
        }
        for (int m = 0; m < NMODE; m++) {
            hit_rect(K_MODE, m, L.cx + (m % 2 ? 4 : -108) * s, L.cy + (-100 + 48 * (m / 2)) * s, 104 * s, 44 * s);
        }
        hit_rect(K_DONE, 0, L.cx - 58 * s, L.cy + 94 * s, 116 * s, 42 * s);
    } else {
        hit_disc(E_CENTRE, 0, 104 * s);
        for (int i = 0; i < NSLOT; i++) {
            hit_sect(S_SLOT, i, 112 * s, R + 40, i * 45 + 1, i * 45 + 44);
        }
    }
}
static void lay_side(void)
{
    float R = L.R, sx0 = 2 * R + 14, sx1 = gfx_w - 6, H = gfx_h;
    bool big = gfx_h >= 300;
    L.fl = big ? &KF_NAME : &KF_LABEL;
    L.fv = big ? &KF_BIG : &KF_NAME;
    L.fb = big ? &KF_TITLE : &KF_BIG;
    L.rc = 0.30f * R;
    L.g0 = 0.40f * R;
    L.gd = 0.092f * R;
    L.zc = 0.34f * R;
    L.zi = 0.42f * R;
    L.zr = 0.72f * R;
    L.zo = 0.98f * R;
    float g = 6, cw = (sx1 - sx0 - g) / 2, rh = (H - 8 - 3 * g) / 4;
    if (s_view == V_MAIN || s_view == V_KEY) {
        static const uint8_t MAIN[8] = {B_KEY, B_SWING, B_MODE, B_UNDO, B_SLOTS, B_VARY, B_EXPORT, B_PLAY};
        for (int i = 0; i < 8; i++) {
            int id = s_view == V_MAIN ? MAIN[i] : K_MODE;
            hit_rect(id, s_view == V_MAIN ? 0 : i, sx0 + (i % 2) * (cw + g), 4 + (i / 2) * (rh + g), cw, rh);
        }
        if (s_view == V_MAIN) {
            hit_disc(E_CENTRE, 0, L.rc + 4);
            hit_disc(E_RINGS, 0, R + 4);
        } else {
            hit_disc(K_DONE, 0, 0.42f * R);
            for (int i = 0; i < 12; i++) {
                hit_sect(K_KEY, (uint8_t)((i * 7) % 12), 0.5f * R, R + 6, i * 30 - 15, i * 30 + 15);
            }
        }
    } else if (s_view == V_RING) {
        // the values in rows (a tap on the left half: less, on the right half: more; a drag up and down)
        hit_disc(E_CENTRE, 0, L.zc + 3);
        hit_disc(E_BAND, 0, R + 6);
        float h5 = (H - 8 - 4 * g) / 5;
        static const uint8_t VAL[4] = {P_HITS, P_STEPS, P_SND, P_LVL};
        for (int i = 0; i < 4; i++) {
            hit_rect(VAL[i], 0, sx0, 4 + i * (h5 + g), sx1 - sx0, h5);
        }
        uint8_t id[2] = {B_MUTE, B_SOLO}, arg[2] = {0, 0};
        static const float W2[2] = {1, 1};
        hit_row(id, arg, W2, 2, sx0, sx1, 4 + 4 * (h5 + g), h5, g);
    } else {
        hit_disc(E_CENTRE, 0, 0.40f * R);
        for (int i = 0; i < NSLOT; i++) {
            hit_sect(S_SLOT, i, 0.44f * R, R + 6, i * 45 + 1, i * 45 + 44);
        }
    }
}
static void lay_bar(void)
{
    float R = L.R, x0 = 2 * R + 14, x1 = gfx_w - 6;
    L.fl = &KF_LABEL;
    L.fv = &KF_NAME;
    L.fb = &KF_BIG;
    L.rc = 0.34f * R;
    L.g0 = 0.44f * R;
    L.gd = 0.085f * R;
    L.lx = 8;
    L.ly = 6;
    L.lw = gfx_w - 16;
    L.lh = 104;
    if (s_view == V_MAIN) {
        hit_disc(E_CENTRE, 0, L.rc + 4);
        hit_disc(E_RINGS, 0, R + 4);
        uint8_t id[7], arg[7];
        float wt[7];
        for (int i = 0; i < 7; i++) {
            id[i] = B_RING;
            arg[i] = (uint8_t)i;
            wt[i] = 1;
        }
        hit_row(id, arg, wt, 7, x0, x1, 6, 72, 6);
        static const uint8_t B[7] = {B_KEY, B_SWING, B_MODE, B_UNDO, B_SLOTS, B_VARY, B_EXPORT};
        static const uint8_t Z[7] = {0};
        hit_row(B, Z, wt, 7, x0, x1, 86, 80, 6);
    } else if (s_view == V_RING) {
        hit_rect(E_LANE, 0, L.lx, L.ly, L.lw, L.lh);
        static const uint8_t ID[13] = {B_BACK,  B_MINUS, P_HITS, B_PLUS, B_MINUS, P_STEPS, B_PLUS,
                                       B_MINUS, P_SND,   B_PLUS, P_LVL,  B_PREV,  B_NEXT};
        static const uint8_t AR[13] = {0, 0, 0, 0, 1, 0, 1, 2, 0, 2, 0, 0, 0};
        static const float WT[13] = {1.1f, 0.7f, 1.1f, 0.7f, 0.7f, 1.1f, 0.7f, 0.7f, 1.7f, 0.7f, 1.1f, 0.7f, 0.7f};
        hit_row(ID, AR, WT, 13, 8, gfx_w - 8, 116, 52, 4);
    } else if (s_view == V_KEY) {
        uint8_t id[12], arg[12];
        float wt[12];
        for (int i = 0; i < 12; i++) {
            id[i] = K_KEY;
            arg[i] = (uint8_t)i;
            wt[i] = 1;
        }
        hit_row(id, arg, wt, 12, 8, gfx_w - 8, 6, 52, 4);
        for (int i = 0; i < NMODE; i++) {
            id[i] = K_MODE;
            arg[i] = (uint8_t)i;
        }
        hit_row(id, arg, wt, NMODE, 8, gfx_w - 8, 64, 52, 4);
        hit_rect(K_DONE, 0, gfx_w - 168, 122, 160, 46);
    } else {
        uint8_t id[NSLOT], arg[NSLOT];
        float wt[NSLOT];
        for (int i = 0; i < NSLOT; i++) {
            id[i] = S_SLOT;
            arg[i] = (uint8_t)i;
            wt[i] = 1;
        }
        hit_row(id, arg, wt, NSLOT, 8, gfx_w - 8, 6, 110, 6);
        hit_rect(K_DONE, 0, gfx_w - 168, 122, 160, 46);
    }
}
static void lay_build(void)
{
    s_nhit = 0;
    L.mm = mm_px();
    if (gfx_round()) {
        L.kind = L_ROUND;
        L.R = gfx_w / 2.0f;
        L.cx = L.cy = L.R;
        L.s = L.R / 233.0f;
        lay_round();
    } else if (gfx_w >= 3 * gfx_h) {
        L.kind = L_BAR;
        L.R = gfx_h / 2.0f - 3;
        L.cx = L.R + 4;
        L.cy = gfx_h / 2.0f;
        L.s = L.R / 233.0f;
        lay_bar();
    } else {
        L.kind = L_SIDE;
        float r1 = gfx_h / 2.0f - 3, r2 = (gfx_w * 0.64f - 14) / 2;
        L.R = r1 < r2 ? r1 : r2;
        L.cx = L.R + 4;
        L.cy = gfx_h / 2.0f;
        L.s = L.R / 233.0f;
        lay_side();
    }
}
// the radius of ring i on V_MAIN
static float ring_r(int i)
{
    return L.g0 + i * L.gd;
}
// the ring nearest to the distance d from the centre
static int ring_at(float d)
{
    int i = (int)((d - L.g0) / L.gd + 0.5f);
    return i < 0 ? 0 : i >= NR ? NR - 1 : i;
}
// the radius of a step dot on the big ring (V_RING)
static float zoom_dot(int n)
{
    float c = 6.2831853f * L.zr / n * 0.27f, m = 0.11f * L.zr;
    return c < m ? c : m;
}
// the offset of the pitch of a step: -0.5 .. 0.5
static float pitch_f(int r, int note)
{
    if (!melodic(r)) {
        return 0;
    }
    float lo = note_lo(r), hi = note_hi(r);
    return (note - lo) / (hi - lo) - 0.5f;
}
