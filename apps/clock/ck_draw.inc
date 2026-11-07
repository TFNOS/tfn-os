// CLOCK: THE SCREENS (they read the state of app_clock.c; the touch targets of each frame go into s_hit[]).
// The task: the app task (render and input of app_clock.c, which includes this file).
// The memory: the file statics of this part (the layout, the touch targets); no heap.
//
// NIGHT MODE: each colour goes through nc(): a dim red of the same light (no blue light at night).

static uint16_t nc(uint16_t c)
{
    if (!s_night) {
        return c;
    }
    int r, g, b;
    tfd_unrgb(c, &r, &g, &b);
    const int l = (r * 3 + g * 6 + b) / 10;
    return tfd_rgb(l * 62 / 100, l * 12 / 100, l * 6 / 100);
}
#define C_BG    nc(rgb(6, 8, 14))
#define C_PANEL nc(rgb(15, 20, 32))
#define C_ROW   nc(rgb(11, 15, 24))
#define C_LINE  nc(rgb(30, 40, 56))
#define C_TEXT  nc(rgb(236, 240, 246))
#define C_DIM   nc(rgb(124, 138, 158))
#define C_FAINT nc(rgb(66, 78, 96))
#define C_ACC   nc(rgb(255, 150, 90))  // the colour of the app (a warm orange: the morning)
#define C_ON    nc(rgb(80, 210, 130))
#define C_SUN   nc(rgb(255, 200, 70))
#define C_MOON  nc(rgb(170, 190, 255))
#define C_BTN   nc(rgb(26, 34, 50))
#define C_BTN_E nc(rgb(44, 56, 78))
#define C_WARN  nc(rgb(255, 120, 90))

static int s_W, s_H, s_top_h;
static bool s_narrow;
static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    s_narrow = s_W < 480;
    s_top_h = BAR ? 26 : (s_narrow ? 30 : 44);  // (the Tab5, 640 x 360: a bar of 7.5 mm, a BACK key for a thumb)
}

// ---------------------------------------------------------------- the touch targets
enum {
    H_ALARMS,
    H_WORLD,
    H_BACK,
    H_ADD,
    H_ROW,
    H_TOGGLE,
    H_UP,
    H_DOWN,
    H_DAY,
    H_QUICK,
    H_SOUND,
    H_VOL,
    H_SNZ,
    H_LABEL,
    H_SAVE,
    H_DEL,
    H_SNOOZE,
    H_CITY,
    H_WDEL,
    H_ADDCITY,
    H_HOME,
    H_TIMER,
    H_SW,
    H_TGO,
    H_TRESET,
    H_TDEL,
    H_TNEW,
    H_TPRE,
    H_TMIN,
    H_TSEC,
    H_TLABEL,
    H_TSTART,
    H_SWGO,
    H_SWLAP
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

// EDIT: the boxes of the hour and the minute (a drag changes them); RING: the box of STOP
static int s_dig_x[2], s_dig_y, s_dig_w, s_dig_h;
static int s_stop_x, s_stop_y, s_stop_w, s_stop_h;
static int digit_at(int x, int y)
{
    for (int i = 0; i < 2; i++) {
        if (x >= s_dig_x[i] && x < s_dig_x[i] + s_dig_w && y >= s_dig_y && y < s_dig_y + s_dig_h) {
            return i;
        }
    }
    return -1;
}
static bool hold_at(int x, int y)
{
    return x >= s_stop_x - 6 && x < s_stop_x + s_stop_w + 6 && y >= s_stop_y - 6 && y < s_stop_y + s_stop_h + 6;
}

// the touch targets of TIMER, NEW TIMER and STOPWATCH (false: not one of them)
static bool hit_timer(const hit_t *h)
{
    switch (h->id) {
    case H_TGO:
        tm_go(h->arg);
        return true;
    case H_TRESET:
        tm_reset(h->arg);
        return true;
    case H_TDEL:
        if (s_tdel_i == h->arg && s_tdel_t && s_now - s_tdel_t < 3000) {
            s_tm[h->arg].state = TIMER_FREE;
            tm_save();
            s_tdel_i = -1;
        } else {
            s_tdel_i = h->arg;
            s_tdel_t = s_now;  // (a second tap in 3 s deletes)
            toast(s_he ? "הקישו שוב כדי למחוק" : "TAP AGAIN TO DELETE");
        }
        return true;
    case H_TNEW:
        s_tn_label[0] = 0;
        s_scr = SC_TNEW;
        return true;
    case H_TPRE:
        s_tn_min = h->arg;
        s_tn_sec = 0;
        return true;
    case H_TMIN:
        s_tn_min = (s_tn_min + h->arg + 181) % 181;
        return true;
    case H_TSEC:
        s_tn_sec = (s_tn_sec + h->arg + 60) % 60;
        return true;
    case H_TLABEL:
        snprintf(s_kbuf, sizeof s_kbuf, "%s", s_tn_label);
        s_kbd_for = 1;
        s_scr = SC_KBD;
        kbd_open(TEXT[T_KBD_TIMER][0], s_kbuf, (int)sizeof s_tn_label, false);
        kbd_set_ok_question(NULL);
        return true;
    case H_TSTART:
        tm_new();
        return true;
    case H_SWGO:
        sw_go();
        return true;
    case H_SWLAP:
        sw_lap();
        return true;
    }
    return false;
}

// the touch targets of ALARMS, EDIT and RING (false: not one of them)
static bool hit_edit(const hit_t *h)
{
    switch (h->id) {
    case H_ADD:
        if (s_nal >= ALARM_MAX) {
            toast(TX(T_FULL));
        } else {
            edit_open(-1);
        }
        return true;
    case H_ROW:
        edit_open(h->arg);
        return true;
    case H_TOGGLE:
        s_al[h->arg].on ^= 1;
        al_save();
        return true;
    case H_UP:
    case H_DOWN: {  // (one step a tap; a drag on the digits goes fast)
        const int d = h->id == H_UP ? 1 : -1;
        if (h->arg == 0) {
            s_ed.hour = (uint8_t)((s_ed.hour + d + 24) % 24);
        } else {
            s_ed.min = (uint8_t)((s_ed.min + d + 60) % 60);
        }
        return true;
    }
    case H_DAY:
        s_ed.days ^= (uint8_t)(1 << h->arg);
        return true;
    case H_QUICK:
        s_ed.days = (uint8_t)h->arg;
        return true;
    case H_SOUND:
        s_ed.sound = (uint8_t)h->arg;
        alarm_preview(h->arg, s_ed.vol);
        return true;
    case H_VOL:
        s_ed.vol = (uint8_t)h->arg;
        alarm_preview(s_ed.sound, s_ed.vol);
        return true;
    case H_SNZ:
        s_ed.snooze = (uint8_t)h->arg;
        return true;
    case H_LABEL:
        snprintf(s_kbuf, sizeof s_kbuf, "%s", s_ed.label);
        s_kbd_for = 0;
        s_scr = SC_KBD;
        kbd_open(TEXT[T_KBD_LABEL][0], s_kbuf, (int)sizeof s_ed.label, false);  // (English: no Hebrew in its font)
        kbd_set_ok_question(NULL);
        return true;
    case H_SAVE:
        edit_save();
        return true;
    case H_DEL:
        if (s_del_t && s_now - s_del_t < 3000) {
            edit_delete();
        } else {
            s_del_t = s_now;  // (a second tap in 3 s deletes)
            toast(s_he ? "הקישו שוב כדי למחוק" : "TAP AGAIN TO DELETE");
        }
        return true;
    case H_SNOOZE: {
        const int r = alarm_ringing();
        if (r >= ALARM_MAX) {  // (a timer: one minute more)
            alarm_snooze();
            s_scr = SC_TIMER;
            return true;
        }
        const int snz = r >= 0 && r < s_nal ? s_al[r].snooze : 9;
        alarm_snooze();
        s_scr = SC_CLOCK;
        int off;
        const int64_t lt = city_local(s_home, &off, NULL) + snz * 60;
        char b[64];
        snprintf(b, sizeof b, TX(T_SNOOZED), (int)(lt % 86400 / 3600), (int)(lt % 3600 / 60));
        toast(b);
        return true;
    }
    }
    return false;
}

// the touch targets of the pages and the cities
static void hit_page(const hit_t *h)
{
    switch (h->id) {
    case H_ALARMS:
        s_scr = SC_ALARMS;
        s_scroll = 0;
        al_load();
        break;
    case H_WORLD:
        s_scr = SC_WORLD;
        s_scroll = 0;
        break;
    case H_TIMER:
        s_scr = SC_TIMER;
        s_scroll = 0;
        tm_load();
        break;
    case H_SW:
        s_scr = SC_SW;
        s_scroll = 0;
        break;
    case H_BACK:
        s_scr = s_scr == SC_EDIT                     ? SC_ALARMS
                : s_scr == SC_TNEW                   ? SC_TIMER
                : s_scr == SC_CITIES && !s_pick_home ? SC_WORLD
                                                     : SC_CLOCK;
        s_scroll = 0;
        break;
    case H_CITY:
        if (s_pick_home) {
            set_home(h->arg);
            s_scr = SC_CLOCK;
        } else {
            bool have = false;
            for (int i = 0; i < s_nworld; i++) {
                have |= s_world[i] == h->arg;
            }
            if (!have && s_nworld < WORLD_MAX) {
                s_world[s_nworld++] = h->arg;
            } else if (!have) {
                toast(TX(T_FULL));
            }
            save();
            s_scr = SC_WORLD;
        }
        s_scroll = 0;
        break;
    case H_WDEL:
        if (h->arg < s_nworld) {
            memmove(&s_world[h->arg], &s_world[h->arg + 1], sizeof(int) * (size_t)(s_nworld - h->arg - 1));
            s_nworld--;
            save();
        }
        break;
    case H_ADDCITY:
        if (s_nworld >= WORLD_MAX) {
            toast(TX(T_FULL));
        } else {
            s_pick_home = 0;
            s_scroll = 0;
            s_scr = SC_CITIES;
        }
        break;
    case H_HOME:
        s_pick_home = 1;
        s_scroll = 0;
        s_scr = SC_CITIES;
        break;
    }
}

static void hit_do(int i)
{
    const hit_t *h = &s_hit[i];
    if (hit_timer(h) || hit_edit(h)) {
        return;
    }
    hit_page(h);
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
            button(g + i * (w + g), y, w, 38, l[i], hot[i], id[i], 0);
        }
    } else {
        const int x = s_W - 100, y0 = (s_scr == SC_CLOCK ? 4 : s_top_h + 4), g = 4;
        const int h = (s_H - y0 - 2 - (n - 1) * g) / n;
        tfd_rect(s_W - 104, y0 - 3, 104, s_H - y0 + 3, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            button(x, y0 + i * (h + g), 96, h, l[i], hot[i], id[i], 0);
        }
    }
}

