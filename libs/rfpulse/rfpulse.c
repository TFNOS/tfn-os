// RFPULSE: the list of the pulse receivers of this board (rfpulse.h). A new receiver = one line here.
#include "rfpulse.h"

int rfp_enum(rfp_src_t *out, int max, bool tests)
{
    (void)tests;
    int n = 0;
    rfp_src_t s;
    if (n < max && rfp_gpio(&s)) {
        out[n++] = s;
    }
    // (here: a transceiver chip on SPI with its data pin, for example a CC1101: OOK, ASK and FSK)
    if (n < max && rfp_demo(&s)) {
        out[n++] = s;
    }
    return n;
}

int rfp_enum_ir(rfp_src_t *out, int max)
{
    int n = 0;
    rfp_src_t s;
    if (n < max && rfp_ir(&s)) {
        out[n++] = s;
    }
    if (n < max && rfp_ir_demo(&s)) {
        out[n++] = s;
    }
    return n;
}
