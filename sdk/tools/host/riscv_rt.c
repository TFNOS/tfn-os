// TFN OS SDK: the small helpers that the RISC-V compiler (the Tab5, ESP32-P4) calls in an app file. The firmware does
// not give them to the app files (they are not names of the ABI), so mk_app.py links this file into each RISC-V app
// file. The Xtensa apps do not need them.
#include <stdint.h>
#include <string.h>

// a signaling NaN (newlib: isnan(), fmaxf() and others call it on RISC-V)
int __issignalingf(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof u);
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0 && (u & 0x00400000u) == 0;
}

// the count of the zero bits at the low end (libgcc; the P4 core has no instruction for it). (a = 0: 32)
int __ctzsi2(uint32_t a)
{
    int n = 0;
    if (!a) return 32;
    while (!(a & 1u)) {
        a >>= 1;
        n++;
    }
    return n;
}
