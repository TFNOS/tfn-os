// HEBREW CALENDAR: THE SCREENS (they read the state of app_hebcal.c; the touch targets of each frame go into s_hit[]).
// The 3.49 (640 x 172): the buttons in a column at the right. The 320 x 240 frame: the buttons in a bar at the bottom.
// In Hebrew the lines start at the right and the week runs from the right (Sunday at the right).

#define C_BG    rgb(8, 10, 20)
#define C_PANEL rgb(16, 20, 36)
#define C_ROW   rgb(12, 15, 28)
#define C_LINE  rgb(32, 40, 62)
#define C_TEXT  rgb(238, 240, 246)
#define C_DIM   rgb(130, 140, 165)
#define C_FAINT rgb(70, 80, 104)
#define C_ACC   rgb(246, 196, 92)  // the colour of the app (the gold of the candles)
#define C_BTN   rgb(28, 34, 56)
#define C_BTN_E rgb(46, 56, 84)
#define C_SHAB  rgb(150, 168, 255)
#define C_FAST  rgb(255, 122, 96)
#define C_RC    rgb(90, 206, 190)
#define C_MOD   rgb(120, 180, 255)
#define C_MINOR rgb(214, 178, 120)

static int s_W, s_H, s_top_h;
static bool s_narrow;
static bool s_rnd;  // the round layout (hc_round.inc)
static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    s_rnd = gfx_round();
    s_narrow = s_W < 480;
    s_top_h = BAR ? 26 : (s_narrow ? 30 : 44);  // (the Tab5, 640 x 360: a bar of 7.5 mm, a BACK key for a thumb)
}
static uint16_t ev_col(int id)
{
    const int k = HC_KIND[id];
    return k & (HK_YT | HK_MAJOR) ? C_ACC
           : k & HK_FAST          ? C_FAST
           : k & HK_MODERN        ? C_MOD
           : k & HK_MINOR         ? C_MINOR
           : k & HK_SHAB          ? C_SHAB
           : k & HK_RC            ? C_RC
                                  : C_DIM;
}

// ---------------------------------------------------------------- the touch targets
enum {
    H_BACK,
    H_CAL,
    H_TIMES,
    H_TODAY,
    H_CELL,
    H_PREV,
    H_NEXT,
    H_CITY,
    H_DAYNEXT
};
typedef struct {
    int16_t x, y, w, h, id, arg;
} hit_t;
#define HITN 96
static hit_t s_hit[HITN];
static int s_nhit;
static void hit(int x, int y, int w, int h, int id, int arg)
{
    if (s_nhit < HITN) {
        s_hit[s_nhit++] = (hit_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)id, (int16_t)arg};
    }
}
// a target in a list that scrolls: only its part between y0 and y1 (no target under 26 px)
static void hit_in(int x, int y, int w, int h, int id, int arg, int y0, int y1)
{
    const int a = y < y0 ? y0 : y, b = y + h > y1 ? y1 : y + h;
    if (b - a >= 26) {
        hit(x, a, w, b - a, id, arg);
    }
}
static int hit_at(int x, int y)
{
    for (int i = s_nhit - 1; i >= 0; i--) {
        const hit_t *h = &s_hit[i];
        if (x >= h->x - 4 && x < h->x + h->w + 4 && y >= h->y - 4 && y < h->y + h->h + 4) {
            return i;
        }
    }
    return -1;
}
static void hit_do(int i)
{
    const hit_t *h = &s_hit[i];
    sfx_ui(SFX_CLICK);
    switch (h->id) {
    case H_BACK:
        if (s_scr == SC_DAY) {
            s_scr = s_day_from;
        } else {
            s_scr = SC_TODAY;
        }
        s_scroll = 0;
        break;
    case H_CAL:
        s_scr = SC_MONTH;
        s_wk = 0;
        s_wk_v = 0;
        break;
    case H_TIMES:
        open_day(s_today, SC_TODAY);
        break;
    case H_TODAY:
        if (s_scr == SC_DAY) {
            s_sel = s_today;
        }
        s_wk = 0;
        s_wk_v = 0;
        break;
    case H_CELL:
        open_day(s_today + h->arg, SC_MONTH);
        break;
    case H_PREV:
        s_sel--;
        s_scroll = 0;
        break;
    case H_NEXT:
        s_sel++;
        s_scroll = 0;
        break;
    case H_DAYNEXT:
        open_day(s_td.next_f ? s_td.next_f : s_today, SC_TODAY);
        break;
    case H_CITY:
        set_city(h->arg);
        s_scr = SC_TODAY;
        break;
    }
}

