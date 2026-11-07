// The unit test of ui_hit() (main/uikit.h) against the hit tests that it replaced (R4c): each one on a grid of points
// round boxes of many sizes, also boxes with no width or height.
// Build and run: python tests/unit/run.py
#include <stdio.h>
#include "uikit.h"

typedef struct {
    int x, y, w, h;
} box_t;

static bool old_in0(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static bool old_in2(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx - 2 && x < bx + bw + 2 && y >= by - 2 && y < by + bh + 2;
}

static bool old_in6(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx - 6 && x < bx + bw + 6 && y >= by - 6 && y < by + bh + 6;
}

static bool old_in_m(int x, int y, int bx, int by, int bw, int bh, int m)
{
    return x >= bx - m && x < bx + bw + m && y >= by - m && y < by + bh + m;
}

static bool old_box(const box_t *b, int x, int y)
{
    return b->w && x >= b->x - 6 && x < b->x + b->w + 6 && y >= b->y - 6 && y < b->y + b->h + 6;
}

static bool new_box(const box_t *b, int x, int y)
{
    return b->w && ui_hit(x, y, b->x, b->y, b->w, b->h, 6);
}

int main(void)
{
    static const int sizes[] = {0, 1, 2, 5, 44};
    static const int places[] = {-7, 0, 3, 600};
    long n = 0, fails = 0;
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            for (int c = 0; c < 5; c++) {
                for (int d = 0; d < 5; d++) {
                    int bx = places[a], by = places[b], bw = sizes[c], bh = sizes[d];
                    box_t box = {bx, by, bw, bh};
                    for (int x = bx - 12; x < bx + bw + 12; x++) {
                        for (int y = by - 12; y < by + bh + 12; y++) {
                            n++;
                            fails += old_in0(x, y, bx, by, bw, bh) != ui_hit(x, y, bx, by, bw, bh, 0);
                            fails += old_in2(x, y, bx, by, bw, bh) != ui_hit(x, y, bx, by, bw, bh, 2);
                            fails += old_in6(x, y, bx, by, bw, bh) != ui_hit(x, y, bx, by, bw, bh, 6);
                            fails += old_in_m(x, y, bx, by, bw, bh, 3) != ui_hit(x, y, bx, by, bw, bh, 3);
                            fails += old_box(&box, x, y) != new_box(&box, x, y);
                        }
                    }
                }
            }
        }
    }
    printf("UI HIT TESTS %s (%ld points)\n", fails ? "FAILED" : "OK", n);
    return fails ? 1 : 0;
}