static void top_bar(const char *title)
{
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    tfd_rect(0, s_top_h, s_W, 1, C_LINE, 256);
    const int base = s_top_h / 2 + 6;
    tfd_rrect(4, 3, 64, s_top_h - 6, 6, C_BTN, 256);
    tfd_text_c(&KF_LABEL, 36, base - 1, TX(T_BACK), C_TEXT, 60);
    hit(0, 0, 80, s_top_h + 4, H_BACK, 0);
    tfd_text_l(&KF_NAME, 80, base, title, C_TEXT, s_narrow ? s_W - 90 : s_W / 2 - 100);  // (the 3.49: left of the grip)
}

static void toast_draw(void)
{
    if (!s_toast[0] || s_now - s_toast_t > 2600) {
        return;
    }
    const int w = ui_text_w(&KF_NAME, s_toast) + 28;
    const int ww = w > s_W - 20 ? s_W - 20 : w;
    const int cx = s_narrow ? s_W / 2 : (s_W - 104) / 2;
    const int y = s_narrow ? s_H - 86 : s_H - 44;
    const int a = s_now - s_toast_t > 2200 ? (int)(256 * (2600 - (s_now - s_toast_t)) / 400) : 256;
    tfd_rrect(cx - ww / 2, y, ww, 34, 10, nc(rgb(40, 52, 76)), a);
    tfd_text_c(&KF_NAME, cx, y + 23, s_toast, C_TEXT, ww - 16);
}

static float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}

// a sun (day: 06:00 .. 18:00) or a moon
static void daynight(int cx, int cy, float r, int hour)
{
    if (hour >= 6 && hour < 18) {
        for (int i = 0; i < 8; i++) {
            const float a = i * 0.785f;
            tfd_line(cx + cosf(a) * r * 1.35f, cy + sinf(a) * r * 1.35f, cx + cosf(a) * r * 1.8f,
                     cy + sinf(a) * r * 1.8f, 1.6f, C_SUN, 256);
        }
        tfd_disc((float)cx, (float)cy, r, C_SUN, 256);
    } else {
        tfd_disc((float)cx, (float)cy, r, C_MOON, 256);
        tfd_disc(cx + r * 0.45f, cy - r * 0.3f, r * 0.85f, C_BG, 256);
    }
}

// the width of a time in a face (the digits have one width)
static int tw(const mfont_t *f, const char *s)
{
    return mf_width(f, s, 0);
}

// ---------------------------------------------------------------- CLOCK