// ---------------------------------------------------------------- small parts
static void button(int x, int y, int w, int h, const char *label, bool hot, int id, int arg)
{
    tfd_rrect(x, y, w, h, 8, hot ? C_ACC : C_BTN, 256);
    if (!hot) {
        tfd_rrect(x, y, w, 1, 0, C_BTN_E, 256);
    }
    const kfont_t *f = ui_text_w(&KF_NAME, label) <= w - 10 ? &KF_NAME : &KF_LABEL;
    tfd_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, label, hot ? C_BG : C_TEXT, w - 8);
    hit(x, y, w, h, id, arg);
}
static void side_buttons(const char *const *l, const int *id, const bool *hot, int n)
{
    if (s_narrow) {
        const int g = 6, w = (s_W - (n + 1) * g) / n, y = s_H - 42;
        tfd_rect(0, y - 4, s_W, 46, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            const int k = s_he ? n - 1 - i : i;  // (Hebrew: the first button at the right)
            button(g + k * (w + g), y, w, 38, l[i], hot[i], id[i], 0);
        }
    } else {
        const int x = s_W - 100, y0 = (s_scr == SC_TODAY ? 4 : s_top_h + 4), g = 4;
        const int h = (s_H - y0 - 2 - (n - 1) * g) / n;
        tfd_rect(s_W - 104, y0 - 3, 104, s_H - y0 + 3, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            button(x, y0 + i * (h + g), 96, h, l[i], hot[i], id[i], 0);
        }
    }
}
static void top_bar(const char *title, const char *right)
{
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    tfd_rect(0, s_top_h, s_W, 1, C_LINE, 256);
    const int base = s_top_h / 2 + 6;
    tfd_rrect(4, 3, 64, s_top_h - 6, 6, C_BTN, 256);
    tfd_text_c(&KF_LABEL, 36, base - 1, TX(T_BACK), C_TEXT, 60);
    hit(0, 0, 80, s_top_h + 4, H_BACK, 0);
    tfd_text_l(&KF_NAME, 80, base, title, C_TEXT, s_narrow ? s_W - 90 : s_W / 2 - 110);  // (the 3.49: left of the grip)
    if (right && right[0] && !s_narrow) {
        tfd_text_r(&KF_LABEL, s_W - 110, base, right, C_DIM, s_W / 2 - 150);
    }
}
// the content: x0 .. x1 (the 3.49: left of the buttons)
static int cx1(void)
{
    return s_narrow ? s_W : s_W - 104;
}
// a line at the start side (the left in English, the right in Hebrew)
static void text_s(const kfont_t *f, int x0, int x1, int base, const char *s, uint16_t c)
{
    tfd_text_s(f, x0, x1, base, s, c, s_he);
}
static void hm_text(char *o, int n, float t)
{
    if (t < 0) {
        snprintf(o, (size_t)n, "--:--");
        return;
    }
    int r = hc_round(t);
    r = ((r % 1440) + 1440) % 1440;
    snprintf(o, (size_t)n, "%d:%02d", r / 60, r % 60);
}

