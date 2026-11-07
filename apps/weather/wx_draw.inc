// WEATHER: the drawing of the pages (included by app_weather.c).
// The task: the render hook of the app task.
// The memory: the file statics; the screen picture s_pic (PSRAM) keeps the bands that did not change, and
// bands_free() frees it at leave.
#define C_TEXT  tfd_rgb(240, 244, 250)
#define C_DIM   tfd_rgb(168, 180, 198)
#define C_FAINT tfd_rgb(110, 122, 140)
#define C_RAIN  tfd_rgb(110, 178, 255)
#define C_GOLD  tfd_rgb(255, 196, 92)

enum {
    RG_TOP,
    RG_PAGE,
    RG_LIST,
    RG_DAYS,
    RG_N
};
// ---------------------------------------------------------------- the bands of the screen
// render() paints the whole frame each time. A band draws again only when its key changed; then its rows go into a
// copy of the picture (PSRAM). A band with the same key copies its rows back from that copy (a memcpy).
static uint16_t *s_pic;  // the copy of the last picture (W x H)
static uint32_t s_bkey[RG_N];
static bool s_bvalid[RG_N];
static int s_sys_frames, s_sc_drawn = -1;
static void bands_reset(void)
{
    memset(s_bvalid, 0, sizeof s_bvalid);
}
// true: draw the band (the rectangle x0, y0 .. x1, y1), then call band_end_r(); false: it came back from the copy
static bool band_r(int rg, uint32_t key, int x0, int y0, int x1, int y1)
{
    int fw = gfx_w;
    // (the copy holds the largest frame: 172 x 640, or the Tab5 frame 360 x 640)
    if (!s_pic) {
        s_pic = heap_caps_malloc((LCD_W * LCD_H > 172 * 640 ? LCD_W * LCD_H : 172 * 640) * 2, MALLOC_CAP_SPIRAM);
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > gfx_h) {
        y1 = gfx_h;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > fw) {
        x1 = fw;
    }
    if (s_pic && s_bvalid[rg] && s_bkey[rg] == key) {
        for (int y = y0; y < y1 && x1 > x0; y++) {
            memcpy(&g_fb[y * fw + x0], &s_pic[y * fw + x0], (size_t)(x1 - x0) * 2);
        }
        return false;
    }
    s_bkey[rg] = key;
    s_bvalid[rg] = s_pic != NULL;
    return true;
}
static void band_end_r(int x0, int y0, int x1, int y1)
{
    int fw = gfx_w;
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > gfx_h) {
        y1 = gfx_h;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > fw) {
        x1 = fw;
    }
    if (!s_pic) {
        return;
    }
    for (int y = y0; y < y1 && x1 > x0; y++) {
        memcpy(&s_pic[y * fw + x0], &g_fb[y * fw + x0], (size_t)(x1 - x0) * 2);
    }
}
static bool band(int rg, uint32_t key, int y0, int y1)
{
    return band_r(rg, key, 0, y0, gfx_w, y1);
}
static void band_end(int y0, int y1)
{
    band_end_r(0, y0, gfx_w, y1);
}
static void bands_free(void)
{
    free(s_pic);
    s_pic = NULL;
    bands_reset();
}
static uint32_t kmix(uint32_t h, int32_t v)
{
    return (h ^ (uint32_t)v) * 16777619u + 0x9E37u;
}