static void next_alarm_text(char *t, int n, char *in, int ni)
{
    int idx;
    const int64_t nx = alarm_next(&idx);
    if (!nx || !s_time_ok) {
        snprintf(t, (size_t)n, "%s", TX(T_NOALARM));
        in[0] = 0;
        return;
    }
    int off;
    city_local(s_home, &off, NULL);
    tz_rule_t z;
    tz_parse(CITIES[s_home].tz, &z);
    const int64_t l = nx + tz_offset(&z, nx, NULL);
    const char *ap;
    char hm[16];
    hm_text(hm, sizeof hm, (int)(l % 86400 / 3600), (int)(l % 3600 / 60), &ap);
    snprintf(t, (size_t)n, "%s%s%s", hm, ap[0] ? " " : "", ap);
    const int64_t d = (nx - s_utc + 59) / 60;  // (the minutes, up: "IN 1 MIN" also 20 s before)
    if (d < 60) {
        snprintf(in, (size_t)ni, TX(T_IN_MIN), (int)d);
    } else {
        snprintf(in, (size_t)ni, TX(T_IN), (int)(d / 60), (int)(d % 60));
    }
}

static void bell(int cx, int cy, float s, uint16_t c)
{
    tfd_disc((float)cx, cy - 1 * s, 5.5f * s, c, 256);
    tfd_rect(cx - (int)(5.5f * s), cy - (int)s, (int)(11 * s), (int)(5 * s), c, 256);
    tfd_rect(cx - (int)(7 * s), cy + (int)(4 * s), (int)(14 * s), (int)(2 * s), c, 256);
    tfd_disc((float)cx, cy + 7.5f * s, 1.6f * s, c, 256);
}

static void offset_text(char *o, int n, int c);

static void draw_clock(void)
{
    int off;
    const char *zn;
    const int64_t lt = city_local(s_home, &off, &zn);
    tz_tm_t m;
    tz_split(lt, &m);
    char hm[16], sec[8], date[96], hz[64];
    const char *ap = "";
    if (s_time_ok) {
        hm_text(hm, sizeof hm, m.h, m.mi, &ap);
    } else {
        snprintf(hm, sizeof hm, "--:--");
    }
    snprintf(sec, sizeof sec, "%02d", m.s);
    if (s_he) {
        snprintf(date, sizeof date, "%s, %d %s %d", DAY_LONG[m.wd][1], m.d, MONTHS[m.mo - 1][1], m.y);
    } else {
        snprintf(date, sizeof date, "%s, %d %s %d", DAY_LONG[m.wd][0], m.d, MONTHS[m.mo - 1][0], m.y);
    }
    snprintf(hz, sizeof hz, "%s  %s", CITIES[s_home].name[s_he ? 1 : 0], zn ? zn : "");
    const mfont_t *big = s_narrow ? &CK_L : &CK_XL;
    const int area = s_narrow ? s_W : s_W - 104 - (BAR ? 190 : 0);
    const int bw = tw(big, hm), sw = s_secs && s_time_ok ? tw(&CK_M, sec) + 8 : 0;
    const int apw = ap[0] ? tw(&CK_S, ap) + 6 : 0;
    const int x0 = (area - bw - (sw > apw ? sw : apw)) / 2;
    const int base = s_narrow ? 96 : 92;
    mf_text(big, x0, base, hm, s_time_ok ? C_TEXT : C_FAINT, 255, 0);
    if (sw) {
        mf_text(&CK_M, x0 + bw + 8, base, sec, C_ACC, 255, 0);
    }
    if (ap[0]) {
        mf_text(&CK_S, x0 + bw + 8, base - big->asc + CK_S.asc + 2, ap, C_DIM, 255, 0);
    }
    if (s_time_ok) {
        tfd_text_c(&KF_NAME, area / 2, base + 30, date, C_TEXT, area - 16);
        tfd_text_c(&KF_LABEL, area / 2, base + 50, hz, C_DIM, area - 16);
    } else {
        tfd_text_c(&KF_NAME, area / 2, base + 30, TX(T_NOTSET), C_WARN, area - 16);
        tfd_text_c(&KF_LABEL, area / 2, base + 50, TX(T_NOTSET2), C_DIM, area - 16);
    }
    // the next alarm and two cities of the world clock
    char at[32], in[48];
    next_alarm_text(at, sizeof at, in, sizeof in);
    if (BAR) {
        const int x = s_W - 104 - 186;
        tfd_rrect(x, 8, 180, 60, 10, C_PANEL, 256);
        bell(x + 18, 30, 1.4f, in[0] ? C_ACC : C_FAINT);
        mf_text(&CK_M, x + 36, 40, at[0] && in[0] ? at : "", C_TEXT, 255, 0);
        if (!in[0]) {
            tfd_text_l(&KF_LABEL, x + 36, 36, at, C_DIM, 136);
        }
        tfd_text_l(&KF_LABEL, x + 12, 60, in, C_DIM, 160);
        hit(x, 8, 180, 60, H_ALARMS, 0);
        for (int i = 0; i < 2 && i < s_nworld; i++) {
            const int y = 76 + i * 46;
            tfd_rrect(x, y, 180, 42, 10, C_PANEL, 256);
            int o2;
            const int64_t l2 = city_local(s_world[i], &o2, NULL);
            const char *ap2;
            char t2[16];
            hm_text(t2, sizeof t2, (int)(l2 % 86400 / 3600), (int)(l2 % 3600 / 60), &ap2);
            daynight(x + 16, y + 21, 5.5f, (int)(l2 % 86400 / 3600));
            tfd_text_l(&KF_LABEL, x + 30, y + 26, CITIES[s_world[i]].name[s_he ? 1 : 0], C_DIM, 74);
            mf_text(&CK_S, x + 174 - tw(&CK_S, t2), y + 27, s_time_ok ? t2 : "--:--", C_TEXT, 255, 0);
            hit(x, y, 180, 42, H_WORLD, 0);
        }
    } else if (!s_narrow) {
        // the Tab5 (640 x 360, the side keys at the right): the next alarm in a card under the date, then up to 4
        // cities of the world clock in 2 rows of 2 cards (a tap opens ALARMS or WORLD)
        const int g = 8, cw = (area - 3 * g) / 2, ch = 64, ya = base + 62;
        tfd_rrect(g, ya, area - 2 * g, 46, 10, C_PANEL, 256);
        bell(g + 24, ya + 22, 1.4f, in[0] ? C_ACC : C_FAINT);
        if (in[0]) {
            mf_text(&CK_M, g + 48, ya + 34, at, C_TEXT, 255, 0);
            tfd_text_l(&KF_NAME, g + 60 + tw(&CK_M, at), ya + 30, in, C_DIM, area - 140 - tw(&CK_M, at));
        } else {
            tfd_text_l(&KF_NAME, g + 48, ya + 30, at, C_DIM, area - 80);
        }
        hit(g, ya, area - 2 * g, 46, H_ALARMS, 0);
        for (int i = 0; i < 4 && i < s_nworld; i++) {
            const int x = g + ((i % 2) ^ (s_he ? 1 : 0)) * (cw + g),
                      y = ya + 54 + (i / 2) * (ch + g);  // (Hebrew: the first at the right)
            tfd_rrect(x, y, cw, ch, 10, C_PANEL, 256);
            int o2;
            const int64_t l2 = city_local(s_world[i], &o2, NULL);
            const char *ap2;
            char t2[16], d2[64];
            hm_text(t2, sizeof t2, (int)(l2 % 86400 / 3600), (int)(l2 % 3600 / 60), &ap2);
            daynight(x + 22, y + ch / 2, 7, (int)(l2 % 86400 / 3600));
            const int tx = x + cw - 12 - tw(&CK_M, t2) - (ap2[0] ? 26 : 0);
            tfd_text_l(&KF_NAME, x + 42, y + 27, CITIES[s_world[i]].name[s_he ? 1 : 0], C_TEXT, tx - x - 50);
            offset_text(d2, sizeof d2, s_world[i]);
            tfd_text_l(&KF_LABEL, x + 42, y + 49, d2, C_DIM, tx - x - 50);
            mf_text(&CK_M, tx, y + 42, s_time_ok ? t2 : "--:--", C_TEXT, 255, 0);
            if (ap2[0]) {
                mf_text(&CK_S, tx + tw(&CK_M, t2) + 4, y + 42, ap2, C_DIM, 255, 0);
            }
            hit(x, y, cw, ch, H_WORLD, 0);
        }
    } else {
        // the narrow frame: the next alarm under the date, then two cities in one row
        const int y = base + 62;
        bell(16, y + 2, 1.1f, in[0] ? C_ACC : C_FAINT);
        char line[96];
        snprintf(line, sizeof line, "%s   %s", at, in);
        tfd_text_l(&KF_LABEL, 30, y + 7, line, in[0] ? C_TEXT : C_DIM, s_W - 40);
        for (int i = 0; i < 2 && i < s_nworld; i++) {
            const int x = 6 + i * (s_W / 2), w = s_W / 2 - 12;
            tfd_rrect(x, y + 16, w, 26, 8, C_PANEL, 256);
            int o2;
            const int64_t l2 = city_local(s_world[i], &o2, NULL);
            const char *ap2;
            char t2[16];
            hm_text(t2, sizeof t2, (int)(l2 % 86400 / 3600), (int)(l2 % 3600 / 60), &ap2);
            daynight(x + 13, y + 29, 4.5f, (int)(l2 % 86400 / 3600));
            tfd_text_l(&KF_LABEL, x + 25, y + 34, CITIES[s_world[i]].name[s_he ? 1 : 0], C_DIM, w - 70);
            mf_text(&CK_S, x + w - 6 - tw(&CK_S, t2), y + 34, s_time_ok ? t2 : "--:--", C_TEXT, 255, 0);
        }
    }
    const char *l[4] = {TX(T_ALARMS), TX(T_WORLD), TX(T_TIMER), TX(T_STOPWATCH)};
    const int id[4] = {H_ALARMS, H_WORLD, H_TIMER, H_SW};
    bool run = false;
    for (int i = 0; i < TIMER_MAX; i++) {
        run |= s_tm[i].state == TIMER_RUN;
    }
    const bool hot[4] = {false, false, run, s_sw_run};  // (a timer or the stopwatch runs: its button lights)
    side_buttons(l, id, hot, 4);
}

