// STANDBY: the three faces (standby.h) on each frame, each with its own layout:
//   LY_BAR   the 3.49 bar (640 x 172): the time at the left, the rest at the right
//   LY_TALL  a narrow upright frame (the 3.49 upright 172 x 640, the CoreS3 upright 240 x 320): the hours over the
//            minutes
//   LY_WIDE  a landscape frame (the CoreS3 320 x 240, the Tab5 640 x 360, the AMOLED-1.8 on its side)
//   LY_PORT  an upright frame (the AMOLED-1.8 368 x 448, the Tab5 upright)
//   LY_ROUND the round boards (the 1.75C 466, the Watcher 412): all of the circle
// The text: the big numbers in Inter (libs/interfont/sb_fonts.inc), the words in the quest fonts (English and
// Hebrew, uikit.h). The slots of GLANCE are big tiles (about 10 mm high or more on each board).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "alarm.h"
#include "board.h"
#include "friends.h"
#include "gfx.h"
#include "kfont.h"
#include "notify.h"
#include "settings.h"
#include "standby_priv.h"
#include "stocks.h"
#include "tfn/mfont.h"
#include "uikit.h"
#include "interfont/sb_fonts.inc"

enum {
    LY_BAR,
    LY_TALL,
    LY_WIDE,
    LY_PORT,
    LY_ROUND
};
enum {
    AL_L,
    AL_C,
    AL_R
};
enum {
    IC_NONE,
    IC_SUN,
    IC_MOON,
    IC_CLOUD,
    IC_RAIN,
    IC_SNOW,
    IC_STORM,
    IC_FOG
};

static bool s_shadow;          // the text has a shadow (on the sky)
static uint16_t s_ink, s_dim;  // the colours of the text (sb_draw_frame sets them)

static int layout(void)
{
    if (gfx_round()) {
        return LY_ROUND;
    }
    if (gfx_w * 2 >= gfx_h * 5) {
        return LY_BAR;
    }
    if (gfx_h > gfx_w && (gfx_w < 290 || gfx_h * 2 >= gfx_w * 5)) {
        return LY_TALL;
    }
    return gfx_w > gfx_h ? LY_WIDE : LY_PORT;
}

