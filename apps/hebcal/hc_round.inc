// HEBREW CALENDAR: THE ROUND LAYOUT (APP_ROUND_HI: the frame of the round panel, 466 x 466 on the 1.75C, 412 on the
// Watcher). The sizes come from gfx_w (the layout of 466, scaled). The top cap has the BACK key and the title, the
// wide middle band has the content, and the bottom cap has the keys. Each line of a list gets the width of the
// circle at its height (the chord).

// a y (or a size) of the layout of 466, scaled to this frame
static int rv(int v)
{
    return v * s_W / 466;
}
// the half width of the circle at the row y (0 out of the circle)
static int rc_half(int y)
{
    const int R = s_W / 2, dy = y - s_H / 2, h2 = R * R - dy * dy;
    return h2 > 0 ? (int)sqrtf((float)h2) : 0;
}
// the half width of a band from y0 to y1 that is all inside the circle
static int rc_band(int y0, int y1)
{
    const int a = rc_half(y0), b = rc_half(y1);
    return a < b ? a : b;
}

// a round key (the BACK key, the arrows): a disc with a ring and a glyph
static void rnd_key(int cx, int cy, int r, const char *glyph, bool hot, int id, int arg)
{
    tfd_disc((float)cx, (float)cy, (float)r, hot ? C_ACC : C_BTN, 256);
    tfd_ring((float)cx, (float)cy, (float)r - 0.5f, 1.5f, hot ? C_ACC : C_BTN_E, 256);
    tfd_text_c(&KF_BIG, cx, cy + KF_BIG.asc / 2 - 1, glyph, hot ? C_BG : C_TEXT, 2 * r);
    hit(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8, id, arg);
}
// the top cap: the BACK key at the top, the title and a second line under it
static void rnd_top(const char *title, const char *sub)
{
    const int cx = s_W / 2, yb = rv(118);
    tfd_rect(0, 0, s_W, yb, C_PANEL, 256);
    tfd_rect(0, yb, s_W, 1, C_LINE, 256);
    rnd_key(cx, rv(44), rv(25) < 24 ? 24 : rv(25), s_he ? ">" : "<", false, H_BACK, 0);
    const int y1 = rv(90);
    tfd_text_c(tfd_pick(&KF_BIG, &KF_NAME, title, 2 * rc_half(y1 - 24) - 40), cx, y1, title, C_TEXT,
               2 * rc_half(y1 - 24) - 40);
    if (sub && sub[0]) {
        tfd_text_c(&KF_TEXT, cx, rv(112), sub, C_DIM, 2 * rc_half(rv(96)) - 40);
    }
}
// the keys of the bottom cap: n keys in one row, centred, inside the circle
static void rnd_bottom(const char *const *l, const int *id, const bool *hot, int n)
{
    const int y = rv(360), h = rv(52) < 48 ? 48 : rv(52), g = 8;
    const int half = rc_band(y, y + h) - 14;
    int w = (2 * half - (n - 1) * g) / n;
    if (w > 170) {
        w = 170;
    }
    const int x0 = s_W / 2 - (n * w + (n - 1) * g) / 2;
    for (int i = 0; i < n; i++) {
        const int k = s_he ? n - 1 - i : i;  // (Hebrew: the first key at the right)
        button(x0 + k * (w + g), y, w, h, l[i], hot[i], id[i], 0);
    }
}