// ---------------------------------------------------------------- TODAY
static void draw_today(void)
{
    const int x0 = 12, x1 = cx1() - 12, w = x1 - x0;
    const hc_city_t *city = &HC_CITIES[s_city];
    char t[96], u[64];
    // the city and the time (small), the date
    if (s_time_ok) {
        snprintf(t, sizeof t, "%s  %d:%02d", city->name[s_he], s_min_now / 60, s_min_now % 60);
        text_s(&KF_LABEL, x0, x1, 18, t, C_DIM);
    } else {
        text_s(&KF_LABEL, x0, x1, 18, city->name[s_he], C_DIM);
    }
    if (!s_time_ok) {
        tfd_text_c(&KF_BIG, x0 + w / 2, s_narrow ? 90 : 76, TX(T_NOTSET), C_ACC, w);
        tfd_text_c(&KF_LABEL, x0 + w / 2, s_narrow ? 116 : 104, TX(T_NOTSET2), C_DIM, w);
    } else {
        today_calc();
        // the Hebrew date (big)
        heb_date_he(s_today, t, sizeof t);
        const kfont_t *big = tfd_pick(&KF_TITLE, &KF_BIG, t, w);
        tfd_text_c(big, x0 + w / 2, s_narrow ? 66 : 60, t, C_TEXT, w);
        // the date of the world: the 3.49 at the end side of the top line, the 320 frame under the Hebrew date
        greg_text(s_today, u, sizeof u, true);
        if (s_narrow) {
            tfd_text_c(&KF_LABEL, x0 + w / 2, 88, u, C_DIM, w);
        } else if (s_he) {
            tfd_text_l(&KF_LABEL, x0, 18, u, C_DIM, w / 2 + 40);
        } else {
            tfd_text_r(&KF_LABEL, x1, 18, u, C_DIM, w / 2 + 40);
        }
        // the Hebrew date in Latin letters (English), and after the sunset the date of the night
        const hc_zman_t *z = zman(s_today);
        const bool night = z->t[HZ_SUNSET] >= 0 && s_min_now >= hc_round(z->t[HZ_SUNSET]);
        t[0] = 0;
        int p = 0;
        if (!s_he && !(s_narrow && night)) {  // (the 320 frame at night: only the date of the night)
            heb_date_en(s_today, u, sizeof u);
            p = snprintf(t, sizeof t, "%s", u);
        }
        if (night) {
            char h[40];
            if (s_he) {
                heb_date_he(s_today + 1, h, sizeof h);
            } else {
                int ny, nm, nd;
                hc_heb(s_today + 1, &ny, &nm, &nd);
                snprintf(h, sizeof h, "%d %s", nd, hmonth(ny, nm));
            }
            snprintf(t + p, sizeof t - (size_t)p, "%s", p ? "   -   " : "");
            p = (int)strlen(t);
            snprintf(t + p, sizeof t - (size_t)p, TX(T_TONIGHT), h);
        }
        if (t[0]) {
            tfd_text_c(&KF_LABEL, x0 + w / 2, s_narrow ? 104 : 80, t, night && s_he ? C_FAINT : C_DIM, w);
        }
        // the events of today, else the next holiday (a tap: its day)
        hc_ev_t e[12];
        const int n = day_events(s_today, e, 12);
        int y, m, d;
        hc_heb(s_today, &y, &m, &d);
        t[0] = 0;
        uint16_t col = C_DIM;
        p = 0;
        for (int i = 0; i < n && i < 3; i++) {
            if (e[i].id == HE_OMER && i) {
                break;
            }
            char one[48];
            ev_text(&e[i], y, one, sizeof one);
            if (!p) {
                col = ev_col(e[i].id);
            }
            p += snprintf(t + p, sizeof t - (size_t)p, "%s%s", p ? "  -  " : "", one);
            if (p >= (int)sizeof t - 1) {
                break;
            }
        }
        // Chanukah: the candles of tonight
        {
            hc_ev_t e1[12];
            const int n1 = hc_events(s_today + 1, israel(), e1, 12);
            for (int i = 0; i < n1; i++) {
                if (e1[i].id == HE_CHANUKAH && p < (int)sizeof t - 48) {
                    char one[48];
                    if (e1[i].n == 1) {
                        snprintf(one, sizeof one, "%s", TX(T_CANDLE_1));
                    } else {
                        snprintf(one, sizeof one, TX(T_CANDLE_N), e1[i].n);
                    }
                    if (!p) {
                        col = C_ACC;
                    }
                    p += snprintf(t + p, sizeof t - (size_t)p, "%s%s", p ? "  -  " : "", one);
                }
            }
        }
        const int ey = s_narrow ? 122 : 102;
        if (t[0]) {
            tfd_text_c(tfd_pick(&KF_NAME, &KF_LABEL, t, w), x0 + w / 2, ey, t, col, w);
        } else if (s_td.next[0]) {
            tfd_text_c(&KF_NAME, x0 + w / 2, ey, s_td.next, C_DIM, w);
            hit(x0, ey - 24, w, 36, H_DAYNEXT, 0);
        }
        // THE NEXT CANDLE LIGHTING AND HAVDALAH (a box)
        if (s_td.cf) {
            const int by = s_narrow ? 130 : 116, bh = s_narrow ? 60 : 52, bx = x0 - 4, bw = w + 8;
            tfd_rrect(bx, by, bw, bh, 10, C_PANEL, 256);
            tfd_rrect(s_he ? bx + bw - 4 : bx, by, 4, bh, 2, C_ACC, 256);
            char ct[12], ht[12], a[48];
            hm_text(ct, sizeof ct, s_td.ct);
            hm_text(ht, sizeof ht, s_td.ht);
            // the day of the candles when it is not today ("FRI", "ו'")
            if (s_td.cf != s_today) {
                snprintf(a, sizeof a, "%s  %s", WDAY_S[hc_dow(s_td.cf)][s_he], ct);
            } else {
                snprintf(a, sizeof a, "%s", ct);
            }
            const char *cl = TX(s_td.cnight ? T_CANDLES_NIGHT : T_CANDLES);
            if (s_narrow) {
                tfd_text_c(&KF_NAME, bx + bw / 2, by + 20, s_td.occasion, C_ACC, bw - 16);
                const int c1 = bx + bw / 4, c2 = bx + bw * 3 / 4;
                tfd_text_c(&KF_LABEL, s_he ? c2 : c1, by + 36, cl, C_DIM, bw / 2 - 8);
                tfd_text_c(&KF_NAME, s_he ? c2 : c1, by + 56, a, C_TEXT, bw / 2 - 8);
                tfd_text_c(&KF_LABEL, s_he ? c1 : c2, by + 36, TX(T_HAVDALAH), C_DIM, bw / 2 - 8);
                snprintf(a, sizeof a, "%s  %s", WDAY_S[hc_dow(s_td.hf)][s_he], ht);
                tfd_text_c(&KF_NAME, s_he ? c1 : c2, by + 56, a, C_TEXT, bw / 2 - 8);
            } else {
                // the occasion at the start side, the two times at the end side
                const int tw2 = 130, s0 = s_he ? bx + 2 * tw2 + 8 : bx + 14,
                          s1 = s_he ? bx + bw - 14 : bx + bw - 2 * tw2 - 8;
                text_s(&KF_NAME, s0, s1, by + 32, s_td.occasion, C_ACC);
                const int c1 = s_he ? bx + tw2 * 3 / 2 + 4 : bx + bw - tw2 * 3 / 2 - 4;
                const int c2 = s_he ? bx + tw2 / 2 + 4 : bx + bw - tw2 / 2 - 4;
                tfd_text_c(&KF_LABEL, c1, by + 18, cl, C_DIM, tw2 - 6);
                tfd_text_c(&KF_NAME, c1, by + 42, a, C_TEXT, tw2 - 6);
                tfd_text_c(&KF_LABEL, c2, by + 18, TX(T_HAVDALAH), C_DIM, tw2 - 6);
                snprintf(a, sizeof a, "%s  %s", WDAY_S[hc_dow(s_td.hf)][s_he], ht);
                tfd_text_c(&KF_NAME, c2, by + 42, a, C_TEXT, tw2 - 6);
            }
        }
    }
    if (s_time_ok && !s_narrow && !BAR) {
        // the Tab5 (640 x 360): the times of today in 3 rows of 3 cards under the box (the next one is lit; a tap
        // opens the day)
        const hc_zman_t *z = zman(s_today);
        const int g = 8, cw = (w - 2 * g) / 3, ch = 52, y0 = 178;
        int next_i = -1;
        for (int i = 0; i < HZ_N; i++) {
            if (z->t[i] >= 0 && hc_round(z->t[i]) > s_min_now && (next_i < 0 || z->t[i] < z->t[next_i])) {
                next_i = i;
            }
        }
        for (int i = 0; i < HZ_N; i++) {
            const int c = s_he ? 2 - i % 3 : i % 3, x = x0 + c * (cw + g), y = y0 + (i / 3) * (ch + 6);
            tfd_rrect(x, y, cw, ch, 10, C_PANEL, 256);
            if (i == next_i) {
                tfd_rrect(s_he ? x + cw - 4 : x, y, 4, ch, 2, C_ACC, 256);
            }
            char tm[12];
            hm_text(tm, sizeof tm, z->t[i]);
            tfd_text_c(&KF_LABEL, x + cw / 2, y + 20, ZNAME[i][s_he], C_DIM, cw - 12);
            tfd_text_c(&KF_NAME, x + cw / 2, y + 42, tm, i == next_i ? C_ACC : C_TEXT, cw - 12);
        }
        hit(x0, y0, w, 3 * ch + 12, H_TIMES, 0);
    }
    const char *l[2] = {TX(T_CALENDAR), TX(T_TIMES)};
    const int id[2] = {H_CAL, H_TIMES};
    const bool hot[2] = {true, false};
    side_buttons(l, id, hot, 2);
}

