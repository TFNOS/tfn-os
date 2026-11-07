// RFSRC: the list of the sources of this board (rfsrc.h). A new radio = one line here.
#include "rfsrc.h"

#include <string.h>

int rfsrc_enum(rfsrc_t *out, int max, bool tests)
{
    int n = 0;
    rfsrc_t s;
    if (n < max && rfsrc_native(&s) && s.ops->state(s.ctx) != RFSRC_ABSENT) {
        out[n++] = s;
    }
    // (here: the radios on the pins, when the board has them; for example a CC1101 as a SWEEP source)
    if (n < max && rfsrc_demo(&s)) {
        out[n++] = s;
    }
    if (tests && n < max && rfsrc_demo_sweep(&s)) {
        out[n++] = s;
    }
    return n;
}

bool rfsrc_find(const char *id, rfsrc_t *out)
{
    rfsrc_t all[8];
    int n = rfsrc_enum(all, 8, true);
    for (int i = 0; i < n; i++) {
        if (!strcmp(all[i].caps->id, id)) {
            *out = all[i];
            return true;
        }
    }
    return false;
}
