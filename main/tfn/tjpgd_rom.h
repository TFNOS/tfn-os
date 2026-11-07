#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if !defined(CONFIG_IDF_TARGET_ESP32P4) || !CONFIG_IDF_TARGET_ESP32P4
#include "esp32s3/rom/tjpgd.h"
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JD_SZBUF            512     // the size of the stream input buffer
#define JD_FORMAT           0       // the output: 0 = RGB888 (3 bytes a pixel)
#define JD_USE_SCALE        1       // the output can be 1/2, 1/4, 1/8
#define JD_TBLCLIP          1       // a table for the saturation
#define JD_FASTDECODE       1       // a 32-bit barrel shifter (the pool of 4096 bytes of the apps is enough)
#define JD_DEFAULT_HUFFMAN  0

typedef unsigned int    UINT;       // (the types of the S3 ROM header; on the P4 UINT is size_t)
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef int16_t jd_yuv_t;

typedef enum {
    JDR_OK = 0,     // succeeded
    JDR_INTR,       // interrupted by the output function
    JDR_INP,        // a device error or a wrong end of the input stream
    JDR_MEM1,       // not enough memory pool for the image
    JDR_MEM2,       // not enough stream input buffer
    JDR_PAR,        // a parameter error
    JDR_FMT1,       // a data format error (maybe broken data)
    JDR_FMT2,       // a right format, but not supported
    JDR_FMT3        // a JPEG standard that is not supported
} JRESULT;

typedef struct {
    WORD left, right, top, bottom;
} JRECT;

typedef struct JDEC JDEC;
struct JDEC {
    size_t dctr;
    uint8_t *dptr;
    uint8_t *inbuf;
    uint8_t dbit;
    uint8_t scale;
    uint8_t msx, msy;
    uint8_t qtid[3];
    uint8_t ncomp;
    int16_t dcv[3];
    uint16_t nrst;
    uint16_t width, height;         // the size of the input image (pixels)
    uint8_t *huffbits[2][2];
    uint16_t *huffcode[2][2];
    uint8_t *huffdata[2][2];
    int32_t *qttbl[4];
    uint32_t wreg;
    uint8_t marker;
    void *workbuf;
    jd_yuv_t *mcubuf;
    void *pool;
    size_t sz_pool;
    UINT (*infunc)(JDEC *, BYTE *, UINT);
    void *device;                   // the I/O device of the caller
};

JRESULT jd_prepare(JDEC *jd, UINT (*infunc)(JDEC *, BYTE *, UINT), void *pool, UINT sz_pool, void *dev);
JRESULT jd_decomp(JDEC *jd, UINT (*outfunc)(JDEC *, void *, JRECT *), BYTE scale);

#ifdef __cplusplus
}
#endif
#endif
