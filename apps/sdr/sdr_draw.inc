// SDR: THE VIEWS (they only read the model, the plan, the traces and the history; app_sdr.c holds the lock).
// The task: the app task (render of app_sdr.c, which includes this file), under the lock s_lock.
// The memory: the file statics of this part (the colour maps, the scale); no heap.
//
//   SPLIT       the spectrum over the waterfall
//   SPECTRUM    the traces: LIVE (with a fill in the colours of the level), MAX, MIN, AVERAGE; the peaks, the marker
//   WATERFALL   the history: each row at its own frequencies (a pan moves the old rows), 5 s marks at the left
//   PERSISTENCE how often each level came at each frequency (a burst and a steady signal look different)
//   CHANNELS    the channel plans in the span (WiFi, Bluetooth LE, Zigbee): the power and the use of each channel
//   I/Q SCOPE   the samples of the last block in time, and the constellation (I against Q)

// ---------------------------------------------------------------- the colours
#define C_BG     rgb(5, 8, 13)
#define C_PLOT   rgb(8, 12, 19)
#define C_GRID   rgb(26, 36, 50)
#define C_GRID2  rgb(17, 24, 34)
#define C_AXIS   rgb(140, 158, 180)
#define C_DIM    rgb(96, 112, 132)
#define C_TEXT   rgb(220, 230, 240)
#define C_BAR    rgb(12, 18, 28)
#define C_LIVE   rgb(255, 214, 74)
#define C_MAX    rgb(255, 92, 92)
#define C_MIN    rgb(80, 140, 255)
#define C_AVG    rgb(90, 240, 160)
#define C_MARK   rgb(255, 255, 255)
#define C_WARN   rgb(255, 80, 64)
#define C_BTN    rgb(20, 28, 42)
#define C_BTN_ON rgb(40, 70, 110)

// the face of the axis and the level marks (the round frame: the bigger mono face)
#define F_AX (gfx_round() ? &SF_SM : &SF_AX)

static uint16_t s_cmap[256];  // the colour map (s_m.cmap)
static uint16_t s_qlut[256];  // a level of the waterfall (0.5 dB steps) -> its colour, for the scale now

static void cmap_make(void)
{
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f, r, g, b;
        switch (s_m.cmap) {
        case 1: {  // CLASSIC: black, blue, cyan, yellow, red (the colours of the old SDR programs)
            static const float K[6][3] = {{0, 0, 0}, {0, 0, 0.6f}, {0, 0.7f, 1}, {1, 1, 0}, {1, 0.15f, 0}, {1, 1, 1}};
            float u = t * 5;
            int k = (int)u;
            if (k > 4) {
                k = 4;
            }
            float f = u - k;
            r = K[k][0] + (K[k + 1][0] - K[k][0]) * f;
            g = K[k][1] + (K[k + 1][1] - K[k][1]) * f;
            b = K[k][2] + (K[k + 1][2] - K[k][2]) * f;
            break;
        }
        case 2:
            r = g = b = powf(t, 0.9f);
            break;
        case 3: {  // EMBER: black, deep red, orange, yellow, white
            r = fminf(1, t * 2.2f);
            g = fminf(1, fmaxf(0, t * 2.0f - 0.55f));
            b = fminf(1, fmaxf(0, t * 3.0f - 2.0f));
            break;
        }
        default:  // TURBO (the polynomial of Google's Turbo map)
            r = 0.13572138f +
                t * (4.61539260f + t * (-42.66032258f + t * (132.13108234f + t * (-152.94239396f + t * 59.28637943f))));
            g = 0.09140261f +
                t * (2.19418839f + t * (4.84296658f + t * (-14.18503333f + t * (4.27729857f + t * 2.82956604f))));
            b = 0.10667330f +
                t * (12.64194608f + t * (-60.58204836f + t * (110.36276771f + t * (-89.90310912f + t * 27.34824973f))));
            if (i < 8) {
                r *= i / 8.0f;
                g *= i / 8.0f;
                b *= i / 8.0f;
            }  // (the bottom of the scale: near black)
            break;
        }
        s_cmap[i] = tfd_rgb((int)(r * 255), (int)(g * 255), (int)(b * 255));
    }
}

static float s_top, s_rng;          // the scale of this frame: the top (dB) and the range (dB)
static inline int lvl_of(float db)  // a level -> 0..255 of the colour map
{
    float t = (db - (s_top - s_rng)) / s_rng;
    return t <= 0 ? 0 : t >= 1 ? 255 : (int)(t * 255);
}
// The waterfall has its own scale: from a little under the noise floor (dark) to the top of the plot, so the noise is
// near black and each signal is bright (the scale of the spectrum when the floor is not known yet)
static void qlut_make(void)
{
    float lo = s_floor > SDR_NODATA ? s_floor - 5 : s_top - s_rng, hi = s_top;
    if (hi < lo + 20) {
        hi = lo + 20;
    }
    for (int q = 0; q < 256; q++) {
        float t = (sdr_wf_db((uint8_t)q) - lo) / (hi - lo);
        s_qlut[q] = q ? s_cmap[t <= 0 ? 0 : t >= 1 ? 255 : (int)(t * 255)] : C_PLOT;
    }
}