// ---------------------------------------------------------------- ALARMS

static void toggle(int x, int y, bool on)
{
    tfd_rrect(x, y, 48, 26, 13, on ? C_ON : C_BTN_E, 256);
    tfd_disc((float)(on ? x + 35 : x + 13), (float)y + 13, 10, C_TEXT, 256);
}

static void draw_alarms(void)
{
    const int x1 = s_narrow ? s_W : s_W - 104, y0 = s_top_h + 1, y1 = s_narrow ? s_H - 46 : s_H - 20;
    const int rh = 48;
    const float mx = (float)(s_nal * rh - (y1 - y0));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    char t[16], d[96];
    for (int i = 0; i < s_nal; i++) {
        const int y = y0 + i * rh - (int)s_scroll;
        if (y + rh < y0 || y > y1) {
            continue;
        }
        const alarm_t *a = &s_al[i];
        tfd_rect(0, y, x1, rh - 1, i & 1 ? C_ROW : C_BG, 256);
        const char *ap;
        hm_text(t, sizeof t, a->hour, a->min, &ap);
        mf_text(&CK_M, 12, y + 34, t, a->on ? C_TEXT : C_FAINT, 255, 0);
        if (ap[0]) {
            mf_text(&CK_S, 14 + tw(&CK_M, t), y + 34, ap, C_DIM, 255, 0);
        }
        days_text(d, sizeof d, a->days);
        const int tx = 12 + tw(&CK_M, "00:00") + (ap[0] ? 30 : 14);
        tfd_text_l(&KF_NAME, tx, y + 21, a->label[0] ? a->label : TX(T_ALARM), a->on ? C_TEXT : C_FAINT, x1 - tx - 72);
        tfd_text_l(&KF_LABEL, tx, y + 39, d, a->on ? C_ACC : C_FAINT, x1 - tx - 72);
        hit_in(0, y, x1 - 70, rh - 1, H_ROW, i, y0, y1);
        toggle(x1 - 60, y + 11, a->on);
        hit_in(x1 - 68, y, 68, rh - 1, H_TOGGLE, i, y0, y1);
    }
    if (!s_nal) {
        tfd_text_c(&KF_NAME, x1 / 2, (y0 + y1) / 2, TX(T_NOALARM), C_DIM, x1 - 20);
    }
    if (!s_narrow) {
        tfd_text_c(&KF_LABEL, x1 / 2, s_H - 5, TX(T_RINGS_HERE), C_FAINT, x1 - 10);
    }
    top_bar(TX(T_ALARMS));
    const char *l[1] = {TX(T_ADD)};
    const int id[1] = {H_ADD};
    const bool hot[1] = {true};
    side_buttons(l, id, hot, 1);
}

// ---------------------------------------------------------------- EDIT

typedef struct {
    int x0, x1, x, y, h;
} flow_t;
static void flow_label(flow_t *f, const char *s)
{
    f->x = f->x0;
    f->y += f->h ? f->h + 10 : 4;
    tfd_text_s(&KF_LABEL, f->x0 + 2, f->x1 - 2, f->y + 13, s, C_DIM, s_he);
    f->y += 18;
    f->h = 0;
}
static int chip_w(const char *s)
{
    return ui_text_w(&KF_LABEL, s) + 22;
}
static void fchip(flow_t *f, const char *s, bool on, int id, int arg, int ytop)
{
    const int w = chip_w(s);
    if (f->x + w > f->x1 && f->x > f->x0) {
        f->x = f->x0;
        f->y += 36;
    }
    const int ybot = s_narrow ? s_H - 46 : s_H;  // (the bar of the buttons of the 320 x 240 frame)
    if (f->y + 30 > ytop && f->y < ybot) {
        tfd_rrect(f->x, f->y, w, 30, 15, on ? C_ACC : C_BTN, 256);
        tfd_text_c(&KF_LABEL, f->x + w / 2, f->y + 20, s, on ? C_BG : C_TEXT, w);
        hit_in(f->x, f->y, w, 30, id, arg, ytop, ybot);
    }
    f->x += w + 6;
    f->h = 30;
}

