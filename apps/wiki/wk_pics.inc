// WIKI on the Tab5: the pictures of the article (only app_wiki.c includes this file, and only for the Tab5).
// The task: the app task of TFN OS (one picture decodes in each frame).
// The memory: the pixels of each picture and of the big picture are in PSRAM; pics_free() frees them.
//
// wk_html.inc lists the pictures of the page (s_doc.pic: the <img> of 100 x 60 px or more, also in the
// infobox and the frames that the text skips). Each one is a line of its own in the article column, before the block
// that comes after it on the page (the first picture of the infobox: at the top). The pictures are WebP in the ZIM
// file (libs/simplewebp, wk_webp.c): one picture decodes in each frame, the ones on the screen first. A tap on a picture
// shows it big (the whole frame); a tap closes it.
#include "simplewebp/simplewebp.h"

#define BK_PIC    9                  // the kind of a picture line (wline_t.blk; w0 = the picture)
#define PIC_MAX_H 240                // the height of a picture in the column at most (frame px; 41 mm)
#define PIC_GAP   10                 // the space above and below a picture
static uint16_t *s_pic_px[WK_PICS];  // the decoded pictures (the size of their line), NULL: not yet or a fault
static int16_t s_pic_w[WK_PICS], s_pic_h[WK_PICS];
static uint8_t s_pic_st[WK_PICS];  // 0 not decoded, 1 decoded, 2 a fault (not WebP, no memory)
static int s_bigpic = -1;          // the picture that shows big (-1: none)
static uint16_t *s_big_px;
static int s_big_w, s_big_h;
static const uint8_t PLATE[3] = {236, 234, 226};  // (under the clear parts of a picture: a logo, a map)

static void pics_free(void)
{
    for (int i = 0; i < WK_PICS; i++) {
        free(s_pic_px[i]);
        s_pic_px[i] = NULL;
        s_pic_st[i] = 0;
    }
    free(s_big_px);
    s_big_px = NULL;
    s_bigpic = -1;
}

// The picture lines of the pictures that show before the block bi (UINT32_MAX: all that are left), from y; returns the y
// after them. The size: 1.25 frame px for each px of the page, in the width of the column, PIC_MAX_H high at most.
static int32_t pics_before(uint32_t bi, int32_t y)
{
    for (int i = 0; i < s_doc.npic; i++) {
        const wpic_t *p = &s_doc.pic[i];
        if (p->blk > bi || (bi != UINT32_MAX && p->blk != bi)) {
            continue;
        }
        int cw = AR - AL, tw = p->w * 5 / 4, th;
        if (tw > cw) {
            tw = cw;
        }
        th = p->h * tw / p->w;
        if (th > PIC_MAX_H) {
            th = PIC_MAX_H;
            tw = p->w * th / p->h;
        }
        if (tw < 8 || th < 8) {
            continue;
        }
        s_pic_w[i] = (int16_t)tw;
        s_pic_h[i] = (int16_t)th;
        if (!add_line((uint32_t)i, (uint32_t)i, y, th + 2 * PIC_GAP, BK_PIC, true)) {
            break;
        }
        y += th + 2 * PIC_GAP;
    }
    return y;
}

// The WebP picture of a URL of the file, to tw x th RGB565 (the byte order of the frame; the clear parts over PLATE).
// fit: tw x th is the box; the picture keeps its shape in it, at most 2.5 times its size (*ow, *oh: the size).
static uint16_t *pic_decode(const char *url, int tw, int th, bool fit, int *ow, int *oh)
{
    uint32_t idx = zim_find_url(s_z, 'C', url);
    if (idx == ZIM_NONE) {
        idx = zim_find_url(s_z, 'I', url);
    }
    if (idx != ZIM_NONE) {
        idx = zim_follow(s_z, idx);
    }
    zim_entry_t e;
    if (idx == ZIM_NONE || !zim_entry(s_z, idx, &e)) {
        return NULL;
    }
    uint32_t len = 0;
    const uint8_t *b = zim_blob(s_z, e.cluster, e.blob, &len);
    if (!b || len < 16 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WEBP", 4)) {
        return NULL;
    }
    simplewebp *sw = NULL;
    if (simplewebp_load_from_memory((void *)b, len, NULL, &sw) != SIMPLEWEBP_NO_ERROR || !sw) {
        return NULL;
    }
    size_t iw = 0, ih = 0;
    simplewebp_get_dimensions(sw, &iw, &ih);
    uint8_t *rgba = iw && ih && iw <= 4096 && ih <= 4096 ? wk_alloc(iw * ih * 4) : NULL;
    bool ok = rgba && simplewebp_decode(sw, rgba, NULL) == SIMPLEWEBP_NO_ERROR;
    simplewebp_unload(sw);
    if (!ok) {
        free(rgba);
        return NULL;
    }
    if (fit) {
        float k = (float)tw / (float)iw;
        if ((float)th / (float)ih < k) {
            k = (float)th / (float)ih;
        }
        if (k > 2.5f) {
            k = 2.5f;
        }
        tw = (int)((float)iw * k);
        th = (int)((float)ih * k);
        if (tw < 1) {
            tw = 1;
        }
        if (th < 1) {
            th = 1;
        }
    }
    uint16_t *out = wk_alloc((size_t)tw * th * 2);
    if (out) {
        // bilinear: the middle of each output pixel in the picture
        for (int y = 0; y < th; y++) {
            float sy = ((float)y + 0.5f) * (float)ih / (float)th - 0.5f;
            int y0 = sy < 0 ? 0 : (int)sy, y1 = y0 + 1 < (int)ih ? y0 + 1 : y0;
            int fy = (int)((sy - (float)y0) * 256);
            if (fy < 0) {
                fy = 0;
            }
            for (int x = 0; x < tw; x++) {
                float sx = ((float)x + 0.5f) * (float)iw / (float)tw - 0.5f;
                int x0 = sx < 0 ? 0 : (int)sx, x1 = x0 + 1 < (int)iw ? x0 + 1 : x0;
                int fx = (int)((sx - (float)x0) * 256);
                if (fx < 0) {
                    fx = 0;
                }
                const uint8_t *a = rgba + ((size_t)y0 * iw + x0) * 4, *bb = rgba + ((size_t)y0 * iw + x1) * 4;
                const uint8_t *c = rgba + ((size_t)y1 * iw + x0) * 4, *d = rgba + ((size_t)y1 * iw + x1) * 4;
                int v[4];
                for (int k = 0; k < 4; k++) {
                    int top = a[k] * (256 - fx) + bb[k] * fx, bot = c[k] * (256 - fx) + d[k] * fx;
                    v[k] = (top * (256 - fy) + bot * fy) >> 16;
                }
                int al = v[3];
                uint8_t r = (uint8_t)((v[0] * al + PLATE[0] * (255 - al)) / 255);
                uint8_t g = (uint8_t)((v[1] * al + PLATE[1] * (255 - al)) / 255);
                uint8_t bl = (uint8_t)((v[2] * al + PLATE[2] * (255 - al)) / 255);
                out[(size_t)y * tw + x] = rgb(r, g, bl);
            }
        }
    }
    free(rgba);
    *ow = tw;
    *oh = th;
    return out;
}