// ---------------------------------------------------------------- small drawing helpers
// the pixel (x, y) of the screen frame (the round frame: the point in the middle box, rl_origin.h)
#define FBP(x, y) (&g_fb[((y) + rl_oy) * gfx_w + (x) + rl_ox])
static inline void px_set(int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)s_W && (unsigned)y < (unsigned)s_H) {
        *FBP(x, y) = c;
    }
}
static void vspan(int x, int y0, int y1, uint16_t c, int a)
{
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    if ((unsigned)x >= (unsigned)s_W) {
        return;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= s_H) {
        y1 = s_H - 1;
    }
    uint16_t *p = FBP(x, y0);
    for (int y = y0; y <= y1; y++, p += gfx_w) {
        *p = a >= 256 ? c : tfd_mix(*p, c, a);
    }
}
static void fmt_mhz(char *b, int n, double hz, int dec)
{
    snprintf(b, n, "%.*f", dec, hz / 1e6);
}
// a width in Hz as a short text: "84 MHz", "500 kHz", "2.9 kHz"
static void fmt_bw(char *b, int n, double hz)
{
    if (hz >= 1e6) {
        snprintf(b, n, hz >= 1e7 ? "%.0f MHz" : "%.1f MHz", hz / 1e6);
    } else if (hz >= 1e3) {
        snprintf(b, n, hz >= 1e4 ? "%.0f kHz" : "%.1f kHz", hz / 1e3);
    } else {
        snprintf(b, n, "%.0f Hz", hz);
    }
}
// a dB number with the real minus sign (0x96 of the faces)
static void fmt_db(char *b, int n, float db, int dec)
{
    char t[24];
    snprintf(t, sizeof t, "%.*f", dec, fabsf(db));
    snprintf(b, n, db < -0.05f * (dec ? 1 : 10) ? "\x96%s" : "%s", t);
}

// The step of the frequency marks: 1, 2 or 5 x 10^k Hz, about each `px` pixels
static double tick_step(double span, int w, int px)
{
    double raw = span * px / w, e = pow(10, floor(log10(raw))), m = raw / e;
    return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * e;
}

static int yof(float db, int y0, int y1)
{
    float t = (s_top - db) / s_rng;
    int y = y0 + (int)(t * (y1 - y0));
    return y < y0 ? y0 : y > y1 ? y1 : y;
}

// the grid of the frequency marks (vertical lines) and of the levels (each 10 or 20 dB)
static void grid(int y0, int y1, bool levels)
{
    const double f0 = (double)s_m.center - (double)s_m.span / 2, f1 = f0 + (double)s_m.span;
    const double st = tick_step((double)s_m.span, s_bins, BAR ? 96 : 64);
    for (double f = ceil(f0 / st) * st; f < f1; f += st) {
        int x = (int)hz_to_x(f);
        if (x > s_PX0 && x < s_PX1) {
            vspan(x, y0, y1, C_GRID2, 256);
        }
    }
    if (!levels) {
        return;
    }
    const float ds = s_rng > 60 ? 20.0f : 10.0f;
    int last = -100;
    for (float d = floorf(s_top / ds) * ds; d >= s_top - s_rng; d -= ds) {
        int y = yof(d, y0, y1);
        if (y <= y0 || y >= y1) {
            continue;
        }
        for (int x = s_PX0; x < s_PX1; x++) {
            *FBP(x, y) = (x & 3) ? C_GRID : C_GRID2;
        }
        if (y - last < 14 || y - 10 < y0) {
            continue;  // (a short plot: a label on every second line)
        }
        last = y;
        char b[16];
        fmt_db(b, sizeof b, d, 0);
        mf_text(F_AX, s_PX0 + 3, y - 2, b, C_DIM, 255, 0);
    }
}

// ---------------------------------------------------------------- the spectrum
static void trace_line(const float *v, int y0, int y1, uint16_t c, int a)
{
    int py = -1;
    for (int i = 0; i < s_plan.req.bins && i < s_bins; i++) {
        if (v[i] <= SDR_NODATA) {
            py = -1;
            continue;
        }
        int y = yof(v[i], y0, y1);
        vspan(s_PX0 + i, py < 0 ? y : (py < y ? py + 1 : py - 1 > y ? py - 1 : y), y, c, a);
        py = y;
    }
}

static void draw_peaks(int y0, int y1)
{
    if (s_m.pk) {
        return;  // (PEAK LABELS: 0 = ON)
    }
    int last = -1000;
    int done[PK_MAX], nd = 0;
    for (int k = 0; k < s_npk && k < 4; k++) {
        int x = s_PX0 + (int)lroundf(s_pk[k].x);
        bool near = false;
        for (int j = 0; j < nd; j++) {
            if (abs(done[j] - x) < 52) {
                near = true;
            }
        }
        if (near) {
            continue;
        }
        done[nd++] = x;
        last = x;
        int y = yof(s_pk[k].db, y0, y1);
        char b[24];
        fmt_mhz(b, sizeof b, sdr_plan_freq(&s_plan, s_pk[k].x), s_m.span < 2 * MHZ ? 3 : s_m.span < 20 * MHZ ? 2 : 1);
        int w = mf_width(F_AX, b, 0);
        int tx = x - w / 2;
        if (tx < s_PX0 + 2) {
            tx = s_PX0 + 2;
        }
        if (tx + w > s_PX1 - 2) {
            tx = s_PX1 - 2 - w;
        }
        int ty = y - 5 < y0 + 10 ? y0 + 10 : y - 5;
        tfd_rect(tx - 2, ty - 9, w + 4, 11, C_BG, 150);
        mf_text(F_AX, tx, ty, b, C_TEXT, 255, 0);
        px_set(x, y - 2, C_TEXT);
        px_set(x - 1, y - 3, C_TEXT);
        px_set(x + 1, y - 3, C_TEXT);
    }
    (void)last;
}