// ---------------------------------------------------------------- TODAY
static void rnd_today_box(int by)
{
    const int cx = s_W / 2, bh = rv(104);
    const int half = rc_band(by, by + bh) - 12, bx = cx - half, bw = 2 * half;
    tfd_rrect(bx, by, bw, bh, 16, C_PANEL, 256);
    tfd_rrect(bx + 20, by, bw - 40, 3, 1, C_ACC, 256);
    char ct[12], ht[12], a[48];
    hm_text(ct, sizeof ct, s_td.ct);
    hm_text(ht, sizeof ht, s_td.ht);
    tfd_text_c(&KF_NAME, cx, by + rv(28), s_td.occasion, C_ACC, bw - 40);
    const int c1 = cx - bw / 4, c2 = cx + bw / 4, ca = s_he ? c2 : c1, cb = s_he ? c1 : c2;
    tfd_text_c(&KF_TEXT, ca, by + rv(56), TX(s_td.cnight ? T_CANDLES_NIGHT : T_CANDLES), C_DIM, bw / 2 - 12);
    tfd_text_c(&KF_TEXT, cb, by + rv(56), TX(T_HAVDALAH), C_DIM, bw / 2 - 12);
    if (s_td.cf != s_today) {
        snprintf(a, sizeof a, "%s %s", WDAY_S[hc_dow(s_td.cf)][s_he], ct);
    } else {
        snprintf(a, sizeof a, "%s", ct);
    }
    tfd_text_c(tfd_pick(&KF_BIG, &KF_NAME, a, bw / 2 - 12), ca, by + rv(90), a, C_TEXT, bw / 2 - 12);
    snprintf(a, sizeof a, "%s %s", WDAY_S[hc_dow(s_td.hf)][s_he], ht);
    tfd_text_c(tfd_pick(&KF_BIG, &KF_NAME, a, bw / 2 - 12), cb, by + rv(90), a, C_TEXT, bw / 2 - 12);
    tfd_rect(cx, by + rv(40), 1, bh - rv(52), C_LINE, 256);
}
// the events of today (else the next holiday) and the candles of Chanukah, in one line
static uint16_t rnd_events(char *t, int tn)
{
    hc_ev_t e[12];
    const int n = day_events(s_today, e, 12);
    int y, m, d;
    hc_heb(s_today, &y, &m, &d);
    uint16_t col = C_DIM;
    int p = 0;
    t[0] = 0;
    for (int i = 0; i < n && i < 3; i++) {
        if (e[i].id == HE_OMER && i) {
            break;
        }
        char one[48];
        ev_text(&e[i], y, one, sizeof one);
        if (!p) {
            col = ev_col(e[i].id);
        }
        p += snprintf(t + p, (size_t)(tn - p), "%s%s", p ? "  -  " : "", one);
        if (p >= tn - 1) {
            return col;
        }
    }
    hc_ev_t e1[12];
    const int n1 = hc_events(s_today + 1, israel(), e1, 12);
    for (int i = 0; i < n1; i++) {
        if (e1[i].id == HE_CHANUKAH && p < tn - 48) {
            char one[48];
            if (e1[i].n == 1) {
                snprintf(one, sizeof one, "%s", TX(T_CANDLE_1));
            } else {
                snprintf(one, sizeof one, TX(T_CANDLE_N), e1[i].n);
            }
            if (!p) {
                col = C_ACC;
            }
            p += snprintf(t + p, (size_t)(tn - p), "%s%s", p ? "  -  " : "", one);
        }
    }
    return col;
}
static void rnd_today(void)
{
    const int cx = s_W / 2;
    const hc_city_t *city = &HC_CITIES[s_city];
    char t[96], u[64];
    // the top cap: the city and the time
    tfd_disc((float)cx, (float)(-s_W * 3 / 4), (float)(s_W * 3 / 4 + rv(88)), C_PANEL, 256);
    if (s_time_ok) {
        snprintf(t, sizeof t, "%s  %d:%02d", city->name[s_he], s_min_now / 60, s_min_now % 60);
    } else {
        snprintf(t, sizeof t, "%s", city->name[s_he]);
    }
    tfd_text_c(&KF_NAME, cx, rv(62), t, C_DIM, 2 * rc_half(rv(46)) - 30);
    if (!s_time_ok) {
        tfd_text_c(&KF_BIG, cx, s_H / 2 - rv(10), TX(T_NOTSET), C_ACC, s_W - 60);
        tfd_text_c(&KF_TEXT, cx, s_H / 2 + rv(22), TX(T_NOTSET2), C_DIM, s_W - 60);
    } else {
        today_calc();
        heb_date_he(s_today, t, sizeof t);
        const int wd = 2 * rc_half(rv(110)) - 30;
        tfd_text_c(tfd_pick(&KF_TITLE, &KF_BIG, t, wd), cx, rv(142), t, C_TEXT, wd);
        greg_text(s_today, u, sizeof u, true);
        tfd_text_c(&KF_TEXT, cx, rv(172), u, C_DIM, s_W - 60);
        // the Hebrew date in Latin letters (English), and after the sunset the date of the night
        const hc_zman_t *z = zman(s_today);
        const bool night = z->t[HZ_SUNSET] >= 0 && s_min_now >= hc_round(z->t[HZ_SUNSET]);
        t[0] = 0;
        int p = 0;
        if (!s_he) {
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
            snprintf(t + p, sizeof t - (size_t)p, "%s", p ? "  -  " : "");
            p = (int)strlen(t);
            snprintf(t + p, sizeof t - (size_t)p, TX(T_TONIGHT), h);
        }
        if (t[0]) {
            tfd_text_c(tfd_pick(&KF_TEXT, &KF_LABEL, t, s_W - 50), cx, rv(198), t, C_DIM, s_W - 50);
        }
        const uint16_t col = rnd_events(t, sizeof t);
        const int ey = rv(232);
        if (t[0]) {
            tfd_text_c(tfd_pick(&KF_NAME, &KF_TEXT, t, s_W - 40), cx, ey, t, col, s_W - 40);
        } else if (s_td.next[0]) {
            tfd_text_c(&KF_NAME, cx, ey, s_td.next, C_DIM, s_W - 40);
            hit(30, ey - 28, s_W - 60, 44, H_DAYNEXT, 0);
        }
        if (s_td.cf) {
            rnd_today_box(rv(248));
        }
    }
    const char *l[2] = {TX(T_CALENDAR), TX(T_TIMES)};
    const int id[2] = {H_CAL, H_TIMES};
    const bool hot[2] = {true, false};
    rnd_bottom(l, id, hot, 2);
}