// ---------------------------------------------------------------- helpers
// the colour of a temperature (C): purple, blue, cyan, green, yellow, orange, red
static uint16_t temp_col(float c)
{
    static const float T[] = {-10, 0, 10, 18, 24, 30, 38};
    static const uint8_t C[][3] = {{150, 110, 255}, {80, 160, 255}, {70, 210, 220}, {120, 220, 120},
                                   {250, 210, 80},  {255, 140, 60}, {240, 70, 60}};
    if (isnan(c)) {
        return C_DIM;
    }
    if (c <= T[0]) {
        return tfd_rgb(C[0][0], C[0][1], C[0][2]);
    }
    for (int i = 0; i < 6; i++) {
        if (c <= T[i + 1]) {
            float k = (c - T[i]) / (T[i + 1] - T[i]);
            return tfd_rgb((int)(C[i][0] + (C[i + 1][0] - C[i][0]) * k), (int)(C[i][1] + (C[i + 1][1] - C[i][1]) * k),
                           (int)(C[i][2] + (C[i + 1][2] - C[i][2]) * k));
        }
    }
    return tfd_rgb(C[6][0], C[6][1], C[6][2]);
}
static int icon_of(int code, bool day)
{
    const cond_t *c = cond_of(code);
    return day ? c->ic_d : c->ic_n;
}
// an icon of the conditions (26 or 48 px) with its middle at cx, cy
static void wicon(int idx, int sz, int cx, int cy)
{
    if (idx < 0 || idx >= IC_N) {
        return;
    }
    const uint16_t *cs = sz == 48 ? WI_48_C : WI_26_C;
    const uint8_t *as = sz == 48 ? WI_48_A : WI_26_A;
    int n = sz * sz, off = idx * n;
    int x0 = cx - sz / 2, y0 = cy - sz / 2;
    for (int y = 0; y < sz; y++) {
        int yy = y0 + y;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        uint16_t *o = &tfd_fb[yy * tfd_w];
        for (int x = 0; x < sz; x++) {
            int xx = x0 + x;
            if ((unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int i = off + y * sz + x;
            int a = (i & 1) ? as[i >> 1] >> 4 : as[i >> 1] & 15;
            if (!a) {
                continue;
            }
            o[xx] = a == 15 ? cs[i] : tfd_mix(o[xx], cs[i], a * 17);
        }
    }
}
// a text with a soft dark copy under it (on the sky)
static void stext_c(const kfont_t *f, int cx, int base, const char *s, uint16_t c, int w)
{
    tfd_text_c(f, cx + 1, base + 1, s, tfd_rgb(10, 16, 30), w);
    tfd_text_c(f, cx, base, s, c, w);
}
// the start of a row (the left end in English, the right end in Hebrew) and its end
static int sx(int x)
{
    return s_he ? W - x : x;
}
static void text_at(const kfont_t *f, int x0, int x1, int base, const char *s, uint16_t c)
{
    // (x0 .. x1 in the frame of the English layout; in Hebrew the line turns)
    if (s_he) {
        tfd_text_s(f, W - x1, W - x0, base, s, c, true);
    } else {
        tfd_text_s(f, x0, x1, base, s, c, false);
    }
}
// a number (an mfont: Inter) at the end side: right-aligned at x in English, left-aligned at W - x in Hebrew
static void num_end(const mfont_t *f, int x, int base, const char *s, uint16_t c)
{
    int w = mf_width(f, s, 0);
    if (s_he) {
        mf_text(f, W - x, base, s, c, 255, 0);
    } else {
        mf_text(f, x - w, base, s, c, 255, 0);
    }
}
static void num_at(const mfont_t *f, int x, int base, const char *s, uint16_t c)
{
    int w = mf_width(f, s, 0);
    if (s_he) {
        mf_text(f, W - x - w, base, s, c, 255, 0);
    } else {
        mf_text(f, x, base, s, c, 255, 0);
    }
}
static void card(int y, int h, uint16_t page)
{
    int r, g, b;
    tfd_unrgb(page, &r, &g, &b);
    tfd_rrect(CARD_X, y, CARD_W, h, 14, tfd_rgb(r + 18, g + 20, b + 26), 256);
    tfd_rrect(CARD_X + 14, y, CARD_W - 28, 1, 0, tfd_rgb(255, 255, 255), 30);
}
static void card_head(int y, const char *label, int ic)
{
    // a small mark and the label
    int mx = sx(CARD_X + 16);
    if (ic == 0) {
        tfd_ring((float)mx, (float)y + 17, 5, 1.6f, C_DIM, 256);
        tfd_line(mx, y + 17, mx, y + 14, 1.4f, C_DIM, 256);
        tfd_line(mx, y + 17, mx + 2, y + 18, 1.4f, C_DIM, 256);
    } else if (ic == 1) {
        tfd_rrect(mx - 5, y + 12, 10, 10, 2, C_DIM, 256);
        tfd_rect(mx - 5, y + 14, 10, 2, tfd_rgb(40, 50, 70), 256);
    } else {
        tfd_disc((float)mx, (float)y + 18, 3.5f, C_RAIN, 256);
        tfd_line(mx, y + 11, mx - 3, y + 16, 2, C_RAIN, 256);
        tfd_line(mx, y + 11, mx + 3, y + 16, 2, C_RAIN, 256);
    }
    text_at(&WK_LABEL, CARD_X + 26, CARD_X + CARD_W - 10, y + 22, label, C_DIM);
    tfd_rect(CARD_X + 10, y + 30, CARD_W - 20, 1, tfd_rgb(255, 255, 255), 26);
}
// The credit of the data (T_CREDIT): one line when it fits in w, else two lines (the first at base, the second gap
// lower). sky = on the picture of the sky: the text has a shadow (stext_c).
static void credit(const kfont_t *f, int cx, int base, int gap, int w, uint16_t c, bool sky)
{
    bool one = ui_text_w(f, T_CREDIT) <= w;
    const char *a = one ? T_CREDIT : T_CREDIT1;
    if (sky) {
        stext_c(f, cx, base, a, c, w);
    } else {
        tfd_text_c(f, cx, base, a, c, w);
    }
    if (one) {
        return;
    }
    if (sky) {
        stext_c(f, cx, base + gap, T_CREDIT2, c, w);
    } else {
        tfd_text_c(f, cx, base + gap, T_CREDIT2, c, w);
    }
}
static void spinner(int cx, int cy, int r, int64_t t, uint16_t c)
{
    float a = (float)(t % 900) / 900.0f * 6.2832f;
    tfd_arc((float)cx, (float)cy, (float)r, 2.4f, a - 1.7f, a + 1.7f, c, 256);
    tfd_ring((float)cx, (float)cy, (float)r, 2.4f, c, 50);
}

// ---------------------------------------------------------------- the hours
// the rows of the next hours: 24 hours from now; *ev = the rows of a sunrise or a sunset between them
static int hours_rows(const wx_t *w, int64_t now, int *ev)
{
    *ev = 0;
    if (!w->fetched || w->nh <= 0) {
        return 0;
    }
    int i0 = 0;
    while (i0 < w->nh - 1 && w->h0 + (int64_t)(i0 + 1) * 3600 <= now) {
        i0++;
    }
    int n = w->nh - i0 < 24 ? w->nh - i0 : 24;
    int64_t t0 = w->h0 + (int64_t)i0 * 3600, t1 = t0 + (int64_t)n * 3600;
    for (int d = 0; d < w->nd; d++) {
        if (w->drise[d] > now && w->drise[d] >= t0 && w->drise[d] < t1) {
            (*ev)++;
        }
        if (w->dset[d] > now && w->dset[d] >= t0 && w->dset[d] < t1) {
            (*ev)++;
        }
    }
    return n;
}

static void draw_hours(const wx_t *w, int64_t now, int y)
{
    int ev;
    int n = hours_rows(w, now, &ev);
    card(y, 34 + n * HR_H + ev * EV_H + 6, s_page_c);
    card_head(y, TX(T_HOURS), 0);
    if (!n) {
        return;
    }
    int i0 = 0;
    while (i0 < w->nh - 1 && w->h0 + (int64_t)(i0 + 1) * 3600 <= now) {
        i0++;
    }
    int ry = y + 34;
    for (int k = 0; k < n; k++) {
        int i = i0 + k;
        int64_t ht = w->h0 + (int64_t)i * 3600;
        // a sunrise or a sunset in this hour: a gold row before it
        for (int d = 0; d < w->nd; d++) {
            for (int e = 0; e < 2; e++) {
                int64_t et = e ? w->dset[d] : w->drise[d];
                if (et <= now || et < ht || et >= ht + 3600) {
                    continue;
                }
                int hh, mm;
                hm_of(et, w->off, &hh, &mm);
                char b[48];
                snprintf(b, sizeof b, "%s  %02d:%02d", e ? TX(T_SET) : TX(T_RISE), hh, mm);
                tfd_rect(CARD_X + 12, ry + EV_H / 2, CARD_W - 24, 1, C_GOLD, 90);
                int tw = ui_text_w(&WK_LABEL, b);
                tfd_rect(W / 2 - tw / 2 - 6, ry + 2, tw + 12, EV_H - 4, s_page_c, 256);
                ui_text_c(&WK_LABEL, W / 2, ry + EV_H - 5, b, C_GOLD);
                ry += EV_H;
            }
        }
        if (ry > tfd_cy1 + 40) {
            break;
        }
        int hh, mm;
        hm_of(ht, w->off, &hh, &mm);
        char b[16];
        uint16_t tc = C_TEXT;
        if (k == 0) {
            text_at(&WK_TEXT, CARD_X + 8, CARD_X + 46, ry + 20, TX(T_NOW), tc);
        } else {
            snprintf(b, sizeof b, "%02d:00", hh);
            num_at(&WF_SMALL, CARD_X + 8, ry + 20, b, C_DIM);
        }
        const int hdx = W >= 300 ? 22 : 0;  // (the wide page of the Tab5: the icon clear of the hour)
        wicon(icon_of(w->hcode[i], w->hday[i]), 26, sx(CARD_X + 64 + hdx), ry + 15);
        float pop = w->hpop[i];
        if (!isnan(pop) && pop >= 20) {
            snprintf(b, sizeof b, "%d", (int)pop);
            int bx = sx(CARD_X + 82 + hdx);
            tfd_disc((float)bx, (float)ry + 17, 2.6f, C_RAIN, 256);
            tfd_line(bx, ry + 11, bx - 2, ry + 16, 1.6f, C_RAIN, 256);
            tfd_line(bx, ry + 11, bx + 2, ry + 16, 1.6f, C_RAIN, 256);
            num_at(&WF_TINY, CARD_X + 87 + hdx, ry + 19, b, C_RAIN);
        }
        t_str(b, sizeof b, w->ht[i]);
        num_end(&WF_NUM, CARD_X + CARD_W - 14, ry + 22, b, C_TEXT);
        // the strip of the temperature at the end of the row
        tfd_rect(sx(CARD_X + CARD_W - 9) - (s_he ? 3 : 0), ry + 2, 3, HR_H - 1, temp_col(w->ht[i]), 230);
        ry += HR_H;
    }
}

// ---------------------------------------------------------------- the days
static void draw_days(const wx_t *w, int64_t now, int y)
{
    card(y, 34 + w->nd * DY_H + 6, s_page_c);
    card_head(y, TX(T_DAYS), 1);
    float lo = 1e9f, hi = -1e9f;
    for (int d = 0; d < w->nd; d++) {
        if (!isnan(w->dmin[d]) && w->dmin[d] < lo) {
            lo = w->dmin[d];
        }
        if (!isnan(w->dmax[d]) && w->dmax[d] > hi) {
            hi = w->dmax[d];
        }
    }
    if (hi - lo < 4) {
        hi += 2;
        lo -= 2;
    }
    int ry = y + 34;
    for (int d = 0; d < w->nd; d++, ry += DY_H) {
        if (ry > tfd_cy1 + 40) {
            break;
        }
        if (ry + DY_H < tfd_cy0 - 40) {
            continue;
        }
        const char *dn = d == 0 ? TX(T_TODAY) : TX(DAYS[wday_of(w->dt[d] + 43200, 0)]);
        text_at(tfd_pick(&WK_TEXT, &WK_TINY, dn, 36), CARD_X + 6, CARD_X + 42, ry + 20, dn, C_TEXT);
        float pop = w->dpop[d];
        char b[16];
        if (!isnan(pop) && pop >= 20) {
            snprintf(b, sizeof b, "%d%%", (int)pop);
            num_at(&WF_TINY, CARD_X + 6, ry + 37, b, C_RAIN);
        }
        wicon(w->dcode[d] ? icon_of(w->dcode[d], true) : IC_SUN, 26, sx(CARD_X + 54), ry + 22);
        // the range of the day on the range of the ten days, in the colours of the temperatures
        int bx0 = CARD_X + 95, bx1 = CARD_X + CARD_W - 31, by = ry + 21;
        int a = bx0 + (int)((w->dmin[d] - lo) / (hi - lo) * (bx1 - bx0)),
            z = bx0 + (int)((w->dmax[d] - lo) / (hi - lo) * (bx1 - bx0));
        if (z < a + 6) {
            z = a + 6;
        }
        int ra = s_he ? W - z : a, rz = s_he ? W - a : z;
        tfd_rrect(s_he ? W - bx1 : bx0, by - 2, bx1 - bx0, 4, 2, tfd_rgb(255, 255, 255), 30);
        for (int x = ra; x < rz; x++) {
            float tt = w->dmin[d] + (w->dmax[d] - w->dmin[d]) * (float)(s_he ? rz - x : x - ra) / (float)(rz - ra);
            tfd_rect(x, by - 2, 1, 4, temp_col(tt), 256);
        }
        tfd_disc((float)ra + 1.5f, (float)by, 2, temp_col(s_he ? w->dmax[d] : w->dmin[d]), 256);
        tfd_disc((float)rz - 1.5f, (float)by, 2, temp_col(s_he ? w->dmin[d] : w->dmax[d]), 256);
        if (d == 0 && !isnan(w->t)) {
            int nx = bx0 + (int)((w->t - lo) / (hi - lo) * (bx1 - bx0));
            if (s_he) {
                nx = W - nx;
            }
            tfd_disc((float)nx, (float)by, 4.5f, s_page_c, 256);
            tfd_disc((float)nx, (float)by, 3, tfd_rgb(255, 255, 255), 256);
        }
        t_str(b, sizeof b, w->dmin[d]);
        num_end(&WF_SMALL, CARD_X + 92, ry + 26, b, C_DIM);
        t_str(b, sizeof b, w->dmax[d]);
        num_end(&WF_SMALL, CARD_X + CARD_W - 6, ry + 26, b, C_TEXT);
        if (d < w->nd - 1) {
            tfd_rect(CARD_X + 10, ry + DY_H - 1, CARD_W - 20, 1, tfd_rgb(255, 255, 255), 16);
        }
    }
}

// ---------------------------------------------------------------- the rain soon
static void draw_rain(const wx_t *w, int64_t now, int y)
{
    card(y, 124, s_page_c);
    card_head(y, TX(T_RAIN), 2);
    int m;
    int rs = rain_soon(w, now, &m);
    char b[64];
    if (rs == 1) {
        snprintf(b, sizeof b, TX(T_RAININ), m);
    } else if (rs == 2) {
        snprintf(b, sizeof b, TX(T_RAINSTOP), m);
    } else {
        snprintf(b, sizeof b, "%s", TX(rs == 3 ? T_RAINLONG : T_NORAIN));
    }
    tfd_text_c(&WK_TEXT, W / 2, y + 54, b, C_TEXT, CARD_W - 16);
    // the bars of the quarters of an hour (4 hours)
    float mx = 0.5f;
    for (int i = 0; i < w->nm; i++) {
        if (!isnan(w->mpr[i]) && w->mpr[i] > mx) {
            mx = w->mpr[i];
        }
    }
    int bx0 = CARD_X + 12, bw = (CARD_W - 24) / (w->nm > 0 ? w->nm : 1), base = y + 104;
    for (int i = 0; i < w->nm; i++) {
        float v = isnan(w->mpr[i]) ? 0 : w->mpr[i];
        int h = (int)(v / mx * 36);
        int x = s_he ? W - bx0 - (i + 1) * bw : bx0 + i * bw;
        tfd_rrect(x + 1, base - 36, bw - 2, 36, 2, tfd_rgb(255, 255, 255), 14);
        if (h > 0) {
            tfd_rrect(x + 1, base - h, bw - 2, h, 2, C_RAIN, 230);
        }
    }
    int hh, mmn;
    for (int k = 0; k <= 4; k += 2) {
        if (!w->nm) {
            break;
        }
        hm_of(w->m0 + (int64_t)k * 3600, w->off, &hh, &mmn);
        snprintf(b, sizeof b, "%02d:%02d", hh, mmn);
        int x = bx0 + k * 4 * bw;
        if (s_he) {
            x = W - x;
        }
        int tw = mf_width(&WF_TINY, b, 0);
        int xx = x - tw / 2;
        if (xx < CARD_X + 6) {
            xx = CARD_X + 6;
        }
        if (xx + tw > CARD_X + CARD_W - 6) {
            xx = CARD_X + CARD_W - 6 - tw;
        }
        mf_text(&WF_TINY, xx, base + 14, b, C_FAINT, 255, 0);
    }
}

// ---------------------------------------------------------------- the tiles
static void tile_box(int x, int y, const char *label)
{
    int r, g, b;
    tfd_unrgb(s_page_c, &r, &g, &b);
    tfd_rrect(x, y, TILE, TILE, 12, tfd_rgb(r + 18, g + 20, b + 26), 256);
    if (s_he) {
        tfd_text_r(&WK_LABEL, x + TILE - 7, y + 16, label, C_DIM, TILE - 10);
    } else {
        tfd_text_l(&WK_LABEL, x + 7, y + 16, label, C_DIM, TILE - 10);
    }
}
// the wind: a compass, the speed, the unit
static void tile_wind(const wx_t *w, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_WIND));
    float cx = x + TILE / 2.0f, cy = yy + 42, r = 18;
    tfd_ring(cx, cy, r, 1.6f, C_FAINT, 256);
    for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f;
        tfd_line(cx + sinf(a) * (r - 3), cy - cosf(a) * (r - 3), cx + sinf(a) * r, cy - cosf(a) * r, k % 2 ? 1 : 1.6f,
                 C_FAINT, 256);
    }
    mf_text_c(&MF_TINY, (int)cx, (int)(cy - r + 12), "N", C_DIM, 255, 0);
    if (!isnan(w->wdir)) {
        float a = (w->wdir + 180) * 0.0174533f;  // (the wind comes FROM wdir: the arrow points where it goes)
        float ax = sinf(a), ay = -cosf(a);
        tfd_line(cx - ax * (r - 4), cy - ay * (r - 4), cx + ax * (r - 6), cy + ay * (r - 6), 2.2f, C_TEXT, 256);
        float hx = cx + ax * (r - 3), hy = cy + ay * (r - 3);
        tfd_line(hx, hy, hx - ax * 7 + ay * 4, hy - ay * 7 - ax * 4, 2.2f, C_TEXT, 256);
        tfd_line(hx, hy, hx - ax * 7 - ay * 4, hy - ay * 7 + ax * 4, 2.2f, C_TEXT, 256);
    }
    tfd_disc(cx, cy, 10, tfd_rgb(30, 40, 58), 230);
    snprintf(b, sizeof b, "%d", isnan(w->wind) ? 0 : (int)(w_disp(w->wind) + 0.5f));
    mf_text_c(&WF_SMALL, (int)cx, (int)cy + 5, b, C_TEXT, 255, 0);
    tfd_text_c(&WK_LABEL, (int)cx, yy + TILE - 4, TX(WUNIT_SHORT[s_wunit & 3]), C_DIM, TILE - 6);
}

