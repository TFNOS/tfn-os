// MINESWEEPER: the icon (included by app_mines.c). A corner of a garden: grass tiles, open tiles of earth
// with their numbers, a flag that waves, DIGBY the mole who comes up out of his hole and looks round, and a gold coin
// that turns. 16 frames of 125 ms = one loop of 2 s.
#include "tfdraw/tfdraw.h"

// a raised tile of grass (its light top edge, its dark lower edge), or an open tile of earth
static void msi_tile(float x, float y, float w, bool open)
{
    if (open) {
        tfd_rrect((int)x, (int)y, (int)w, (int)w, (int)(w * 0.12f), tfd_rgb(120, 78, 46), 256);
        tfd_rrect((int)(x + w * 0.06f), (int)(y + w * 0.06f), (int)(w * 0.88f), (int)(w * 0.88f), (int)(w * 0.1f),
                  tfd_rgb(152, 104, 64), 256);
        return;
    }
    tfd_rrect((int)x, (int)y, (int)w, (int)w, (int)(w * 0.14f), tfd_rgb(54, 120, 46), 256);
    tfd_rrect((int)x, (int)y, (int)w, (int)(w * 0.9f), (int)(w * 0.14f), tfd_rgb(104, 190, 72), 256);
    tfd_rrect((int)(x + w * 0.1f), (int)(y + w * 0.08f), (int)(w * 0.8f), (int)(w * 0.12f), (int)(w * 0.06f),
              tfd_rgb(156, 226, 110), 200);
    for (int k = 0; k < 3; k++) {
        tfd_line(x + w * (0.25f + k * 0.22f), y + w * 0.62f, x + w * (0.22f + k * 0.22f), y + w * 0.48f, w * 0.05f,
                 tfd_rgb(70, 150, 56), 220);
    }
}
// a number of the board as thick strokes (1 blue, 2 green: the colours of the game)
static void msi_num(float cx, float cy, float h, int n)
{
    float w = h * 0.16f;
    if (n == 1) {
        uint16_t c = tfd_rgb(60, 110, 240);
        tfd_line(cx + h * 0.05f, cy - h * 0.5f, cx + h * 0.05f, cy + h * 0.5f, w, c, 256);
        tfd_line(cx + h * 0.05f, cy - h * 0.5f, cx - h * 0.2f, cy - h * 0.28f, w, c, 256);
        tfd_line(cx - h * 0.18f, cy + h * 0.5f, cx + h * 0.28f, cy + h * 0.5f, w, c, 256);
        return;
    }
    uint16_t c = tfd_rgb(40, 150, 60);
    tfd_arc(cx, cy - h * 0.2f, h * 0.27f, w, 3.3f, 6.9f, c, 256);
    tfd_line(cx + h * 0.24f, cy - h * 0.08f, cx - h * 0.27f, cy + h * 0.5f, w, c, 256);
    tfd_line(cx - h * 0.27f, cy + h * 0.5f, cx + h * 0.3f, cy + h * 0.5f, w, c, 256);
}
static void icon(int x, int y, int s)
{
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    float u = s / 100.0f, t = icon_time(), ph = fmodf(t, 2.0f) / 2.0f, w2 = ph * 6.2831853f;
    tfd_vgrad(x, y, s, s, tfd_rgb(150, 210, 250), tfd_rgb(70, 140, 70));
    float T = 31 * u, G = 3 * u, ox = x + 2 * u, oy = y + 2 * u;
    // the board: (row, column) -> 0 grass, 1 open, the numbers, the hole of the mole in the middle
    static const int8_t OPEN[9] = {0, 1, 0, 1, 1, 1, 0, 1, 0}, NUM[9] = {0, 1, 0, 2, 0, 1, 0, 0, 0};
    for (int k = 0; k < 9; k++) {
        float tx = ox + (k % 3) * (T + G), ty = oy + (k / 3) * (T + G);
        msi_tile(tx, ty, T, OPEN[k]);
        if (NUM[k]) {
            msi_num(tx + T / 2, ty + T / 2, T * 0.56f, NUM[k]);
        }
    }
    // the flag on the tile at the top right: it waves (four waves in each loop)
    {
        float fx = ox + 2 * (T + G) + T * 0.34f, fy = oy + T * 0.18f;
        tfd_line(fx, fy, fx, fy + T * 0.7f, 2.2f * u, tfd_rgb(80, 54, 36), 256);
        tfd_disc(fx + 0.5f * u, fy + T * 0.72f, 3.5f * u, tfd_rgb(70, 60, 50), 200);
        float pts[12];
        for (int i = 0; i < 3; i++) {
            float a = (float)i / 2, wv = sinf(w2 * 2 + a * 4) * 2.2f * u * a;
            pts[2 * i] = fx + a * 15 * u;
            pts[2 * i + 1] = fy + 1 * u + wv;
            pts[2 * (5 - i)] = fx + a * 15 * u;
            pts[2 * (5 - i) + 1] = fy + (11 - a * 3) * u + wv;
        }
        tfd_poly(pts, 6, tfd_rgb(230, 40, 44), 256);
        tfd_line(pts[0], pts[1] + u, pts[4], pts[5] + u, 1.4f * u, tfd_rgb(255, 120, 110), 200);
    }
    // the coin on the open tile at the bottom: it turns (its width), and shines
    {
        float cx = ox + (T + G) + T / 2, cy = oy + 2 * (T + G) + T / 2, wd = fabsf(cosf(w2)) * 9 * u + 1.2f * u;
        tfd_glow(cx, cy, 15 * u, tfd_rgb(255, 210, 80), 70);
        float pts[32];
        for (int i = 0; i < 16; i++) {
            float a = i * 0.3927f;
            pts[2 * i] = cx + cosf(a) * wd;
            pts[2 * i + 1] = cy + sinf(a) * 9 * u;
        }
        tfd_poly(pts, 16, tfd_rgb(200, 140, 30), 256);
        for (int i = 0; i < 16; i++) {
            pts[2 * i] = cx + (pts[2 * i] - cx) * 0.75f;
            pts[2 * i + 1] = cy + (pts[2 * i + 1] - cy) * 0.75f;
        }
        tfd_poly(pts, 16, tfd_rgb(255, 212, 70), 256);
        if (fmodf(ph * 2, 1.0f) < 0.25f) {
            tfd_glow(cx - wd * 0.4f, cy - 4 * u, 5 * u, tfd_rgb(255, 255, 230), 220);
        }
    }
    // DIGBY: up out of the hole in the middle tile, a look to the left and to the right, then down again
    {
        float cx = ox + (T + G) + T / 2, hy = oy + (T + G) + T * 0.72f;
        float up = ph < 0.15f ? ph / 0.15f : ph < 0.8f ? 1 : ph < 0.95f ? 1 - (ph - 0.8f) / 0.15f : 0;
        up = up * up * (3 - 2 * up);
        float look = ph > 0.25f && ph < 0.75f ? sinf((ph - 0.25f) / 0.5f * 6.2831853f) * 2.2f * u : 0;
        tfd_disc(cx, hy, 11 * u, tfd_rgb(60, 36, 24), 256);
        float my = hy + (1 - up) * 16 * u - 9 * u;
        tfd_clip(x, y, x + s, (int)(hy + 2 * u));
        tfd_disc(cx, my, 9.5f * u, tfd_rgb(110, 84, 74), 256);
        tfd_disc(cx, my + 6 * u, 8 * u, tfd_rgb(110, 84, 74), 256);
        tfd_disc(cx + look, my + 3.5f * u, 4.6f * u, tfd_rgb(214, 180, 160), 256);
        tfd_disc(cx - 3.6f * u + look, my - 1.5f * u, 1.7f * u, tfd_rgb(20, 16, 16), 256);
        tfd_disc(cx + 3.6f * u + look, my - 1.5f * u, 1.7f * u, tfd_rgb(20, 16, 16), 256);
        tfd_disc(cx - 3.2f * u + look, my - 2 * u, 0.6f * u, tfd_rgb(255, 255, 255), 256);
        tfd_disc(cx + look, my + 2.4f * u, 2.2f * u, tfd_rgb(240, 110, 130), 256);
        tfd_disc(cx - 7 * u, my + 9 * u, 3 * u, tfd_rgb(214, 180, 160), 256);
        tfd_disc(cx + 7 * u, my + 9 * u, 3 * u, tfd_rgb(214, 180, 160), 256);
        tfd_clip(x, y, x + s, y + s);
        tfd_arc(cx, hy, 11 * u, 2.4f * u, 0.1f, 3.04f, tfd_rgb(94, 60, 36), 256);
        // bits of earth when he comes up
        if (ph < 0.3f) {
            for (int k = 0; k < 4; k++) {
                float q = ph / 0.3f, dx = (k - 1.5f) * 6 * u * q;
                tfd_disc(cx + dx, hy - 6 * u - 14 * u * q * (1 - q) * 2, 1.4f * u, tfd_rgb(120, 78, 46),
                         (int)(256 * (1 - q)));
            }
        }
    }
    tfd_noclip();
}