// ---------------------------------------------------------------- CALENDAR: the weeks
static int row_h(void)
{
    if (s_rnd) {
        return gfx_w * 50 / 466;
    }
    return s_narrow ? 37 : 36;
}
static void draw_month(void)
{
    const int x1 = cx1(), hdr = 16, y0 = s_top_h + 1 + hdr, y1 = s_narrow ? s_H - 46 : s_H;
    const int rh = row_h(), cw = x1 / 7;
    const bool il = israel();
    // the first row: the Sunday of the week before today + s_wk weeks
    const int64_t base = s_today - hc_dow(s_today) - 7;
    const int wk = (int)floor(s_wk);
    const int dy = (int)((s_wk - (float)wk) * (float)rh);
    tfd_clip(0, y0, x1, y1);
    int64_t first = 0, last = 0;
    for (int r = 0; y0 + r * rh - dy < y1; r++) {
        const int ry = y0 + r * rh - dy;
        for (int c = 0; c < 7; c++) {
            const int64_t f = base + (int64_t)(wk + r) * 7 + c;
            if (!first) {
                first = f;
            }
            last = f;
            const int x = (s_he ? 6 - c : c) * cw;
            int gy, gm, gd, hy, hmn, hd;
            hc_greg(f, &gy, &gm, &gd);
            hc_heb(f, &hy, &hmn, &hd);
            const bool odd = gm & 1;
            tfd_rect(x, ry, cw - 1, rh - 1, c == 6 ? rgb(20, 24, 46) : odd ? C_ROW : C_BG, 256);
            if (f == s_today) {
                tfd_rrect(x + 1, ry + 1, cw - 3, rh - 3, 6, C_ACC, 70);
                tfd_rrect(x + 1, ry + 1, cw - 3, 2, 1, C_ACC, 256);
            }
            char a[24], b[24];
            // the day of the month (the first day: with the month), the Hebrew day (the first day: the month)
            if (gd == 1 && !s_narrow) {
                snprintf(a, sizeof a, "%d %s", gd, s_he ? GMONTH[gm - 1][1] : GMON3[gm - 1]);
            } else {
                snprintf(a, sizeof a, "%d", gd);
            }
            if (hd == 1) {
                const bool he = s_he;
                s_he = true;
                snprintf(b, sizeof b, "%s", hmonth(hy, hmn));
                s_he = he;
            } else {
                hc_gematria(hd, b, sizeof b);
            }
            const kfont_t *fa = gd == 1 && !s_narrow ? &KF_LABEL : &KF_NAME;
            if (s_he) {
                tfd_text_r(fa, x + cw - 5, ry + 16, a, gd == 1 ? C_ACC : C_TEXT, cw - 8);
                tfd_text_l(&KF_LABEL, x + 4, ry + rh - 8, b, hd == 1 ? C_RC : C_DIM, cw - 8);
            } else {
                tfd_text_l(fa, x + 5, ry + 16, a, gd == 1 ? C_ACC : C_TEXT, cw - 8);
                tfd_text_r(&KF_LABEL, x + cw - 4, ry + rh - 8, b, hd == 1 ? C_RC : C_DIM, cw - 8);
            }
            // the mark of the most important event (no Omer, no eve)
            hc_ev_t e[12];
            const int n = hc_events(f, il, e, 12);
            int best = -1;
            for (int i = 0; i < n; i++) {
                if (e[i].id == HE_OMER || (HC_KIND[e[i].id] & HK_EREV)) {
                    continue;
                }
                if (best < 0 || ev_rank(e[i].id) < ev_rank(e[best].id)) {
                    best = i;
                }
            }
            if (best >= 0) {
                const int mx = s_he ? x + cw - 16 : x + 6;
                tfd_rrect(mx, ry + rh - 9, 10, 4, 2, ev_col(e[best].id), 256);
            }
            hit_in(x, ry, cw - 1, rh - 1, H_CELL, (int)(f - s_today), y0, y1);
        }
    }
    tfd_noclip();
    // the names of the days (after the rows: the text of a row that goes under the top is not clipped)
    tfd_rect(0, s_top_h + 1, x1, hdr, C_PANEL, 256);
    for (int c = 0; c < 7; c++) {
        const int col = s_he ? 6 - c : c;
        tfd_text_c(&KF_LABEL, col * cw + cw / 2, s_top_h + 13, WDAY_S[c][s_he], c == 6 ? C_SHAB : C_DIM, cw - 4);
    }
    if (s_narrow) {
        tfd_rect(0, y1, s_W, s_H - y1, C_BG, 256);
    }
    // the title: the month of the middle of the view; the Hebrew months at the right
    const int64_t mid = first + (last - first) / 2;
    int gy, gm, gd, hy1, hm1, hd1, hy2, hm2, hd2;
    hc_greg(mid, &gy, &gm, &gd);
    hc_heb(first, &hy1, &hm1, &hd1);
    hc_heb(last, &hy2, &hm2, &hd2);
    char t[48], r[64], yr[16];
    snprintf(t, sizeof t, "%s %d", GMONTH[gm - 1][s_he], gy);
    if (s_he) {
        hc_gematria(hy2, yr, sizeof yr);
    } else {
        snprintf(yr, sizeof yr, "%d", hy2);
    }
    if (hm1 != hm2) {
        snprintf(r, sizeof r, "%s-%s %s", hmonth(hy1, hm1), hmonth(hy2, hm2), yr);
    } else {
        snprintf(r, sizeof r, "%s %s", hmonth(hy2, hm2), yr);
    }
    top_bar(t, r);
    if (s_narrow) {  // (the bar: TODAY at the end side, the Hebrew months at the start)
        const int y = s_H - 42, bw = 110, bx = s_he ? 6 : s_W - 6 - bw;
        tfd_rect(0, y - 4, s_W, 46, C_PANEL, 256);
        button(bx, y, bw, 38, TX(T_TODAY), false, H_TODAY, 0);
        text_s(&KF_LABEL, s_he ? bw + 16 : 10, s_he ? s_W - 10 : s_W - bw - 16, y + 24, r, C_DIM);
    } else {
        const char *l[1] = {TX(T_TODAY)};
        const int id[1] = {H_TODAY};
        const bool hot[1] = {false};
        side_buttons(l, id, hot, 1);
    }
}