// the UV index: the value, its level, a bar of colours with a mark
static void tile_uv(const wx_t *w, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_UV));
    float uv = isnan(w->uv) ? 0 : w->uv;
    snprintf(b, sizeof b, "%d", (int)(uv + 0.5f));
    num_at(&WF_MID, s_he ? W - x - TILE + 9 : x + 9, yy + 50, b, C_TEXT);
    int lv = uv < 3 ? 0 : uv < 6 ? 1 : uv < 8 ? 2 : uv < 11 ? 3 : 4;
    tfd_text_s(tfd_pick(&WK_LABEL, &WK_TINY, TX(UVL[lv]), TILE - 10), x + 6, x + TILE - 4, yy + 61, TX(UVL[lv]), C_DIM,
               s_he);
    for (int k = 0; k < TILE - 16; k++) {
        float f = (float)k / (TILE - 16);
        int r = (int)(f < 0.5f ? 90 + f * 2 * 160 : 250), g = (int)(f < 0.5f ? 210 : 210 - (f - 0.5f) * 2 * 160),
            bb = 80 + (int)(f * 60);
        tfd_rect(s_he ? x + TILE - 8 - k : x + 8 + k, yy + 67, 1, 4, tfd_rgb(r, g, bb), 256);
    }
    float f = uv / 11;
    if (f > 1) {
        f = 1;
    }
    int mx = s_he ? x + TILE - 8 - (int)(f * (TILE - 16)) : x + 8 + (int)(f * (TILE - 16));
    tfd_disc((float)mx, (float)yy + 69, 3.5f, tfd_rgb(255, 255, 255), 256);
}

// the sun: its arc from the sunrise to the sunset, with the sun now
static void tile_sun(const wx_t *w, int64_t now, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_SUN));
    int d = 0;
    int64_t day0 = now - ((now + w->off) % 86400 + 86400) % 86400;
    for (int k = 0; k < w->nd; k++) {
        if (w->dt[k] >= day0 - 43200 && w->dt[k] <= day0 + 43200) {
            d = k;
        }
    }
    int64_t r0 = w->drise[d], r1 = w->dset[d];
    float cx = x + TILE / 2.0f, cy = yy + 54, rr = 26;
    tfd_rect(x + 6, (int)cy, TILE - 12, 1, C_FAINT, 200);
    tfd_arc(cx, cy, rr, 1.6f, 3.14159f, 6.2832f, C_GOLD, 120);
    if (r1 > r0) {
        float f = (float)(now - r0) / (float)(r1 - r0);
        float ff = f < 0 ? 0 : f > 1 ? 1 : f;
        float a = 3.14159f + ff * 3.14159f;
        if (s_he) {
            a = 6.2832f - ff * 3.14159f;
        }
        float px = cx + cosf(a) * rr, py = cy + sinf(a) * rr;
        if (f > 0 && f < 1) {
            tfd_glow(px, py, 9, C_GOLD, 120);
            tfd_disc(px, py, 4, tfd_rgb(255, 230, 150), 256);
        } else {
            tfd_disc(px, py, 3, C_FAINT, 256);
        }
    }
    int hh, mm;
    hm_of(s_he ? r1 : r0, w->off, &hh, &mm);
    snprintf(b, sizeof b, "%02d:%02d", hh, mm);
    mf_text(&WF_TINY, x + 6, yy + TILE - 6, b, C_DIM, 255, 0);
    hm_of(s_he ? r0 : r1, w->off, &hh, &mm);
    snprintf(b, sizeof b, "%02d:%02d", hh, mm);
    mf_text(&WF_TINY, x + TILE - 6 - mf_width(&WF_TINY, b, 0), yy + TILE - 6, b, C_DIM, 255, 0);
}