static float trace_at(const float *v, double hz)
{
    int i = (int)lround(sdr_plan_point(&s_plan, hz));
    if (i < 0 || i >= s_plan.req.bins) {
        return SDR_NODATA;
    }
    return v[i];
}

static void draw_marker(int y0, int y1, bool level)
{
    if (s_m.mk == MK_OFF || s_mk_hz <= 0) {
        return;
    }
    int x = (int)hz_to_x(s_mk_hz);
    if (x < s_PX0 || x >= s_PX1) {
        return;
    }
    for (int y = y0; y < y1; y += 4) {
        vspan(x, y, y + 1, C_MARK, 120);
    }
    if (!level) {
        return;
    }
    float d = trace_at(s_tr.v[SDR_TR_LIVE], s_mk_hz);
    if (d <= SDR_NODATA) {
        return;
    }
    int y = yof(d, y0, y1);
    for (int k = -3; k <= 3; k++) {
        int h = 3 - abs(k);
        vspan(x + k, y - h, y + h, C_MARK, 256);
    }
}

static void draw_spectrum(int y0, int y1)
{
    tfd_rect(s_PX0, y0, s_bins, y1 - y0, C_PLOT, 256);
    grid(y0, y1, true);
    const int n = s_plan.req.bins < s_bins ? s_plan.req.bins : s_bins;
    // the fill under LIVE: the colour of the level, soft
    const float *lv = s_tr.v[SDR_TR_LIVE];
    if (trc_on(SDR_TR_LIVE)) {
        for (int i = 0; i < n; i++) {
            if (lv[i] <= SDR_NODATA) {
                continue;
            }
            int y = yof(lv[i], y0, y1);
            if (y < y1 - 1) {
                vspan(s_PX0 + i, y + 1, y1 - 1, s_cmap[lvl_of(lv[i])], 70);
            }
        }
    }
    if (trc_on(SDR_TR_MIN)) {
        trace_line(s_tr.v[SDR_TR_MIN], y0, y1 - 1, C_MIN, 200);
    }
    if (trc_on(SDR_TR_AVG)) {
        trace_line(s_tr.v[SDR_TR_AVG], y0, y1 - 1, C_AVG, 256);
    }
    if (trc_on(SDR_TR_MAX)) {
        trace_line(s_tr.v[SDR_TR_MAX], y0, y1 - 1, C_MAX, 230);
    }
    if (trc_on(SDR_TR_LIVE)) {
        trace_line(lv, y0, y1 - 1, C_LIVE, 256);
    }
    draw_peaks(y0, y1);
    draw_marker(y0, y1, true);
}

// ---------------------------------------------------------------- the waterfall
static void draw_waterfall(int y0, int y1, bool marks)
{
    qlut_make();
    const int n = s_bins;
    for (int r = 0; r < y1 - y0; r++) {
        uint16_t *p = FBP(s_PX0, y0 + r);
        const sdr_wf_row_t *hd;
        const uint8_t *q = sdr_wf_row(&s_wf, r, &hd);
        if (!q) {
            for (int x = 0; x < n; x++) {
                p[x] = C_PLOT;
            }
            continue;
        }
        if (hd->f0_hz == s_plan.f0_hz && fabsf(hd->df_hz - (float)s_plan.df_hz) < 1e-3f) {
            for (int x = 0; x < n; x++) {
                p[x] = s_qlut[q[x]];
            }
            continue;
        }
        // a row with an other axis: each pixel takes the point of the row at its frequency. (float in the loop: the FPU
        // has no double; a double for each pixel makes a render of about 115 ms after a zoom)
        const float k = (float)(s_plan.df_hz / hd->df_hz);
        const float a = (float)(((double)s_plan.f0_hz - (double)hd->f0_hz) / hd->df_hz) + 0.5f * k;
        for (int x = 0; x < n; x++) {
            int j = (int)floorf(a + (float)x * k);
            p[x] = j >= 0 && j < s_wf.w ? s_qlut[q[j]] : C_PLOT;
        }
    }
    if (!marks) {
        return;
    }
    // the time marks: each 5 s of the rows
    int64_t t_now = s_now;
    int next = 5;
    for (int r = 0; r < y1 - y0; r++) {
        const sdr_wf_row_t *hd;
        if (!sdr_wf_row(&s_wf, r, &hd)) {
            break;
        }
        int age = (int)((t_now - (int64_t)hd->t_ms) / 1000);
        if (age >= next) {
            for (int x = s_PX0; x < s_PX0 + 6; x++) {
                px_set(x, y0 + r, C_TEXT);
            }
            char b[12];
            snprintf(b, sizeof b, "%ds", next);
            if (r > 8 && r < y1 - y0 - 2) {
                tfd_rect(s_PX0 + 7, y0 + r - 8, mf_width(F_AX, b, 0) + 3, 10, C_BG, 140);
                mf_text(F_AX, s_PX0 + 8, y0 + r + 1, b, C_TEXT, 230, 0);
            }
            next += 5;
        }
    }
}