static void arrow(int cx, int cy, bool up)
{
    const float p[6] = {(float)cx - 12, (float)cy + (up ? 6 : -6), (float)cx + 12, (float)cy + (up ? 6 : -6),
                        (float)cx,      (float)cy + (up ? -7 : 7)};
    tfd_poly(p, 3, C_DIM, 256);
}

static void draw_edit(void)
{
    // the time: two boxes (a drag up or down, or the arrows), the hour and the minute. The 3.49: at the left, and the
    // choices scroll at its right. The 320 x 240 frame: at the top, and the whole page scrolls (the time too).
    const mfont_t *f = &CK_L;
    const int dw = tw(f, "00") + 10, cw = tw(f, ":");
    const int sc = s_narrow ? (int)s_scroll : 0;
    const int px = s_narrow ? (s_W - 2 * dw - cw) / 2 : 12, py = s_top_h + 34 - sc;
    const int y0 = s_top_h + 1, y1 = s_narrow ? s_H - 46 : s_H;
    s_dig_x[0] = px;
    s_dig_x[1] = px + dw + cw;
    s_dig_y = py - 2;
    s_dig_w = dw;
    s_dig_h = f->asc + 14;
    char b[64];
    for (int k = 0; k < 2; k++) {
        const int x = s_dig_x[k];
        tfd_rrect(x, s_dig_y, dw, s_dig_h, 10, C_PANEL, 256);
        const int v = k == 0 ? (s_h24 ? s_ed.hour : (s_ed.hour % 12 ? s_ed.hour % 12 : 12)) : s_ed.min;
        snprintf(b, sizeof b, "%02d", v);
        mf_text(f, x + (dw - tw(f, b)) / 2, s_dig_y + 7 + f->asc, b, s_drag_digit == k ? C_ACC : C_TEXT, 255, 0);
        arrow(x + dw / 2, s_dig_y - 13, true);
        arrow(x + dw / 2, s_dig_y + s_dig_h + 13, false);
        hit_in(x, s_dig_y - 30, dw, 28, H_UP, k, y0, y1);
        hit_in(x, s_dig_y + s_dig_h + 2, dw, 28, H_DOWN, k, y0, y1);
    }
    mf_text(f, px + dw, s_dig_y + 7 + f->asc, ":", C_DIM, 255, 0);
    if (!s_h24) {
        mf_text(&CK_S, s_dig_x[1] + dw + 6, s_dig_y + 22, s_ed.hour < 12 ? "AM" : "PM", C_DIM, 255, 0);
    }
    // the choices: the days, the sound, the volume, the snooze, the name
    const int fx0 = s_narrow ? 8 : s_dig_x[1] + dw + 40, fx1 = s_narrow ? s_W - 8 : s_W - 110;
    const int fy = s_narrow ? s_dig_y + s_dig_h + 32 : s_top_h + 2 - (int)s_scroll;
    flow_t fl = {fx0, fx1, fx0, fy, 0};
    flow_label(&fl, TX(T_DAYS));
    for (int i = 0; i < 7; i++) {
        fchip(&fl, DAY_SHORT[i][s_he ? 1 : 0], (s_ed.days >> i) & 1, H_DAY, i, y0);
    }
    fl.x = fl.x1;  // (a new row: the quick choices)
    fchip(&fl, TX(T_ONCE), s_ed.days == 0, H_QUICK, 0, y0);
    fchip(&fl, TX(T_EVERYDAY), s_ed.days == 0x7F, H_QUICK, 0x7F, y0);
    fchip(&fl, TX(T_SUNTHU), s_ed.days == 0x1F, H_QUICK, 0x1F, y0);
    fchip(&fl, TX(T_MONFRI), s_ed.days == 0x3E, H_QUICK, 0x3E, y0);
    flow_label(&fl, TX(T_SOUND));
    for (int i = 0; i < 4; i++) {
        fchip(&fl, SOUNDS[i][s_he ? 1 : 0], s_ed.sound == i, H_SOUND, i, y0);
    }
    flow_label(&fl, TX(T_VOLUME));
    fchip(&fl, TX(T_LOW), s_ed.vol <= 40, H_VOL, 30, y0);
    fchip(&fl, TX(T_MID), s_ed.vol > 40 && s_ed.vol <= 75, H_VOL, 65, y0);
    fchip(&fl, TX(T_HIGH), s_ed.vol > 75, H_VOL, 100, y0);
    flow_label(&fl, TX(T_SNOOZE));
    static const int SNZ[4] = {5, 9, 10, 15};
    for (int i = 0; i < 4; i++) {
        snprintf(b, sizeof b, "%d %s", SNZ[i], TX(T_MIN));
        fchip(&fl, b, s_ed.snooze == SNZ[i], H_SNZ, SNZ[i], y0);
    }
    flow_label(&fl, TX(T_LABEL));
    fchip(&fl, s_ed.label[0] ? s_ed.label : "...", s_ed.label[0] != 0, H_LABEL, 0, y0);
    // the scroll limit: the end of the page at the bottom of the room
    const int bottom = fl.y + 40 + (int)s_scroll;  // (the end of the page with no scroll)
    const float mx = (float)(bottom - y1);
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    top_bar(TX(s_edit < 0 ? T_ADD : T_ALARM));
    const char *l[2] = {TX(T_SAVE), TX(T_DELETE)};
    const int id[2] = {H_SAVE, H_DEL};
    const bool hot[2] = {true, false};
    side_buttons(l, id, hot, s_edit < 0 ? 1 : 2);
}