// the moon: its phase and its name
static void tile_moon(int64_t now, int x, int yy)
{
    tile_box(x, yy, TX(T_MOON));
    float age = moon_age(now);
    float cx = x + TILE / 2.0f, cy = yy + 40, mr = 14;
    float ph = age * 6.2832f, lx = sinf(ph), lz = -cosf(ph);
    for (int py = (int)(cy - mr - 1); py <= (int)(cy + mr + 1); py++) {
        for (int px = (int)(cx - mr - 1); px <= (int)(cx + mr + 1); px++) {
            float dx = (px + 0.5f - cx) / mr, dy = (py + 0.5f - cy) / mr, q = dx * dx + dy * dy;
            if (q >= 1.08f) {
                continue;
            }
            float nz = q < 1 ? sqrtf(1 - q) : 0;
            float lit = dx * lx + nz * lz;
            int edge = q > 1 ? (int)((1.08f - q) / 0.08f * 256) : 256;
            uint16_t c = lit > 0.06f    ? tfd_rgb(240, 236, 214)
                         : lit > -0.06f ? tfd_rgb(150, 150, 142)
                                        : tfd_rgb(46, 52, 70);
            tfd_px(px, py, c, edge);
        }
    }
    const char *nm = TX(MOONS[moon_phase8(age)]);
    char l1[32], l2[32];
    snprintf(l1, sizeof l1, "%s", nm);
    l2[0] = 0;
    char *sp = strchr(l1, ' ');
    if (sp && ui_text_w(&WK_LABEL, nm) > TILE - 8) {
        *sp = 0;
        snprintf(l2, sizeof l2, "%s", sp + 1);
    }
    tfd_text_c(&WK_LABEL, (int)cx, yy + (l2[0] ? 63 : 70), l1, C_DIM, TILE - 6);
    if (l2[0]) {
        tfd_text_c(&WK_LABEL, (int)cx, yy + 74, l2, C_DIM, TILE - 6);
    }
}

// the humidity and the dew point
static void tile_humid(const wx_t *w, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_HUMID));
    snprintf(b, sizeof b, "%d%%", isnan(w->hum) ? 0 : (int)(w->hum + 0.5f));
    num_at(&WF_MID, s_he ? W - x - TILE + 9 : x + 9, yy + 50, b, C_TEXT);
    char d[24];
    t_str_u(d, sizeof d, w->dew);
    snprintf(b, sizeof b, "%s %s", TX(T_DEW), d);
    tfd_text_s(&WK_LABEL, x + 8, x + TILE - 6, yy + 68, b, C_DIM, s_he);
}

// the pressure: a gauge from 980 to 1040 hPa
static void tile_press(const wx_t *w, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_PRESS));
    float p = isnan(w->press) ? 1013 : w->press;
    float cx = x + TILE / 2.0f, cy = yy + 52, r = 22;
    tfd_arc(cx, cy, r, 3, 3.6652f, 5.7596f, C_FAINT, 200);
    float f = (p - 980) / 60;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    float a = 3.6652f + f * (5.7596f - 3.6652f);
    tfd_line(cx, cy, cx + cosf(a) * (r + 2), cy + sinf(a) * (r + 2), 2, C_TEXT, 256);
    tfd_disc(cx, cy, 3, C_TEXT, 256);
    snprintf(b, sizeof b, "%d", (int)(p + 0.5f));
    mf_text_c(&WF_SMALL, (int)cx, yy + TILE - 6, b, C_TEXT, 255, 0);
}

// the visibility
static void tile_vis(const wx_t *w, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_VIS));
    float v = isnan(w->vis) ? 0 : w->vis / 1000;
    if (v >= 10) {
        snprintf(b, sizeof b, "%d", (int)(v + 0.5f));
    } else {
        snprintf(b, sizeof b, "%d.%d", (int)v, (int)(v * 10) % 10);
    }
    num_at(&WF_MID, s_he ? W - x - TILE + 9 : x + 9, yy + 50, b, C_TEXT);
    tfd_text_s(&WK_LABEL, x + 8, x + TILE - 6, yy + 68, TX(T_KM), C_DIM, s_he);
}

// the air (US AQI)
static void tile_air(const wx_t *w, int x, int yy)
{
    char b[48];
    tile_box(x, yy, TX(T_AIR));
    if (w->aqi >= 0) {
        int q = w->aqi;
        int lv = q <= 50 ? 0 : q <= 100 ? 1 : q <= 150 ? 2 : q <= 200 ? 3 : q <= 300 ? 4 : 5;
        static const uint8_t AC[6][3] = {{90, 210, 110}, {240, 210, 70}, {255, 150, 60},
                                         {240, 80, 70},  {170, 90, 200}, {140, 40, 60}};
        snprintf(b, sizeof b, "%d", q);
        num_at(&WF_MID, s_he ? W - x - TILE + 9 : x + 9, yy + 50, b, C_TEXT);
        int dx = s_he ? x + 12 : x + TILE - 12;
        tfd_disc((float)dx, (float)yy + 38, 5, tfd_rgb(AC[lv][0], AC[lv][1], AC[lv][2]), 256);
        tfd_text_s(&WK_LABEL, x + 8, x + TILE - 6, yy + 68, TX(AQL[lv]), C_DIM, s_he);
    } else {
        mf_text_c(&WF_MID, x + TILE / 2, yy + 50, "--", C_FAINT, 255, 0);
    }
}

static void draw_tiles(const wx_t *w, int64_t now, int y)
{
    int xa = CARD_X, xb = CARD_X + CARD_W - TILE;
    if (s_he) {
        int k = xa;
        xa = xb;
        xb = k;
    }
    tile_wind(w, xa, y);
    tile_uv(w, xb, y);
    y += TILE + 8;
    tile_sun(w, now, xa, y);
    tile_moon(now, xb, y);
    y += TILE + 8;
    tile_humid(w, xa, y);
    tile_press(w, xb, y);
    y += TILE + 8;
    tile_vis(w, xa, y);
    tile_air(w, xb, y);
}

// ---------------------------------------------------------------- the hero: the place, the temperature now
static void draw_hero(const wx_t *w, const place_t *pl, int64_t now, int top, int64_t t)
{
    int cx = W / 2 + (int)s_xoff;
    // the place, with a pin for MY PLACE
    char nb[64];
    snprintf(nb, sizeof nb, "%s", pl->name);
    const kfont_t *nf = tfd_pick(&WK_PLACE, tfd_pick(&WK_COND, &WK_TEXT, nb, W - 84), nb, W - 84);
    stext_c(nf, cx, top + 36, nb, C_TEXT, W - 84);
    if (pl->here) {
        int px = cx + (s_he ? 1 : -1) * ((ui_text_w(nf, nb) < W - 84 ? ui_text_w(nf, nb) : W - 84) / 2 + 10);
        tfd_disc((float)px, (float)top + 24, 4.5f, C_TEXT, 256);
        tfd_line(px - 3.5f, top + 26.5f, px, top + 33, 2, C_TEXT, 256);
        tfd_line(px + 3.5f, top + 26.5f, px, top + 33, 2, C_TEXT, 256);
        tfd_disc((float)px, (float)top + 24, 1.8f, tfd_rgb(40, 60, 90), 256);
    }
    // the dots of the pages
    if (s_npl > 1) {
        int dw = 10, x0 = W / 2 - (s_npl - 1) * dw / 2;
        for (int i = 0; i < s_npl; i++) {
            int k = s_he ? s_npl - 1 - i : i;
            tfd_disc((float)(x0 + k * dw), (float)top + 52, i == s_cur ? 3 : 2.2f, tfd_rgb(255, 255, 255),
                     i == s_cur ? 256 : 110);
        }
    }
    if (!w->fetched) {
        if (updating(s_cur) || s_ip_need) {
            spinner(cx, top + 150, 16, t, C_TEXT);
            stext_c(&WK_TEXT, cx, top + 200, TX(s_ip_need && !s_npl ? T_FINDING : T_LOADING), C_TEXT, W - 16);
        } else {
            stext_c(&WK_TEXT, cx, top + 160, TX(s_wifi ? T_NOUPD : T_NOWIFI), C_TEXT, W - 16);
        }
        return;
    }
    char b[24];
    snprintf(b, sizeof b, "%d", t_disp(w->t));
    int tw = mf_width(&WF_HUGE, b, 0);
    int tx = cx - tw / 2 - 5;
    mf_text(&WF_HUGE, tx + 2, top + 172, b, tfd_rgb(10, 16, 30), 90, 0);
    mf_text(&WF_HUGE, tx, top + 170, b, C_TEXT, 255, 0);
    float dgx = tx + tw + 9, dgy = top + 98;
    if (dgx > W - 9) {
        dgx = W - 9;
    }
    tfd_ring(dgx + 1, dgy + 1, 6.5f, 2.6f, tfd_rgb(10, 16, 30), 90);
    tfd_ring(dgx, dgy, 6.5f, 2.6f, C_TEXT, 256);
    const cond_t *c = cond_of(w->code);
    bool day = s_sky_to.elev > -0.8f;
    stext_c(&WK_COND, cx, top + 202, s_he ? c->he : (day ? c->en : c->en_n), C_TEXT, W - 12);
    char hl[64], a1[16], a2[16];
    t_str_u(a1, sizeof a1, w->nd ? w->dmax[0] : NAN);
    t_str_u(a2, sizeof a2, w->nd ? w->dmin[0] : NAN);
    snprintf(hl, sizeof hl, "%s %s   %s %s", TX(T_HIGH), a1, TX(T_LOW), a2);
    stext_c(&WK_TEXT, cx, top + 228, hl, C_TEXT, W - 12);
    t_str_u(a1, sizeof a1, w->feels);
    snprintf(hl, sizeof hl, "%s %s", TX(T_FEELS), a1);
    stext_c(&WK_LABEL, cx, top + 248, hl, C_DIM, W - 12);
    // the rain soon: a small sign
    int m;
    int rs = rain_soon(w, now, &m);
    if (rs == 1 || rs == 2) {
        snprintf(hl, sizeof hl, rs == 1 ? TX(T_RAININ) : TX(T_RAINSTOP), m);
        int pw = ui_text_w(&WK_LABEL, hl) + 22;
        if (pw > W - 16) {
            pw = W - 16;
        }
        tfd_rrect(cx - pw / 2, top + 260, pw, 22, 11, tfd_rgb(20, 40, 80), 200);
        tfd_rrect(cx - pw / 2, top + 260, pw, 22, 11, C_RAIN, 40);
        tfd_text_c(&WK_LABEL, cx, top + 275, hl, tfd_rgb(200, 226, 255), pw - 10);
    }
}