// ---------------------------------------------------------------- the persistence
static void draw_persist(int y0, int y1)
{
    tfd_rect(s_PX0, y0, s_bins, y1 - y0, C_PLOT, 256);
    grid(y0, y1, true);
    const int h = y1 - y0 < s_ps.levels ? y1 - y0 : s_ps.levels, w = s_bins < s_ps.w ? s_bins : s_ps.w;
    for (int l = 0; l < h; l++) {
        const uint16_t *d = &s_ps.d[(size_t)l * s_ps.w];
        uint16_t *p = FBP(s_PX0, y0 + l);
        for (int x = 0; x < w; x++) {
            if (d[x] < 64) {
                continue;
            }
            int i = (int)(sqrtf((float)d[x] / 65535.0f) * 255.0f);
            p[x] = s_cmap[i > 255 ? 255 : i < 24 ? 24 : i];
        }
    }
    if (trc_on(SDR_TR_MAX)) {
        trace_line(s_tr.v[SDR_TR_MAX], y0, y1 - 1, C_MAX, 160);
    }
    draw_marker(y0, y1, false);
}

// ---------------------------------------------------------------- the channels
static void draw_channels(int y0, int y1)
{
    tfd_rect(s_PX0, y0, s_bins, y1 - y0, C_PLOT, 256);
    grid(y0, y1, false);
    const sdr_chplan_t *pl[8];
    int np = sdr_chplans(pl, 8);
    // the plans with a channel in the span, and the first channel index of each plan
    int show[8], first[8], ns = 0, g = 0;
    const double f0 = (double)s_plan.f0_hz, f1 = f0 + s_plan.df_hz * s_plan.req.bins;
    for (int k = 0; k < np; k++) {
        int cnt = 0;
        for (int c = 0; c < pl[k]->n; c++) {
            if ((double)pl[k]->ch[c].f_hz > f0 && (double)pl[k]->ch[c].f_hz < f1) {
                cnt++;
            }
        }
        if (cnt && ns < 3) {
            show[ns] = k;
            first[ns] = g;
            ns++;
        }
        g += pl[k]->n;
    }
    if (!ns) {
        tfd_text_c(&KF_LABEL, (s_PX0 + s_PX1) / 2, (y0 + y1) / 2 + 5, TX(T_NOCHAN), C_DIM, s_bins - 20);
        return;
    }
    const int rh = (y1 - y0) / ns;
    for (int s = 0; s < ns; s++) {
        const sdr_chplan_t *P = pl[show[s]];
        const int ry0 = y0 + s * rh, ry1 = ry0 + rh - 2, base = ry1 - 12;
        tfd_rect(s_PX0, ry1, s_bins, 1, C_GRID, 256);
        int name_end = -1000;  // (the right end of the last channel name: no overlap)
        for (int c = 0; c < P->n; c++) {
            const sdr_chan_t *ch = &P->ch[c];
            int gi = first[s] + c;
            if (gi >= CH_MAX) {
                break;
            }
            int cx = (int)hz_to_x((double)ch->f_hz);
            if (cx < s_PX0 || cx >= s_PX1) {
                continue;
            }
            // the width of a bar: the space to the next channel (the WiFi channels overlap), at most the channel
            double sp = ch->bw_hz;
            if (c + 1 < P->n && (double)(P->ch[c + 1].f_hz - ch->f_hz) < sp) {
                sp = (double)(P->ch[c + 1].f_hz - ch->f_hz);
            }
            int bw = (int)(sp / s_plan.df_hz) - 2;
            if (bw < 3) {
                bw = 3;
            }
            // the height: the use of the channel (the share of the frames with a signal in it); the colour: its mean
            // power (the colour map of the levels)
            float busy = s_busy[gi], pw = s_chpow[gi];
            int bh = (int)(fmaxf(0.04f, fminf(1.0f, busy)) * (base - ry0 - 11));  // (11 px: the % over the bar)
            uint16_t col = pw > SDR_NODATA ? s_cmap[lvl_of(pw)] : C_GRID;
            tfd_rrect(cx - bw / 2, base - bh, bw, bh, bw > 8 ? 2 : 0, col, 230);
            int nw = mf_width(F_AX, ch->name, 0);
            if (cx - nw / 2 > name_end + 3) {
                mf_text_c(F_AX, cx, ry1 - 2, ch->name, C_TEXT, 230, 0);
                name_end = cx + nw / 2;
            }
            if (bw >= 22 && busy > 0.005f) {
                char b[12];
                snprintf(b, sizeof b, "%d%%", (int)lroundf(busy * 100));
                mf_text_c(F_AX, cx, base - bh - 2, b, C_TEXT, 220, 0);
            }
        }
        // the name of the plan: over the bars, on a dark box at the end of the start side
        const char *pn = P->name[s_he ? 1 : 0];
        int pw = ui_text_w(&KF_LABEL, pn) + 8;
        int px = s_he ? s_PX1 - pw - 2 : s_PX0 + 2;
        tfd_rect(px, ry0 + 1, pw, 16, C_BG, 200);
        tfd_text_s(&KF_LABEL, px + 4, px + pw - 4, ry0 + 14, pn, C_AXIS, s_he);
    }
}