// ---------------------------------------------------------------- CALENDAR: the weeks in the middle band
static void rnd_month(void)
{
    const int y0 = rv(146), y1 = rv(356), hdr = rv(26);
    const int half = rc_band(y0 - hdr, y1) - 6, cw = 2 * half / 7, gx = s_W / 2 - cw * 7 / 2;
    const int rh = row_h();
    const bool il = israel();
    const int64_t base = s_today - hc_dow(s_today) - 7;
    const int wk = (int)floor(s_wk);
    const int dy = (int)((s_wk - (float)wk) * (float)rh);
    tfd_clip(0, y0, s_W, y1);
    int64_t first = 0, last = 0;
    for (int r = 0; y0 + r * rh - dy < y1; r++) {
        const int ry = y0 + r * rh - dy;
        if (ry + rh > y1 + 2) {
            break;  // (a row that goes under the bottom: not drawn, no cut text)
        }
        for (int c = 0; c < 7; c++) {
            const int64_t f = base + (int64_t)(wk + r) * 7 + c;
            if (!first) {
                first = f;
            }
            last = f;
            const int x = gx + (s_he ? 6 - c : c) * cw;
            int gy, gm, gd, hy, hmn, hd;
            hc_greg(f, &gy, &gm, &gd);
            hc_heb(f, &hy, &hmn, &hd);
            tfd_rrect(x + 1, ry + 1, cw - 2, rh - 2, 6, c == 6 ? rgb(20, 24, 46) : (gm & 1) ? C_ROW : C_BG, 256);
            if (f == s_today) {
                tfd_rrect(x + 1, ry + 1, cw - 2, rh - 2, 6, C_ACC, 70);
                tfd_rrect(x + 3, ry + 1, cw - 6, 2, 1, C_ACC, 256);
            }
            char a[12], b[24];
            snprintf(a, sizeof a, "%d", gd);
            if (hd == 1) {
                const bool he = s_he;
                s_he = true;
                snprintf(b, sizeof b, "%s", hmonth(hy, hmn));
                s_he = he;
            } else {
                hc_gematria(hd, b, sizeof b);
            }
            tfd_text_c(&KF_NAME, x + cw / 2, ry + rv(20), a, gd == 1 ? C_ACC : C_TEXT, cw - 4);
            tfd_text_c(&KF_TEXT, x + cw / 2, ry + rh - rv(6), b, hd == 1 ? C_RC : C_DIM, cw - 4);
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
                tfd_rrect(x + cw / 2 - 8, ry + rh - 4, 16, 3, 1, ev_col(e[best].id), 256);
            }
            hit_in(x, ry, cw, rh - 1, H_CELL, (int)(f - s_today), y0, y1);
        }
    }
    tfd_noclip();
    tfd_rect(0, y1, s_W, s_H - y1, C_BG, 256);  // (the text of a row that goes under the bottom is not clipped)
    tfd_rect(0, y0 - hdr, s_W, hdr, C_BG, 256);
    for (int c = 0; c < 7; c++) {
        const int col = s_he ? 6 - c : c;
        tfd_text_c(&KF_TEXT, gx + col * cw + cw / 2, y0 - 6, WDAY_S[c][s_he], c == 6 ? C_SHAB : C_DIM, cw - 2);
    }
    // the title: the month of the middle of the view, the Hebrew months under it
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
    rnd_top(t, r);
    button(s_W / 2 - rv(72), rv(366), 2 * rv(72), 48, TX(T_TODAY), false, H_TODAY, 0);
}