// the button of the places (the start side of the top) and the state (the other side)
static void draw_top_buttons(int64_t t, int top)
{
    int bx = sx(24), by = top + 28;
    tfd_disc((float)bx, (float)by, 17, tfd_rgb(0, 0, 0), 60);
    for (int k = 0; k < 3; k++) {
        tfd_disc((float)bx - 6, (float)by - 6 + k * 6, 1.4f, C_TEXT, 256);
        tfd_rrect(bx - 3, by - 7 + k * 6, 10, 2, 1, C_TEXT, 256);
    }
    int ox = sx(W - 24);
    if (updating(s_cur) || (s_rq_mine >= 0 && s_rq_what == RQ_IP)) {
        spinner(ox, by, 8, t, C_TEXT);
    } else if (!s_wifi) {
        // no WiFi: a small crossed arc
        tfd_arc((float)ox, (float)by + 5, 10, 2, -2.4f, -0.74f, C_DIM, 256);
        tfd_arc((float)ox, (float)by + 5, 5, 2, -2.4f, -0.74f, C_DIM, 256);
        tfd_line(ox - 9, by - 7, ox + 9, by + 7, 2, tfd_rgb(255, 120, 100), 256);
    }
}

static void draw_toast(int y)
{
    int64_t age = ms_now() - s_toast_t;
    if (!s_toast[0] || age > 2400) {
        return;
    }
    int a = age < 2000 ? 256 : (int)(256 * (2400 - age) / 400);
    int tw = ui_text_w(&WK_TEXT, s_toast);
    if (tw > W - 24) {
        tw = W - 24;
    }
    tfd_rrect(W / 2 - tw / 2 - 12, y - 19, tw + 24, 30, 12, tfd_rgb(10, 14, 22), a * 235 / 256);
    if (a > 100) {
        tfd_text_c(&WK_TEXT, W / 2, y + 1, s_toast, C_TEXT, W - 24);
    }
}

static int32_t toast_key(void)
{
    int64_t age = ms_now() - s_toast_t;
    return s_toast[0] && age < 2500 ? (int32_t)(s_toast_t + (age > 2000 ? (int)(age - 2000) / 50 : 0)) : 0;
}

// ---------------------------------------------------------------- the page of a place
static void draw_page(int64_t t)
{
    int64_t now = unix_now();
    s_skw = W;
    s_skh = 360;
    if (s_cur >= s_npl) {
        // no place yet: the sky and "finding your place"
        if (band(RG_TOP, kmix(1, (int)(t / 33)), 0, H)) {
            sky_draw(0, 0, H, (float)t / 1000.0f);
            spinner(W / 2, 220, 16, t, C_TEXT);
            stext_c(&WK_TEXT, W / 2, 270, TX(s_wifi ? T_FINDING : T_NOWIFI), C_TEXT, W - 16);
            band_end(0, H);
        }
        return;
    }
    const wx_t *w = &s_wx[s_cur];
    const place_t *pl = &s_pl[s_cur];
    lay_t L = page_layout(w, now);
    int sc = (int)s_scroll;
    // the band of the sky: from the top to the first card (its top moves up half as fast as the page)
    int sky_top = sc > 0 ? -sc / 2 : 0;
    int cards_y = HERO_H - sc;  // (the screen row of the first card)
    int tb = cards_y < HDR_H ? HDR_H : cards_y > H ? H : cards_y;
    bool compact = cards_y < HDR_H + 60;
    uint32_t pk =
            kmix(kmix(kmix(kmix(kmix(11, sc), (int)s_xoff), s_cur), s_data_ver), s_unit * 16 + s_wunit * 2 + s_he);
    pk = kmix(kmix(pk, (int)(now / 60)), toast_key());
    pk = kmix(pk, s_pull);
    sky_page();
    pk = kmix(pk, s_page_c);  // (the page follows the colour of the sky: no two colours)
    bool page = band(RG_PAGE, pk, tb, H);
    bool sky_moves = s_motion != 0;
    uint32_t tk = kmix(kmix(pk, sky_moves ? (int)(t / 33) : 0), updating(s_cur) || s_ip_need ? (int)(t / 50) : 0);
    if (page) {
        s_bvalid[RG_TOP] = false;  // (the cards changed: the top too, its text can reach under the band)
    }
    bool top = band(RG_TOP, tk, 0, tb);
    if (page) {
        // the cards (the rows under the band of the sky)
        tfd_rect(0, tb, W, H - tb, s_page_c, 256);
        tfd_clip(0, tb, W, H);
        int ox = (int)s_xoff;
        (void)ox;
        if (w->fetched) {
            if (L.rain_h) {
                draw_rain(w, now, L.rain_y - sc);
            }
            draw_hours(w, now, L.hours_y - sc);
            draw_days(w, now, L.days_y - sc);
            tfd_clip(0, tb, W, H);
            draw_tiles(w, now, L.tiles_y - sc);
            int fy = L.foot_y - sc;
            if (fy < H && fy + 60 > tb) {
                int hh, mm;
                hm_of(w->fetched, w->off, &hh, &mm);
                char b[64];
                snprintf(b, sizeof b, "%s %02d:%02d", TX(T_UPD), hh, mm);
                tfd_text_c(&WK_LABEL, W / 2, fy + 24, b, C_FAINT, W - 16);
                credit(&WK_LABEL, W / 2, fy + 42, 16, W - 16, C_FAINT, false);
            }
        }
        tfd_noclip();
        band_end(tb, H);
    }
    if (top) {
        // the sky: the rows 0 .. tb (when the page scrolled down, only the small head)
        sky_draw(sky_top, 0, tb, (float)t / 1000.0f);
        if (!compact) {
            tfd_clip(0, 0, W, tb);
            draw_hero(w, pl, now, -sc, t);
            tfd_noclip();
            // the pull to update
            if (sc < -20) {
                int a = sc < -64 ? 256 : (-sc - 20) * 256 / 44;
                spinner(W / 2, 20 - sc / 3, 9, s_pull ? t : 0, tfd_mix(C_DIM, C_TEXT, a));
            }
        } else {
            // the small head: the place and the temperature now
            char tt[16];
            t_str(tt, sizeof tt, w->t);
            stext_c(tfd_pick(&WK_COND, tfd_pick(&WK_TEXT, &WK_LABEL, pl->name, W - 100), pl->name, W - 100),
                    W / 2 - (s_he ? -8 : 8), 33, pl->name, C_TEXT, W - 100);
            if (w->fetched) {
                num_end(&WF_NUM, W - 10, 34, tt, C_TEXT);
            }
            tfd_rect(0, HDR_H - 1, W, 1, tfd_rgb(255, 255, 255), 30);
        }
        draw_top_buttons(t, compact ? 0 : -sc);
        band_end(0, tb);
    }
    draw_toast(H - 60);  // (over the bands, each frame while it shows)
}

