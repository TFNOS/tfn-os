// The round view of scn (SCN_WT: 206 x 206 world pixels) in a bigger round frame (APP_ROUND_HI: the frame
// of the AMOLED-1.75C is 466 x 466). scn_render_fit() draws the view to fill fw x fh at the scale fw / 206 (each world
// pixel gets the nearest 2 or 3 screen pixels: sharp, with no blur). Only the pixels in the circle of diameter fw are
// written (the panel shows no more); the corners get black. The same steps as scn_render: prep, the scene in bands of
// band_rows rows of 206 pixels, then the mix with the frozen frame (fw x fh) and the fade.
// Header only: SCENES and STATION X compile it into their app files (scn.c is in the firmware, and the ABI of the app
// files has no new names for it).
#pragma once
#include <math.h>
#include <string.h>
// (scn.h first: the app includes it; this file has no include of it, so one copy of its types is in the app)

#define SCNF_VW   206  // the round view of scn.c (SCN_VW[SCN_WT], SCN_VH[SCN_WT])
#define SCNF_MAXW 480  // the most columns of the frame

// the pixel mixes of scn_render (scn.c keeps its copies static); the pixels are in the LCD byte order
static inline uint16_t scnf_scale(uint16_t p, int k)
{
    uint16_t v = (uint16_t)((p >> 8) | (p << 8));
    int r = ((v >> 11) & 31) * k >> 8, g = ((v >> 5) & 63) * k >> 8, b = (v & 31) * k >> 8;
    v = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint16_t scnf_mix(uint16_t a, uint16_t b, int k)
{
    uint16_t va = (uint16_t)((a >> 8) | (a << 8)), vb = (uint16_t)((b >> 8) | (b << 8));
    int ra = (va >> 11) & 31, ga = (va >> 5) & 63, ba = va & 31, rb = (vb >> 11) & 31, gb = (vb >> 5) & 63,
        bb = vb & 31;
    int r = ra + ((rb - ra) * k >> 8), g = ga + ((gb - ga) * k >> 8), bl = ba + ((bb - ba) * k >> 8);
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | bl);
    return (uint16_t)((v >> 8) | (v << 8));
}
// the first and the last + 1 column of row Y in the circle of the frame
static inline void scnf_span(int Y, int fw, int fh, int *a, int *b)
{
    const float R = fw * 0.5f, dy = Y + 0.5f - fh * 0.5f;
    const float h = dy * dy < R * R ? sqrtf(R * R - dy * dy) : 0;
    int x0 = (int)(R - h);
    if (x0 < 0) {
        x0 = 0;
    }
    if (x0 > fw / 2) {
        x0 = fw / 2;
    }
    *a = x0;
    *b = fw - x0;
}
static void scn_render_fit(const scn_def_t *S, float t, float L, uint16_t *band, int band_rows, uint16_t *fb, int fw,
                           int fh, int fade, const uint16_t *freeze, int mix)
{
    static int16_t xt[SCNF_MAXW];
    static uint16_t row[SCNF_MAXW];
    const int vw = SCNF_VW, vh = SCNF_VW;
    if (fw > SCNF_MAXW || band_rows < 1) {
        return;
    }
    int vx = 0, vy = 0;
    S->prep(t, L, &vx, &vy);
    for (int x = 0; x < fw; x++) {
        xt[x] = (int16_t)(x * vw / fw);
    }
    const int k = fade < 0 ? 0 : fade;
    scn_surf out = {vw, band_rows, vx, vy, band};
    for (int y0 = 0; y0 < vh; y0 += band_rows) {
        const int bh = vh - y0 < band_rows ? vh - y0 : band_rows;
        out.h = bh;
        out.oy = vy + y0;
        memset(band, 0, (size_t)vw * bh * 2);
        S->draw(&out);
        for (int y = 0; y < bh; y++) {
            const int sy = y0 + y;
            // the screen rows of this world row: Y * vh / fh == sy
            const int ya = (sy * fh + vh - 1) / vh, yb = ((sy + 1) * fh + vh - 1) / vh;
            if (ya >= yb || ya >= fh) {
                continue;
            }
            uint16_t *r = band + y * vw;
            if (k < 256 && !(mix > 0 && freeze)) {
                for (int x = 0; x < vw; x++) {
                    r[x] = scnf_scale(r[x], k);
                }
            }
            // the widest span of these rows (the row nearest the middle), then the row at the scale of the frame
            int lo = fw, a, b;
            for (int Y = ya; Y < yb && Y < fh; Y++) {
                scnf_span(Y, fw, fh, &a, &b);
                if (a < lo) {
                    lo = a;
                }
            }
            for (int x = lo; x < fw - lo; x++) {
                row[x] = r[xt[x]];
            }
            for (int Y = ya; Y < yb && Y < fh; Y++) {
                scnf_span(Y, fw, fh, &a, &b);
                uint16_t *d = fb + (size_t)Y * fw;
                memset(d, 0, (size_t)a * 2);
                memset(d + b, 0, (size_t)(fw - b) * 2);
                if (mix > 0 && freeze) {
                    const uint16_t *f = freeze + (size_t)Y * fw;
                    for (int x = a; x < b; x++) {
                        uint16_t p = scnf_mix(f[x], row[x], mix);
                        d[x] = k < 256 ? scnf_scale(p, k) : p;
                    }
                } else {
                    memcpy(d + a, row + a, (size_t)(b - a) * 2);
                }
            }
        }
    }
}