// ---------------------------------------------------------------- TIMER
static void draw_timer(void)
{
    const int x1 = s_narrow ? s_W : s_W - 104, y0 = s_top_h + 1, y1 = s_narrow ? s_H - 46 : s_H - 14;
    // the 320 frame: two lines in a row (the time, the name under it); one line when 4 timers must fit
    int cnt = 0;
    for (int i = 0; i < TIMER_MAX; i++) {
        cnt += tm_left(i) >= 0;
    }
    const bool two = s_narrow && cnt * 50 <= y1 - y0;
    const int rh = two ? 50 : s_narrow ? (y1 - y0) / (cnt ? cnt : 1) : 36;
    char t[16];
    int n = 0;
    for (int i = 0; i < TIMER_MAX; i++) {
        const int64_t left = tm_left(i);
        if (left < 0) {
            continue;
        }
        const ctimer_t *tm = &s_tm[i];
        const int y = y0 + n * rh;
        n++;
        tfd_rect(0, y, x1, rh - 1, n & 1 ? C_BG : C_ROW, 256);
        left_text(t, sizeof t, left);
        const bool done = tm->state == TIMER_DONE, run = tm->state == TIMER_RUN;
        mf_text(&CK_M, 10, two ? y + 27 : y + rh / 2 + 10, done ? "0:00" : t,
                done  ? C_WARN
                : run ? C_TEXT
                      : C_DIM,
                255, 0);
        char lb[48];
        if (tm->label[0]) {
            snprintf(lb, sizeof lb, "%s", tm->label);
        } else {
            left_text(lb, sizeof lb, (int64_t)tm->len_s * 1000);
        }
        const int tx = 18 + tw(&CK_M, s_narrow ? (done ? "0:00" : t) : "00:00:00");
        // the buttons at the right: START / PAUSE, RESET, X
        const int bw = s_narrow ? 62 : 74, bw2 = 58, bx = 34, bh = rh - 8;
        const int xg = x1 - 6 - bx, xr = xg - 4 - bw2, xs = xr - 4 - bw;
        if (two) {
            tfd_text_l(&KF_LABEL, 10, y + rh - 6, done ? TX(T_DONE) : lb, done ? C_WARN : C_DIM, xs - 16);
        } else {
            tfd_text_l(&KF_LABEL, tx, y + rh / 2 + 5, done ? TX(T_DONE) : lb, done ? C_WARN : C_DIM, xs - tx - 6);
        }
        button(xs, y + 4, bw, bh, run ? TX(T_PAUSE) : TX(T_START), !run, H_TGO, i);
        button(xr, y + 4, bw2, bh, TX(T_RESET), false, H_TRESET, i);
        button(xg, y + 4, bx, bh, "X", s_tdel_i == i && s_now - s_tdel_t < 3000, H_TDEL, i);
    }
    if (!n) {
        tfd_text_c(&KF_NAME, x1 / 2, (y0 + y1) / 2, TX(T_NOTIMER), C_DIM, x1 - 20);
    }
    if (!s_narrow && n < 4) {
        tfd_text_c(&KF_LABEL, x1 / 2, s_H - 3, TX(T_TIMERS_RING), C_FAINT, x1 - 10);  // (4 rows fill the 3.49)
    }
    top_bar(TX(T_TIMER));
    const char *l[1] = {TX(T_NEWTIMER)};
    const int id[1] = {H_TNEW};
    const bool hot[1] = {true};
    side_buttons(l, id, hot, 1);
}

// NEW TIMER: the time with - + for the minutes and the seconds, the presets; NAME and START in the bar of buttons
static void draw_tnew(void)
{
    const int x1 = s_narrow ? s_W : s_W - 104, y0 = s_top_h + 1;
    char t[16];
    snprintf(t, sizeof t, "%d:%02d", s_tn_min, s_tn_sec);
    const mfont_t *f = &CK_L;
    // the 3.49: the time and its buttons at the left, the presets at the right; the 320 frame: one under the other
    const int tcx = s_narrow ? x1 / 2 : 150, ty = y0 + (s_narrow ? 52 : 60);
    mf_text(f, tcx - tw(f, t) / 2, ty, t, C_TEXT, 255, 0);
    const int bw = 44, bh = 30, by = ty + 8, g = 4;
    const int mx = s_narrow ? 14 : tcx - 140, sx = s_narrow ? x1 - 14 - 2 * bw - g : tcx + 140 - 2 * bw - g;
    button(mx, by, bw, bh, "-", false, H_TMIN, -1);
    button(mx + bw + g, by, bw, bh, "+", false, H_TMIN, 1);
    tfd_text_c(&KF_LABEL, mx + bw + g / 2, by + bh + 13, TX(T_MIN), C_DIM, 2 * bw);
    button(sx, by, bw, bh, "-", false, H_TSEC, -5);
    button(sx + bw + g, by, bw, bh, "+", false, H_TSEC, 5);
    tfd_text_c(&KF_LABEL, sx + bw + g / 2, by + bh + 13, TX(T_SEC), C_DIM, 2 * bw);
    // the presets (minutes)
    static const int PRE[5] = {1, 3, 5, 10, 15};
    const int pw = s_narrow ? (x1 - 12 - 4 * 5) / 5 : 40, ph = s_narrow ? 32 : 36;
    const int px0 = s_narrow ? 6 : 318, py = s_narrow ? by + bh + 22 : y0 + 36;
    if (!s_narrow) {
        tfd_text_c(&KF_LABEL, px0 + (5 * pw + 4 * 5) / 2, py - 10, TX(T_MIN), C_DIM, 200);
    }
    for (int i = 0; i < 5; i++) {
        snprintf(t, sizeof t, "%d", PRE[i]);
        button(px0 + i * (pw + 5), py, pw, ph, t, s_tn_min == PRE[i] && !s_tn_sec, H_TPRE, PRE[i]);
    }
    if (s_tn_label[0] && !s_narrow) {
        tfd_text_c(&KF_NAME, px0 + (5 * pw + 20) / 2, py + ph + 28, s_tn_label, C_ACC, 200);
    }
    top_bar(s_tn_label[0] && s_narrow ? s_tn_label : TX(T_NEWTIMER));
    const char *l[2] = {TX(T_START), TX(T_LABEL)};
    const int id[2] = {H_TSTART, H_TLABEL};
    const bool hot[2] = {true, false};
    side_buttons(l, id, hot, 2);
}