// ---------------------------------------------------------------- the desk view (landscape, 640 x 172)
#define LW      s_lw
#define LH      s_lh
#define PW      s_pw                     // the panel of the sky
#define DX0     s_dx0                    // the column of the days (the narrow desk: none, the days are under the chart)
#define LNARROW (LH > 200)               // the narrow desk, 320 x 240 (and the big desk of the Tab5, 640 x 360)
#define LBIG    (LH > 200 && LW >= 400)  // the big desk of the Tab5: the narrow layout, a taller chart
#define LDY     (LNARROW ? (LH - 172) / 2 : 0)  // (the hero of the narrow desk: lower, in the middle of its panel)
static int lx(int x0, int x1, int x)
{
    return s_he ? x0 + x1 - x : x;
}  // (a mirror inside a column in Hebrew)
static void draw_hero_land(const wx_t *w, const place_t *pl, int64_t now, int64_t t)
{
    int ox = (int)s_xoff;
    // the button of the places, the state
    tfd_disc(20, 22, 15, tfd_rgb(0, 0, 0), 60);
    for (int k = 0; k < 3; k++) {
        tfd_disc(14, 16 + k * 6, 1.3f, C_TEXT, 256);
        tfd_rrect(17, 15 + k * 6, 9, 2, 1, C_TEXT, 256);
    }
    if (updating(s_cur) || (s_rq_mine >= 0 && s_rq_what == RQ_IP)) {
        spinner(PW - 16, 22, 7, t, C_TEXT);
    } else if (!s_wifi) {
        tfd_line(PW - 24, 14, PW - 8, 30, 2, tfd_rgb(255, 120, 100), 256);
    }
    const int nw = LNARROW ? PW - 44 : PW - 90, ncx = LNARROW ? (PW + 34) / 2 : 122;
    const kfont_t *nf = tfd_pick(&WK_COND, tfd_pick(&WK_TEXT, &WK_LABEL, pl->name, nw), pl->name, nw);
    stext_c(nf, ncx + ox, 28, pl->name, C_TEXT, nw);
    if (s_npl > 1) {
        int x0 = ncx - (s_npl - 1) * 5;
        for (int i = 0; i < s_npl; i++) {
            int k = s_he ? s_npl - 1 - i : i;
            tfd_disc((float)(x0 + k * 10), 40, i == s_cur ? 2.6f : 2, tfd_rgb(255, 255, 255), i == s_cur ? 256 : 110);
        }
    }
    int cx = PW / 2 + ox;
    const int up = LNARROW ? 0 : 7;  // (the wide desk: the rows go up to give the credit a line at the bottom)
    if (!w->fetched) {
        if (updating(s_cur) || s_ip_need) {
            spinner(cx, 92 + LDY, 14, t, C_TEXT);
        }
        stext_c(&WK_TEXT, cx, 130 + LDY,
                TX(s_ip_need && !s_npl ? T_FINDING
                   : s_wifi            ? T_LOADING
                                       : T_NOWIFI),
                C_TEXT, PW - 16);
        return;
    }
    char b[24];
    snprintf(b, sizeof b, "%d", t_disp(w->t));
    int tw = mf_width(&WF_BIG, b, 0);
    int tx = cx - tw / 2 - 4;
    mf_text(&WF_BIG, tx + 2, 103 + LDY, b, tfd_rgb(10, 16, 30), 90, 0);
    mf_text(&WF_BIG, tx, 101 + LDY, b, C_TEXT, 255, 0);
    tfd_ring((float)(tx + tw + 7), 59 + LDY, 4.5f, 2, C_TEXT, 256);
    const cond_t *c = cond_of(w->code);
    bool day = s_sky_to.elev > -0.8f;
    stext_c(&WK_TEXT, cx, 124 + LDY - up / 2, s_he ? c->he : (day ? c->en : c->en_n), C_TEXT, PW - 12);
    char hl[64], a1[16], a2[16];
    t_str_u(a1, sizeof a1, w->nd ? w->dmax[0] : NAN);
    t_str_u(a2, sizeof a2, w->nd ? w->dmin[0] : NAN);
    snprintf(hl, sizeof hl, "%s %s   %s %s", TX(T_HIGH), a1, TX(T_LOW), a2);
    stext_c(&WK_LABEL, cx, 143 + LDY - up / 2, hl, C_TEXT, PW - 12);
    int m;
    int rs = rain_soon(w, now, &m);
    if (rs == 1 || rs == 2) {
        snprintf(hl, sizeof hl, rs == 1 ? TX(T_RAININ) : TX(T_RAINSTOP), m);
        int pw = ui_text_w(&WK_TINY, hl) + 18;
        if (pw > PW - 12) {
            pw = PW - 12;
        }
        tfd_rrect(cx - pw / 2, 150 + LDY - up, pw, 18, 9, tfd_rgb(20, 40, 80), 210);
        tfd_text_c(&WK_TINY, cx, 163 + LDY - up, hl, tfd_rgb(200, 226, 255), pw - 8);
    } else {
        t_str_u(a1, sizeof a1, w->feels);
        snprintf(hl, sizeof hl, "%s %s", TX(T_FEELS), a1);
        stext_c(&WK_TINY, cx, 162 + LDY - up, hl, C_DIM, PW - 12);
    }
    // the credit of the data: at the bottom of the panel of the sky
    if (LNARROW) {
        credit(&WK_TINY, PW / 2, LH - 25, 15, PW - 8, C_DIM, true);
    } else {
        credit(&WK_TINY, PW / 2, LH - 3, 15, PW - 8, C_DIM, true);
    }
}
// the next 24 hours as a curve
static void draw_chart(const wx_t *w, int64_t now)
{
    int x0 = PW + 16, x1 = DX0 - 14;
    // (the rows of the wide desk; the narrow one maps them into its top 136 rows)
    const int ch = LBIG ? 200 : LNARROW ? 136 : 172;
#define CR(v) ((v) * ch / 172)
    const int kt = LNARROW && !LBIG ? 4 : 3, ki = LNARROW && !LBIG ? 6 : 4;  // (fewer labels in a narrow chart)
    int ev;
    int n = hours_rows(w, now, &ev);
    if (n < 2) {
        return;
    }
    int i0 = 0;
    while (i0 < w->nh - 1 && w->h0 + (int64_t)(i0 + 1) * 3600 <= now) {
        i0++;
    }
    float dx = (float)(x1 - x0) / (n - 1);
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < n; k++) {
        float v = w->ht[i0 + k];
        if (isnan(v)) {
            continue;
        }
        if (v < lo) {
            lo = v;
        }
        if (v > hi) {
            hi = v;
        }
    }
    if (hi - lo < 3) {
        hi += 1.5f;
        lo -= 1.5f;
    }
#define CXK(k) (s_he ? x1 - (k) * dx : x0 + (k) * dx)
// (the narrow chart: the curve a little lower and flatter, its labels under the title)
#define CYV(v)                                                                                                         \
    (LBIG ? CR(86) - ((v) - lo) / (hi - lo) * CR(50)                                                                   \
          : (LNARROW ? 76 : 86) - ((v) - lo) / (hi - lo) * (LNARROW ? 34 : 50))
    // the night: a darker ground
    for (int k = 0; k < n; k++) {
        if (w->hday[i0 + k]) {
            continue;
        }
        int a = (int)(CXK(k) - dx / 2), b = (int)(CXK(k) + dx / 2) + 1;
        if (a > b) {
            int q = a;
            a = b;
            b = q;
        }
        tfd_rect(a, CR(22), b - a, CR(132), tfd_rgb(0, 0, 0), 46);
    }
    if (s_he) {
        tfd_text_r(&WK_LABEL, DX0 - 10, 15, TX(T_HOURS), C_DIM, DX0 - PW - 20);
    } else {
        tfd_text_l(&WK_LABEL, PW + 12, 15, TX(T_HOURS), C_DIM, DX0 - PW - 20);
    }
    // the sunrise and the sunset: a gold line
    int64_t t0 = w->h0 + (int64_t)i0 * 3600;
    for (int d = 0; d < w->nd; d++) {
        for (int e = 0; e < 2; e++) {
            int64_t et = e ? w->dset[d] : w->drise[d];
            float k = (float)(et - t0) / 3600.0f;
            if (k < 0 || k > n - 1) {
                continue;
            }
            int xx = (int)CXK(k);
            for (int y = CR(24); y < CR(152); y += 6) {
                tfd_rect(xx, y, 1, 3, C_GOLD, 140);
            }
            tfd_disc((float)xx, CR(25), 3, C_GOLD, 256);
        }
    }
    // the rain: bars at the bottom
    tfd_rect(x0 - 6, CR(153), x1 - x0 + 12, 1, tfd_rgb(255, 255, 255), 24);
    for (int k = 0; k < n; k++) {
        float pop = w->hpop[i0 + k];
        if (isnan(pop) || pop < 10) {
            continue;
        }
        int h = (int)(pop / 100 * CR(22));
        tfd_rrect((int)CXK(k) - 3, CR(153) - h, 6, h, 2, C_RAIN, 90 + (int)(pop * 1.6f));
    }
    // the curve, in the colours of the temperatures
    for (int k = 0; k + 1 < n; k++) {
        float a = w->ht[i0 + k], b = w->ht[i0 + k + 1];
        if (isnan(a) || isnan(b)) {
            continue;
        }
        tfd_line(CXK(k), CYV(a), CXK(k + 1), CYV(b), 2.6f, temp_col((a + b) / 2), 256);
    }
    char b[16];
    for (int k = 0; k < n; k++) {
        float v = w->ht[i0 + k];
        float x = CXK(k), y = isnan(v) ? CR(86) : CYV(v);
        if (k % kt == 0 && !isnan(v)) {
            tfd_disc(x, y, 3, tfd_rgb(255, 255, 255), 256);
            t_str(b, sizeof b, v);
            mf_text_c(&WF_TINY, (int)x, (int)y - 7, b, C_TEXT, 255, 0);
        }
        if (k % ki == 0) {
            wicon(icon_of(w->hcode[i0 + k], w->hday[i0 + k]), 26, (int)x, CR(112) + (LNARROW ? 3 : 0));
            int hh, mm;
            hm_of(t0 + (int64_t)k * 3600, w->off, &hh, &mm);
            if (k == 0) {
                tfd_text_c(&WK_TINY, (int)x, CR(168), TX(T_NOW), C_TEXT, 40);
            } else {
                snprintf(b, sizeof b, "%02d:00", hh);
                mf_text_c(&WF_TINY, (int)x, CR(168), b, C_DIM, 255, 0);
            }
        }
    }
