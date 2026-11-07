#pragma once
// SDRDSP: THE HISTORY of the spectrum (pure C).
//   THE WATERFALL: a ring of rows. Each row keeps its own axis (f0, df), so a view draws the old rows at their real
//   frequencies after a pan or a zoom. A level is one byte: 0.5 dB steps from SDR_WF_DB0 (0) up (255 = SDR_WF_DB0 +
//   127.5 dB): a new reference or range of the view colours the old rows right.
//   THE PERSISTENCE: a map of how often each level came at each point (a density), with a slow fade: a burst signal
//   (WiFi) and a steady one (a tone) look different.
#include <stdbool.h>
#include <stdint.h>

#define SDR_WF_DB0 (-140.0f)

typedef struct {
    int64_t f0_hz;
    float df_hz;
    uint32_t t_ms;
} sdr_wf_row_t;

typedef struct {
    int w, rows;      // the points of a row, the rows of the ring
    int head, count;  // the next row to write, the rows with data
    uint8_t *q;       // rows * w
    sdr_wf_row_t *hdr;
} sdr_wf_t;

bool sdr_wf_init(sdr_wf_t *h, int w, int rows);
void sdr_wf_free(sdr_wf_t *h);
void sdr_wf_clear(sdr_wf_t *h);
// One row (n <= w points) on the axis f0 / df. The SDR_NODATA points become 0.
void sdr_wf_push(sdr_wf_t *h, const float *db, int n, int64_t f0_hz, double df_hz, uint32_t t_ms);
// The row of this age (0 = the newest), or NULL.
const uint8_t *sdr_wf_row(const sdr_wf_t *h, int age, const sdr_wf_row_t **hdr);
static inline float sdr_wf_db(uint8_t q)
{
    return SDR_WF_DB0 + q * 0.5f;
}
static inline uint8_t sdr_wf_q(float db)
{
    float v = (db - SDR_WF_DB0) * 2.0f;
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f));
}

typedef struct {
    int w, levels;
    uint16_t *d;             // levels * w (row 0 = the top level)
    float top_db, range_db;  // the levels: top_db .. top_db - range_db
    int64_t f0_hz;
    double df_hz;
} sdr_persist_t;

bool sdr_persist_init(sdr_persist_t *p, int w, int levels);
void sdr_persist_free(sdr_persist_t *p);
// A new scale or a new axis clears the map
void sdr_persist_scale(sdr_persist_t *p, float top_db, float range_db, int64_t f0_hz, double df_hz);
// One frame: a fade of all cells (shift: 5 = a fade of 1/32 a frame), then +1 at each point's level
void sdr_persist_push(sdr_persist_t *p, const float *db, int n, int shift);