// ---------------------------------------------------------------- the I/Q scope
static void draw_scope(int y0, int y1)
{
    tfd_rect(s_PX0, y0, s_bins, y1 - y0, C_PLOT, 256);
    if (cur_kind() != RFSRC_IQ || s_scope_n <= 0) {
        tfd_text_c(&KF_LABEL, (s_PX0 + s_PX1) / 2, (y0 + y1) / 2 + 5, TX(T_NOSCOPE), C_DIM, s_bins - 20);
        return;
    }
    int cw = (y1 - y0) - 4;  // the constellation: a square at the right
    if (cw > s_bins * 45 / 100) {
        cw = s_bins * 45 / 100;
    }
    const int tw = s_bins - cw - 8;
    int mx = 8;
    for (int k = 0; k < 2 * s_scope_n; k++) {
        if (abs(s_scope[k]) > mx) {
            mx = abs(s_scope[k]);
        }
    }
    const int ym = (y0 + y1) / 2, hh = (y1 - y0) / 2 - 3;
    tfd_rect(s_PX0, ym, tw, 1, C_GRID, 256);
    int pi = -1, pq = -1;
    for (int x = 0; x < tw && x < s_scope_n; x++) {
        int yi = ym - s_scope[2 * x] * hh / mx, yq = ym - s_scope[2 * x + 1] * hh / mx;
        if (pi >= 0) {
            vspan(s_PX0 + x, pq, yq, rgb(80, 220, 255), 200);
            vspan(s_PX0 + x, pi, yi, C_LIVE, 230);
        }
        pi = yi;
        pq = yq;
    }
    mf_text(F_AX, s_PX0 + 4, y0 + 11, "I", C_LIVE, 255, 0);
    mf_text(F_AX, s_PX0 + 14, y0 + 11, "Q", rgb(80, 220, 255), 255, 0);
    const int cx0 = s_PX1 - cw - 4, cyc = (y0 + y1) / 2, ccx = cx0 + cw / 2, cy0 = cyc - cw / 2;
    tfd_rect(cx0, cy0, cw, cw, C_BG, 256);
    tfd_rect(cx0, cyc, cw, 1, C_GRID, 256);
    tfd_rect(ccx, cy0, 1, cw, C_GRID, 256);
    for (int k = 0; k < s_scope_n; k++) {
        int x = ccx + s_scope[2 * k] * (cw / 2 - 2) / mx, y = cyc - s_scope[2 * k + 1] * (cw / 2 - 2) / mx;
        if (x > cx0 && x < cx0 + cw && y > cy0 && y < cy0 + cw) {
            uint16_t *p = FBP(x, y);
            *p = tfd_add(*p, rgb(90, 200, 255), 90);
        }
    }
}

// ---------------------------------------------------------------- the bars: the top, the axis, the buttons
static void draw_axis(void)
{
    const int y0 = s_PY1;
    tfd_rect(0, y0, s_PX1, s_H - y0, C_BAR, 256);
    const double f0 = (double)s_m.center - (double)s_m.span / 2, f1 = f0 + (double)s_m.span;
    const double st = tick_step((double)s_m.span, s_bins, BAR ? 96 : 64);
    int dec = st >= 1e6 ? 0 : st >= 1e5 ? 1 : st >= 1e4 ? 2 : 3;
    for (double f = ceil(f0 / st) * st; f < f1; f += st) {
        int x = (int)hz_to_x(f);
        if (x < s_PX0 + 12 || x > s_PX1 - 12) {
            continue;
        }
        vspan(x, y0, y0 + 2, C_AXIS, 256);
        char b[20];
        fmt_mhz(b, sizeof b, f, dec);
        int w = mf_width(F_AX, b, 0);
        if (x - w / 2 < s_PX0 + 1 || x + w / 2 > s_PX1 - 1) {
            continue;  // (no label cut at an edge)
        }
        mf_text_c(F_AX, x, s_H - 2, b, C_AXIS, 255, 0);
    }
}

// the round frame: the readout in the top cap (the centre frequency, the span and the RBW, the marker), the state
// (OVERLOAD, FROZEN, the gain, the demo, the frames a second) under the keys of the bottom cap (the screen points)
static void draw_top_r(void)
{
    const int W = gfx_w, ox = rl_ox, oy = rl_oy;
    rl_ox = rl_oy = 0;
    gfx_fill_rect(0, 0, W, oy, C_BAR);
    const int lw = rl_half(oy - 1, 2);
    gfx_fill_rect(W / 2 - lw, oy - 1, 2 * lw, 1, C_GRID);
    char b[64], c[24];
    fmt_mhz(b, sizeof b, (double)s_m.center, s_m.span < 2 * MHZ ? 4 : 3);
    const int w1 = mf_width(&SF_BIG, b, 0), w2 = mf_width(&SF_MD, "MHz", 0), x1 = W / 2 - (w1 + 6 + w2) / 2;
    mf_text(&SF_BIG, x1, W * 40 / 466, b, C_TEXT, 255, 0);
    mf_text(&SF_MD, x1 + w1 + 6, W * 40 / 466, "MHz", C_DIM, 255, 0);
    fmt_bw(c, sizeof c, (double)s_m.span);
    int k = snprintf(b, sizeof b, "SPAN %s", c);
    if (cur_kind() == RFSRC_IQ && s_plan.rbw_hz > 0) {
        fmt_bw(c, sizeof c, s_plan.rbw_hz);
        snprintf(b + k, sizeof b - (size_t)k, "   RBW %s", c);
    }
    mf_text_c(&SF_MD, W / 2, W * 63 / 466, b, C_AXIS, 255, 0);
    if (s_m.mk != MK_OFF && s_mk_hz > 0) {
        const float d = trace_at(s_tr.v[SDR_TR_LIVE], s_mk_hz);
        char db[16] = "";
        if (d > SDR_NODATA) {
            fmt_db(db, sizeof db, d, 1);
        }
        fmt_mhz(c, sizeof c, s_mk_hz, s_m.span < 2 * MHZ ? 4 : 3);
        const rfsrc_caps_t *cp = cur_caps();
        snprintf(b, sizeof b, "M %s  %s %s", c, db, cp && (cp->flags & RFSRC_F_DBM) ? "dBm" : "dBFS");
        mf_text_c(&SF_MD, W / 2, W * 86 / 466, b, C_MARK, 255, 0);
    }
    const char *st = NULL;
    uint16_t sc = C_DIM;
    if (s_now - s_over_t < 1500) {
        st = "OVERLOAD";
        sc = C_WARN;
    } else if (s_m.frozen) {
        st = "FROZEN";
        sc = rgb(120, 190, 255);
    }
    if (!st) {
        const rfsrc_caps_t *cp = cur_caps();
        char g[20] = "";
        if (cp && cp->ngains > 1) {
            snprintf(g, sizeof g, "%s%s  ", s_m.agc ? "A\xB7" : "", cp->gain_name[s_m.gain % cp->ngains][0]);
        }
        snprintf(b, sizeof b, "%s%s%.0f fps", cp && (cp->flags & RFSRC_F_DEMO) ? "DEMO  " : "", g, s_fps);
        st = b;
    }
    mf_text_c(&SF_MD, W / 2, rl_t_key() + rl_t_kh() + W * 24 / 466, st, sc, 255, 0);
    rl_ox = ox;
    rl_oy = oy;
}