// ---------------------------------------------------------------- DAY: one column; each line follows the chord
static void rnd_day(void)
{
    const int cx = s_W / 2, y0 = rv(119), y1 = rv(352);
    char t[96], u[64];
    int hy, hmn, hd;
    hc_heb(s_sel, &hy, &hmn, &hd);
    hc_ev_t e[12];
    const int n = day_events(s_sel, e, 12);
    const int par = hc_parsha(s_sel, israel());
    const int sc = (int)s_scroll;
    int yy = y0 + 8 - sc;
    tfd_clip(0, y0, s_W, y1);
    heb_date_he(s_sel, t, sizeof t);
    tfd_text_c(tfd_pick(&KF_BIG, &KF_NAME, t, 2 * rc_half(yy + 6) - 40), cx, yy + 30, t, C_TEXT, 2 * rc_half(yy) - 40);
    yy += 40;
    if (!s_he) {
        heb_date_en(s_sel, t, sizeof t);
        tfd_text_c(&KF_TEXT, cx, yy + 16, t, C_DIM, 2 * rc_half(yy) - 40);
        yy += 24;
    }
    if (par) {
        parsha_text(par, u, sizeof u);
        snprintf(t, sizeof t, "%s %s", s_he ? "פרשת" : "PARASHAT", u);
        tfd_text_c(&KF_NAME, cx, yy + 18, t, C_SHAB, 2 * rc_half(yy) - 40);
        yy += 26;
    }
    for (int i = 0; i < n; i++) {
        ev_text(&e[i], hy, t, sizeof t);
        tfd_text_c(&KF_NAME, cx, yy + 18, t, ev_col(e[i].id), 2 * rc_band(yy, yy + 22) - 40);
        yy += 26;
    }
    hc_ev_t e1[12];
    const int n1 = hc_events(s_sel + 1, israel(), e1, 12);
    for (int i = 0; i < n1; i++) {
        if (e1[i].id == HE_CHANUKAH) {
            if (e1[i].n == 1) {
                snprintf(t, sizeof t, "%s", TX(T_CANDLE_1));
            } else {
                snprintf(t, sizeof t, TX(T_CANDLE_N), e1[i].n);
            }
            tfd_text_c(&KF_TEXT, cx, yy + 18, t, C_ACC, 2 * rc_band(yy, yy + 22) - 40);
            yy += 24;
        }
    }
    if (!n && !par) {
        tfd_text_c(&KF_TEXT, cx, yy + 18, TX(T_NONE), C_FAINT, 2 * rc_band(yy, yy + 22) - 40);
        yy += 24;
    }
    // the times: a row for each, as wide as the circle at its height
    trow_t rows[16];
    const int nr = day_rows(rows);
    const int rh = rv(36) < 34 ? 34 : rv(36);
    int ry = yy + 8;
    const int now_m = s_sel == s_today ? s_min_now : -1;
    int next_i = -1;
    for (int i = 0; i < nr; i++) {
        if (now_m >= 0 && rows[i].t >= 0 && hc_round(rows[i].t) > now_m && (next_i < 0 || rows[i].t < rows[next_i].t)) {
            next_i = i;
        }
    }
    for (int i = 0; i < nr; i++, ry += rh) {
        if (ry + rh < y0 || ry + rh > y1 + 2) {  // (a row that goes under the bottom: not drawn, no cut text)
            continue;
        }
        int half = rc_band(ry, ry + rh) - 16;
        if (half > 190) {
            half = 190;
        }
        if (half < 60) {
            continue;
        }
        const int lx = cx - half, lw = 2 * half;
        tfd_rrect(lx, ry, lw, rh - 2, 8, i & 1 ? C_ROW : C_PANEL, 256);
        if (i == next_i) {
            tfd_rrect(s_he ? lx + lw - 4 : lx, ry, 4, rh - 2, 2, C_ACC, 256);
        }
        char tm[12];
        hm_text(tm, sizeof tm, rows[i].t);
        const uint16_t c = rows[i].col == C_TEXT ? C_DIM : rows[i].col;
        const int base = ry + rh / 2 + 6;
        if (s_he) {
            tfd_text_r(&KF_TEXT, lx + lw - 12, base, rows[i].name, c, lw - 90);
            tfd_text_l(&KF_NAME, lx + 12, base, tm, rows[i].col, 72);
        } else {
            tfd_text_l(&KF_TEXT, lx + 12, base, rows[i].name, c, lw - 90);
            tfd_text_r(&KF_NAME, lx + lw - 12, base, tm, rows[i].col, 72);
        }
    }
    tfd_noclip();
    tfd_rect(0, y1, s_W, s_H - y1, C_BG, 256);
    const float mx = (float)(ry + sc - y1 + 10);
    if (s_scroll > mx) {
        s_scroll = mx > 0 ? mx : 0;
    }
    int gy, gm, gd;
    hc_greg(s_sel, &gy, &gm, &gd);
    snprintf(t, sizeof t, "%s %d %s %d", WDAY_S[hc_dow(s_sel)][s_he], gd, GMONTH[gm - 1][s_he], gy);
    rnd_top(t, HC_CITIES[s_city].name[s_he]);
    // the bottom cap: the day before, TODAY, the next day (the arrows point in the direction of the reading)
    const int ky = rv(386), kr = rv(26) < 24 ? 24 : rv(26), kd = rv(98);
    rnd_key(cx + (s_he ? kd : -kd), ky, kr, s_he ? ">" : "<", false, H_PREV, 0);
    rnd_key(cx + (s_he ? -kd : kd), ky, kr, s_he ? "<" : ">", false, H_NEXT, 0);
    if (s_sel != s_today) {
        button(cx - rv(56), ky - 24, 2 * rv(56), 48, TX(T_TODAY), false, H_TODAY, 0);
    }
}

