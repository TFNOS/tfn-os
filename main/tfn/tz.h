#pragma once

// The dates of the civil calendar and the local time zones (plain C, no ESP-IDF: the host tests compile it). A local
// zone comes from a POSIX TZ text (for example "IST-2IDT,M3.4.4/26,M10.5.0"), with no use of the C library time zone.
// CLOCK, the alarms, the menus, STOCKS and other apps use it.
// The old app packages call these functions by their first names (stk_tz_parse, stk_tz_offset, stk_split): the ABI
// keeps those names as aliases (main/tz.c).
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int y, mo, d, h, mi, s, wd;
} tz_tm_t;  // wd: 0 = Sunday

// a time (seconds, already moved to a zone) as its parts
void tz_split(int64_t t, tz_tm_t *o);

// A local zone from a POSIX TZ text. Only the "Mm.w.d[/time]" rules (the rules of nearly all zones).
typedef struct {
    int std_east, dst_east;  // the offsets east of UTC, in seconds
    bool has_dst;
    int sm, sw, sd, st;  // the start of the summer time: month, week (5 = the last), day (0 = Sunday), seconds
    int em, ew, ed, et;  // its end
    char std_name[8], dst_name[8];
} tz_rule_t;
bool tz_parse(const char *s, tz_rule_t *z);
// the offset east of UTC (seconds) at this time; *name = the short name of the zone then (NULL: not needed)
int tz_offset(const tz_rule_t *z, int64_t utc, const char **name);
