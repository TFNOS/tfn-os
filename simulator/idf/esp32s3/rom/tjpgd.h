// The simulator: the JPEG decoder of the ROM (TJpgDec R0.01) has the same names here, but it decodes nothing: the
// start picture of sysui_splash() does not show on the PC.
#pragma once
#include <stdint.h>

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef enum { JDR_OK = 0, JDR_INTR, JDR_INP, JDR_MEM1, JDR_MEM2, JDR_PAR, JDR_FMT1, JDR_FMT2, JDR_FMT3 } JRESULT;
typedef struct { WORD left, right, top, bottom; } JRECT;
typedef struct JDEC JDEC;
struct JDEC {
    UINT dctr;
    BYTE *dptr;
    BYTE *inbuf;
    BYTE dmsk;
    BYTE scale;
    BYTE msx, msy;
    BYTE qtid[3];
    int16_t dcv[3];
    WORD nrst;
    UINT width, height;
    void *device;
};
static inline JRESULT jd_prepare(JDEC *jd, UINT (*infunc)(JDEC *, BYTE *, UINT), void *pool, UINT sz_pool, void *dev)
{
    (void)infunc; (void)pool; (void)sz_pool;
    jd->device = dev;
    jd->width = jd->height = 0;
    return JDR_INP;
}
static inline JRESULT jd_decomp(JDEC *jd, UINT (*outfunc)(JDEC *, void *, JRECT *), BYTE scale)
{
    (void)jd; (void)outfunc; (void)scale;
    return JDR_INP;
}