// ---------------------------------------------------------------- DAY
// the rows of the times: candle lighting, havdalah, the fast, then the times of the day
typedef struct {
    const char *name;
    float t;
    uint16_t col;
} trow_t;
static int day_rows(trow_t *o)
{
    hc_times_t tt;
    day_times(s_sel, &tt);
    const hc_zman_t *z = zman(s_sel);
    int n = 0;
    if (tt.fast_begin >= 0) {
        o[n++] = (trow_t){TX(T_FAST_BEGINS), tt.fast_begin, C_FAST};
    }
    if (tt.candles >= 0) {
        o[n++] = (trow_t){TX(tt.candles_night ? T_CANDLES_NIGHT : T_CANDLES), tt.candles, C_ACC};
    }
    if (tt.havdalah >= 0) {
        o[n++] = (trow_t){TX(T_HAVDALAH), tt.havdalah, C_ACC};
    }
    if (tt.fast_end >= 0) {
        o[n++] = (trow_t){TX(T_FAST_ENDS), tt.fast_end, C_FAST};
    }
    for (int i = 0; i < HZ_N; i++) {
        o[n++] = (trow_t){ZNAME[i][s_he], z->t[i], C_TEXT};
    }
    return n;
}
static void draw_day(void)
{
    const int x1 = cx1(), y0 = s_top_h + 1, y1 = s_narrow ? s_H - 46 : s_H;
    char t[96], u[64];
    int hy, hmn, hd;
    hc_heb(s_sel, &hy, &hmn, &hd);
    hc_ev_t e[12];
    const int n = day_events(s_sel, e, 12);
    const int par = hc_parsha(s_sel, israel());
    // the lines of the information: the Hebrew date, the reading, the events
    const int ix0 = 10, ix1 = s_narrow ? x1 - 10 : 262;
    const int lx = s_narrow ? 0 : ix1 + 8;  // the list of the times
    const int sc = (int)s_scroll;
    int yy = y0 - (s_narrow ? sc : 0);
    if (!s_narrow) {
        tfd_rect(ix1 + 4, y0, 1, s_H - y0, C_LINE, 256);
    }
    tfd_clip(0, y0, x1, y1);
    heb_date_he(s_sel, t, sizeof t);
    tfd_text_s(tfd_pick(&KF_BIG, &KF_NAME, t, ix1 - ix0), ix0, ix1, yy + 30, t, C_TEXT, s_he);
    yy += 36;
    if (!s_he) {
        heb_date_en(s_sel, t, sizeof t);
        text_s(&KF_LABEL, ix0, ix1, yy + 10, t, C_DIM);
        yy += 18;
    }
    if (par) {
        parsha_text(par, u, sizeof u);
        snprintf(t, sizeof t, "%s %s", s_he ? "פרשת" : "PARASHAT", u);
        text_s(&KF_NAME, ix0, ix1, yy + 16, t, C_SHAB);
        yy += 21;
    }
    for (int i = 0; i < n && yy < (s_narrow ? y1 + sc + 400 : y1); i++) {
        ev_text(&e[i], hy, t, sizeof t);
        text_s(&KF_NAME, ix0, ix1, yy + 16, t, ev_col(e[i].id));
        yy += 21;
    }
    // Chanukah: the candles of tonight
    {
        hc_ev_t e1[12];
        const int n1 = hc_events(s_sel + 1, israel(), e1, 12);
        for (int i = 0; i < n1; i++) {
            if (e1[i].id == HE_CHANUKAH) {
                if (e1[i].n == 1) {
                    snprintf(t, sizeof t, "%s", TX(T_CANDLE_1));
                } else {
                    snprintf(t, sizeof t, TX(T_CANDLE_N), e1[i].n);
                }
                text_s(&KF_LABEL, ix0, ix1, yy + 14, t, C_ACC);
                yy += 19;
            }
        }
    }
    if (!n && !par) {
        text_s(&KF_LABEL, ix0, ix1, yy + 14, TX(T_NONE), C_FAINT);
        yy += 19;
    }
    // the times
    trow_t rows[16];
    const int nr = day_rows(rows);
    const int rh = s_narrow ? 26 : (s_H >= 300 ? 30 : 23);  // (the Tab5: taller rows)
    int ry = s_narrow ? yy + 8 : y0 + 4 - sc;
    const int lw = x1 - lx;
    const int now_m = s_sel == s_today ? s_min_now : -1;
    int next_i = -1;
    for (int i = 0; i < nr; i++) {
        if (now_m >= 0 && rows[i].t >= 0 && hc_round(rows[i].t) > now_m && (next_i < 0 || rows[i].t < rows[next_i].t)) {
            next_i = i;
        }
    }
    for (int i = 0; i < nr; i++, ry += rh) {
        if (ry + rh < y0 || ry > y1) {
            continue;
        }
        tfd_rect(lx, ry, lw, rh - 1, i & 1 ? C_ROW : C_BG, 256);
        if (i == next_i) {
            tfd_rect(s_he ? lx + lw - 3 : lx, ry, 3, rh - 1, C_ACC, 256);
        }
        char tm[12];
        hm_text(tm, sizeof tm, rows[i].t);
        const uint16_t c = rows[i].col == C_TEXT ? C_DIM : rows[i].col;
        if (s_he) {
            tfd_text_r(&KF_LABEL, lx + lw - 10, ry + rh / 2 + 5, rows[i].name, c, lw - 80);
            tfd_text_l(&KF_NAME, lx + 10, ry + rh / 2 + 6, tm, rows[i].col, 70);
        } else {
            tfd_text_l(&KF_LABEL, lx + 10, ry + rh / 2 + 5, rows[i].name, c, lw - 80);
            tfd_text_r(&KF_NAME, lx + lw - 10, ry + rh / 2 + 6, tm, rows[i].col, 70);
        }
    }
    tfd_noclip();
    // the scroll limit
    const int bottom = ry + sc;
    const float mx = (float)(bottom - y1 + 6);
    if (s_scroll > mx) {
        s_scroll = mx > 0 ? mx : 0;
    }
    if (s_he) {
        greg_text(s_sel, t, sizeof t, true);
    } else {
        int gy, gm, gd;
        hc_greg(s_sel, &gy, &gm, &gd);
        snprintf(t, sizeof t, "%s %d %s %d", WDAY_S[hc_dow(s_sel)][0], gd, GMONTH[gm - 1][0], gy);
    }
    top_bar(t, HC_CITIES[s_city].name[s_he]);
    const char *l[3] = {TX(T_PREV), TX(T_NEXT), TX(T_TODAY)};
    const int id[3] = {H_PREV, H_NEXT, H_TODAY};
    const bool hot[3] = {false, false, false};
    side_buttons(l, id, hot, s_sel == s_today ? 2 : 3);
}

