// CLOCK ON A ROUND PANEL: a pocket watch face and a digital watch face with mini dials, as the shape and size of the
// panel are similar to a watch. Only BOARD_ROUND (the AMOLED-1.75C at 466 x 466, the Watcher at 412 x 412); the other
// boards do not compile this file.
// The task: the app task (enter, input, render and leave of app_clock.c, which includes this file).
// The memory: s_page and s_dial (PSRAM: round_enter makes them, round_leave frees them); a file of the still part of
// each face in /data.
//
// - THE FACES of the CLOCK page (FACE in the panel, or a swipe to the side on the face): CLASSIC (the CLOCK page of
//   the 320 x 240 layout), POCKET WATCH (a gold case, an enamel dial with Roman numerals, Breguet hands of blued steel,
//   a small seconds dial at 6), CHRONO (a steel bezel with a tachymeter, a digital time in a window, three small dials:
//   the seconds, 24 hours, the day of the week; a date window; an orange sweep second hand).
// - THE TOUCH OF A WATCH FACE: a DOUBLE TAP = the next face (POCKET WATCH, CHRONO, CLASSIC; on CLASSIC a
//   double tap on the time, not on the keys); a SINGLE TAP = the function of the face (POCKET WATCH: a date window at 3
//   for 8 s; CHRONO: a stopwatch: start, stop, back to the time); a LONG PRESS = the CLOCK page (the ALARMS, WORLD,
//   TIMER and STOPWATCH keys) until 12 s with no touch. A swipe to the side also gives the next face.
// - THE STILL PART of each face (the face, the night mode, the language, the size) is kept in a file in /data after its
//   first drawing (about 3 s at 466 x 466): then a face opens at once. The things of the day are in the moving part.
// - THE PAGES (CLOCK, ALARMS, WORLD, the editors, TIMER, STOPWATCH, the keyboard) keep their 320 x 240 layout: they draw
//   into a buffer of 320 x 240 (g_fb, gfx_w and gfx_h point to it while they draw), which goes into the middle of the
//   circle, a little bigger (bilinear), on the colour of the page; a touch goes back through the same scale.
// - The still part of a watch face (the case, the dial, the numerals, the windows of the day) is drawn one time into a
//   cache (PSRAM, the size of the frame); each frame copies it and draws the hands and the time.
#if BOARD_ROUND
#define PAGE_W 320
#define PAGE_H 240
static uint16_t *s_page;            // the pages: 320 x 240 (PSRAM)
static uint16_t *s_dial;            // the still part of the face: gfx_w x gfx_h (PSRAM)
static int s_pw, s_ph, s_px, s_py;  // the page in the round frame: its size and its place
static int64_t s_lt_prev, s_lt_t;   // (the smooth seconds: the local second and the time of its start)
static int s_fdx, s_fdy;            // (a swipe on the face: the point of the touch)
static bool s_fswipe;
// the taps of a face: a single tap waits TAP2_MS for a second one (then a double tap)
#define TAP2_MS 400
static bool s_tap_pend;
static int64_t s_tap_t;
static int64_t s_pk_date_until;    // POCKET WATCH: the date window shows until this time (ms)
static int s_ch_state;             // CHRONO: 0 = the time, 1 = the stopwatch runs, 2 = it stopped
static int64_t s_ch_t0, s_ch_acc;  // (the start of the run, the time of the runs that ended; ms)
// the file of the still part of a face (/data; a new version number when the drawing changes)
#define DIAL_VER 1
static void dial_path(char *p, size_t n, int key)
{
    snprintf(p, n, "/data/ckdial%d_%d_%d.bin", DIAL_VER, key, gfx_w);
}

