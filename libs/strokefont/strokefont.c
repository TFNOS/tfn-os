// The stroke font (strokefont.h): each character is a list of points in a box of 0.6 x 1; -1 starts a new stroke.
#include <math.h>
#include <string.h>
#include "tfn/gfx.h"
#include "strokefont.h"

void stroke_line(float x0, float y0, float x1, float y1, float r, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int n = (int)(len / (r > 2 ? r * 0.6f : 1.2f)) + 1;
    for (int i = 0; i <= n; i++) {
        float k = (float)i / n;
        gfx_fill_circle((int)lroundf(x0 + dx * k), (int)lroundf(y0 + dy * k), (int)r, c);
    }
}

static const float A[] = {0, 1, .3f, 0, .6f, 1, -1, .12f, .64f, .48f, .64f};
static const float B[] = {0,    1,  0,    0,    .38f, 0,   .52f, .12f, .52f, .34f, .38f, .47f, 0,
                          .47f, -1, .38f, .47f, .58f, .6f, .58f, .86f, .44f, 1,    0,    1};
static const float C[] = {.6f, .16f, .46f, .02f, .2f,  .02f, .04f, .2f, .02f,
                          .5f, .04f, .8f,  .2f,  .98f, .46f, .98f, .6f, .84f};
static const float D[] = {0, 0, 0, 1, .32f, 1, .54f, .82f, .6f, .5f, .54f, .18f, .32f, 0, 0, 0};
static const float E[] = {.56f, 0, 0, 0, 0, 1, .56f, 1, -1, 0, .5f, .44f, .5f};
static const float F[] = {.56f, 0, 0, 0, 0, 1, -1, 0, .5f, .44f, .5f};
static const float G[] = {.6f, .16f, .46f, .02f, .2f,  .02f, .04f, .2f, .02f, .5f,  .04f,
                          .8f, .2f,  .98f, .46f, .98f, .6f,  .84f, .6f, .58f, .34f, .58f};
static const float SH[] = {.22f, 0, .14f, 1, -1, .46f, 0, .38f, 1, -1, 0, .33f, .58f, .33f, -1, 0, .67f, .58f, .67f};
static const float D0[] = {.3f, 0, .08f, .12f, 0, .5f, .08f, .88f, .3f, 1, .52f, .88f, .6f, .5f, .52f, .12f, .3f, 0};
static const float D1[] = {.12f, .2f, .34f, 0, .34f, 1, -1, .12f, 1, .56f, 1};
static const float D2[] = {.04f, .2f, .16f, .05f, .32f, 0, .48f, .05f, .58f, .2f, .56f, .38f, .04f, 1, .6f, 1};
static const float D3[] = {.04f, .08f, .2f,  0,    .42f, 0,    .56f, .12f, .56f, .34f, .4f, .47f, .22f, .47f,
                           -1,   .4f,  .47f, .58f, .6f,  .58f, .86f, .42f, 1,    .18f, 1,   .02f, .9f};
static const float D4[] = {.46f, 1, .46f, 0, 0, .68f, .6f, .68f};
static const float D5[] = {.56f, 0,    .08f, 0,    .04f, .44f, .26f, .38f, .46f, .42f,
                           .6f,  .58f, .58f, .84f, .42f, 1,    .18f, 1,    .02f, .9f};
static const float D6[] = {.52f, .06f, .36f, 0,   .18f, .04f, .04f, .24f, 0,    .6f,  .06f, .88f, .28f,
                           1,    .5f,  .92f, .6f, .72f, .54f, .5f,  .32f, .42f, .12f, .48f, .02f, .62f};
static const float D7[] = {0, 0, .6f, 0, .22f, 1};
static const float D8[] = {.3f, 0,    .08f, .1f,  .08f, .36f, .3f,  .47f, .52f, .58f, .56f, .86f, .3f,
                           1,   .04f, .86f, .08f, .58f, .3f,  .47f, .52f, .36f, .52f, .1f,  .3f,  0};
static const float D9[] = {.58f, .4f, .48f, .54f, .28f, .58f, .08f, .5f,  0,    .28f, .1f, .06f, .3f,
                           0,    .5f, .06f, .6f,  .3f,  .58f, .6f,  .46f, .88f, .26f, 1,   .06f, .94f};

void stroke_char(char ch, float x, float y, float h, float r, uint16_t c)
{
    const float *p = NULL;
    int n = 0;
#define L_(a) p = a, n = (int)(sizeof(a) / sizeof(a[0]))
    switch (ch) {
    case 'A':
        L_(A);
        break;
    case 'B':
        L_(B);
        break;
    case 'C':
        L_(C);
        break;
    case 'D':
        L_(D);
        break;
    case 'E':
        L_(E);
        break;
    case 'F':
        L_(F);
        break;
    case 'G':
        L_(G);
        break;
    case '#':
        L_(SH);
        break;
    case '0':
        L_(D0);
        break;
    case '1':
        L_(D1);
        break;
    case '2':
        L_(D2);
        break;
    case '3':
        L_(D3);
        break;
    case '4':
        L_(D4);
        break;
    case '5':
        L_(D5);
        break;
    case '6':
        L_(D6);
        break;
    case '7':
        L_(D7);
        break;
    case '8':
        L_(D8);
        break;
    case '9':
        L_(D9);
        break;
    default:
        return;
    }
#undef L_
    float px = 0, py = 0;
    int have = 0;
    for (int i = 0; i < n;) {
        if (p[i] < -0.5f) {
            have = 0;
            i++;
            continue;
        }
        float qx = x + p[i] * h, qy = y + p[i + 1] * h;
        if (have) {
            stroke_line(px, py, qx, qy, r, c);
        }
        px = qx;
        py = qy;
        have = 1;
        i += 2;
    }
}

float stroke_width(const char *s, float h)
{
    int n = (int)strlen(s);
    return n ? (n - 1) * 0.78f * h + 0.6f * h : 0;
}

float stroke_text(const char *s, float x, float y, float h, float r, uint16_t c)
{
    float x0 = x;
    for (; *s; s++) {
        stroke_char(*s, x, y, h, r, c);
        x += 0.78f * h;
    }
    return x - x0;
}