static void draw_top(void)
{
    if (gfx_round()) {
        draw_top_r();
        return;
    }
    tfd_rect(0, 0, s_W, s_TB, C_BAR, 256);
    tfd_rect(0, s_TB - 1, s_W, 1, C_GRID, 256);
    char b[48], c[24];
    const int b1 = BAR ? 16 : 15, b2 = BAR ? 16 : 30;
    int x = 6;
    fmt_mhz(b, sizeof b, (double)s_m.center, s_m.span < 2 * MHZ ? 4 : 3);
    mf_text(&SF_MD, x, b1, b, C_TEXT, 255, 0);
    x += mf_width(&SF_MD, b, 0) + 3;
    mf_text(&SF_SM, x, b1, "MHz", C_DIM, 255, 0);
    x += mf_width(&SF_SM, "MHz", 0) + 12;
    fmt_bw(c, sizeof c, (double)s_m.span);
    snprintf(b, sizeof b, "SPAN %s", c);
    mf_text(&SF_SM, x, b1, b, C_AXIS, 255, 0);
    x += mf_width(&SF_SM, b, 0) + 10;
    if ((cur_kind() == RFSRC_IQ || BAR) && s_plan.rbw_hz > 0) {
        fmt_bw(c, sizeof c, s_plan.rbw_hz);
        snprintf(b, sizeof b, "RBW %s", c);
        mf_text(&SF_SM, x, b1, b, C_AXIS, 255, 0);
        x += mf_width(&SF_SM, b, 0) + 12;
    }
    // the marker
    int mx = BAR ? x : 6;
    if (s_m.mk != MK_OFF && s_mk_hz > 0) {
        float d = trace_at(s_tr.v[SDR_TR_LIVE], s_mk_hz);
        char db[16] = "";
        if (d > SDR_NODATA) {
            fmt_db(db, sizeof db, d, 1);
        }
        fmt_mhz(c, sizeof c, s_mk_hz, s_m.span < 2 * MHZ ? 4 : 3);
        const rfsrc_caps_t *cp = cur_caps();
        snprintf(b, sizeof b, "M %s  %s %s", c, db, cp && (cp->flags & RFSRC_F_DBM) ? "dBm" : "dBFS");
        mf_text(&SF_SM, mx, b2, b, C_MARK, 255, 0);
    }
    // the state at the right: OVERLOAD, FROZEN, the demo, the frames a second
    const int xr = s_W - 6;
    const char *st = NULL;
    uint16_t sc = C_DIM;
    if (s_now - s_over_t < 1500) {
        st = "OVERLOAD";
        sc = C_WARN;
    } else if (s_m.frozen) {
        st = "FROZEN";
        sc = rgb(120, 190, 255);
    }
    if (st) {
        int w = mf_width(&SF_SM, st, 0);
        tfd_rrect(xr - w - 8, (BAR ? 3 : 18), w + 8, 15, 4, sc, 60);
        mf_text(&SF_SM, xr - w - 4, (BAR ? 15 : 30), st, sc, 255, 0);
    } else {
        const rfsrc_caps_t *cp = cur_caps();
        // the gain (A = AUTO), the demo, the frames a second
        char g[20] = "";
        if (cp && cp->ngains > 1) {
            snprintf(g, sizeof g, "%s%s  ", s_m.agc ? "A\xB7" : "", cp->gain_name[s_m.gain % cp->ngains][0]);
        }
        snprintf(b, sizeof b, "%s%s%.0f fps", cp && (cp->flags & RFSRC_F_DEMO) ? "DEMO  " : "", g, s_fps);
        int w = mf_width(&SF_SM, b, 0);
        mf_text(&SF_SM, xr - w, b2, b, C_DIM, 255, 0);
    }
}