#undef CXK
#undef CYV
#undef CR
}
// the next four days
static void draw_days4(const wx_t *w, int64_t now)
{
    (void)now;
    // (the narrow desk: the days are rows under the chart, from its right part's x0; no title, a line over them)
    const int xd = LNARROW ? PW : DX0, ytop = LBIG ? 212 : LNARROW ? 140 : 24, rh = LBIG ? 37 : LNARROW ? 25 : 37;
    if (LNARROW) {
        tfd_rect(PW + 8, ytop - 2, LW - PW - 16, 1, tfd_rgb(255, 255, 255), 30);
    } else {
        tfd_rect(DX0, 8, 1, LH - 16, tfd_rgb(255, 255, 255), 22);
    }
    int c0 = xd + 10, c1 = LW - 10;
    if (!LNARROW) {
        if (s_he) {
            tfd_text_r(&WK_LABEL, c1, 15, TX(T_NEXTDAYS), C_DIM, c1 - c0);
        } else {
            tfd_text_l(&WK_LABEL, c0, 15, TX(T_NEXTDAYS), C_DIM, c1 - c0);
        }
    }
    char b[16];
    for (int d = 0; d < 4 && d < w->nd; d++) {
        int y = ytop + d * rh - (LNARROW ? 6 : 0);
        if (d) {
            tfd_rect(c0, y, c1 - c0, 1, tfd_rgb(255, 255, 255), 16);
        }
        const char *dn = d == 0 ? TX(T_TODAY) : TX(DAYS[wday_of(w->dt[d] + 43200, 0)]);
        const kfont_t *f = tfd_pick(&WK_TEXT, &WK_TINY, dn, 46);
        if (s_he) {
            tfd_text_r(f, lx(xd, LW, c0), y + 23, dn, C_TEXT, 46);
        } else {
            tfd_text_l(f, c0, y + 23, dn, C_TEXT, 46);
        }
        wicon(w->dcode[d] ? icon_of(w->dcode[d], true) : IC_SUN, 26, lx(xd, LW, c0 + 60), y + 18);
        t_str(b, sizeof b, w->dmax[d]);
        int mw = mf_width(&WF_SMALL, b, 0);
        const int xm = LBIG ? c1 - 80 : c0 + 110;  // (the big desk: the high near the low, at the end side)
        mf_text(&WF_SMALL, s_he ? lx(xd, LW, xm) : xm - mw, y + 24, b, C_TEXT, 255, 0);
        t_str(b, sizeof b, w->dmin[d]);
        mw = mf_width(&WF_SMALL, b, 0);
        mf_text(&WF_SMALL, s_he ? lx(xd, LW, c1) : c1 - mw, y + 24, b, C_DIM, 255, 0);
    }
}
static void draw_page_land(int64_t t)
{
    int64_t now = unix_now();
    s_skw = PW;
    s_skh = LH;
    sky_page();
    if (s_cur >= s_npl) {
        if (band_r(RG_TOP, kmix(41, (int)(t / 33)), 0, 0, LW, LH)) {
            tfd_rect(PW, 0, LW - PW, LH, s_page_c, 256);
            sky_draw(0, 0, LH, (float)t / 1000.0f);
            spinner(PW / 2, 80, 14, t, C_TEXT);
            tfd_text_c(&WK_TEXT, (PW + LW) / 2, 92, TX(s_wifi ? T_FINDING : T_NOWIFI), C_TEXT, LW - PW - 20);
            band_end_r(0, 0, LW, LH);
        }
        return;
    }
    const wx_t *w = &s_wx[s_cur];
    const place_t *pl = &s_pl[s_cur];
    uint32_t base = kmix(kmix(kmix(kmix(31, s_cur), s_data_ver), s_unit * 16 + s_wunit * 2 + s_he), (int)(now / 60));
    base = kmix(base, s_page_c);
    if (band_r(RG_PAGE, base, PW, 0, LW, LH)) {
        tfd_rect(PW, 0, LW - PW, LH, s_page_c, 256);
        if (w->fetched) {
            draw_chart(w, now);
            draw_days4(w, now);
        } else {
            tfd_text_c(&WK_TEXT, (PW + LW) / 2, 92,
                       TX(updating(s_cur) ? T_LOADING
                          : s_wifi        ? T_NOUPD
                                          : T_NOWIFI),
                       C_DIM, LW - PW - 20);
        }
        band_end_r(PW, 0, LW, LH);
    }
    uint32_t tk = kmix(kmix(base, s_motion ? (int)(t / 33) : 0), (int)s_xoff);
    tk = kmix(tk, updating(s_cur) || s_ip_need ? (int)(t / 50) : 0);
    if (band_r(RG_TOP, tk, 0, 0, PW, LH)) {
        sky_draw(0, 0, LH, (float)t / 1000.0f);
        tfd_clip(0, 0, PW, LH);
        draw_hero_land(w, pl, now, t);
        tfd_noclip();
        band_end_r(0, 0, PW, LH);
    }
    // (the toast over the bands, in the middle of the hours)
    int64_t age = ms_now() - s_toast_t;
    if (s_toast[0] && age < 2400) {
        int a = age < 2000 ? 256 : (int)(256 * (2400 - age) / 400);
        int tw = ui_text_w(&WK_TEXT, s_toast);
        if (tw > LW - PW - 40) {
            tw = LW - PW - 40;
        }
        int cx = (PW + DX0) / 2, ty = LNARROW ? 100 : 120;
        tfd_rrect(cx - tw / 2 - 12, ty, tw + 24, 30, 12, tfd_rgb(10, 14, 22), a * 235 / 256);
        if (a > 100) {
            tfd_text_c(&WK_TEXT, cx, ty + 20, s_toast, C_TEXT, LW - PW - 40);
        }
    }
}

