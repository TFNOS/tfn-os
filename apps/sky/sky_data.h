#pragma once
// The star data of the SKY app (a made file: do not change it
// by hand). Sources: the HYG database v4.0 (astronexus, CC BY-SA) for the stars, and
// d3-celestial (Olaf Frohn, BSD) for the constellation lines (Stellarium "modern") and names.

#include <stdint.h>

// angles: right ascension 65536 = 360 degrees; declination 32767 = 90 degrees
#define SKY_RA_DEG(u)  ((float)(u) * (360.0f / 65536.0f))
#define SKY_DEC_DEG(i) ((float)(i) * (90.0f / 32767.0f))

typedef struct {
    uint16_t ra;
    int16_t dec;
    uint16_t ly;    // distance in light-years (0 = not known)
    int8_t mag10;   // magnitude x 10
    uint8_t bv;     // colour index: (B-V + 0.4) x 100, 0..255
    uint8_t con;    // index in SKY_CONS (255 = none)
    uint8_t greek;  // Bayer letter 1..24 = alpha..omega (0 = none)
    char spec;      // spectral class letter (O B A F G K M, ? = not known)
    uint8_t name;   // index in SKY_NAMES (255 = no proper name)
} sky_star_t;

typedef struct {
    char abbr[4];
    const char *name;  // "Orion"
    const char *gen;   // the genitive, "Orionis"
    uint16_t ra;       // the centre of the figure
    int16_t dec;
    uint8_t radius;  // the figure fits in a circle of this many degrees
    uint8_t rank;    // 1 = well known .. 3 = faint
    uint16_t poly0;  // the first polyline in SKY_POLYS
    uint8_t npoly;
} sky_con_t;

typedef struct {
    uint16_t pt0;  // the first point in SKY_PTS
    uint8_t n;     // points
    uint8_t con;
} sky_poly_t;

typedef struct {
    uint16_t ra;
    int16_t dec;
} sky_pt_t;

#define SKY_N_STARS 1022  // brightest first
#define SKY_N_CONS  88
#define SKY_N_POLYS 150
#define SKY_N_PTS   893
#define SKY_N_NAMES 254

extern const sky_star_t SKY_STARS[SKY_N_STARS];
extern const sky_con_t SKY_CONS[SKY_N_CONS];
extern const sky_poly_t SKY_POLYS[SKY_N_POLYS];
extern const sky_pt_t SKY_PTS[SKY_N_PTS];
extern const char *const SKY_NAMES[SKY_N_NAMES];
extern const char *const SKY_GREEK[25];