static void btn_icon(int b, int cx, int cy, uint16_t c)
{
    switch (b) {
    case B_IN:
    case B_OUT:
        tfd_ring((float)cx - 3, (float)cy - 3, 8, 2.4f, c, 256);
        tfd_line((float)cx + 3, (float)cy + 3, (float)cx + 9, (float)cy + 9, 3, c, 256);
        tfd_rect(cx - 7, cy - 4, 9, 2, c, 256);
        if (b == B_IN) {
            tfd_rect(cx - 4, cy - 7, 2, 9, c, 256);
        }
        break;
    case B_VIEW:
        tfd_rrect(cx - 11, cy - 9, 22, 7, 2, c, 256);
        tfd_rrect(cx - 11, cy, 22, 9, 2, s_cmap[180], 256);
        tfd_rect(cx - 9, cy + 2, 6, 5, s_cmap[90], 256);
        tfd_rect(cx + 2, cy + 3, 5, 4, s_cmap[240], 256);
        break;
    case B_CLEAR:
        tfd_arc((float)cx, (float)cy, 8, 2.4f, 0.6f, 5.6f, c, 256);
        tfd_line((float)cx + 7, (float)cy - 9, (float)cx + 8, (float)cy - 3, 2.4f, c, 256);
        tfd_line((float)cx + 2, (float)cy - 3, (float)cx + 8, (float)cy - 3, 2.4f, c, 256);
        break;
    case B_FREEZE:
        if (s_m.frozen) {
            float p[6] = {(float)cx - 6, (float)cy - 8, (float)cx + 8, (float)cy, (float)cx - 6, (float)cy + 8};
            tfd_poly(p, 3, c, 256);
        } else {
            tfd_rect(cx - 6, cy - 8, 4, 16, c, 256);
            tfd_rect(cx + 2, cy - 8, 4, 16, c, 256);
        }
        break;
    }
}

static void draw_buttons(void)
{
    if (gfx_round()) {  // (the panel of the bottom cap under the keys)
        rl_t_bcap(rl_t_key() - 6, C_BAR, C_GRID);
    } else {
        tfd_rect(s_W - s_BW, s_TB, s_BW, s_H - s_TB, C_BG, 256);
    }
    for (int b = 0; b < NBTN; b++) {
        int bx, by, bw, bh;
        btn_box(b, &bx, &by, &bw, &bh);
        tfd_rrect(bx + 3, by + 2, bw - 6, bh - 4, 6, s_pressed == b ? C_BTN_ON : C_BTN, 256);
        btn_icon(b, bx + bw / 2, by + bh / 2, s_pressed == b ? C_TEXT : C_AXIS);
    }
}

// ---------------------------------------------------------------- the card of the radio start
static bool s_need_card(void)
{
    return s_nsrc > 0 && src_state(s_m.src) == RFSRC_NEEDS_START;
}
static void card_boxes(int *bx, int *by, int *bw, int *bh)
{
    const int cw = BAR ? 150 : 120, ch = BAR ? 40 : 44, gap = 16;
    const int y = s_PY1 - ch - (BAR ? 8 : 14), cx = (s_PX0 + s_PX1) / 2;
    bx[0] = cx - gap / 2 - cw;
    bx[1] = cx + gap / 2;
    by[0] = by[1] = y;
    bw[0] = bw[1] = cw;
    bh[0] = bh[1] = ch;
}
static int card_btn_at(int x, int y)
{
    int bx[2], by[2], bw[2], bh[2];
    card_boxes(bx, by, bw, bh);
    for (int k = 0; k < 2; k++) {
        if (x >= bx[k] && x < bx[k] + bw[k] && y >= by[k] - 6 && y < by[k] + bh[k] + 6) {
            return k;
        }
    }
    return -1;
}
static void draw_card(void)
{
    tfd_rect(s_PX0, s_PY0, s_bins, s_PY1 - s_PY0, C_PLOT, 256);
    const int cx = (s_PX0 + s_PX1) / 2, w = s_bins - 16;
    const int y = s_PY0 + (BAR ? 26 : 40);
    tfd_text_c(&KF_LABEL, cx, y, TX(T_NEED1), C_TEXT, w);
    tfd_text_c(&KF_LABEL, cx, y + (BAR ? 20 : 26), TX(T_NEED2), C_AXIS, w);
    int bx[2], by[2], bw[2], bh[2];
    card_boxes(bx, by, bw, bh);
    tfd_rrect(bx[0], by[0], bw[0], bh[0], 8, rgb(30, 120, 220), 256);
    tfd_rrect(bx[1], by[1], bw[1], bh[1], 8, C_BTN_ON, 256);
    tfd_text_c(&KF_LABEL, bx[0] + bw[0] / 2, by[0] + bh[0] / 2 + 5, TX(T_START), C_TEXT, bw[0] - 8);
    tfd_text_c(&KF_LABEL, bx[1] + bw[1] / 2, by[1] + bh[1] / 2 + 5, TX(T_DEMO), C_TEXT, bw[1] - 8);
}

static void draw_message(const char *s, uint16_t c)
{
    tfd_rect(s_PX0, s_PY0, s_bins, s_PY1 - s_PY0, C_PLOT, 256);
    tfd_text_c(&KF_LABEL, (s_PX0 + s_PX1) / 2, (s_PY0 + s_PY1) / 2 + 5, s, c, s_bins - 16);
}