// ---------------------------------------------------------------- STOPWATCH
static void sw_text(char *o, int n, uint32_t ms, char *cs, int ncs)
{
    const uint32_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(o, (size_t)n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(o, (size_t)n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
    snprintf(cs, (size_t)ncs, ".%02u", (unsigned)(ms / 10 % 100));
}
static void draw_sw(void)
{
    const int x1 = s_narrow ? s_W : s_W - 104, y0 = s_top_h + 1;
    const uint32_t ms = sw_ms();
    char t[16], cs[8];
    sw_text(t, sizeof t, ms, cs, sizeof cs);
    // the time at the left (the 3.49) or at the top; the laps at the right or under it
    const mfont_t *f = s_narrow || ms >= 3600000 ? &CK_L : &CK_XL;
    const int area = s_narrow ? x1 : x1 - 200;
    const int w = tw(f, t) + tw(&CK_M, cs) + 4, x = (area - w) / 2;
    const int base = s_narrow ? y0 + 66 : y0 + 96;
    mf_text(f, x, base, t, s_sw_run ? C_TEXT : C_DIM, 255, 0);
    mf_text(&CK_M, x + tw(f, t) + 4, base, cs, C_ACC, 255, 0);
    // the laps (the newest first): the number, the time of the lap, the time from the start
    const int lx = s_narrow ? 8 : area + 4, lw = s_narrow ? x1 - 16 : 196;
    int ly = s_narrow ? base + 12 : y0 + 4;
    const int lh = 22, ly1 = s_narrow ? s_H - 48 : s_H - 4;
    for (int i = s_nlap - 1; i >= 0 && ly + lh <= ly1; i--) {
        const uint32_t lap = s_lap[i] - (i ? s_lap[i - 1] : 0);
        char a[16], b[16], c1[8], c2[8], line[64];
        sw_text(a, sizeof a, lap, c1, sizeof c1);
        sw_text(b, sizeof b, s_lap[i], c2, sizeof c2);
        tfd_rrect(lx, ly, lw, lh - 2, 6, C_PANEL, 256);
        snprintf(line, sizeof line, "%d", i + 1);
        tfd_text_l(&KF_LABEL, lx + 6, ly + 15, line, C_ACC, 24);
        snprintf(line, sizeof line, "%s%s", a, c1);
        tfd_text_l(&KF_LABEL, lx + 32, ly + 15, line, C_TEXT, lw / 2);
        snprintf(line, sizeof line, "%s%s", b, c2);
        tfd_text_l(&KF_LABEL, lx + lw - 6 - ui_text_w(&KF_LABEL, line), ly + 15, line, C_DIM, lw / 2);
        ly += lh;
    }
    top_bar(TX(T_STOPWATCH));
    const char *l[2] = {s_sw_run ? TX(T_PAUSE) : TX(T_START), s_sw_run ? TX(T_LAP) : TX(T_RESET)};
    const int id[2] = {H_SWGO, H_SWLAP};
    const bool hot[2] = {true, false};
    side_buttons(l, id, hot, 2);
}

// ---------------------------------------------------------------- WORLD and CITIES

static void offset_text(char *o, int n, int c)
{
    int oh, oc;
    city_local(s_home, &oh, NULL);
    const int64_t lc = city_local(c, &oc, NULL), lh = s_utc + oh;
    const int64_t dc = lc / 86400 - lh / 86400;
    const int dm = (oc - oh) / 60;
    char h[24];
    if (dm % 60) {
        snprintf(h, sizeof h, "%+d:%02d", dm / 60, abs(dm % 60));
    } else {
        snprintf(h, sizeof h, TX(T_HOURS), dm / 60);
    }
    snprintf(o, (size_t)n, "%s   %s", dc > 0 ? TX(T_TOMORROW) : dc < 0 ? TX(T_YESTERDAY) : TX(T_TODAY), dm ? h : "");
}

static void city_row(int y, int rh, int x1, int c, bool home, int idx, int y0, int y1)
{
    int o;
    const int64_t l = city_local(c, &o, NULL);
    const char *ap;
    char t[16], d[64];
    hm_text(t, sizeof t, (int)(l % 86400 / 3600), (int)(l % 3600 / 60), &ap);
    daynight(20, y + rh / 2, 7, (int)(l % 86400 / 3600));
    tfd_text_l(&KF_NAME, 38, y + 20, CITIES[c].name[s_he ? 1 : 0], C_TEXT, x1 - 200);
    if (home) {
        snprintf(d, sizeof d, "%s", TX(T_HOME));
    } else {
        offset_text(d, sizeof d, c);
    }
    tfd_text_l(&KF_LABEL, 38, y + 38, d, home ? C_ACC : C_DIM, x1 - 200);
    const int tx = x1 - (idx >= 0 ? 52 : 12) - tw(&CK_M, t) - (ap[0] ? 26 : 0);
    mf_text(&CK_M, tx, y + 34, s_time_ok ? t : "--:--", C_TEXT, 255, 0);
    if (ap[0]) {
        mf_text(&CK_S, tx + tw(&CK_M, t) + 4, y + 34, ap, C_DIM, 255, 0);
    }
    if (idx >= 0) {  // X: the city goes
        tfd_rrect(x1 - 44, y + rh / 2 - 15, 36, 30, 8, C_BTN, 256);
        tfd_text_c(&KF_NAME, x1 - 26, y + rh / 2 + 6, "X", C_WARN, 30);
        hit_in(x1 - 50, y, 50, rh - 1, H_WDEL, idx, y0, y1);
    } else if (home) {
        hit_in(0, y, x1, rh - 1, H_HOME, 0, y0, y1);
    }
}

static void draw_world(void)
{
    const int x1 = s_narrow ? s_W : s_W - 104, y0 = s_top_h + 1, y1 = s_narrow ? s_H - 46 : s_H;
    const int rh = 48, n = s_nworld + 1;
    const float mx = (float)(n * rh - (y1 - y0));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    for (int i = 0; i < n; i++) {
        const int y = y0 + i * rh - (int)s_scroll;
        if (y + rh < y0 || y > y1) {
            continue;
        }
        tfd_rect(0, y, x1, rh - 1, i & 1 ? C_ROW : C_BG, 256);
        if (i == 0) {
            city_row(y, rh, x1, s_home, true, -1, y0, y1);
        } else {
            city_row(y, rh, x1, s_world[i - 1], false, i - 1, y0, y1);
        }
    }
    top_bar(TX(T_WORLD));
    const char *l[1] = {TX(T_ADD_CITY)};
    const int id[1] = {H_ADDCITY};
    const bool hot[1] = {true};
    side_buttons(l, id, hot, 1);
}

static void draw_cities(void)
{
    const int x1 = s_W, y0 = s_top_h + 1, y1 = s_H, rh = 40;
    const float mx = (float)(NCITY * rh - (y1 - y0));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    char d[32];
    for (int i = 0; i < NCITY; i++) {
        const int y = y0 + i * rh - (int)s_scroll;
        if (y + rh < y0 || y > y1) {
            continue;
        }
        const bool cur = s_pick_home ? i == s_home : false;
        tfd_rect(0, y, x1, rh - 1, cur ? nc(rgb(40, 30, 22)) : (i & 1 ? C_ROW : C_BG), 256);
        tfd_text_l(&KF_NAME, 14, y + 26, CITIES[i].name[s_he ? 1 : 0], cur ? C_ACC : C_TEXT, x1 - 160);
        int o;
        city_local(i, &o, NULL);
        if (o % 3600) {
            snprintf(d, sizeof d, "UTC%+d:%02d", o / 3600, abs(o % 3600) / 60);
        } else {
            snprintf(d, sizeof d, "UTC%+d", o / 3600);
        }
        mf_text(&CK_S, x1 - 16 - tw(&CK_S, d), y + 26, d, C_DIM, 255, 0);
        hit_in(0, y, x1, rh - 1, H_CITY, i, y0, y1);
    }
    top_bar(TX(s_pick_home ? T_HOME : T_CHOOSE_CITY));
}

// ---------------------------------------------------------------- RING

static void draw_ring(void)
{
    const int r = alarm_ringing();
    const alarm_t *a = r >= 0 && r < s_nal ? &s_al[r] : NULL;
    const ctimer_t *tmr = r >= ALARM_MAX && r < ALARM_MAX + TIMER_MAX ? &s_tm[r - ALARM_MAX] : NULL;
    const float ph = (float)(s_now % 1200) / 1200.0f;
    tfd_rect(0, 0, s_W, s_H, nc(rgb(40 + (int)(30 * sinf(ph * 6.283f)), 18, 12)), 256);
    int off;
    const int64_t lt = city_local(s_home, &off, NULL);
    char hm[16];
    const char *ap;
    hm_text(hm, sizeof hm, (int)(lt % 86400 / 3600), (int)(lt % 3600 / 60), &ap);
    const mfont_t *big = s_narrow ? &CK_L : &CK_XL;
    const int area = s_W;
    const int by = s_narrow ? 90 : 86;
    mf_text(big, (area - tw(big, hm)) / 2, by, hm, C_TEXT, 255, 0);
    // the bell shakes
    const float sh = sinf(ph * 6.283f * 3) * 3;
    bell((int)(area / 2 - tw(big, hm) / 2 - 30 + sh), by - big->asc / 2, 2.0f, C_ACC);
    const char *what = tmr ? (tmr->label[0] ? tmr->label : TX(T_TIMER)) : a && a->label[0] ? a->label : TX(T_ALARM);
    tfd_text_c(&KF_NAME, area / 2, by + 26, what, C_TEXT, area - 20);
    // SNOOZE (a tap) and STOP (a hold of 1 s)
    const int bh = s_narrow ? 52 : 44, y = s_H - bh - 8, w = (s_W - 30) / 2;
    char sb[48];
    if (tmr) {
        snprintf(sb, sizeof sb, "%s", TX(T_PLUSMIN));
    } else {
        snprintf(sb, sizeof sb, TX(T_SNOOZE_BTN), a ? a->snooze : 9);
    }
    button(10, y, w, bh, sb, true, H_SNOOZE, 0);
    s_stop_x = 20 + w;
    s_stop_y = y;
    s_stop_w = w;
    s_stop_h = bh;
    tfd_rrect(s_stop_x, y, w, bh, 8, C_BTN, 256);
    if (s_hold_t) {
        const int fw = (int)((int64_t)w * (s_now - s_hold_t) / 1000);
        tfd_rrect(s_stop_x, y, fw < w ? fw : w, bh, 8, C_ON, 256);
    }
    tfd_text_c(&KF_NAME, s_stop_x + w / 2, y + bh / 2 + 6, TX(T_HOLD_STOP), C_TEXT, w - 10);
}

// ---------------------------------------------------------------- render

#if BOARD_ROUND
static void render_page(int64_t now)
#else
static void render(int64_t now)
#endif
{
    if (s_scr == SC_KBD) {
        kbd_render(now);
        return;
    }
    layout();
    tfd_screen();
    tfd_noclip();
    s_nhit = 0;
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    switch (s_scr) {
    case SC_ALARMS:
        draw_alarms();
        break;
    case SC_EDIT:
        draw_edit();
        break;
    case SC_WORLD:
        draw_world();
        break;
    case SC_CITIES:
        draw_cities();
        break;
    case SC_RING:
        draw_ring();
        break;
    case SC_TIMER:
        draw_timer();
        break;
    case SC_TNEW:
        draw_tnew();
        break;
    case SC_SW:
        draw_sw();
        break;
    default:
        draw_clock();
        break;
    }
    toast_draw();
}

// ---------------------------------------------------------------- the menu icon
#include "tficon/tficon.h"
// the menu icon: an alarm clock: the red second hand goes round in 2 s; at the top of its turn the clock rings
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0xffb347), tfi_hex(0xe8430c));
    float u = s / 100.0f, ph = tfi_ph(2);
    float ring = ph < 0.18f ? fsin(ph / 0.18f * 3.14159f * 6) * (1 - ph / 0.18f) : 0;
    float cx = x + 50 * u + ring * 2 * u, cy = y + 54 * u, rad = 29 * u;
    // the bells, the legs
    tfi_disc(cx - 21 * u, cy - 25 * u, 9 * u, tfi_hex(0xfff0b0), tfi_hex(0xd8a020), 256);
    tfi_disc(cx + 21 * u, cy - 25 * u, 9 * u, tfi_hex(0xfff0b0), tfi_hex(0xd8a020), 256);
    tfi_line(cx - 16 * u, cy + 22 * u, cx - 24 * u, cy + 33 * u, 5 * u, true, tfi_hex(0x5a2a10), 256);
    tfi_line(cx + 16 * u, cy + 22 * u, cx + 24 * u, cy + 33 * u, 5 * u, true, tfi_hex(0x5a2a10), 256);
    // the case, the face
    tfi_drop_disc(cx, cy, rad + 4 * u, 3 * u, 80);
    tfi_disc(cx, cy, rad + 4 * u, tfi_hex(0xff6a4a), tfi_hex(0xa8200c), 256);
    tfi_disc(cx, cy, rad, rgb(255, 255, 255), rgb(255, 255, 255), 256);
    for (int k = 0; k < 12; k++) {
        float a = k * 0.5236f, r0 = (k % 3 ? 24 : 21) * u;
        tfi_line(cx + fcos(a) * r0, cy + fsin(a) * r0, cx + fcos(a) * 26 * u, cy + fsin(a) * 26 * u,
                 (k % 3 ? 1.6f : 2.6f) * u, false, tfi_hex(0x3a3a4a), 256);
    }
    // the hands at 10:10 and the second hand
    tfi_line(cx, cy, cx + fcos(-2.62f) * 14 * u, cy + fsin(-2.62f) * 14 * u, 4 * u, true, tfi_hex(0x2a2a36), 256);
    tfi_line(cx, cy, cx + fcos(-0.52f) * 20 * u, cy + fsin(-0.52f) * 20 * u, 3 * u, true, tfi_hex(0x2a2a36), 256);
    float sa = -1.5708f + ph * 6.2831853f;
    tfi_line(cx - fcos(sa) * 6 * u, cy - fsin(sa) * 6 * u, cx + fcos(sa) * 23 * u, cy + fsin(sa) * 23 * u, 1.8f * u,
             true, tfi_hex(0xe8301c), 256);
    tfi_disc(cx, cy, 3 * u, tfi_hex(0xff5a40), tfi_hex(0xb02010), 256);
    // the sound of the ring
    if (ring != 0) {
        int a = 256;
        tfi_arc(cx, cy - 25 * u, 15 * u, 2.4f * u, -2.6f, -2.0f, rgb(255, 255, 255), a);
        tfi_arc(cx, cy - 25 * u, 15 * u, 2.4f * u, -1.14f, -0.54f, rgb(255, 255, 255), a);
    }
    tfi_end();
}