#include "hc_round.inc"

// ---------------------------------------------------------------- CITIES
static void draw_cities(void)
{
    const int y0 = s_top_h + 1, rh = 40, x1 = s_W;
    const int sc = (int)s_scroll;
    tfd_clip(0, y0, x1, s_H);
    for (int i = 0; i < HC_NCITY; i++) {
        const int y = y0 + i * rh - sc;
        if (y + rh < y0 || y > s_H) {
            continue;
        }
        const hc_city_t *c = &HC_CITIES[i];
        tfd_rect(0, y, x1, rh - 1, i == s_city ? rgb(40, 36, 24) : i & 1 ? C_ROW : C_BG, 256);
        text_s(&KF_NAME, 14, x1 - 14, y + rh / 2 + 6, c->name[s_he], i == s_city ? C_ACC : C_TEXT);
        const char *k = c->israel ? TX(T_ISRAEL) : TX(T_DIASPORA);
        if (s_he) {
            tfd_text_l(&KF_LABEL, 14, y + rh / 2 + 5, k, C_DIM, 120);
        } else {
            tfd_text_r(&KF_LABEL, x1 - 14, y + rh / 2 + 5, k, C_DIM, 120);
        }
        hit_in(0, y, x1, rh - 1, H_CITY, i, y0, s_H);
    }
    tfd_noclip();
    const float mx = (float)(HC_NCITY * rh - (s_H - y0));
    if (s_scroll > mx) {
        s_scroll = mx > 0 ? mx : 0;
    }
    top_bar(TX(T_CHOOSE_CITY), NULL);
}