// ---------------------------------------------------------------- CITIES: rows as wide as the circle
static void rnd_cities(void)
{
    const int cx = s_W / 2, y0 = rv(119), y1 = s_H - rv(30), rh = 48;
    const int sc = (int)s_scroll;
    tfd_clip(0, y0, s_W, y1);
    for (int i = 0; i < HC_NCITY; i++) {
        const int y = y0 + 6 + i * rh - sc;
        if (y + rh < y0 || y + rh > y1 + 4) {
            continue;
        }
        int half = rc_band(y, y + rh) - 14;
        if (half > 200) {
            half = 200;
        }
        if (half < 70) {
            continue;
        }
        const hc_city_t *c = &HC_CITIES[i];
        const int lx = cx - half, lw = 2 * half;
        tfd_rrect(lx, y, lw, rh - 4, 10, i == s_city ? rgb(52, 44, 24) : i & 1 ? C_ROW : C_PANEL, 256);
        tfd_text_s(&KF_NAME, lx + 14, lx + lw - 14, y + rh / 2 + 4, c->name[s_he], i == s_city ? C_ACC : C_TEXT, s_he);
        const char *k = c->israel ? TX(T_ISRAEL) : TX(T_DIASPORA);
        if (s_he) {
            tfd_text_l(&KF_TEXT, lx + 14, y + rh / 2 + 4, k, C_DIM, 110);
        } else {
            tfd_text_r(&KF_TEXT, lx + lw - 14, y + rh / 2 + 4, k, C_DIM, 110);
        }
        hit_in(lx, y, lw, rh - 4, H_CITY, i, y0, y1);
    }
    tfd_noclip();
    tfd_rect(0, y1, s_W, s_H - y1, C_BG, 256);
    const float mx = (float)(HC_NCITY * rh + 12 - (y1 - y0));
    if (s_scroll > mx) {
        s_scroll = mx > 0 ? mx : 0;
    }
    rnd_top(TX(T_CHOOSE_CITY), NULL);
}