// ---------------------------------------------------------------- the lists: the places, the add screen
static void chevron(int cx, int cy, bool left, uint16_t c)
{
    int d = left ? -1 : 1;
    tfd_line(cx - 3 * d, cy - 7, cx + 3 * d, cy, 2.2f, c, 256);
    tfd_line(cx + 3 * d, cy, cx - 3 * d, cy + 7, 2.2f, c, 256);
}
static void list_head(const char *title)
{
    tfd_rect(0, 0, W, LIST_Y, tfd_rgb(16, 22, 34), 256);
    tfd_rect(0, LIST_Y - 1, W, 1, tfd_rgb(255, 255, 255), 30);
    chevron(s_he ? W - 24 : 24, 28, !s_he, C_TEXT);
    const kfont_t *f = tfd_pick(&WK_PLACE, &WK_COND, title, W - 60);
    tfd_text_s(f, s_he ? 8 : 48, s_he ? W - 48 : W - 8, 36, title, C_TEXT, s_he);
}
static void draw_lists(int64_t t)
{
    uint32_t k = kmix(kmix(kmix(kmix(kmix(21, (int)s_lscroll), s_lhot), s_sc * 8 + s_add_tab), s_data_ver), s_he);
    k = kmix(kmix(kmix(k, s_hold_row >= 0 ? (int)((t - s_hold_t) / 40) : -1), toast_key()),
             s_searching ? (int)(t / 50) : 0);
    k = kmix(k, (int)(unix_now() / 60));
    if (!band(RG_LIST, k, 0, H)) {
        return;
    }
    uint16_t bg = tfd_rgb(12, 16, 26);
    tfd_rect(0, LIST_Y, W, H - LIST_Y, bg, 256);
    int top = list_top(), bot = list_bottom(), rh = list_row_h();
    int rows = list_rows();
    tfd_clip(0, top, W, bot);
    for (int r = (int)(s_lscroll / rh) < 0 ? 0 : (int)(s_lscroll / rh); r < rows; r++) {
        int y = top + r * rh - (int)s_lscroll;
        if (y > bot) {
            break;
        }
        if (r == s_lhot) {
            tfd_rect(0, y, W, rh, tfd_rgb(255, 255, 255), 20);
        }
        tfd_rect(12, y + rh - 1, W - 24, 1, tfd_rgb(255, 255, 255), 18);
        if (s_sc == SC_PLACES) {
            const place_t *pl = &s_pl[r];
            const wx_t *w = &s_wx[r];
            // a small window of the sky of the place, with the icon of the condition
            int64_t un = unix_now();
            float el = un ? sun_elev(pl->lat, pl->lon, un) : 30;
            uint16_t c0 = el > 6 ? tfd_rgb(50, 120, 210) : el > -6 ? tfd_rgb(200, 120, 100) : tfd_rgb(20, 28, 64);
            uint16_t c1 = el > 6 ? tfd_rgb(140, 190, 240) : el > -6 ? tfd_rgb(250, 170, 110) : tfd_rgb(40, 50, 96);
            int wx0 = s_he ? W - 50 : 8;
            for (int j = 0; j < 42; j++) {
                tfd_hspan(wx0 + (j < 5 || j > 36 ? 3 : 0), wx0 + 42 - (j < 5 || j > 36 ? 3 : 0), y + 15 + j,
                          tfd_mix(c0, c1, j * 256 / 41), 256);
            }
            tfd_rrect(wx0, y + 15, 42, 42, 9, tfd_rgb(255, 255, 255), 24);
            if (w->fetched) {
                wicon(icon_of(w->code, el > -0.8f), 26, wx0 + 21, y + 36);
            }
            int nx0 = s_he ? 44 : 56, nx1 = s_he ? W - 56 : W - 44;
            char nb[64];
            snprintf(nb, sizeof nb, "%s%s", pl->here ? "\xe2\x80\xa2 " : "", pl->name);
            const kfont_t *f =
                    tfd_pick(&WK_COND, tfd_pick(&WK_TEXT, &WK_LABEL, pl->name, nx1 - nx0), pl->name, nx1 - nx0);
            tfd_text_s(f, nx0, nx1, y + 32, pl->name, r == s_cur ? C_GOLD : C_TEXT, s_he || tfd_heb(pl->name));
            tfd_text_s(&WK_LABEL, nx0, nx1, y + 52, pl->here ? TX(T_MYPLACE) : pl->sub, pl->here ? C_GOLD : C_DIM,
                       s_he || tfd_heb(pl->sub));
            if (w->fetched) {
                char b[16];
                t_str(b, sizeof b, w->t);
                num_end(&WF_NUM, W - 12, y + 42, b, C_TEXT);
            }
            if (s_hold_row == r) {
                float kk = (float)(t - s_hold_t) / 1000.0f;
                if (kk > 0.15f) {
                    tfd_arc((float)(wx0 + 21), (float)(y + 36), 26, 3, -1.5708f, -1.5708f + 6.2832f * (kk > 1 ? 1 : kk),
                            C_GOLD, 256);
                }
            }
        } else if (s_add_tab == 2) {
            const geo_t *g = &s_res[r];
            text_at(&WK_COND, 16, W - 44, y + 25, g->name, C_TEXT);
            text_at(&WK_LABEL, 16, W - 44, y + 44, g->sub, C_DIM);
            bool have = place_find(g->lat, g->lon) >= 0;
            int px = sx(W - 24);
            if (have) {
                tfd_line(px - 6, y + 27, px - 1, y + 32, 2.2f, C_GOLD, 256);
                tfd_line(px - 1, y + 32, px + 7, y + 21, 2.2f, C_GOLD, 256);
            } else {
                tfd_ring((float)px, (float)y + 27, 11, 1.8f, C_DIM, 256);
                tfd_line(px - 5, y + 27, px + 5, y + 27, 2, C_TEXT, 256);
                tfd_line(px, y + 22, px, y + 32, 2, C_TEXT, 256);
            }
        } else if (r == 0) {
            // the search row: a field with a lens
            tfd_rrect(10, y + 8, W - 20, rh - 16, 12, tfd_rgb(32, 42, 60), 256);
            int lx = sx(28);
            tfd_ring((float)lx - 1, (float)y + 25, 6, 1.8f, C_DIM, 256);
            tfd_line(lx + 3, y + 29, lx + 8, y + 34, 2.2f, C_DIM, 256);
            text_at(&WK_TEXT, 42, W - 14, y + 32, TX(T_SEARCH), C_DIM);
        } else {
            int p = add_preset(r);
            if (p < 0) {
                continue;
            }
            text_at(&WK_COND, 16, W - 44, y + 34, s_he ? PRESETS[p].he : PRESETS[p].en, C_TEXT);
            bool have = place_find(PRESETS[p].lat, PRESETS[p].lon) >= 0;
            int px = sx(W - 24);
            if (have) {
                tfd_line(px - 6, y + 27, px - 1, y + 32, 2.2f, C_GOLD, 256);
                tfd_line(px - 1, y + 32, px + 7, y + 21, 2.2f, C_GOLD, 256);
            } else {
                tfd_ring((float)px, (float)y + 27, 11, 1.8f, C_DIM, 256);
                tfd_line(px - 5, y + 27, px + 5, y + 27, 2, C_TEXT, 256);
                tfd_line(px, y + 22, px, y + 32, 2, C_TEXT, 256);
            }
        }
    }
    tfd_noclip();
    if (s_sc == SC_ADD && s_searching) {
        spinner(W / 2, 260, 16, t, C_TEXT);
        tfd_text_c(&WK_TEXT, W / 2, 306, TX(T_SEARCHING), C_DIM, W - 20);
    }
    // the head, the tabs, the bottom (after the rows: a row that scrolls goes under them)
    if (s_sc == SC_PLACES) {
        list_head(TX(T_PLACES));
        tfd_rect(0, H - 70, W, 70, bg, 256);
        tfd_rrect(14, H - 60, W - 28, 46, 23, tfd_rgb(46, 92, 170), 256);
        tfd_rrect(14, H - 60, W - 28, 1, 0, tfd_rgb(255, 255, 255), 60);
        tfd_text_c(&WK_TEXT, W / 2, H - 31, TX(T_ADD), C_TEXT, W - 40);
        if (s_npl > 1) {
            tfd_text_c(&WK_LABEL, W / 2, H - 76, TX(T_HOLD), C_FAINT, W - 16);
        }
    } else {
        list_head(s_add_tab == 2 ? TX(T_SEARCH) : TX(T_ADD));
        tfd_rect(0, LIST_Y, W, 50, bg, 256);
        for (int k = 0; k < 2; k++) {
            int x0 = (k == 0) != (s_he != 0) ? 10 : W / 2 + 2, ww = W / 2 - 12;
            bool on = s_add_tab == k;
            tfd_rrect(x0, LIST_Y + 8, ww, 34, 17, on ? tfd_rgb(46, 92, 170) : tfd_rgb(30, 38, 54), 256);
            const char *lab = TX(k == 0 ? T_ISRAEL : T_WORLD);
            tfd_text_c(tfd_pick(&WK_TEXT, &WK_LABEL, lab, ww - 10), x0 + ww / 2, LIST_Y + 30, lab, on ? C_TEXT : C_DIM,
                       ww - 8);
        }
    }
    draw_toast(H - 100);
    band_end(0, H);
}

// ---------------------------------------------------------------- render
static void render(int64_t now)
{
    tfd_screen();
    tfd_noclip();
    int64_t t = ms_now();
    if (s_sc != s_sc_drawn) {
        bands_reset();
        s_sc_drawn = s_sc;
    }
    if (sysui_active()) {
        s_sys_frames = 3;
    }
    if (s_sys_frames > 0) {
        s_sys_frames--;
        bands_reset();
    }
    if (s_sc == SC_KBD) {
        kbd_render(now);
        return;
    }
    if (s_sc == SC_PLACE) {
        if (s_land) {
            draw_page_land(t);
        } else {
            draw_page(t);
        }
    } else {
        draw_lists(t);
    }
}

// ---------------------------------------------------------------- the menu icon (landscape box of s x s)
#include "tficon/tficon.h"
// the menu icon: the sky in the tile of the utilities (libs/tficon), the sun that turns, a cloud that drifts, drops
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    float t = icon_time();
    float u = s / 100.0f;
    tfi_box_t b = tfi_tile(x, y, s, tfd_rgb(40, 110, 210), tfd_rgb(150, 200, 245));
    tfd_clip(b.x, b.y, b.x + b.w, b.y + b.h);
    float sxp = x + 62 * u, syp = y + 36 * u;
    tfi_glow(sxp, syp, 30 * u, tfd_rgb(255, 210, 120), 110);
    for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f + t * 0.785f;
        tfi_line(sxp + fcos(a) * 19 * u, syp + fsin(a) * 19 * u, sxp + fcos(a) * 25 * u, syp + fsin(a) * 25 * u, 3 * u,
                 true, tfd_rgb(255, 200, 70), 256);
    }
    tfi_disc(sxp, syp, 14 * u, tfd_rgb(255, 232, 130), tfd_rgb(255, 196, 60), 256);
    float cxp = x + (42 + 4 * fsin(t * 3.14159f)) * u, cyp = y + 64 * u;
    uint16_t cl = tfd_rgb(250, 252, 255);
    tfi_disc(cxp - 14 * u, cyp + 4 * u, 12 * u, cl, cl, 256);
    tfi_disc(cxp + 2 * u, cyp - 4 * u, 16 * u, cl, cl, 256);
    tfi_disc(cxp + 18 * u, cyp + 4 * u, 11 * u, cl, cl, 256);
    tfi_rbox((int)(cxp - 26 * u), (int)(cyp + 2 * u), (int)(54 * u), (int)(13 * u), (int)(6 * u), cl, cl, 256);
    tfi_rbox((int)(cxp - 26 * u), (int)(cyp + 12 * u), (int)(54 * u), (int)(3 * u), 1, tfd_rgb(200, 214, 232),
             tfd_rgb(200, 214, 232), 256);
    for (int k = 0; k < 3; k++) {
        float ph = t * 2 + k * 0.33f;
        ph -= (float)(int)ph;
        float dx = cxp + (k - 1) * 14 * u - ph * 4 * u, dy = cyp + (18 + ph * 20) * u;
        tfi_line(dx, dy, dx - 2 * u, dy + 6 * u, 2.4f * u, false, tfd_rgb(70, 140, 255),
                 tfi_qa((int)(256 * (1 - ph)), 3));
    }
    tfi_end();
}