static void render(int64_t now)
{
    (void)now;
    layout();
    tfd_screen();
    tfd_noclip();
    s_nhit = 0;
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    if (s_rnd) {
        switch (s_scr) {
        case SC_MONTH:
            rnd_month();
            break;
        case SC_DAY:
            rnd_day();
            break;
        case SC_CITIES:
            rnd_cities();
            break;
        default:
            rnd_today();
            break;
        }
        return;
    }
    switch (s_scr) {
    case SC_MONTH:
        draw_month();
        break;
    case SC_DAY:
        draw_day();
        break;
    case SC_CITIES:
        draw_cities();
        break;
    default:
        draw_today();
        break;
    }
}

// ---------------------------------------------------------------- the menu icon
#include "tficon/tficon.h"
// one moon of the phase f (0 = new, 0.5 = full; a loop): the dark disc, then the lit part row by row
static void hc_icon_moon(float cx, float cy, float r, float f, uint16_t lit, uint16_t dark)
{
    tfi_disc(cx, cy, r, dark, dark, 256);
    float c = fcos(f * 6.2831853f);  // (the terminator: +1 at new, -1 at full)
    bool wax = f < 0.5f;
    for (int y = tfd_fl(cy - r); y <= tfd_ce(cy + r); y++) {
        float dy = y + 0.5f - cy, h2 = r * r - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = sqrtf(h2), xa = wax ? cx + hw * c : cx - hw, xb = wax ? cx + hw : cx - hw * c;
        if (xb - xa < 0.05f) {
            continue;
        }
        int ia = tfd_fl(xa), ib = tfd_fl(xb);
        for (int xx = ia; xx <= ib; xx++) {
            float cov = tfd_min(xx + 1.0f, xb) - tfd_max((float)xx, xa);
            if (cov > 0) {
                tfd_px(xx, y, lit, cov >= 1 ? 256 : (int)(256 * cov));
            }
        }
    }
}
// the menu icon: a calendar page; on it the moon goes through its month (the Hebrew months follow the moon)
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x5c9cff), tfi_hex(0x1a3d9e));
    float u = s / 100.0f, ph = tfi_ph(2);
    int px = x + (int)(18 * u), py = y + (int)(20 * u), pw = (int)(64 * u), phh = (int)(64 * u);
    tfi_drop(px, py, pw, phh, (int)(8 * u), (int)(3 * u), 80);
    tfi_rbox(px, py, pw, phh, (int)(8 * u), rgb(255, 255, 255), tfi_hex(0xe4e8f2), 256);
    tfi_rbox(px, py, pw, (int)(17 * u), (int)(8 * u), tfi_hex(0xff6a6a), tfi_hex(0xd8283c), 256);
    tfd_rect(px, py + (int)(9 * u), pw, (int)(8 * u), tfi_hex(0xd8283c), 256);
    for (int k = 0; k < 2; k++) {
        tfi_rbox(px + (int)((16 + k * 26) * u), py - (int)(5 * u), (int)(6 * u), (int)(12 * u), (int)(3 * u),
                 tfi_hex(0xf0f0f6), tfi_hex(0x9aa0b4), 256);
    }
    // the moon and its glow
    float full = 0.5f - 0.5f * fcos(ph * 6.2831853f);
    tfi_glow(x + 50 * u, y + 60 * u, 22 * u, tfi_hex(0xffe9a0), (int)(150 * full));
    hc_icon_moon(x + 50 * u, y + 60 * u, 15 * u, ph, tfi_hex(0xffd23f), tfi_hex(0x2b3a66));
    tfi_end();
}