static void round_enter(void)
{
    s_face = clampi_ck((int)store_get("clock_face", FACE_POCKET), 0, FACE_N - 1);
    s_page = heap_caps_malloc(PAGE_W * PAGE_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dial = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dial_key = -1;
    s_peek_t = 0;
    s_tap_pend = false;
    s_pk_date_until = 0;
    s_pw = gfx_w * 78 / 100;
    s_ph = s_pw * PAGE_H / PAGE_W;
    s_px = (gfx_w - s_pw) / 2;
    s_py = (gfx_h - s_ph) / 2;
}
static void round_leave(void)
{
    heap_caps_free(s_page);
    heap_caps_free(s_dial);
    s_page = s_dial = NULL;
}
// the watch face shows now (else a page)
static bool face_on(void)
{
    return s_dial && s_scr == SC_CLOCK && s_face != FACE_CLASSIC && s_now >= s_peek_t;
}

// ---------------------------------------------------------------- the helpers of the faces
static int s_R, s_cx, s_cy;  // the radius and the middle of the round frame
static inline float fx_(float a, float r)
{
    return s_cx + fsin(a) * r;
}  // a = the angle from 12, clockwise
static inline float fy_(float a, float r)
{
    return s_cy - fcos(a) * r;
}
static uint16_t mixc(uint16_t a, uint16_t b, int k)
{
    return tfd_mix(a, b, k);
}
// a tick from the radius r0 to r1 at the angle a
static void tick(float cx, float cy, float a, float r0, float r1, float w, uint16_t c, int al)
{
    tfd_line(cx + fsin(a) * r0, cy - fcos(a) * r0, cx + fsin(a) * r1, cy - fcos(a) * r1, w, c, al);
}
// a metal ring: a band from r0 (out) to r1 (in) in steps from c0 to c1 (a soft relief)
static void metal(float r0, float r1, uint16_t c0, uint16_t c1)
{
    int n = (int)(r0 - r1);
    if (n < 1) {
        n = 1;
    }
    for (int i = 0; i < n; i++) {
        tfd_ring(s_cx, s_cy, r0 - i, 1.4f, mixc(c0, c1, i * 256 / n), 256);
    }
}
// the light of the glass: a soft crescent at the upper left
static void glass_shine(float r)
{
    for (int i = 0; i < 6; i++) {
        tfd_arc(s_cx, s_cy, r - i * r * 0.02f, r * 0.02f, 4.2f, 5.7f, rgb(255, 255, 255), 10 + i * 3);
    }
}
// a hand with a round end: from the middle (a tail back) to the length, its width
static void hand(float a, float len, float tail, float w, uint16_t c)
{
    tfd_line(fx_(a + PI_CK, tail), fy_(a + PI_CK, tail), fx_(a, len), fy_(a, len), w, c, 256);
}
// a Breguet hand: a thin hand with an open ring (the "moon") near its tip
static void breguet(float a, float len, float w, uint16_t c)
{
    const float rr = len * 0.11f, at = len * 0.72f;
    tfd_line(fx_(a + PI_CK, len * 0.12f), fy_(a + PI_CK, len * 0.12f), fx_(a, at - rr), fy_(a, at - rr), w, c, 256);
    tfd_ring(fx_(a, at), fy_(a, at), rr, w * 0.75f, c, 256);
    float tip[6] = {
            fx_(a, at + rr) + fcos(a) * w * 0.7f, fy_(a, at + rr) + fsin(a) * w * 0.7f, fx_(a, len), fy_(a, len),
            fx_(a, at + rr) - fcos(a) * w * 0.7f, fy_(a, at + rr) - fsin(a) * w * 0.7f};
    tfd_poly(tip, 3, c, 256);
}

// ---------------------------------------------------------------- POCKET WATCH
static const char *const ROMAN[12] = {"XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
static void pocket_still(void)
{
    const float R = (float)s_R;
    const uint16_t gold_l = nc(rgb(250, 222, 150)), gold = nc(rgb(214, 168, 78)), gold_d = nc(rgb(120, 82, 30));
    tfd_rect(0, 0, gfx_w, gfx_h, nc(rgb(34, 16, 20)), 256);
    // the case: a bright outer edge, a knurled band, a deep groove, the bezel
    metal(R, R * 0.93f, gold_l, gold_d);
    for (int i = 0; i < 120; i++) {
        tick(s_cx, s_cy, i * TAU_CK / 120, R * 0.985f, R * 0.94f, 1.2f, (i & 1) ? gold_l : gold_d, 200);
    }
    metal(R * 0.93f, R * 0.885f, gold_d, gold_l);
    tfd_ring(s_cx, s_cy, R * 0.885f, 2.5f, nc(rgb(70, 46, 16)), 256);
    // the enamel dial: ivory, a little darker at the edge
    tfd_disc(s_cx, s_cy, R * 0.865f, nc(rgb(236, 224, 196)), 256);
    for (int i = 0; i < 8; i++) {
        tfd_ring(s_cx, s_cy, R * 0.865f - i * 2, 2.2f, nc(rgb(150, 128, 92)), 34 - i * 4);
    }
    tfd_glow(s_cx - R * 0.15f, s_cy - R * 0.2f, R * 0.6f, nc(rgb(255, 250, 236)), 40);
    // the railway track of the minutes
    const uint16_t ink = nc(rgb(44, 30, 24));
    tfd_ring(s_cx, s_cy, R * 0.80f, 1.6f, ink, 256);
    tfd_ring(s_cx, s_cy, R * 0.735f, 1.2f, ink, 256);
    for (int i = 0; i < 60; i++) {
        tick(s_cx, s_cy, i * TAU_CK / 60, R * 0.80f, R * 0.735f, i % 5 ? 1.0f : 2.4f, ink, 256);
    }
    for (int i = 0; i < 12; i++) {
        tfd_disc(fx_(i * TAU_CK / 12, R * 0.77f), fy_(i * TAU_CK / 12, R * 0.77f), R * 0.012f, nc(rgb(150, 30, 26)),
                 230);
    }
    // the Roman numerals (VI makes room for the small seconds)
    for (int i = 0; i < 12; i++) {
        if (i == 6) {
            continue;
        }
        const float a = i * TAU_CK / 12, r = R * 0.615f;
        tfd_text_c(&KF_TITLE, (int)fx_(a, r), (int)(fy_(a, r) + KF_TITLE.asc * 0.42f), ROMAN[i], ink, 120);
    }
    // the name of the maker and the year (an invented house)
    tfd_text_c(&KF_NAME, s_cx, (int)(s_cy - R * 0.30f), "TFN", nc(rgb(110, 40, 30)), 120);
    tfd_text_c(&KF_LABEL, s_cx, (int)(s_cy - R * 0.20f), s_he ? "מאז 1898" : "EST. 1898", ink, 140);
    // the small seconds at 6
    const float sx = s_cx, sy = s_cy + R * 0.38f, sr = R * 0.17f;
    tfd_disc(sx, sy, sr, nc(rgb(226, 212, 182)), 256);
    tfd_ring(sx, sy, sr, 1.4f, ink, 256);
    for (int i = 0; i < 60; i++) {
        tick(sx, sy, i * TAU_CK / 60, sr, sr * (i % 5 ? 0.88f : 0.76f), i % 5 ? 0.8f : 1.6f, ink, 220);
    }
    static const char *const SN[4] = {"60", "15", "30", "45"};
    for (int i = 0; i < 4; i++) {
        const float a = i * TAU_CK / 4;
        tfd_text_c(&KF_LABEL, (int)(sx + fsin(a) * sr * 0.55f), (int)(sy - fcos(a) * sr * 0.55f + KF_LABEL.asc * 0.4f),
                   SN[i], ink, 40);
    }
    glass_shine(R * 0.84f);
    (void)gold;
}
static void pocket_hands(int h, int mi, float sec)
{
    const float R = (float)s_R;
    const uint16_t blue = nc(rgb(28, 48, 128)), gold = nc(rgb(214, 168, 78));
    const float am = (mi + sec / 60.0f) * TAU_CK / 60, ah = ((h % 12) + mi / 60.0f) * TAU_CK / 12;
    // (a soft shadow under the hands, then the hands)
    const uint16_t sh = nc(rgb(120, 104, 80));
    s_cx += 3;
    s_cy += 4;
    breguet(ah, R * 0.44f, R * 0.026f, sh);
    breguet(am, R * 0.66f, R * 0.02f, sh);
    s_cx -= 3;
    s_cy -= 4;
    breguet(ah, R * 0.44f, R * 0.026f, blue);
    breguet(am, R * 0.66f, R * 0.02f, blue);
    tfd_disc(s_cx, s_cy, R * 0.035f, gold, 256);
    tfd_disc(s_cx, s_cy, R * 0.016f, blue, 256);
    // the small second hand
    const float sx = s_cx, sy = s_cy + R * 0.38f, sr = R * 0.17f, as = sec * TAU_CK / 60;
    tfd_line(sx - fsin(as) * sr * 0.25f, sy + fcos(as) * sr * 0.25f, sx + fsin(as) * sr * 0.9f,
             sy - fcos(as) * sr * 0.9f, 1.4f, blue, 256);
    tfd_disc(sx, sy, R * 0.016f, blue, 256);
}
// POCKET WATCH: a single tap shows a date window at 3 (8 s; the last second fades)
static void pocket_date(int dnum)
{
    if (s_now >= s_pk_date_until) {
        return;
    }
    const int64_t left = s_pk_date_until - s_now;
    const int al = left > 1000 ? 256 : (int)(left * 256 / 1000);
    const float R = (float)s_R;
    const int bw = (int)(R * 0.22f), bh = (int)(R * 0.15f), bx = (int)(s_cx + R * 0.40f), by = s_cy - bh / 2;
    tfd_rrect(bx - 3, by - 3, bw + 6, bh + 6, 5, nc(rgb(120, 82, 30)), al);
    tfd_rrect(bx, by, bw, bh, 4, nc(rgb(250, 244, 228)), al);
    char t[8];
    snprintf(t, sizeof t, "%d", dnum);
    tfd_text_c(&KF_NAME, bx + bw / 2, by + bh / 2 + KF_NAME.asc / 2 - 1, t, nc(rgb(44, 30, 24)), al > 255 ? 255 : al);
}

// ---------------------------------------------------------------- CHRONO
// the places of the small dials (parts of R from the middle)
#define CH_WIN_Y (-0.30f)
#define CH_SUB_X 0.46f
#define CH_SUB_Y 0.10f
#define CH_SUB_R 0.21f
#define CH_DAY_Y 0.55f
#define CH_DAY_R 0.20f
static void subdial(float x, float y, float r, int n, int big)
{
    const uint16_t c = nc(rgb(206, 212, 222));
    tfd_disc(x, y, r, nc(rgb(16, 18, 24)), 256);
    for (int i = 0; i < 6; i++) {
        tfd_ring(x, y, r - i * 1.2f, 1.2f, nc(rgb(40, 44, 56)), 200 - i * 30);  // (a sunk ring)
    }
    tfd_ring(x, y, r, 1.6f, nc(rgb(150, 156, 168)), 256);
    for (int i = 0; i < n; i++) {
        tick(x, y, i * TAU_CK / n, r * 0.92f, r * (i % big ? 0.84f : 0.74f), i % big ? 0.8f : 1.6f, c, 230);
    }
}
static void chrono_still(void)
{
    const float R = (float)s_R;
    tfd_rect(0, 0, gfx_w, gfx_h, nc(rgb(8, 8, 10)), 256);
    // the steel bezel with a tachymeter scale
    metal(R, R * 0.83f, nc(rgb(214, 220, 230)), nc(rgb(70, 76, 88)));
    tfd_ring(s_cx, s_cy, R * 0.83f, 2.0f, nc(rgb(20, 22, 26)), 256);
    static const int TACHY[] = {500, 300, 200, 150, 120, 100, 90, 80, 70};  // (60 is at 12: the word is there)
    for (int i = 0; i < (int)(sizeof TACHY / sizeof TACHY[0]); i++) {
        const float a = 3600.0f / TACHY[i] * TAU_CK / 60;
        char t[8];
        snprintf(t, sizeof t, "%d", TACHY[i]);
        tick(s_cx, s_cy, a, R * 0.84f, R * 0.88f, 1.6f, nc(rgb(24, 26, 32)), 256);
        tfd_text_c(&KF_LABEL, (int)fx_(a, R * 0.935f), (int)(fy_(a, R * 0.935f) + KF_LABEL.asc * 0.4f), t,
                   nc(rgb(24, 26, 32)), 40);
    }
    tfd_text_c(&KF_LABEL, s_cx, (int)(s_cy - R * 0.875f + KF_LABEL.asc * 0.4f), "TACHYMETER", nc(rgb(150, 40, 20)),
               (int)(R * 0.5f));
    // the dial: a deep blue-black with a soft light in the middle; the seconds track
    tfd_disc(s_cx, s_cy, R * 0.82f, nc(rgb(14, 18, 30)), 256);
    tfd_glow(s_cx, s_cy - R * 0.1f, R * 0.7f, nc(rgb(40, 56, 90)), 60);
    for (int i = 0; i < 300; i++) {
        if (i % 5 && i % 25) {
            continue;
        }
        if (i % 5) {
            continue;
        }
        const int s = i / 5;
        tick(s_cx, s_cy, s * TAU_CK / 60, R * 0.80f, R * (s % 5 ? 0.765f : 0.72f), s % 5 ? 1.0f : 3.0f,
             s % 15 ? nc(rgb(220, 226, 236)) : nc(rgb(255, 130, 40)), 256);
    }
    // the name
    tfd_text_c(&KF_NAME, s_cx, (int)(s_cy - R * 0.56f), "TFN", nc(rgb(236, 240, 246)), (int)(R * 0.5f));
    tfd_text_c(&KF_LABEL, s_cx, (int)(s_cy - R * 0.47f), "CHRONOGRAPH", nc(rgb(255, 130, 40)), (int)R);
    // the window of the digital time
    const int ww = (int)(R * 1.02f), wh = (int)(R * 0.30f), wx = s_cx - ww / 2,
              wy = (int)(s_cy + R * CH_WIN_Y - wh / 2 + R * 0.08f);
    tfd_rrect(wx - 3, wy - 3, ww + 6, wh + 6, wh / 4 + 3, nc(rgb(150, 156, 168)), 256);
    tfd_rrect(wx, wy, ww, wh, wh / 4, nc(rgb(6, 14, 14)), 256);
    tfd_rrect(wx + 4, wy + 3, ww - 8, wh / 3, wh / 6, nc(rgb(30, 50, 50)), 60);
    // the small dials: the seconds (left), 24 hours (right), the day of the week (below)
    const float lx = s_cx - R * CH_SUB_X, rx = s_cx + R * CH_SUB_X, yy = s_cy + R * CH_SUB_Y, r = R * CH_SUB_R;
    subdial(lx, yy, r, 60, 5);
    subdial(rx, yy, r, 24, 6);
    static const char *const H24[4] = {"24", "6", "12", "18"};
    static const char *const S4[4] = {"60", "15", "30", "45"};
    for (int i = 0; i < 4; i++) {
        const float a = i * TAU_CK / 4;
        tfd_text_c(&KF_LABEL, (int)(lx + fsin(a) * r * 0.5f), (int)(yy - fcos(a) * r * 0.5f + KF_LABEL.asc * 0.4f),
                   S4[i], nc(rgb(206, 212, 222)), 40);
        tfd_text_c(&KF_LABEL, (int)(rx + fsin(a) * r * 0.5f), (int)(yy - fcos(a) * r * 0.5f + KF_LABEL.asc * 0.4f),
                   H24[i], nc(rgb(206, 212, 222)), 40);
    }
    // (the day dial: 7 names round it, today in orange; its hand is still for the day)
    const float dx = s_cx, dy = s_cy + R * CH_DAY_Y, dr = R * CH_DAY_R;
    subdial(dx, dy, dr, 7, 1);
    // (two letters in English, one in Hebrew: seven names fit round a small dial)
    static const char *const D2[7][2] = {{"SU", "א"}, {"MO", "ב"}, {"TU", "ג"}, {"WE", "ד"},
                                         {"TH", "ה"}, {"FR", "ו"}, {"SA", "ש"}};
    for (int i = 0; i < 7; i++) {
        const float a = (i + 0.5f) * TAU_CK / 7;
        const char *t = D2[i][s_he ? 1 : 0];
        tfd_text_c(&KF_LABEL, (int)(dx + fsin(a) * dr * 0.64f), (int)(dy - fcos(a) * dr * 0.64f + KF_LABEL.asc * 0.4f),
                   t, nc(rgb(160, 168, 182)), (int)(dr * 0.8f));
    }
    glass_shine(R * 0.81f);
}
// the stopwatch time now (ms; 0 when it is off)
static int64_t chrono_ms(void)
{
    return s_ch_state == 0 ? 0 : s_ch_acc + (s_ch_state == 1 ? s_now - s_ch_t0 : 0);
}
static void chrono_dynamic(int h, int mi, float sec, int dnum, int wd)
{
    const float R = (float)s_R;
    // the day dial: today in orange and its hand
    {
        static const char *const D2[7][2] = {{"SU", "א"}, {"MO", "ב"}, {"TU", "ג"}, {"WE", "ד"},
                                             {"TH", "ה"}, {"FR", "ו"}, {"SA", "ש"}};
        const float dx = s_cx, dy = s_cy + R * CH_DAY_Y, dr = R * CH_DAY_R, a = (wd + 0.5f) * TAU_CK / 7;
        tfd_text_c(&KF_LABEL, (int)(dx + fsin(a) * dr * 0.64f), (int)(dy - fcos(a) * dr * 0.64f + KF_LABEL.asc * 0.4f),
                   D2[wd % 7][s_he ? 1 : 0], nc(rgb(255, 140, 50)), (int)(dr * 0.8f));
        tfd_line(dx, dy, dx + fsin(a) * dr * 0.42f, dy - fcos(a) * dr * 0.42f, 2.0f, nc(rgb(255, 130, 40)), 256);
        tfd_disc(dx, dy, R * 0.02f, nc(rgb(200, 206, 216)), 256);
    }
    // the date window between the small dials
    char t[16];
    const int bw = (int)(R * 0.17f), bh = (int)(R * 0.13f), bx = s_cx - bw / 2, by = (int)(s_cy + R * 0.18f);
    tfd_rrect(bx, by, bw, bh, 4, nc(rgb(236, 236, 230)), 256);
    snprintf(t, sizeof t, "%d", dnum);
    tfd_text_c(&KF_NAME, s_cx, by + bh / 2 + KF_NAME.asc / 2 - 1, t, nc(rgb(20, 20, 24)), bw);
    // the digital time in its window
    const char *ap = "";
    char hm[16], ss[8];
    const int64_t cms = chrono_ms();
    if (s_ch_state) {
        // (the stopwatch: minutes:seconds and the tenths)
        snprintf(hm, sizeof hm, "%02d:%02d", (int)(cms / 60000 % 100), (int)(cms / 1000 % 60));
        snprintf(ss, sizeof ss, ".%d", (int)(cms / 100 % 10));
    } else {
        if (s_time_ok) {
            hm_text(hm, sizeof hm, h, mi, &ap);
        } else {
            snprintf(hm, sizeof hm, "--:--");
        }
        snprintf(ss, sizeof ss, "%02d", (int)sec);
    }
    const int wh = (int)(R * 0.30f), wy = (int)(s_cy + R * CH_WIN_Y - wh / 2 + R * 0.08f);
    const mfont_t *f = &CK_L;
    const int w1 = mf_width(f, hm, 0), w2 = mf_width(&CK_M, ss, 0) + 6;
    const int x0 = s_cx - (w1 + w2) / 2, base = wy + wh / 2 + f->asc / 2 - 2;
    mf_text(f, x0, base, hm, s_ch_state ? nc(rgb(255, 190, 120)) : nc(rgb(150, 255, 220)), 255, 0);
    if (s_ch_state) {
        tfd_text_c(&KF_LABEL, s_cx, wy - 4, s_ch_state == 1 ? "STOPWATCH" : "STOPPED", nc(rgb(255, 130, 40)), (int)R);
    }
    mf_text(&CK_M, x0 + w1 + 6, base, ss, nc(rgb(255, 150, 60)), 255, 0);
    if (ap[0]) {
        mf_text(&CK_S, x0 + w1 + 6, base - f->asc + CK_S.asc + 2, ap, nc(rgb(120, 200, 180)), 255, 0);
    }
    // the hands of the small dials
    const float lx = s_cx - R * CH_SUB_X, rx = s_cx + R * CH_SUB_X, yy = s_cy + R * CH_SUB_Y, r = R * CH_SUB_R;
    const float as = sec * TAU_CK / 60, a24 = (h + mi / 60.0f) * TAU_CK / 24;
    const uint16_t w = nc(rgb(240, 244, 250));
    tfd_line(lx, yy, lx + fsin(as) * r * 0.82f, yy - fcos(as) * r * 0.82f, 1.6f, w, 256);
    tfd_disc(lx, yy, R * 0.018f, w, 256);
    tfd_line(rx, yy, rx + fsin(a24) * r * 0.78f, yy - fcos(a24) * r * 0.78f, 2.2f, w, 256);
    tfd_disc(rx, yy, R * 0.018f, w, 256);
    // the central sweep second hand (orange, a counterweight); with the stopwatch on: the seconds of the stopwatch
    const uint16_t o = nc(rgb(255, 120, 30));
    const float ac = s_ch_state ? (float)(cms % 60000) / 1000.0f * TAU_CK / 60 : as;
    hand(ac, R * 0.79f, R * 0.18f, 2.0f, o);
    tfd_disc(fx_(ac + PI_CK, R * 0.15f), fy_(ac + PI_CK, R * 0.15f), R * 0.03f, o, 256);
    tfd_disc(s_cx, s_cy, R * 0.03f, o, 256);
    tfd_disc(s_cx, s_cy, R * 0.012f, nc(rgb(30, 30, 34)), 256);
}

// ---------------------------------------------------------------- the frame of a face
static void face_draw(void)
{
    s_R = gfx_w / 2;
    s_cx = gfx_w / 2;
    s_cy = gfx_h / 2;
    int off;
    const int64_t lt = city_local(s_home, &off, NULL);
    if (lt != s_lt_prev) {
        s_lt_prev = lt;
        s_lt_t = s_now;
    }
    tz_tm_t m;
    tz_split(lt, &m);
    float sec = m.s + clampf((float)(s_now - s_lt_t) / 1000.0f, 0, 0.999f);
    int h = m.h, mi = m.mi;
    if (!s_time_ok) {
        h = 10;
        mi = 10;
        sec = 0;
    }  // (no time yet: the hands rest at 10:10, as in a shop)
    // the still part: from its file in /data, or drawn (about 3 s) and kept in the file, when the face, the night mode
    // or the language changes
    const int key = s_face | (s_night ? 8 : 0) | (s_he ? 16 : 0);
    if (key != s_dial_key) {
        char path[48];
        dial_path(path, sizeof path, key);
        const size_t bytes = (size_t)gfx_w * gfx_h * 2;
        FILE *f = fopen(path, "rb");
        const bool got = f && fread(s_dial, 1, bytes, f) == bytes;
        if (f) {
            fclose(f);
        }
        if (!got) {
            uint16_t *keep = g_fb;
            g_fb = s_dial;
            tfd_screen();
            tfd_noclip();
            if (s_face == FACE_POCKET) {
                pocket_still();
            } else {
                chrono_still();
            }
            g_fb = keep;
            f = fopen(path, "wb");
            size_t wn = 0;
            if (f) {
                wn = fwrite(s_dial, 1, bytes, f);
                fclose(f);
                if (wn != bytes) {
                    remove(path);  // (no room: no file; the next open draws it again)
                }
            }
        }
        s_dial_key = key;
    }
    tfd_screen();
    tfd_noclip();
    memcpy(g_fb, s_dial, (size_t)gfx_w * gfx_h * 2);
    // a single tap that waited for a second one: the function of the face
    if (s_tap_pend && s_now - s_tap_t >= TAP2_MS) {
        s_tap_pend = false;
        if (s_now - s_tap_t > 1500) {
            // (a tap of more than 1.5 s ago: from the CLASSIC page, or of another face: no action)
        } else if (s_face == FACE_POCKET) {
            s_pk_date_until = s_now < s_pk_date_until ? 0 : s_now + 8000;
        } else if (s_face == FACE_CHRONO) {
            if (s_ch_state == 0) {
                s_ch_state = 1;
                s_ch_acc = 0;
                s_ch_t0 = s_now;
            } else if (s_ch_state == 1) {
                s_ch_acc += s_now - s_ch_t0;
                s_ch_state = 2;
            } else {
                s_ch_state = 0;
            }
        }
        sfx_ui(SFX_CLICK);
    }
    if (s_face == FACE_POCKET) {
        pocket_date(m.d);
        pocket_hands(h, mi, sec);
    } else {
        chrono_dynamic(h, mi, sec, m.d, m.wd);
    }
}
// a double tap: the next face (POCKET WATCH, CHRONO, CLASSIC)
static void face_next(void)
{
    s_face = s_face == FACE_POCKET ? FACE_CHRONO : s_face == FACE_CHRONO ? FACE_CLASSIC : FACE_POCKET;
    store_set("clock_face", (uint32_t)s_face);
    s_peek_t = 0;
    sfx_ui(SFX_CLICK);
}

// ---------------------------------------------------------------- the pages: 320 x 240 into the circle
static void render_page(int64_t now);
static void input_page(const input_event_t *ev);
// the page buffer into the frame: the colour of the page round it, the page a little bigger (bilinear)
static void page_out(void)
{
    const uint16_t bg = C_BG;
    for (int y = 0; y < gfx_h; y++) {
        uint16_t *row = g_fb + y * gfx_w;
        if (y < s_py || y >= s_py + s_ph) {
            for (int x = 0; x < gfx_w; x++) {
                row[x] = bg;
            }
            continue;
        }
        for (int x = 0; x < s_px; x++) {
            row[x] = bg;
        }
        for (int x = s_px + s_pw; x < gfx_w; x++) {
            row[x] = bg;
        }
        // (the source row: 8.8 fixed point)
        const int sy = ((y - s_py) * 2 + 1) * (PAGE_H * 128) / s_ph - 128;
        const int y0 = sy < 0 ? 0 : sy >> 8, fy = sy < 0 ? 0 : sy & 255, y1 = y0 + 1 < PAGE_H ? y0 + 1 : y0;
        const uint16_t *a = s_page + y0 * PAGE_W, *b = s_page + y1 * PAGE_W;
        const int step = PAGE_W * 256 / s_pw;
        int sx = step / 2 - 128;
        for (int x = 0; x < s_pw; x++, sx += step) {
            const int x0 = sx < 0 ? 0 : sx >> 8, fx = sx < 0 ? 0 : sx & 255, x1 = x0 + 1 < PAGE_W ? x0 + 1 : x0;
            const uint16_t t = tfd_mix(a[x0], a[x1], fx), u = tfd_mix(b[x0], b[x1], fx);
            row[s_px + x] = tfd_mix(t, u, fy);
        }
    }
}
// THE RENDER OF THE ROUND PANEL
static void render(int64_t now)
{
    if (!s_page || !s_dial) {  // (no memory: the 320 x 240 layout in the corner, with no scale)
        render_page(now);
        return;
    }
    if (face_on()) {
        face_draw();
        return;
    }
    // the page: g_fb, gfx_w and gfx_h point to the buffer while the page draws (all drawing of the page is through them)
    uint16_t *keep = g_fb;
    const int kw = gfx_w, kh = gfx_h;
    g_fb = s_page;
    gfx_w = PAGE_W;
    gfx_h = PAGE_H;
    gfx_clip_reset();
    render_page(now);
    g_fb = keep;
    gfx_w = kw;
    gfx_h = kh;
    gfx_clip_reset();
    tfd_screen();
    page_out();
}
// THE TOUCH OF THE ROUND PANEL: a face takes a swipe (the next face) and a tap (the page); a page gets the point in
// its own 320 x 240 frame
static void input(const input_event_t *ev)
{
    if (!s_page || !s_dial) {
        input_page(ev);
        return;
    }
    if (face_on()) {
        if (ev->kind == IN_DOWN) {
            s_fdx = ev->x;
            s_fdy = ev->y;
            s_fswipe = false;
        } else if (ev->kind == IN_MOVE && !s_fswipe && abs(ev->x - s_fdx) > gfx_w / 6 &&
                   abs(ev->x - s_fdx) > 2 * abs(ev->y - s_fdy)) {
            s_fswipe = true;
            input_tap_cancel();
            s_face = (s_face + (ev->x < s_fdx ? 1 : FACE_N - 1)) % FACE_N;
            if (s_face == FACE_CLASSIC) {
                s_face = ev->x < s_fdx ? FACE_POCKET : FACE_CHRONO;  // (a swipe: the watch faces)
            }
            store_set("clock_face", (uint32_t)s_face);
            sfx_ui(SFX_CLICK);
        } else if (ev->kind == IN_TAP && !s_fswipe) {
            const int64_t t = ms_now();
            if (s_tap_pend && t - s_tap_t < TAP2_MS) {
                s_tap_pend = false;
                face_next();
            } else {
                s_tap_pend = true;
                s_tap_t = t;
            }
        } else if (ev->kind == IN_LONG && !s_fswipe) {
            s_tap_pend = false;
            s_peek_t = s_now + 12000;  // (a long press: the CLOCK page)
            sfx_ui(SFX_CLICK);
        }
        return;
    }
    // CLASSIC: a double tap on the time (the upper part of the page, not the keys) = the next face
    if (s_scr == SC_CLOCK && s_face == FACE_CLASSIC && ev->kind == IN_TAP && ev->y < s_py + s_ph * 55 / 100) {
        const int64_t t = ms_now();
        if (s_tap_pend && t - s_tap_t < TAP2_MS) {
            s_tap_pend = false;
            face_next();
            return;
        }
        s_tap_pend = true;
        s_tap_t = t;
    }
    if (s_scr == SC_CLOCK && s_face != FACE_CLASSIC) {
        s_peek_t = s_now + 12000;  // (a touch keeps the page)
    }
    input_event_t e = *ev;
    e.x = (int16_t)((ev->x - s_px) * PAGE_W / (s_pw > 0 ? s_pw : 1));
    e.y = (int16_t)((ev->y - s_py) * PAGE_H / (s_ph > 0 ? s_ph : 1));
    const int kw = gfx_w, kh = gfx_h;
    gfx_w = PAGE_W;
    gfx_h = PAGE_H;
    input_page(&e);
    gfx_w = kw;
    gfx_h = kh;
}
#endif