// One picture in each frame: the first one on the screen (or just below it) that is not decoded.
static void pics_step(void)
{
    // (a decode takes 100-250 ms on the Tab5: not while the page glides)
    if (s_sc != SC_ARTICLE || s_pending != ZIM_NONE || !s_z || s_vel != 0) {
        return;
    }
    int32_t top = (int32_t)s_scroll, bot = top + view_h() * 2;
    for (uint32_t li = 0; li < s_nln; li++) {
        const wline_t *l = &s_ln[li];
        if (l->blk != BK_PIC || l->y + l->h < top) {
            continue;
        }
        if (l->y > bot) {
            break;
        }
        int i = (int)l->w0;
        if (s_pic_st[i]) {
            continue;
        }
        int w, h;
        uint32_t t0 = ms_now();
        s_pic_px[i] = pic_decode(s_doc.pic[i].url, s_pic_w[i], s_pic_h[i], false, &w, &h);
        s_pic_st[i] = s_pic_px[i] ? 1 : 2;
        ESP_LOGI(TAG, "picture %d %s: %dx%d, %u ms", i, s_pic_px[i] ? "ok" : "FAULT", w, h, (unsigned)(ms_now() - t0));
        return;
    }
}

// A picture line: the picture (or its place while it decodes) in the middle of the column.
static void draw_pic(const wline_t *l, int y)
{
    int i = (int)l->w0, w = s_pic_w[i], h = s_pic_h[i], x = (AL + AR) / 2 - w / 2, py = y + PIC_GAP;
    if (!s_pic_px[i]) {
        ui_rrect(x, py, w, h, 8, s_pic_st[i] == 2 ? rgb(34, 30, 40) : rgb(26, 32, 54));
        if (s_pic_st[i] == 2) {
            ui_text_c(&KF_LABEL, x + w / 2, py + h / 2 + 5, T("NO PICTURE", "אין תמונה"), rgb(120, 110, 130));
        }
        return;
    }
    for (int r = 0; r < h; r++) {
        int yy = py + r;
        if (yy < s_top || yy >= s_h) {
            continue;
        }
        int x0 = x < 0 ? 0 : x, x1 = x + w > s_w ? s_w : x + w;
        if (x1 > x0) {
            memcpy(&g_fb[yy * s_w + x0], &s_pic_px[i][(size_t)r * w + (x0 - x)], (size_t)(x1 - x0) * 2);
        }
    }
}

// The picture line under a tap (its index), or -1.
static int pic_at(int x, int y)
{
    int32_t dy = (int32_t)(y - s_top + s_scroll);
    for (uint32_t li = 0; li < s_nln; li++) {
        const wline_t *l = &s_ln[li];
        if (l->blk != BK_PIC || dy < l->y || dy >= l->y + l->h) {
            continue;
        }
        int i = (int)l->w0, w = s_pic_w[i], px = (AL + AR) / 2 - w / 2;
        return x >= px - 12 && x < px + w + 12 && s_pic_st[i] == 1 ? i : -1;
    }
    return -1;
}

static void big_open(int i)
{
    free(s_big_px);
    s_big_px = pic_decode(s_doc.pic[i].url, s_w - 24, s_h - 24, true, &s_big_w, &s_big_h);
    s_bigpic = s_big_px ? i : -1;
}

// The picture big, in the middle of the frame (a tap closes it).
static void draw_big(void)
{
    gfx_fill(rgb(6, 7, 12));
    int x = s_w / 2 - s_big_w / 2, y = s_h / 2 - s_big_h / 2;
    for (int r = 0; r < s_big_h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= s_h) {
            continue;
        }
        memcpy(&g_fb[yy * s_w + x], &s_big_px[(size_t)r * s_big_w], (size_t)s_big_w * 2);
    }
}
