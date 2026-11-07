// The dates of the civil calendar and the local time zones of POSIX TZ texts (see tz.h).
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "tz.h"
#include "tfn_abi_alias.h"

// ---------------------------------------------------------------- dates (the civil calendar, H. Hinnant)

int64_t tz_days(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

void tz_date(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)((int64_t)yoe + era * 400 + (*m <= 2));
}

void tz_split(int64_t t, tz_tm_t *o)
{
    const int64_t days = tz_fdiv(t, 86400);
    const int sec = (int)(t - days * 86400);
    tz_date(days, &o->y, &o->mo, &o->d);
    o->h = sec / 3600;
    o->mi = sec / 60 % 60;
    o->s = sec % 60;
    o->wd = tz_wday(days);
}

// ---------------------------------------------------------------- POSIX TZ

static const char *p_name(const char *s, char *out)
{
    int k = 0;
    if (*s == '<') {
        for (s++; *s && *s != '>'; s++) {
            if (k < 7) {
                out[k++] = *s;
            }
        }
        if (*s != '>') {
            return NULL;
        }
        s++;
    } else {
        for (; isalpha((unsigned char)*s); s++) {
            if (k < 7) {
                out[k++] = *s;
            }
        }
        if (k < 3) {
            return NULL;
        }
    }
    out[k] = 0;
    return k ? s : NULL;
}

static const char *p_time(const char *s, int *sec)
{
    int sign = 1;
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        sign = -1;
        s++;
    }
    if (!isdigit((unsigned char)*s)) {
        return NULL;
    }
    char *e;
    long h = strtol(s, &e, 10), m = 0, x = 0;
    s = e;
    if (*s == ':') {
        m = strtol(s + 1, &e, 10);
        s = e;
    }
    if (*s == ':') {
        x = strtol(s + 1, &e, 10);
        s = e;
    }
    if (h > 167 || m > 59 || x > 59) {
        return NULL;
    }
    *sec = sign * (int)(h * 3600 + m * 60 + x);
    return s;
}

static const char *p_rule(const char *s, int *m, int *w, int *d, int *t)
{
    if (*s != 'M') {
        return NULL;
    }
    char *e;
    *m = (int)strtol(s + 1, &e, 10);
    if (*e != '.') {
        return NULL;
    }
    *w = (int)strtol(e + 1, &e, 10);
    if (*e != '.') {
        return NULL;
    }
    *d = (int)strtol(e + 1, &e, 10);
    if (*m < 1 || *m > 12 || *w < 1 || *w > 5 || *d < 0 || *d > 6) {
        return NULL;
    }
    *t = 7200;  // (the default: 02:00)
    s = e;
    if (*s == '/') {
        s = p_time(s + 1, t);
    }
    return s;
}

bool tz_parse(const char *s, tz_rule_t *z)
{
    memset(z, 0, sizeof *z);
    int off;
    if (!s || !(s = p_name(s, z->std_name)) || !(s = p_time(s, &off))) {
        return false;
    }
    z->std_east = -off;
    if (!*s) {
        return true;
    }
    if (!(s = p_name(s, z->dst_name))) {
        return false;
    }
    z->has_dst = true;
    z->dst_east = z->std_east + 3600;
    if (*s && *s != ',') {
        if (!(s = p_time(s, &off))) {
            return false;
        }
        z->dst_east = -off;
    }
    if (!*s) {  // (no rules: the US rules)
        z->sm = 3;
        z->sw = 2;
        z->sd = 0;
        z->st = 7200;
        z->em = 11;
        z->ew = 1;
        z->ed = 0;
        z->et = 7200;
        return true;
    }
    if (*s != ',' || !(s = p_rule(s + 1, &z->sm, &z->sw, &z->sd, &z->st))) {
        return false;
    }
    if (*s != ',' || !(s = p_rule(s + 1, &z->em, &z->ew, &z->ed, &z->et))) {
        return false;
    }
    return *s == 0;
}

// the day (days since 1970) of "week w (5 = the last), day d" of a month
static int64_t rule_day(int y, int m, int w, int d)
{
    const int64_t first = tz_days(y, m, 1);
    const int64_t next = m == 12 ? tz_days(y + 1, 1, 1) : tz_days(y, m + 1, 1);
    int64_t day = first + (d - tz_wday(first) + 7) % 7 + (int64_t)(w - 1) * 7;
    while (day >= next) {
        day -= 7;
    }
    return day;
}

int tz_offset(const tz_rule_t *z, int64_t utc, const char **name)
{
    bool dst = false;
    if (z->has_dst) {
        int y, m, d;
        tz_date(tz_fdiv(utc + z->std_east, 86400), &y, &m, &d);
        const int64_t a = rule_day(y, z->sm, z->sw, z->sd) * 86400 + z->st - z->std_east;
        const int64_t b = rule_day(y, z->em, z->ew, z->ed) * 86400 + z->et - z->dst_east;
        dst = a < b ? (utc >= a && utc < b) : !(utc >= b && utc < a);
    }
    if (name) {
        *name = dst ? z->dst_name : z->std_name;
    }
    return dst ? z->dst_east : z->std_east;
}

// The first names of these functions (the STOCKS prefix): the installed app packages call them (ABI 1).
TFN_ABI_ALIAS(stk_split, tz_split)
TFN_ABI_ALIAS(stk_tz_parse, tz_parse)
TFN_ABI_ALIAS(stk_tz_offset, tz_offset)