static void render(int64_t now)
{
    const int64_t r0 = esp_timer_get_time();
    s_seq_drawn = s_seq;
    s_now = now;
    tfd_screen();
    tfd_noclip();
    if (s_kbd) {  // (the keyboard of ENTER A FREQUENCY)
        kbd_render(now);
        return;
    }
    s_top = s_m.ref;
    s_rng = RANGE_DB[s_m.rng % 4];
    lock();
    if (gfx_round()) {  // (the round frame: the background of the whole circle; the bottom cap before the state)
        gfx_fill(C_BG);
        draw_buttons();
        draw_top();
    } else {
        draw_top();
        draw_buttons();
    }
    if (!s_nsrc) {
        draw_message(TX(T_NOSRC), C_AXIS);
    } else if (s_need_card()) {
        draw_card();
    } else if (s_eng_err && !s_seq) {
        char b[96];
        snprintf(b, sizeof b, "%s: %s", TX(T_FAIL), s_src[s_m.src].ops->error(s_eng_err));
        draw_message(b, C_WARN);
    } else {
        const int y0 = s_PY0, y1 = s_PY1;
        switch (s_m.view) {
        case V_SPEC:
            draw_spectrum(y0, y1);
            break;
        case V_WF:
            draw_waterfall(y0, y1, true);
            draw_marker(y0, y1, false);
            break;
        case V_PERS:
            draw_persist(y0, y1);
            break;
        case V_CHAN:
            draw_channels(y0, y1);
            break;
        case V_SCOPE:
            draw_scope(y0, y1);
            break;
        default: {
            int ys = y0 + (y1 - y0) * (BAR ? 42 : 45) / 100;
            draw_spectrum(y0, ys);
            tfd_rect(s_PX0, ys, s_bins, 2, C_BG, 256);
            draw_waterfall(ys + 2, y1, true);
            draw_marker(ys + 2, y1, false);
            break;
        }
        }
    }
    draw_axis();
    unlock();
    s_prof.render += (float)(esp_timer_get_time() - r0) / 1000.0f;
    s_prof.rn++;
    // the toast (the name of a view, a message)
    if (s_toast[0] && now - s_toast_t < 1400) {
        int w = ui_text_w(&KF_LABEL, s_toast) + 20;
        int x = (s_PX0 + s_PX1) / 2 - w / 2, y = s_PY0 + 8;
        tfd_rrect(x, y, w, 24, 8, C_BG, 220);
        tfd_text_c(&KF_LABEL, x + w / 2, y + 17, s_toast, C_TEXT, w);
    }
}

// ---------------------------------------------------------------- the menu icon
#include "rndlay/rl_origin_end.h"  // (the menu icon draws at its own points)
#include "tficon/tficon.h"
// a peak of the spectrum at fx (0 .. 1): a soft bump of width w (no expf: a rational curve)
static float sdr_icon_bump(float fx, float at, float w)
{
    float d = (fx - at) / w;
    return 1 / (1 + d * d * d * d);
}
// the menu icon: a screen in the tile: the spectrum (three signals; the first one breathes) over the waterfall that
// runs down (one period of each signal in 2 s: the loop of the store has no jump)
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x3d6bff), tfi_hex(0x101c6a));
    float u = s / 100.0f, ph = tfi_ph(2);
    int sx0 = x + (int)(13 * u), sy0 = y + (int)(14 * u), sw = (int)(74 * u), sh = (int)(70 * u);
    tfi_drop(sx0, sy0, sw, sh, (int)(7 * u), (int)(3 * u), 90);
    tfi_rbox(sx0, sy0, sw, sh, (int)(7 * u), tfi_hex(0x0a1024), tfi_hex(0x04060e), 256);
    static const uint32_t HEAT[6] = {0x0c1450, 0x1f3bb0, 0x1fa8e0, 0x40e080, 0xffd84a, 0xff5a3a};
    // the waterfall (the lower part): the rows run down, so each row shows the past
    int wx0 = sx0 + (int)(4 * u), wx1 = sx0 + sw - (int)(4 * u), wy0 = y + (int)(52 * u), wy1 = y + (int)(80 * u);
    int cw = (int)(2 * u) > 1 ? (int)(2 * u) : 1;
    for (int yy = wy0; yy < wy1; yy += cw) {
        float row = (yy - wy0) / (float)(wy1 - wy0) - ph;  // (the time of this row: one loop over the height)
        float on1 = fsin(row * 6.2831853f) > -0.3f ? 1.0f : 0.2f, on2 = fsin(row * 12.566f + 1) > 0.2f ? 0.8f : 0.1f;
        for (int xx = wx0; xx < wx1; xx += cw) {
            float fx = (xx - wx0) / (float)(wx1 - wx0);
            float v = 0.1f + 0.85f * on1 * sdr_icon_bump(fx, 0.25f, 0.08f) +
                      0.7f * on2 * sdr_icon_bump(fx, 0.58f, 0.1f) + 0.9f * sdr_icon_bump(fx, 0.84f, 0.03f);
            int k = (int)(v * 6);
            tfd_rect(xx, yy, cw, cw, tfi_hex(HEAT[k < 0 ? 0 : k > 5 ? 5 : k]), 256);
        }
    }
    // the spectrum (the upper part): an orange trace with a soft line under it
    float br = 0.8f + 0.2f * fsin(ph * 6.2831853f), px = 0, py = 0;
    for (int xx = wx0; xx <= wx1; xx += cw * 2) {
        float fx = (xx - wx0) / (float)(wx1 - wx0);
        float v = 0.06f + 0.8f * br * sdr_icon_bump(fx, 0.25f, 0.08f) + 0.55f * sdr_icon_bump(fx, 0.58f, 0.1f) +
                  0.95f * sdr_icon_bump(fx, 0.84f, 0.025f) + 0.04f * fsin(fx * 60 + ph * 12.566f);
        float X = (float)xx, Y = y + (48 - 30 * (v > 1 ? 1 : v)) * u;
        if (xx > wx0) {
            tfi_line(px, py, X, Y, 2.6f * u, true, tfi_hex(0xffd84a), 256);
        }
        px = X;
        py = Y;
    }
    tfi_end();
}