// ---------------------------------------------------------------- the text
static int al_x(int x, int w, int al)
{
    return al == AL_L ? x : al == AL_C ? x - w / 2 : x - w;
}
// a line in a quest font (English or Hebrew); the largest of the fonts that fits in maxw (0 = no limit)
static int kt(const kfont_t *f, int x, int base, const char *s, uint16_t c, int al, int maxw)
{
    if (!s || !*s) {
        return 0;
    }
    if (maxw > 0 && ui_text_w(f, s) > maxw) {
        static const kfont_t *const SMALLER[] = {&KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
        int i = 0;
        while (i < 3 && SMALLER[i] != f) {
            i++;
        }
        f = ui_text_fit(SMALLER + i, 4 - i, s, maxw);
        if (ui_text_w(f, s) > maxw) {
            return 0;  // (no room even for the smallest font: no line over another thing)
        }
    }
    const int w = ui_text_w(f, s), x0 = al_x(x, w, al);
    if (s_shadow) {
        ui_text(f, x0 + 1, base + 1, s, THEME_STANDBY_SHADOW);
    }
    ui_text(f, x0, base, s, c);
    return w;
}
// a number in Inter (the bytes of sb_fonts.inc: digits, ':', '-', '+', '%', the degree 0xB0)
static int mt(const mfont_t *f, int x, int base, const char *s, uint16_t c, int al)
{
    const int w = mf_width(f, s, 0), x0 = al_x(x, w, al);
    if (s_shadow) {
        mf_text(f, x0 + 2, base + 2, s, THEME_STANDBY_SHADOW, 110, 0);
    }
    mf_text(f, x0, base, s, c, 255, 0);
    return w;
}

// ---------------------------------------------------------------- the texts of the moment
typedef struct {
    char hm[8], hh[4], mm[4];            // the time ("--:--" when the board does not know it)
    char day_en[12], dm_en[24];          // "Friday", "13 November"
    char day_he[24], hd_he[48], hy[16];  // "יום שישי", "כ״ב בחשוון", "תשפ״ז"
    char next[64];                       // "Alarm 06:45"
    char temp[12], cond[48], hilo[32];   // "22°", "Partly cloudy", "H 26  L 17"
    int icon;
    bool wx;
} txt_t;

static bool h24(void)
{
    const uint32_t o = store_get("ck_opts", 0);  // (the choice of CLOCK: 24 h unless it says 12 h)
    return !(o & 0x80) || (o & 1);
}
static void fmt_hm(const sb_now_t *n, int64_t utc, char *o, int max)
{
    tz_tm_t t;
    tz_split(utc + n->off, &t);
    const int h = h24() ? t.h : (t.h % 12 ? t.h % 12 : 12);
    snprintf(o, max, h24() ? "%02d:%02d" : "%d:%02d", h, t.mi);
}
static int icon_of(int code, bool day)
{
    switch (sb_sky_kind(code)) {
    case SK_CLEAR:
        return day ? IC_SUN : IC_MOON;
    case SK_FOG:
        return IC_FOG;
    case SK_DRIZZLE:
    case SK_RAIN:
    case SK_HEAVY:
        return IC_RAIN;
    case SK_SNOW:
        return IC_SNOW;
    case SK_STORM:
        return IC_STORM;
    default:
        return IC_CLOUD;
    }
}
static const char *next_name(int kind, bool he)
{
    static const char *const EN[] = {"", "Timer", "Alarm", "Candles", "Havdalah", "Sunrise", "Sunset"};
    static const char *const HE[] = {"", "טיימר", "השכמה", "הדלקת נרות", "הבדלה", "זריחה", "שקיעה"};
    return (he ? HE : EN)[(unsigned)kind <= NX_SUNSET ? kind : 0];
}
// the value of NEXT: a timer = the time that is left, else the clock time of the item
static void next_value(const sb_now_t *n, char *o, int max)
{
    if (n->nx_kind == NX_TIMER) {
        const int s = (int)(n->nx_at - n->utc);
        if (s >= 3600) {
            snprintf(o, max, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        } else {
            snprintf(o, max, "%d:%02d", s / 60, s % 60);
        }
        return;
    }
    fmt_hm(n, n->nx_at, o, max);
}

// the day of a later item: "" (today), "tomorrow", else the short day name
static const char *day_word(const sb_now_t *n, int64_t at, bool he)
{
    static const char *const EN[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const HE[7] = {"יום א׳", "יום ב׳", "יום ג׳", "יום ד׳", "יום ה׳", "יום ו׳", "שבת"};
    const int64_t d0 = (n->utc + n->off) / 86400, d1 = (at + n->off) / 86400;
    if (d1 <= d0) {
        return "";
    }
    if (d1 == d0 + 1) {
        return he ? "מחר" : "tomorrow";
    }
    tz_tm_t tm;
    tz_split(at + n->off, &tm);
    return (he ? HE : EN)[(tm.wd + 7) % 7];
}

static void texts(const sb_now_t *n, txt_t *t)
{
    static const char *const DAY[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const DHE[7] = {"ראשון", "שני", "שלישי", "רביעי", "חמישי", "שישי", "שבת"};
    static const char *const MON[12] = {"January", "February", "March",     "April",   "May",      "June",
                                        "July",    "August",   "September", "October", "November", "December"};
    const bool he = sb_he();
    memset(t, 0, sizeof *t);
    if (n->time_ok) {
        fmt_hm(n, n->utc, t->hm, sizeof t->hm);
        const char *c = strchr(t->hm, ':');
        snprintf(t->hh, sizeof t->hh, "%.*s", (int)(c - t->hm), t->hm);
        snprintf(t->mm, sizeof t->mm, "%s", c + 1);
        const int wd = (n->tm.wd + 7) % 7;
        snprintf(t->day_en, sizeof t->day_en, "%s", DAY[wd]);
        snprintf(t->dm_en, sizeof t->dm_en, "%d %s", n->tm.d, MON[(n->tm.mo + 11) % 12]);
        snprintf(t->day_he, sizeof t->day_he, wd == 6 ? "%s" : "יום %s", DHE[wd]);
        sb_hdate(n, t->hd_he, sizeof t->hd_he, false);
        char y[48];
        sb_hdate(n, y, sizeof y, true);
        snprintf(t->hy, sizeof t->hy, "%s", y + strlen(t->hd_he) + (y[strlen(t->hd_he)] ? 1 : 0));
    } else {
        snprintf(t->hm, sizeof t->hm, "--:--");
        snprintf(t->hh, sizeof t->hh, "--");
        snprintf(t->mm, sizeof t->mm, "--");
    }
    if (n->nx_kind != NX_NONE) {
        char v[16];
        next_value(n, v, sizeof v);
        const char *dw = n->nx_kind == NX_TIMER ? "" : day_word(n, n->nx_at, he);
        snprintf(t->next, sizeof t->next, "%s %s%s%s", next_name(n->nx_kind, he), dw, dw[0] ? " " : "", v);
    }
    const sbw_t *w = sb_wx();
    t->wx = w->at != 0;
    if (t->wx) {
        snprintf(t->temp, sizeof t->temp, "%d\xB0", (int)lroundf(w->t));
        snprintf(t->cond, sizeof t->cond, "%s", sb_cond(w->code, he));
        if (!isnan(w->hi) && !isnan(w->lo)) {
            snprintf(t->hilo, sizeof t->hilo, he ? "%d / %d" : "H %d  L %d", (int)lroundf(w->hi), (int)lroundf(w->lo));
        }
        t->icon = icon_of(w->code, n->elev > -1);
    } else {
        snprintf(t->temp, sizeof t->temp, "--");
        snprintf(t->cond, sizeof t->cond, "%s", he ? "אין עדיין נתוני מזג אוויר" : "No weather data yet");
    }
}

// ---------------------------------------------------------------- the small weather icons
static void icon(int k, int cx, int cy, int r)
{
    if (k == IC_SUN) {
        for (int i = 0; i < 8; i++) {
            const float a = i * 0.7853982f;
            ui_circle(cx + (int)(fcos(a) * r * 0.85f), cy + (int)(fsin(a) * r * 0.85f), r / 7 + 1,
                      THEME_STANDBY_ICON_SUN, 230);
        }
        ui_circle(cx, cy, r * 5 / 9, THEME_STANDBY_ICON_SUN, 255);
        return;
    }
    if (k == IC_MOON) {
        ui_circle(cx, cy, r * 3 / 5, THEME_STANDBY_ICON_MN, 255);
        ui_circle(cx + r / 3, cy - r / 4, r / 2, rgb(24, 26, 56), 235);
        return;
    }
    if (k == IC_FOG) {
        for (int i = 0; i < 3; i++) {
            ui_rrect_a(cx - r + (i & 1) * r / 4, cy - r / 2 + i * r / 2, r * 7 / 4, r / 4 + 1, r / 8 + 1,
                       THEME_STANDBY_ICON_CL, 220);
        }
        return;
    }
    if (k == IC_NONE) {
        return;
    }
    const int y = k == IC_CLOUD ? cy : cy - r / 4;
    ui_circle(cx - r / 3, y, r * 2 / 5, THEME_STANDBY_ICON_CL, 255);
    ui_circle(cx + r / 5, y - r / 5, r / 2, THEME_STANDBY_ICON_CL, 255);
    ui_rrect_a(cx - r * 3 / 4, y, r * 3 / 2, r * 2 / 5, r / 5, THEME_STANDBY_ICON_CL, 255);
    for (int i = 0; i < 3 && k != IC_CLOUD; i++) {
        const int x = cx - r / 2 + i * r / 2, yy = y + r * 3 / 5;
        if (k == IC_SNOW) {
            ui_circle(x, yy + r / 6, r / 8 + 1, THEME_STANDBY_TEXT, 255);
        } else if (k == IC_STORM && i == 1) {
            ui_rrect_a(x - 1, yy, 3, r / 2, 1, THEME_STANDBY_ICON_SUN, 255);
        } else {
            ui_rrect_a(x - 1, yy, 2, r / 3, 1, THEME_STANDBY_ICON_RN, 255);
        }
    }
}

// ---------------------------------------------------------------- the slots of GLANCE
typedef struct {
    char label[48], val[24], big[64], s1[64], s2[64];
    char low_label[64], low_sub[32];  // a low slot: the label (when not empty) and the short line by the value
    int icon;
} tile_t;

static void tile_weather(const sb_now_t *n, const txt_t *x, tile_t *t, bool he)
{
    (void)n;
    const sb_place_t *p = sb_place();
    snprintf(t->label, sizeof t->label, "%s", p->ok && p->name[0] ? p->name : he ? "מזג אוויר" : "WEATHER");
    snprintf(t->val, sizeof t->val, "%s", x->temp);
    if (x->wx) {
        snprintf(t->s1, sizeof t->s1, "%s", x->cond);
        snprintf(t->s2, sizeof t->s2, "%s", x->hilo);
        snprintf(t->low_sub, sizeof t->low_sub, "%.31s", x->hilo);
        t->icon = x->icon;
    } else {
        snprintf(t->s1, sizeof t->s1, "%s", he ? "אין נתונים" : "NO DATA YET");
        snprintf(t->s2, sizeof t->s2, "Open-Meteo.com");
    }
}
static void tile_hebrew(const sb_now_t *n, const txt_t *x, tile_t *t, bool he)
{
    snprintf(t->label, sizeof t->label, "%s", he ? "תאריך עברי" : "HEBREW DATE");
    snprintf(t->big, sizeof t->big, "%s", x->hd_he[0] ? x->hd_he : "--");
    if (n->shab_kind != NX_NONE) {
        char v[16];
        fmt_hm(n, n->shab_at, v, sizeof v);
        const char *dw = day_word(n, n->shab_at, he);
        snprintf(t->s1, sizeof t->s1, "%s %s%s%s", next_name(n->shab_kind, he), dw, dw[0] ? " " : "", v);
        snprintf(t->low_label, sizeof t->low_label, "%s", t->s1);
    }
    snprintf(t->s2, sizeof t->s2, "%s", x->hy);
}
static void tile_notices(tile_t *t, bool he)
{
    snprintf(t->label, sizeof t->label, "%s", he ? "התראות חדשות" : "NEW NOTICES");
    snprintf(t->val, sizeof t->val, "%d", notify_unread(NULL));
    static char names[NT_MAX][NT_NAME];
    const int na = notify_apps(names, NT_MAX);
    int k = 0;
    for (int i = 0; i < na && k < 2; i++) {
        const int u = notify_unread(names[i]);
        if (u > 0) {
            snprintf(k ? t->s2 : t->s1, sizeof t->s1, "%.40s %d", names[i], u);
            k++;
        }
    }
    if (!k) {
        snprintf(t->s1, sizeof t->s1, "%s", he ? "אין חדשות" : "NOTHING NEW");
    }
}
static void tile_next(const sb_now_t *n, tile_t *t, bool he)
{
    if (n->nx_kind == NX_NONE) {
        snprintf(t->label, sizeof t->label, "%s", he ? "הבא" : "NEXT");
        snprintf(t->val, sizeof t->val, "--");
        return;
    }
    snprintf(t->label, sizeof t->label, "%s", next_name(n->nx_kind, he));
    if (!he) {
        for (char *c = t->label; *c; c++) {
            *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
        }
    }
    next_value(n, t->val, sizeof t->val);
    if (n->nx_kind == NX_TIMER) {
        snprintf(t->s1, sizeof t->s1, "%s", he ? "נותר" : "LEFT");
        snprintf(t->low_sub, sizeof t->low_sub, "%.31s", t->s1);
        return;
    }
    const int m = (int)((n->nx_at - n->utc + 59) / 60);
    const char *dw = day_word(n, n->nx_at, he);
    if (m < 60) {
        snprintf(t->s1, sizeof t->s1, he ? "בעוד %d דקות" : "IN %d MIN", m);
        snprintf(t->low_sub, sizeof t->low_sub, he ? "בעוד %d ד׳" : "IN %d MIN", m);
    } else {
        snprintf(t->s1, sizeof t->s1, he ? "בעוד %d:%02d שעות" : "IN %d H %02d MIN", m / 60, m % 60);
        snprintf(t->low_sub, sizeof t->low_sub, "%s", dw[0] ? dw : he ? "היום" : "today");
    }
    snprintf(t->s2, sizeof t->s2, "%s", dw);
}
static void tile_other(int kind, tile_t *t, bool he)
{
    if (kind == SL_BATTERY) {
        bool chg = false;
        const int p = board_battery(&chg, NULL);
        snprintf(t->label, sizeof t->label, "%s", he ? "סוללה" : "BATTERY");
        if (p >= 0) {
            snprintf(t->val, sizeof t->val, "%d%%", p);
        } else {
            snprintf(t->val, sizeof t->val, "--");
        }
        snprintf(t->s1, sizeof t->s1, "%s",
                 chg     ? (he ? "בטעינה" : "CHARGING")
                 : p < 0 ? (he ? "אין סוללה" : "NO BATTERY")
                         : "");
        return;
    }
    if (kind == SL_FRIENDS) {
        int idx[8];
        const int nn = friends_near(idx, 8);
        snprintf(t->label, sizeof t->label, "%s", he ? "חברים קרובים" : "FRIENDS NEAR");
        snprintf(t->val, sizeof t->val, "%d", nn);
        if (nn > 0 && friends_get(idx[0])) {
            snprintf(t->s1, sizeof t->s1, "%s", friends_get(idx[0])->name);
        }
        return;
    }
    snprintf(t->label, sizeof t->label, "STOCKS");
    snprintf(t->val, sizeof t->val, "--");
#if CONFIG_TFN_EXTRA_STOCKS
    static stk_quote_t q[STK_MAX];
    static stk_state_t st;
    const int nq = stocks_get(q, &st);
    for (int i = 0; i < nq; i++) {
        if (q[i].qt) {
            snprintf(t->label, sizeof t->label, "%s", q[i].sym);
            snprintf(t->val, sizeof t->val, "%+.1f%%", (double)q[i].dp);
            snprintf(t->s1, sizeof t->s1, "%.2f", (double)q[i].c);
            return;
        }
    }
#endif
    snprintf(t->s1, sizeof t->s1, "%s", he ? "פתחו את STOCKS" : "OPEN STOCKS");
}

static void tile_fill(int kind, const sb_now_t *n, const txt_t *x, tile_t *t)
{
    const bool he = sb_he();
    memset(t, 0, sizeof *t);
    switch (kind) {
    case SL_WEATHER:
        tile_weather(n, x, t, he);
        break;
    case SL_HEBREW:
        tile_hebrew(n, x, t, he);
        break;
    case SL_NOTICES:
        tile_notices(t, he);
        break;
    case SL_NEXT:
        tile_next(n, t, he);
        break;
    default:
        tile_other(kind, t, he);
        break;
    }
}

// the hours of the weather in a big slot: up to 4 columns (the hour, the temperature) on the line base
static void tile_hours(const sb_now_t *n, int x, int w, int base)
{
    const sbw_t *wx = sb_wx();
    int k = 0;
    for (int i = 0; i < wx->nh && k < 4; i++) {
        const int64_t at = wx->h0 + (int64_t)i * 3600;
        if (at <= n->utc) {
            continue;
        }
        tz_tm_t tm;
        tz_split(at + n->off, &tm);
        char h[8], v[12];
        snprintf(h, sizeof h, "%02d", tm.h);
        snprintf(v, sizeof v, "%d\xB0", (int)lroundf(wx->ht[i]));
        const int cx = x + w * (2 * k + 1) / 8;
        kt(&KF_LABEL, cx, base - SB_S.asc - 6, h, s_dim, AL_C, 0);
        mt(&SB_S, cx, base, v, s_ink, AL_C);
        k++;
    }
}

// the lines of a slot under the value (a high slot), or the short line by the value (a low slot)
static void tile_lines(int kind, const tile_t *t, int x, int y, int w, int h, int vb, int vw, const sb_now_t *n)
{
    const bool he = sb_he();
    const int pad = w < 130 ? 8 : 12, al = he ? AL_R : AL_L, ax = he ? x + w - pad : x + pad;
    const int room = y + h - pad - vb;
    if (kind == SL_WEATHER && t->icon && w >= 240 && room >= 64) {
        kt(&KF_NAME, ax, vb + 20, t->s1, s_dim, al, w - 2 * pad);
        tile_hours(n, x, w, y + h - pad);
    } else if (room >= 18) {
        kt(&KF_NAME, ax, vb + 18, t->s1, s_dim, al, w - 2 * pad);
        if (room >= 36) {
            kt(&KF_NAME, ax, vb + 36, t->s2, s_dim, al, w - 2 * pad);
        }
    } else if (t->low_sub[0]) {
        // (the value is at the right for Hebrew and for a Hebrew date: the short line at the left)
        const bool left = he || !t->val[0];
        const int mw = w - 2 * pad - vw - 10;
        if (mw > 20) {
            kt(&KF_NAME, left ? x + pad : x + w - pad, vb, t->low_sub, s_dim, left ? AL_L : AL_R, mw);
        }
    }
}

static void draw_tile(int i, int kind, int x, int y, int w, int h, const sb_now_t *n, const txt_t *x_t)
{
    if (kind == SL_NONE || w < 40 || h < 30) {
        return;
    }
    sb_hit[sb_hit_n++] = (sb_hit_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (uint8_t)kind};
    tile_t t;
    tile_fill(kind, n, x_t, &t);
    const bool he = sb_he(), down = sb_pressed() == i, low = h < 92;
    const int r = h / 5 < 8 ? 8 : h / 5 > 16 ? 16 : h / 5;
    ui_rrect_a(x, y, w, h, r, down ? THEME_STANDBY_TILE_HI : THEME_STANDBY_TILE, down ? 210 : 150);
    ui_rrect_band(x, y, w, h, r, THEME_STANDBY_TILE_EDGE, 70, 2);  // (a light edge: the slot shows also on a night sky)
    const int pad = w < 130 ? 8 : 12;
    const int al = he ? AL_R : AL_L, ax = he ? x + w - pad : x + pad;
    const int ir = low ? 11 : 15;
    const int lw = w - 2 * pad - (t.icon ? 2 * ir + 6 : 0);
    const bool sh = s_shadow;
    s_shadow = false;
    if (low && t.low_label[0] && kt(&KF_LABEL, ax, y + pad + 11, t.low_label, sb_accent(), al, lw)) {
        memcpy(t.s1, t.s2, sizeof t.s1);  // (the label has the first line now)
        t.s2[0] = 0;
    } else if (!kt(&KF_LABEL, ax, y + pad + 11, t.label, sb_accent(), al, lw)) {
        // (a narrow slot: the label with no icon, else its first word)
        t.icon = 0;
        if (!kt(&KF_LABEL, ax, y + pad + 11, t.label, sb_accent(), al, w - 2 * pad)) {
            char *sp = strchr(t.label, ' ');
            if (sp) {
                *sp = 0;
            }
            kt(&KF_LABEL, ax, y + pad + 11, t.label, sb_accent(), al, w - 2 * pad);
        }
    }
    if (t.icon) {
        icon(t.icon, he ? x + pad + ir : x + w - pad - ir, y + pad + ir - 2, ir);
    }
    const mfont_t *vf = h >= 68 && mf_width(&SB_V, t.val, 0) <= w - 2 * pad ? &SB_V : &SB_S;  // (a narrow slot)
    const int vb = y + pad + 11 + 8 + (t.val[0] ? vf->asc : KF_BIG.asc);
    int vw;
    if (t.val[0]) {
        vw = mt(vf, ax, vb, t.val, s_ink, al);
    } else {
        vw = kt(&KF_BIG, x + w - pad, vb, t.big, s_ink, AL_R, w - 2 * pad);
    }
    tile_lines(kind, &t, x, y, w, h, vb, vw, n);
    s_shadow = sh;
}

// the four slots in a 2 x 2 grid in the box (gap g)
static void tiles_grid(int x, int y, int w, int h, int g, const sb_now_t *n, const txt_t *t)
{
    const int tw = (w - g) / 2, th = (h - g) / 2;
    for (int i = 0; i < SB_SLOTS; i++) {
        draw_tile(i, sb_cfg.slot[i], x + (i & 1) * (tw + g), y + (i >> 1) * (th + g), tw, th, n, t);
    }
}

// ---------------------------------------------------------------- the parts of the faces
// the weather of LIVE SKY: the icon and the temperature on base, the condition and the high / low under it
static void sky_weather(int x, int base, int al, const txt_t *t)
{
    const int tw = mf_width(&SB_V, t->temp, 0), ir = 16;
    const int full = tw + (t->icon ? 2 * ir + 8 : 0);
    const int x0 = al_x(x, full, al);
    if (t->icon) {
        icon(t->icon, x0 + ir, base - SB_V.asc / 2, ir);
    }
    mt(&SB_V, x0 + full - tw, base, t->temp, s_ink, AL_L);
}
// the line of NEXT in the accent colour (a dark pill under it on the sky)
static void next_line(int x, int base, int al, const txt_t *t, const kfont_t *f, int maxw)
{
    if (!t->next[0]) {
        return;
    }
    kt(f, x, base, t->next, sb_accent(), al, maxw);
}

static void face_sky_bar(const txt_t *t)
{
    const int W = gfx_w;
    mt(&SB_T, 24, 96, t->hm, s_ink, AL_L);
    char d[48];
    snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
    kt(&KF_BIG, 26, 130, d, s_dim, AL_L, 300);
    snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
    kt(&KF_TEXT, 26, 158, t->day_he[0] ? d : "", s_dim, AL_L, 300);
    sky_weather(W - 22, 62, AL_R, t);
    kt(&KF_TEXT, W - 22, 92, t->cond, s_ink, AL_R, 280);
    kt(&KF_NAME, W - 22, 116, t->hilo, s_dim, AL_R, 280);
    next_line(W - 22, 156, AL_R, t, &KF_TEXT, 290);
}

static void face_sky_tall(const txt_t *t)
{
    const int W = gfx_w, H = gfx_h, cx = W / 2;
    const bool big = H >= 500;  // (the 3.49 upright; the CoreS3 upright is 320 high)
    char d[64];
    snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
    kt(&KF_TEXT, cx, big ? 40 : 22, d, s_dim, AL_C, W - 12);
    const int hb = big ? 150 : 104, mb = big ? 255 : 186;
    mt(&SB_T, cx, hb, t->hh, s_ink, AL_C);
    mt(&SB_T, cx, mb, t->mm, s_ink, AL_C);
    if (big) {
        kt(&KF_BIG, cx, 300, t->day_en, s_dim, AL_C, W - 12);
        kt(&KF_TEXT, cx, 326, t->dm_en, s_dim, AL_C, W - 12);
        sky_weather(cx, 430, AL_C, t);
        kt(&KF_TEXT, cx, 462, t->cond, s_ink, AL_C, W - 12);
        kt(&KF_NAME, cx, 486, t->hilo, s_dim, AL_C, W - 12);
        next_line(cx, 590, AL_C, t, &KF_TEXT, W - 12);
        return;
    }
    snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
    kt(&KF_NAME, cx, 214, d, s_dim, AL_C, W - 12);
    sky_weather(cx, 262, AL_C, t);
    next_line(cx, 302, AL_C, t, &KF_NAME, W - 12);
}

static void face_sky_wide(const txt_t *t)
{
    const int W = gfx_w, H = gfx_h, cx = W / 2;
    char d[64];
    if (W >= 560) {
        // (a wide frame: the time at the left, the weather at the right)
        const int tb = H / 2 + 20, lx = W / 16;
        snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
        kt(&KF_TEXT, lx, tb - SB_T.asc - 22, d, s_dim, AL_L, W / 2);
        mt(&SB_T, lx, tb, t->hm, s_ink, AL_L);
        snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
        kt(&KF_BIG, lx, tb + 40, d, s_dim, AL_L, W / 2);
        sky_weather(W - lx, H / 3, AL_R, t);
        kt(&KF_BIG, W - lx, H / 3 + 34, t->cond, s_ink, AL_R, W / 3);
        kt(&KF_TEXT, W - lx, H / 3 + 62, t->hilo, s_dim, AL_R, W / 3);
        next_line(W - lx, H - H / 8, AL_R, t, &KF_BIG, W / 2);
        return;
    }
    const float k = H / 240.0f;
    snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
    kt(&KF_TEXT, cx, (int)(26 * k), d, s_dim, AL_C, W - 16);
    mt(&SB_T, cx, (int)(108 * k), t->hm, s_ink, AL_C);
    snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
    kt(&KF_TEXT, cx, (int)(136 * k), d, s_dim, AL_C, W - 16);
    // the weather on one line: the icon, the temperature, the condition
    const int cw = ui_text_w(&KF_TEXT, t->cond) + 12, tw = mf_width(&SB_V, t->temp, 0) + (t->icon ? 40 : 0);
    sky_weather(cx - (cw + tw) / 2 + tw, (int)(182 * k), AL_R, t);
    kt(&KF_TEXT, cx - (cw + tw) / 2 + tw + 12, (int)(182 * k) - 6, t->cond, s_ink, AL_L, W - tw - 24);
    next_line(cx, (int)(220 * k), AL_C, t, &KF_TEXT, W - 16);
}

static void face_sky_port(const txt_t *t)
{
    const int W = gfx_w, H = gfx_h, cx = W / 2;
    char d[64];
    snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
    kt(&KF_TEXT, cx, H * 10 / 100, d, s_dim, AL_C, W - 20);
    mt(&SB_T, cx, H * 32 / 100, t->hm, s_ink, AL_C);
    snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
    kt(&KF_BIG, cx, H * 32 / 100 + 38, d, s_dim, AL_C, W - 20);
    sky_weather(cx, H * 60 / 100, AL_C, t);
    kt(&KF_BIG, cx, H * 60 / 100 + 34, t->cond, s_ink, AL_C, W - 20);
    kt(&KF_TEXT, cx, H * 60 / 100 + 60, t->hilo, s_dim, AL_C, W - 20);
    next_line(cx, H * 87 / 100, AL_C, t, &KF_BIG, W - 30);
}

// the seconds on the rim of a round frame (the accent colour)
static void rim_seconds(const sb_now_t *n, int64_t now)
{
    const int S = gfx_w, c = S / 2, r = S / 2 - 12;
    ui_ring(c, c, r, 5, THEME_STANDBY_TEXT, 36);
    const float f = (n->tm.s + (float)(now % 1000) / 1000.0f) / 60.0f;
    const int steps = (int)(f * 240);
    for (int i = 0; i <= steps; i++) {
        const float a = -1.5707963f + i * (6.2831853f / 240);
        ui_circle(c + (int)(fcos(a) * r), c + (int)(fsin(a) * r), 3, sb_accent(), 255);
    }
}

static void face_sky_round(const sb_now_t *n, const txt_t *t, int64_t now)
{
    const int S = gfx_w, c = S / 2;
    rim_seconds(n, now);
    char d[64];
    snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
    const sb_place_t *p = sb_place();
    kt(&KF_NAME, c, S * 15 / 100, p->ok ? p->name : "", s_dim, AL_C, S / 2);
    kt(&KF_TEXT, c, S * 27 / 100, d, s_dim, AL_C, S * 7 / 10);
    mt(&SB_T, c, S / 2 + 20, t->hm, s_ink, AL_C);
    snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
    kt(&KF_BIG, c, S / 2 + 56, d, s_dim, AL_C, S * 3 / 4);
    // the weather on one line under the date
    const int tw = mf_width(&SB_V, t->temp, 0) + (t->icon ? 40 : 0);
    const int cw = ui_text_w(&KF_TEXT, t->cond) + 12;
    const int x0 = c - (tw + cw) / 2;
    sky_weather(x0 + tw, S * 75 / 100, AL_R, t);
    kt(&KF_TEXT, x0 + tw + 12, S * 75 / 100 - 6, t->cond, s_ink, AL_L, S / 2);
    next_line(c, S * 86 / 100, AL_C, t, &KF_TEXT, S * 3 / 5);
}

// ---------------------------------------------------------------- GLANCE
static void face_glance(const sb_now_t *n, const txt_t *t)
{
    const int W = gfx_w, H = gfx_h, cx = W / 2;
    char d[64];
    switch (layout()) {
    case LY_BAR:
        mt(&SB_T, 18, 94, t->hm, s_ink, AL_L);
        snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
        kt(&KF_TEXT, 20, 126, d, s_dim, AL_L, 290);
        snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
        kt(&KF_NAME, 20, 152, d, s_dim, AL_L, 290);
        tiles_grid(326, 8, W - 334, H - 16, 8, n, t);
        return;
    case LY_TALL:
        if (H >= 500) {
            snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
            kt(&KF_NAME, cx, 28, d, s_dim, AL_C, W - 12);
            mt(&SB_T, cx, 120, t->hh, s_ink, AL_C);
            mt(&SB_T, cx, 212, t->mm, s_ink, AL_C);
            snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
            kt(&KF_TEXT, cx, 246, d, s_dim, AL_C, W - 12);
            for (int i = 0; i < SB_SLOTS; i++) {
                draw_tile(i, sb_cfg.slot[i], 8, 262 + i * 94, W - 16, 88, n, t);
            }
            return;
        }
        mt(&SB_V, cx, 46, t->hm, s_ink, AL_C);
        snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
        kt(&KF_NAME, cx, 72, d, s_dim, AL_C, W - 12);
        tiles_grid(8, 84, W - 16, H - 92, 8, n, t);
        return;
    case LY_WIDE:
        if (W >= 560) {
            const int lx = W / 20, tb = H / 2 + 10;
            mt(&SB_T, lx, tb, t->hm, s_ink, AL_L);
            snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
            kt(&KF_BIG, lx, tb + 40, d, s_dim, AL_L, W / 2 - lx);
            snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
            kt(&KF_TEXT, lx, tb + 70, d, s_dim, AL_L, W / 2 - lx);
            tiles_grid(W / 2, 12, W / 2 - 12, H - 24, 10, n, t);
            return;
        }
        mt(&SB_T, cx, 80 * H / 240, t->hm, s_ink, AL_C);
        snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
        kt(&KF_NAME, cx, 100 * H / 240, d, s_dim, AL_C, W - 16);
        tiles_grid(8, 110 * H / 240, W - 16, H - 110 * H / 240 - 8, 8, n, t);
        return;
    case LY_PORT:
        snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
        kt(&KF_TEXT, cx, H * 8 / 100, d, s_dim, AL_C, W - 20);
        mt(&SB_T, cx, H * 8 / 100 + 26 + SB_T.asc, t->hm, s_ink, AL_C);
        snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
        kt(&KF_TEXT, cx, H * 8 / 100 + 60 + SB_T.asc, d, s_dim, AL_C, W - 20);
        tiles_grid(12, H * 8 / 100 + 76 + SB_T.asc, W - 24, H - (H * 8 / 100 + 76 + SB_T.asc) - 12, 10, n, t);
        return;
    default: {
        // the round frame: two slots over the time and two under it, all in the circle
        const int S = W, c = S / 2, tw = S * 33 / 100, g = S * 2 / 100;
        for (int i = 0; i < SB_SLOTS; i++) {
            const int y = i < 2 ? S * 13 / 100 : S * 70 / 100;
            draw_tile(i, sb_cfg.slot[i], c - tw - g / 2 + (i & 1) * (tw + g), y, tw, S * 17 / 100, n, t);
        }
        snprintf(d, sizeof d, "%s, %s", t->day_he, t->hd_he);
        kt(&KF_TEXT, c, S * 38 / 100, d, s_dim, AL_C, S * 3 / 4);
        mt(&SB_T, c, S * 40 / 100 + 4 + SB_T.asc, t->hm, s_ink, AL_C);
        snprintf(d, sizeof d, "%s %s", t->day_en, t->dm_en);
        kt(&KF_TEXT, c, S * 40 / 100 + 28 + SB_T.asc, d, s_dim, AL_C, S * 3 / 4);
        return;
    }
    }
}

// ---------------------------------------------------------------- NIGHT
// dim warm text on black; all of it moves a few px each minute (no burn-in of the AMOLED pixels)
static void face_night(const sb_now_t *n, const txt_t *t)
{
    gfx_fill(THEME_STANDBY_NIGHT_BG);
    const int W = gfx_w, H = gfx_h;
    const int m = n->tm.h * 60 + n->tm.mi;
    const int dx = (m * 7) % 9 - 4, dy = (m * 5) % 7 - 3;
    const uint16_t warm = THEME_STANDBY_NIGHT, dim = ui_mix(THEME_STANDBY_NIGHT_BG, THEME_STANDBY_NIGHT, 150);
    s_shadow = false;
    const char *line = n->nx_kind == NX_TIMER || n->nx_kind == NX_ALARM ? t->next : "";
    if (layout() == LY_TALL) {
        const int y = H / 2 + dy;
        mt(&SB_T, W / 2 + dx, y - 12, t->hh, warm, AL_C);
        mt(&SB_T, W / 2 + dx, y + SB_T.asc + 12, t->mm, warm, AL_C);
        kt(&KF_NAME, W / 2 + dx, y + SB_T.asc + 60, line, dim, AL_C, W - 12);
        return;
    }
    const int base = H / 2 + SB_T.asc / 2 + dy - (line[0] ? 12 : 0);
    mt(&SB_T, W / 2 + dx, base, t->hm, warm, AL_C);
    kt(&KF_TEXT, W / 2 + dx, base + 40, line, dim, AL_C, W * 3 / 4);
    if (gfx_round()) {
        // (the round frame: the outside of the circle stays black; nothing more)
    }
}

// ---------------------------------------------------------------- the frame
static bool night_now(const sb_now_t *n)
{
    if (!sb_cfg.autonight || !n->time_ok || sb_cfg.nfrom == sb_cfg.nto) {
        return false;
    }
    const int h = n->tm.h;
    return sb_cfg.nfrom < sb_cfg.nto ? (h >= sb_cfg.nfrom && h < sb_cfg.nto) : (h >= sb_cfg.nfrom || h < sb_cfg.nto);
}

void sb_draw_frame(int64_t now, float dt)
{
    sb_now_t n;
    sb_now(&n);
    txt_t t;
    texts(&n, &t);
    sb_hit_n = 0;
    s_ink = THEME_STANDBY_TEXT;
    s_dim = THEME_STANDBY_TEXT_DIM;
    const int face = night_now(&n) ? SB_FACE_NIGHT : sb_cfg.face;
    sb_night_backlight(face == SB_FACE_NIGHT);
    if (face == SB_FACE_NIGHT) {
        face_night(&n, &t);
        sb_period_ms = 1000;
        return;
    }
    sb_sky_target(sb_wx(), &n);
    sb_sky_step(dt);
    sb_sky_draw((float)(now % 600000) / 1000.0f);
    s_shadow = true;
    if (face == SB_FACE_GLANCE) {
        face_glance(&n, &t);
    } else {
        switch (layout()) {
        case LY_BAR:
            face_sky_bar(&t);
            break;
        case LY_TALL:
            face_sky_tall(&t);
            break;
        case LY_WIDE:
            face_sky_wide(&t);
            break;
        case LY_PORT:
            face_sky_port(&t);
            break;
        default:
            face_sky_round(&n, &t, now);
            break;
        }
    }
    const bool fast = sb_sky_moves() || n.nx_kind == NX_TIMER || (layout() == LY_ROUND && face == SB_FACE_SKY);
    sb_period_ms = fast ? 100 : 1000;
}
