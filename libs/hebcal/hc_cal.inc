// HEBREW CALENDAR: THE CALCULATIONS (plain C: the PC tests include this file alone).
//   The days: R.D. numbers (day 1 = Monday 1 January of the year 1; 1970-01-01 = 719163), 0 = Sunday in hc_dow().
//   The Hebrew calendar: the arithmetic rules of Calendrical Calculations (Dershowitz and Reingold). The months:
//   1 = Nisan .. 7 = Tishrei .. 12 = Adar (Adar I in a leap year), 13 = Adar II. A year starts at 1 Tishrei.
//   The holidays of Israel or of the Diaspora, the weekly reading (hc_parsha.inc), the special Shabbatot, the modern
//   days of Israel, and the sun times (the NOAA solar calculator, in float).
// The PC tests check it against pyluach and the hebcal.com data.

#define HC_EPOCH  (-1373427)
#define HC_RD1970 719163

static int hc_dow(int64_t f)
{
    return (int)(((f % 7) + 7) % 7);
}

static int64_t hc_div(int64_t a, int64_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}  // (a floor)

static int64_t hc_fixed_greg(int y, int m, int d)
{
    const int64_t y1 = y - 1;
    int64_t f = 365 * y1 + hc_div(y1, 4) - hc_div(y1, 100) + hc_div(y1, 400) + (367 * m - 362) / 12 + d;
    if (m > 2) {
        f -= ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 2;
    }
    return f;
}
static void hc_greg(int64_t f, int *y, int *m, int *d)
{
    const int64_t d0 = f - 1, n400 = hc_div(d0, 146097), d1 = d0 - n400 * 146097;
    const int64_t n100 = d1 / 36524, d2 = d1 % 36524, n4 = d2 / 1461, d3 = d2 % 1461, n1 = d3 / 365;
    int yy = (int)(400 * n400 + 100 * n100 + 4 * n4 + n1);
    if (!(n100 == 4 || n1 == 4)) {
        yy++;
    }
    const int64_t jan1 = hc_fixed_greg(yy, 1, 1), mar1 = hc_fixed_greg(yy, 3, 1);
    const int64_t prior = f - jan1, corr = f < mar1 ? 0 : (((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0) ? 1 : 2);
    const int mm = (int)((12 * (prior + corr) + 373) / 367);
    *y = yy;
    *m = mm;
    *d = (int)(f - hc_fixed_greg(yy, mm, 1) + 1);
}

// ---------------------------------------------------------------- the Hebrew calendar
static bool hc_leap(int y)
{
    return (7 * (int64_t)y + 1) % 19 < 7;
}
static int hc_last_month(int y)
{
    return hc_leap(y) ? 13 : 12;
}
static int64_t hc_elapsed(int y)
{
    const int64_t me = (235 * (int64_t)y - 234) / 19;
    const int64_t parts = 12084 + 13753 * me;
    const int64_t day = me * 29 + parts / 25920;
    return (3 * (day + 1)) % 7 < 3 ? day + 1 : day;
}
// the start of a year (1 Tishrei); a small cache: the drawing asks for the same years again and again
static int64_t hc_new_year(int y)
{
    static int s_y[4];
    static int64_t s_v[4];
    static int s_k;
    for (int i = 0; i < 4; i++) {
        if (s_y[i] == y) {
            return s_v[i];
        }
    }
    const int64_t a = hc_elapsed(y - 1), b = hc_elapsed(y), c = hc_elapsed(y + 1);
    const int delay = c - b == 356 ? 2 : b - a == 382 ? 1 : 0;
    s_k = (s_k + 1) & 3;
    s_y[s_k] = y;
    s_v[s_k] = HC_EPOCH + b + delay;
    return s_v[s_k];
}
static int hc_year_len(int y)
{
    return (int)(hc_new_year(y + 1) - hc_new_year(y));
}
static int hc_month_len(int y, int m)
{
    if (m == 2 || m == 4 || m == 6 || m == 10 || m == 13) {
        return 29;
    }
    if (m == 12 && !hc_leap(y)) {
        return 29;
    }
    if (m == 8 && hc_year_len(y) % 10 != 5) {
        return 29;  // (Cheshvan: 30 days only in a complete year)
    }
    if (m == 9 && hc_year_len(y) % 10 == 3) {
        return 29;  // (Kislev: 29 days in a deficient year)
    }
    return 30;
}
static int64_t hc_fixed_heb(int y, int m, int d)
{
    int64_t f = hc_new_year(y) + d - 1;
    if (m < 7) {
        for (int i = 7; i <= hc_last_month(y); i++) {
            f += hc_month_len(y, i);
        }
        for (int i = 1; i < m; i++) {
            f += hc_month_len(y, i);
        }
    } else {
        for (int i = 7; i < m; i++) {
            f += hc_month_len(y, i);
        }
    }
    return f;
}
static void hc_heb(int64_t f, int *y, int *m, int *d)
{
    int yy = (int)hc_div((f - HC_EPOCH) * 98496, 35975351);  // (the estimate: one less than the year or the year)
    while (hc_new_year(yy + 1) <= f) {
        yy++;
    }
    int mm = f < hc_fixed_heb(yy, 1, 1) ? 7 : 1;
    while (f > hc_fixed_heb(yy, mm, hc_month_len(yy, mm))) {
        mm++;
    }
    *y = yy;
    *m = mm;
    *d = (int)(f - hc_fixed_heb(yy, mm, 1) + 1);
}
// the month after m in the year y (the order of a year: Tishrei .. Elul)
static int hc_next_month(int y, int m)
{
    return m == hc_last_month(y) ? 1 : m + 1;
}
// the Adar of Purim (Adar II in a leap year)
static int hc_adar(int y)
{
    return hc_leap(y) ? 13 : 12;
}

// ---------------------------------------------------------------- the events of a day
enum {
    HE_EREV_RH,
    HE_RH,
    HE_GEDALIAH,
    HE_SHUVA,
    HE_EREV_YK,
    HE_YK,
    HE_EREV_SUKKOT,
    HE_SUKKOT,
    HE_SUKKOT_CHM,
    HE_HOSHANA,
    HE_SHMINI,
    HE_SIMCHAT,
    HE_CHANUKAH,
    HE_TEVET10,
    HE_TUBISHVAT,
    HE_SHIRAH,
    HE_SHEKALIM,
    HE_ZACHOR,
    HE_ESTHER,
    HE_PURIM,
    HE_SHUSHAN,
    HE_PURIM_KATAN,
    HE_PARAH,
    HE_HACHODESH,
    HE_HAGADOL,
    HE_EREV_PESACH,
    HE_PESACH,
    HE_PESACH_CHM,
    HE_SHOAH,
    HE_ZIKARON,
    HE_ATZMAUT,
    HE_PESACH_SHENI,
    HE_LAG,
    HE_YERUSHALAYIM,
    HE_EREV_SHAVUOT,
    HE_SHAVUOT,
    HE_TAMMUZ17,
    HE_CHAZON,
    HE_AV9,
    HE_NACHAMU,
    HE_TUBEAV,
    HE_RC,
    HE_OMER,
    HE_BECHOROT,
    HE_N
};
// the kind of each event
enum {
    HK_YT = 1,
    HK_MAJOR = 2,
    HK_MINOR = 4,
    HK_FAST = 8,
    HK_MODERN = 16,
    HK_SHAB = 32,
    HK_RC = 64,
    HK_EREV = 128
};
static const uint8_t HC_KIND[HE_N] = {
        [HE_EREV_RH] = HK_EREV,
        [HE_RH] = HK_YT | HK_MAJOR,
        [HE_GEDALIAH] = HK_FAST,
        [HE_SHUVA] = HK_SHAB,
        [HE_EREV_YK] = HK_EREV,
        [HE_YK] = HK_YT | HK_MAJOR | HK_FAST,
        [HE_EREV_SUKKOT] = HK_EREV,
        [HE_SUKKOT] = HK_YT | HK_MAJOR,
        [HE_SUKKOT_CHM] = HK_MAJOR,
        [HE_HOSHANA] = HK_MAJOR,
        [HE_SHMINI] = HK_YT | HK_MAJOR,
        [HE_SIMCHAT] = HK_YT | HK_MAJOR,
        [HE_CHANUKAH] = HK_MAJOR,
        [HE_TEVET10] = HK_FAST,
        [HE_TUBISHVAT] = HK_MINOR,
        [HE_SHIRAH] = HK_SHAB,
        [HE_SHEKALIM] = HK_SHAB,
        [HE_ZACHOR] = HK_SHAB,
        [HE_ESTHER] = HK_FAST,
        [HE_PURIM] = HK_MAJOR,
        [HE_SHUSHAN] = HK_MINOR,
        [HE_PURIM_KATAN] = HK_MINOR,
        [HE_PARAH] = HK_SHAB,
        [HE_HACHODESH] = HK_SHAB,
        [HE_HAGADOL] = HK_SHAB,
        [HE_EREV_PESACH] = HK_EREV,
        [HE_PESACH] = HK_YT | HK_MAJOR,
        [HE_PESACH_CHM] = HK_MAJOR,
        [HE_SHOAH] = HK_MODERN,
        [HE_ZIKARON] = HK_MODERN,
        [HE_ATZMAUT] = HK_MODERN,
        [HE_PESACH_SHENI] = HK_MINOR,
        [HE_LAG] = HK_MINOR,
        [HE_YERUSHALAYIM] = HK_MODERN,
        [HE_EREV_SHAVUOT] = HK_EREV,
        [HE_SHAVUOT] = HK_YT | HK_MAJOR,
        [HE_TAMMUZ17] = HK_FAST,
        [HE_CHAZON] = HK_SHAB,
        [HE_AV9] = HK_FAST,
        [HE_NACHAMU] = HK_SHAB,
        [HE_TUBEAV] = HK_MINOR,
        [HE_RC] = HK_RC,
        [HE_OMER] = 0,
        [HE_BECHOROT] = HK_FAST,
};
// a yom tov with no work: a Sukkot day or a Pesach day is not one in chol hamoed (n gives the day)
typedef struct {
    uint8_t id, n;
} hc_ev_t;

// the weekly reading of a Shabbat: 0 = none, 1..54 = one reading (n - 1), 101..154 = two (n - 101 and the next)
static int hc_parsha(int64_t f, bool israel)
{
    if (hc_dow(f) != 6) {
        return 0;
    }
    int y, m, d;
    hc_heb(f, &y, &m, &d);
    const int64_t rh = hc_new_year(y);
    const int len = hc_year_len(y), dow = hc_dow(rh);
    const int64_t first = rh + (6 - dow + 7) % 7;
    const int k = (int)((f - first) / 7);
    for (int i = 0; i < (int)(sizeof HC_PTYPES / sizeof HC_PTYPES[0]); i++) {
        const hc_ptype_t *t = &HC_PTYPES[i];
        if (t->len == len && t->dow == dow && t->israel == israel) {
            return k >= 0 && k < t->n ? t->code[k] : 0;
        }
    }
    return 0;
}

static int hc_add(hc_ev_t *o, int n, int max, int id, int k)
{
    if (n < max) {
        o[n] = (hc_ev_t){(uint8_t)id, (uint8_t)k};
    }
    return n + 1;
}
// the events of the day f (the day part: an evening belongs to the day before, as in a printed calendar)
static int hc_events(int64_t f, bool israel, hc_ev_t *o, int max)
{
    int y, m, d, n = 0;
    hc_heb(f, &y, &m, &d);
    const int dow = hc_dow(f);
    const int adar = hc_adar(y);
    // Rosh Chodesh: day 30 (the month after) and day 1 (not Tishrei)
    if (d == 30) {
        n = hc_add(o, n, max, HE_RC, hc_next_month(y, m));
    }
    if (d == 1 && m != 7) {
        n = hc_add(o, n, max, HE_RC, m);
    }
    switch (m) {
    case 7:
        if (d <= 2) {
            n = hc_add(o, n, max, HE_RH, d);
        }
        if ((d == 3 && dow != 6) || (d == 4 && dow == 0)) {
            n = hc_add(o, n, max, HE_GEDALIAH, 0);
        }
        if (d >= 3 && d <= 9 && dow == 6) {
            n = hc_add(o, n, max, HE_SHUVA, 0);
        }
        if (d == 9) {
            n = hc_add(o, n, max, HE_EREV_YK, 0);
        }
        if (d == 10) {
            n = hc_add(o, n, max, HE_YK, 0);
        }
        if (d == 14) {
            n = hc_add(o, n, max, HE_EREV_SUKKOT, 0);
        }
        if (d == 15 || (d == 16 && !israel)) {
            n = hc_add(o, n, max, HE_SUKKOT, d - 14);
        }
        if (d >= (israel ? 16 : 17) && d <= 20) {
            n = hc_add(o, n, max, HE_SUKKOT_CHM, d - 14);
        }
        if (d == 21) {
            n = hc_add(o, n, max, HE_HOSHANA, 0);
        }
        if (d == 22) {
            n = hc_add(o, n, max, HE_SHMINI, 0);
        }
        if ((d == 22 && israel) || (d == 23 && !israel)) {
            n = hc_add(o, n, max, HE_SIMCHAT, 0);
        }
        break;
    case 10:
        if (d == 10) {
            n = hc_add(o, n, max, HE_TEVET10, 0);
        }
        break;
    case 11:
        if (d == 15) {
            n = hc_add(o, n, max, HE_TUBISHVAT, 0);
        }
        break;
    case 1:
        if (d == 14) {
            n = hc_add(o, n, max, HE_EREV_PESACH, 0);
        }
        if ((d == 14 && dow != 6) || (d == 12 && dow == 4)) {
            n = hc_add(o, n, max, HE_BECHOROT, 0);  // (the firstborn)
        }
        if (d == 15 || d == 21 || (!israel && (d == 16 || d == 22))) {
            n = hc_add(o, n, max, HE_PESACH, d - 14);
        }
        if (d >= (israel ? 16 : 17) && d <= 20) {
            n = hc_add(o, n, max, HE_PESACH_CHM, d - 14);
        }
        break;
    case 2:
        if (d == 14) {
            n = hc_add(o, n, max, HE_PESACH_SHENI, 0);
        }
        if (d == 18) {
            n = hc_add(o, n, max, HE_LAG, 0);
        }
        if (d == 28) {
            n = hc_add(o, n, max, HE_YERUSHALAYIM, 0);
        }
        break;
    case 3:
        if (d == 5) {
            n = hc_add(o, n, max, HE_EREV_SHAVUOT, 0);
        }
        if (d == 6 || (d == 7 && !israel)) {
            n = hc_add(o, n, max, HE_SHAVUOT, d - 5);
        }
        break;
    case 4:
        if ((d == 17 && dow != 6) || (d == 18 && dow == 0)) {
            n = hc_add(o, n, max, HE_TAMMUZ17, 0);
        }
        break;
    case 5:
        if ((d == 9 && dow != 6) || (d == 10 && dow == 0)) {
            n = hc_add(o, n, max, HE_AV9, 0);
        }
        if (d == 15) {
            n = hc_add(o, n, max, HE_TUBEAV, 0);
        }
        break;
    case 6:
        if (d == 29) {
            n = hc_add(o, n, max, HE_EREV_RH, 0);
        }
        break;
    }
    if (m == adar) {
        if ((d == 13 && dow != 6) || (d == 11 && dow == 4)) {
            n = hc_add(o, n, max, HE_ESTHER, 0);
        }
        if (d == 14) {
            n = hc_add(o, n, max, HE_PURIM, 0);
        }
        if (d == 15) {
            n = hc_add(o, n, max, HE_SHUSHAN, 0);
        }
    }
    if (m == 12 && hc_leap(y) && d == 14) {
        n = hc_add(o, n, max, HE_PURIM_KATAN, 0);
    }
    // Chanukah: 25 Kislev, 8 days (Kislev and Tevet are in the same Hebrew year)
    if (m == 9 || m == 10) {
        const int64_t c0 = hc_fixed_heb(y, 9, 25);
        if (f >= c0 && f < c0 + 8) {
            n = hc_add(o, n, max, HE_CHANUKAH, (int)(f - c0 + 1));
        }
    }
    // the Omer: 16 Nisan .. 5 Sivan (the count of the evening before)
    if (m >= 1 && m <= 3) {
        const int64_t o0 = hc_fixed_heb(y, 1, 16);
        if (f >= o0 && f < o0 + 49) {
            n = hc_add(o, n, max, HE_OMER, (int)(f - o0 + 1));
        }
    }
    // the modern days of Israel (the rules of the Knesset: no day next to Shabbat)
    if (m == 1 || m == 2) {
        int64_t sh = hc_fixed_heb(y, 1, 27);
        if (hc_dow(sh) == 5) {
            sh--;
        } else if (hc_dow(sh) == 0) {
            sh++;
        }
        if (f == sh) {
            n = hc_add(o, n, max, HE_SHOAH, 0);
        }
        const int64_t a = hc_fixed_heb(y, 2, 5);
        int64_t at = a;
        switch (hc_dow(a)) {
        case 5:
            at = a - 1;
            break;
        case 6:
            at = a - 2;
            break;
        case 1:
            at = a + 1;
            break;
        }
        if (f == at - 1) {
            n = hc_add(o, n, max, HE_ZIKARON, 0);
        }
        if (f == at) {
            n = hc_add(o, n, max, HE_ATZMAUT, 0);
        }
    }
    // the special Shabbatot
    if (dow == 6 && (m == 11 || m >= 12 || m == 1)) {
        const int64_t rca = hc_fixed_heb(y, adar, 1), pur = hc_fixed_heb(y, adar, 14), n1 = hc_fixed_heb(y, 1, 1);
        const int64_t pes = hc_fixed_heb(y, 1, 15);
        if (f >= rca - 6 && f <= rca) {
            n = hc_add(o, n, max, HE_SHEKALIM, 0);
        }
        if (f >= pur - 7 && f <= pur - 1) {
            n = hc_add(o, n, max, HE_ZACHOR, 0);
        }
        if (f >= n1 - 13 && f <= n1 - 7) {
            n = hc_add(o, n, max, HE_PARAH, 0);
        }
        if (f >= n1 - 6 && f <= n1) {
            n = hc_add(o, n, max, HE_HACHODESH, 0);
        }
        if (f >= pes - 7 && f <= pes - 1) {
            n = hc_add(o, n, max, HE_HAGADOL, 0);
        }
    }
    if (dow == 6 && (m == 4 || m == 5)) {
        const int64_t av9 = hc_fixed_heb(y, 5, 9);
        if (f >= av9 - 6 && f <= av9) {
            n = hc_add(o, n, max, HE_CHAZON, 0);
        }
        if (f >= av9 + 1 && f <= av9 + 7) {
            n = hc_add(o, n, max, HE_NACHAMU, 0);
        }
    }
    if (dow == 6 && (m == 10 || m == 11)) {
        if (hc_parsha(f, israel) == 16) {
            n = hc_add(o, n, max, HE_SHIRAH, 0);  // (Beshalach)
        }
    }
    return n < max ? n : max;
}
// a day of rest: Shabbat or a yom tov (no work)
static bool hc_yomtov(int64_t f, bool israel)
{
    hc_ev_t e[8];
    const int n = hc_events(f, israel, e, 8);
    for (int i = 0; i < n; i++) {
        if (HC_KIND[e[i].id] & HK_YT) {
            return true;
        }
    }
    return false;
}
static bool hc_holy(int64_t f, bool israel)
{
    return hc_dow(f) == 6 || hc_yomtov(f, israel);
}

// ---------------------------------------------------------------- the sun (the NOAA solar calculator)
// The minutes after 00:00 UTC of the day f when the centre of the sun is at the angle `below` under the horizon
// (0.833 = sunrise and sunset with the refraction); rise = true: in the morning. -1 = never on that day.
#define HC_RAD 0.0174532925f
static float hc_wrap(float a)
{
    return a - 360.0f * (float)floor(a / 360.0f);
}
static float hc_sun_event(int64_t f, float lat, float lon, float below, bool rise)
{
    float t_min = 720.0f;  // (the first estimate: noon UTC, then twice at the event)
    for (int it = 0; it < 3; it++) {
        const float jc = ((float)(f - 730120) - 0.5f + t_min / 1440.0f) / 36525.0f;  // (Julian centuries from J2000)
        const float l0 = hc_wrap(280.46646f + jc * (36000.76983f + jc * 0.0003032f));
        const float ma = 357.52911f + jc * (35999.05029f - 0.0001537f * jc);
        const float ec = 0.016708634f - jc * (0.000042037f + 0.0000001267f * jc);
        const float mr = ma * HC_RAD;
        const float c = sinf(mr) * (1.914602f - jc * (0.004817f + 0.000014f * jc)) +
                        sinf(2 * mr) * (0.019993f - 0.000101f * jc) + sinf(3 * mr) * 0.000289f;
        const float om = (125.04f - 1934.136f * jc) * HC_RAD;
        const float lam = (l0 + c - 0.00569f - 0.00478f * sinf(om)) * HC_RAD;
        const float eps0 =
                23.0f + (26.0f + (21.448f - jc * (46.815f + jc * (0.00059f - jc * 0.001813f))) / 60.0f) / 60.0f;
        const float eps = (eps0 + 0.00256f * cosf(om)) * HC_RAD;
        const float dec = asinf(sinf(eps) * sinf(lam));
        const float yv = tanf(eps / 2) * tanf(eps / 2), l0r = l0 * HC_RAD;
        const float eqt = 4.0f / HC_RAD *
                          (yv * sinf(2 * l0r) - 2 * ec * sinf(mr) + 4 * ec * yv * sinf(mr) * cosf(2 * l0r) -
                           0.5f * yv * yv * sinf(4 * l0r) - 1.25f * ec * ec * sinf(2 * mr));
        const float la = lat * HC_RAD;
        const float ch = (cosf((90.0f + below) * HC_RAD) - sinf(la) * sinf(dec)) / (cosf(la) * cosf(dec));
        if (ch < -1 || ch > 1) {
            return -1;
        }
        const float ha = acosf(ch) / HC_RAD;
        t_min = 720.0f - 4.0f * (lon + (rise ? ha : -ha)) - eqt;
    }
    return t_min;
}

// the times of a day (minutes after the local 00:00; -1 = none)
enum {
    HZ_ALOT,
    HZ_SUNRISE,
    HZ_SHMA,
    HZ_TFILA,
    HZ_CHATZOT,
    HZ_MINCHA,
    HZ_PLAG,
    HZ_SUNSET,
    HZ_TZEIT,
    HZ_N
};
typedef struct {
    float t[HZ_N];
    float tzeit7, tzeit645;  // the end of a fast (7.083 and 6.45 degrees)
} hc_zman_t;
static void hc_zmanim(int64_t f, float lat, float lon, int off_min, hc_zman_t *z)
{
    const float rise = hc_sun_event(f, lat, lon, 0.833f, true), set = hc_sun_event(f, lat, lon, 0.833f, false);
    const float o = (float)off_min;
    const float alot = hc_sun_event(f, lat, lon, 16.1f, true), tz = hc_sun_event(f, lat, lon, 8.5f, false);
    const float tz7 = hc_sun_event(f, lat, lon, 7.083f, false), tz6 = hc_sun_event(f, lat, lon, 6.45f, false);
    z->t[HZ_ALOT] = alot < 0 ? -1 : alot + o;
    z->t[HZ_TZEIT] = tz < 0 ? -1 : tz + o;
    z->tzeit7 = tz7 < 0 ? -1 : tz7 + o;
    z->tzeit645 = tz6 < 0 ? -1 : tz6 + o;
    if (rise < 0 || set < 0) {
        for (int i = HZ_SUNRISE; i <= HZ_SUNSET; i++) {
            z->t[i] = -1;
        }
        return;
    }
    const float h = (set - rise) / 12.0f;  // (an hour of the day: a twelfth of the daylight)
    z->t[HZ_SUNRISE] = rise + o;
    z->t[HZ_SHMA] = rise + 3 * h + o;
    z->t[HZ_TFILA] = rise + 4 * h + o;
    z->t[HZ_CHATZOT] = rise + 6 * h + o;
    z->t[HZ_MINCHA] = rise + 6.5f * h + o;
    z->t[HZ_PLAG] = rise + 10.75f * h + o;
    z->t[HZ_SUNSET] = set + o;
}
// THE TIMES OF A DAY of rest and of a fast (the rules of hebcal.com): candle lighting on Friday and before a yom tov
// (the sunset - the candle minutes; on a day of rest before a yom tov: at the night, 8.5 degrees), havdalah at the end
// of the days of rest (8.5 degrees); a fast begins at the dawn (16.1 degrees; Tisha B'Av and Yom Kippur: at the sunset
// before) and ends at 7.083 degrees (Israel: 15 minutes after the sunset; Tisha B'Av: 6.45 degrees; a fast on a
// Friday or on the eve of Pesach: no time). Minutes after the local 00:00 of the day f, -1 = none.
typedef struct {
    float candles, havdalah, fast_begin, fast_end;
    bool candles_night;  // (after the night: the lights come from a flame of the holy day)
    bool fast_eve;       // fast_begin is the start of the fast of the next day
} hc_times_t;
static void hc_day_times(int64_t f, bool israel, const hc_zman_t *z, int cmin, hc_times_t *o)
{
    o->candles = o->havdalah = o->fast_begin = o->fast_end = -1;
    o->candles_night = o->fast_eve = false;
    const bool holy = hc_holy(f, israel), holy1 = hc_holy(f + 1, israel), yt1 = hc_yomtov(f + 1, israel);
    if (hc_dow(f) == 5 || (yt1 && !holy)) {
        o->candles = z->t[HZ_SUNSET] < 0 ? -1 : z->t[HZ_SUNSET] - (float)cmin;
    } else if (yt1 && holy) {
        o->candles = z->t[HZ_TZEIT];
        o->candles_night = true;
    }
    if (holy && !holy1) {
        o->havdalah = z->t[HZ_TZEIT];
    }
    hc_ev_t e[12];
    int n = hc_events(f + 1, israel, e, 12);
    for (int i = 0; i < n; i++) {
        if (e[i].id == HE_AV9) {
            o->fast_begin = z->t[HZ_SUNSET];
            o->fast_eve = true;
        }
    }
    n = hc_events(f, israel, e, 12);
    for (int i = 0; i < n; i++) {
        if (e[i].id == HE_YK || !(HC_KIND[e[i].id] & HK_FAST)) {
            continue;
        }
        if (e[i].id == HE_AV9) {
            o->fast_end = z->tzeit645;
        } else {
            if (!o->fast_eve) {
                o->fast_begin = z->t[HZ_ALOT];
            }
            if (hc_dow(f) != 5 && !(e[i].id == HE_BECHOROT && yt1)) {
                o->fast_end = israel ? (z->t[HZ_SUNSET] < 0 ? -1 : z->t[HZ_SUNSET] + 15) : z->tzeit7;
            }
        }
    }
}

// a time in minutes as whole minutes (to the nearest minute, as hebcal.com shows them)
static int hc_round(float t)
{
    return (int)floor(t + 0.5f);
}

// ---------------------------------------------------------------- Hebrew numbers (gematria)
// n (1 .. 999) in Hebrew letters with the geresh / gershayim; 15 = ט"ו, 16 = ט"ז
static void hc_gematria(int n, char *o, int max)
{
    static const char *const ONES[10] = {"", "א", "ב", "ג", "ד", "ה", "ו", "ז", "ח", "ט"};
    static const char *const TENS[10] = {"", "י", "כ", "ל", "מ", "נ", "ס", "ע", "פ", "צ"};
    static const char *const HUND[5] = {"", "ק", "ר", "ש", "ת"};
    const char *l[12];
    int k = 0;
    int h = (n % 1000) / 100;
    while (h >= 4) {
        l[k++] = HUND[4];
        h -= 4;
    }
    if (h) {
        l[k++] = HUND[h];
    }
    int r = n % 100;
    if (r == 15 || r == 16) {
        l[k++] = "ט";
        l[k++] = r == 15 ? "ו" : "ז";
    } else {
        if (r / 10) {
            l[k++] = TENS[r / 10];
        }
        if (r % 10) {
            l[k++] = ONES[r % 10];
        }
    }
    int p = 0;
    o[0] = 0;
    for (int i = 0; i < k && p < max - 8; i++) {
        if (i == k - 1 && k > 1) {
            p += snprintf(o + p, (size_t)(max - p), "\xD7\xB4");  // (gershayim before the last)
        }
        p += snprintf(o + p, (size_t)(max - p), "%s", l[i]);
    }
    if (k == 1 && p < max - 3) {
        snprintf(o + p, (size_t)(max - p), "\xD7\xB3");  // (geresh after one letter)
    }
}
