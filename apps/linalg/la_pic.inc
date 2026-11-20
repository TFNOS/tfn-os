// LINEAR ALGEBRA: the parts of the live pictures. A view maps the plane of the mathematics (x right, y up) into the
// box of the picture; thin lines are Wu lines (cheap, smooth), thick lines and arrows come from libs/tfdraw.

// a filled polygon with a smooth edge: tfd_poly of libs/tfdraw with its row of samples in static memory (the 2.6 KB
// of that row on the stack, under the frames of the firmware, was too much for the 8 KB of the app task)
static void la_poly(const float *p, int n, uint16_t c, int a)
{
    float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
    for (int i = 0; i < n; i++) {
        x0 = tfd_min(x0, p[2 * i]);
        x1 = tfd_max(x1, p[2 * i]);
        y0 = tfd_min(y0, p[2 * i + 1]);
        y1 = tfd_max(y1, p[2 * i + 1]);
    }
    int xa = tfd_fl(x0), xb = tfd_ce(x1), ya = tfd_fl(y0), yb = tfd_ce(y1);
    if (xa < tfd_cx0) {
        xa = tfd_cx0;
    }
    if (xa < 0) {
        xa = 0;
    }
    if (xb > tfd_cx1 - 1) {
        xb = tfd_cx1 - 1;
    }
    if (xb > tfd_w - 1) {
        xb = tfd_w - 1;
    }
    if (ya < tfd_cy0) {
        ya = tfd_cy0;
    }
    if (ya < 0) {
        ya = 0;
    }
    if (yb > tfd_cy1 - 1) {
        yb = tfd_cy1 - 1;
    }
    if (yb > tfd_h - 1) {
        yb = tfd_h - 1;
    }
    int w = xb - xa + 1;
    if (w <= 0 || w > 640 || n < 3 || n > 32) {
        return;
    }
    static float cov[642];  // (static: the stack of the app task is small)
    for (int y = ya; y <= yb; y++) {
        bool any = false;
        memset(cov, 0, sizeof(float) * (size_t)(w + 1));
        for (int s = 0; s < 4; s++) {
            float sy = y + 0.125f + s * 0.25f, xs[32];
            int k = 0;
            for (int i = 0; i < n; i++) {
                float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                    xs[k++] = ax + (sy - ay) * (bx - ax) / (by - ay);
                }
            }
            for (int i = 1; i < k; i++) {
                for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                    float t = xs[j];
                    xs[j] = xs[j - 1];
                    xs[j - 1] = t;
                }
            }
            for (int j = 0; j + 1 < k; j += 2) {
                float l = tfd_max(0, xs[j] - xa), r = tfd_min((float)w, xs[j + 1] - xa);
                if (r <= l) {
                    continue;
                }
                int il = (int)l, ir = (int)r;
                any = true;
                if (il == ir) {
                    cov[il] += (r - l) * 0.25f;
                    continue;
                }
                cov[il] += (il + 1 - l) * 0.25f;
                for (int i = il + 1; i < ir; i++) {
                    cov[i] += 0.25f;
                }
                if (ir < w) {
                    cov[ir] += (r - ir) * 0.25f;
                }
            }
        }
        if (!any) {
            continue;
        }
        for (int i = 0; i < w; i++) {
            if (cov[i] > 0.002f) {
                tfd_px(xa + i, y, c, cov[i] >= 1 ? a : (int)(a * cov[i]));
            }
        }
    }
}

#define PIC_BG rgb(16, 19, 34)    // the colour of the box of a picture
static rect_t s_pr;               // the box of the picture now
static float s_vcx, s_vcy, s_vs;  // the view: the point (0, 0) on the screen, px for one unit
static bool s_mini;               // the small picture of a card on the chapters screen (less detail)

static inline float VX(float x)
{
    return s_vcx + x * s_vs;
}
static inline float VY(float y)
{
    return s_vcy - y * s_vs;
}
// the view: span units across the smaller side of the box, (cx, cy) of the mathematics in the middle
static void view_set(rect_t r, float span, float cx, float cy)
{
    s_pr = r;
    float m = (float)(r.w < r.h ? r.w : r.h);
    s_vs = m / span;
    s_vcx = r.x + r.w * 0.5f - cx * s_vs;
    s_vcy = r.y + r.h * 0.5f + cy * s_vs;
    tfd_clip(r.x, r.y, r.x + r.w, r.y + r.h);
}
static inline float ease_io(float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}
static inline float clamp01(float t)
{
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

// ---------------------------------------------------------------- lines
// cut the segment to the box of the picture (Liang-Barsky); false = nothing is in the box
static bool clip_seg(float *x0, float *y0, float *x1, float *y1)
{
    float bx0 = s_pr.x - 1, by0 = s_pr.y - 1, bx1 = s_pr.x + s_pr.w + 1, by1 = s_pr.y + s_pr.h + 1;
    float dx = *x1 - *x0, dy = *y1 - *y0, t0 = 0, t1 = 1;
    float p[4] = {-dx, dx, -dy, dy}, q[4] = {*x0 - bx0, bx1 - *x0, *y0 - by0, by1 - *y0};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) {
                return false;
            }
            continue;
        }
        float r = q[i] / p[i];
        if (p[i] < 0) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
    }
    float ax = *x0 + t0 * dx, ay = *y0 + t0 * dy;
    *x1 = *x0 + t1 * dx;
    *y1 = *y0 + t1 * dy;
    *x0 = ax;
    *y0 = ay;
    return true;
}
static inline float fpart(float x)
{
    return x - (float)(int)(x < 0 ? x - 1 : x);
}
// a thin smooth line in screen px (Xiaolin Wu)
static void wu(float x0, float y0, float x1, float y1, uint16_t c, int a)
{
    if (!clip_seg(&x0, &y0, &x1, &y1)) {
        return;
    }
    bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    if (steep) {
        float t = x0;
        x0 = y0;
        y0 = t;
        t = x1;
        x1 = y1;
        y1 = t;
    }
    if (x0 > x1) {
        float t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
    }
    float dx = x1 - x0, g = dx < 0.01f ? 0 : (y1 - y0) / dx;
    int xs = (int)(x0 + 0.5f), xe = (int)(x1 + 0.5f);
    float y = y0 + g * (xs - x0);
    for (int x = xs; x <= xe; x++) {
        int iy = (int)(y < 0 ? y - 1 : y);
        float f = y - iy;
        int a1 = (int)(a * (1 - f)), a2 = (int)(a * f);
        if (steep) {
            tfd_px(iy, x, c, a1);
            tfd_px(iy + 1, x, c, a2);
        } else {
            tfd_px(x, iy, c, a1);
            tfd_px(x, iy + 1, c, a2);
        }
        y += g;
    }
}
// a line in the plane of the mathematics (thin: w <= 1; else a thick line of tfdraw, cut to the box)
static void seg(float ax, float ay, float bx, float by, float w, uint16_t c, int a)
{
    float x0 = VX(ax), y0 = VY(ay), x1 = VX(bx), y1 = VY(by);
    if (w <= 1.01f) {
        wu(x0, y0, x1, y1, c, a);
        return;
    }
    if (clip_seg(&x0, &y0, &x1, &y1)) {
        tfd_line(x0, y0, x1, y1, w, c, a);
    }
}
// a line of dashes (screen px of a dash: d)
static void dashed(float ax, float ay, float bx, float by, uint16_t c, int a)
{
    float x0 = VX(ax), y0 = VY(ay), x1 = VX(bx), y1 = VY(by);
    float L = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    int n = (int)(L / 6);
    for (int i = 0; i < n; i += 2) {
        float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        wu(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1, c, a);
    }
}
// an arrow from (ax, ay) to (bx, by): a thick shaft and a filled head
static void arrow(float ax, float ay, float bx, float by, uint16_t c, float w, int a)
{
    float x0 = VX(ax), y0 = VY(ay), x1 = VX(bx), y1 = VY(by);
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
    if (!(L >= 1.5f)) {  // (a short arrow, or a NaN of a view with no size: the fuzz found it in the icon)
        if (x1 == x1 && y1 == y1) {
            tfd_disc(x1, y1, w, c, a);
        }
        return;
    }
    float ux = dx / L, uy = dy / L, hl = w * 3.2f + 3, hw = w * 1.9f + 2;
    if (hl > L * 0.6f) {
        hl = L * 0.6f;
        hw = hl * 0.6f;
    }
    float sx = x1 - ux * hl * 0.8f, sy = y1 - uy * hl * 0.8f;
    tfd_line(x0, y0, sx, sy, w, c, a);
    float p[6] = {
            x1, y1, x1 - ux * hl - uy * hw, y1 - uy * hl + ux * hw, x1 - ux * hl + uy * hw, y1 - uy * hl - ux * hw};
    la_poly(p, 3, c, a);
}
static void dot(float x, float y, float r, uint16_t c, int a)
{
    tfd_disc(VX(x), VY(y), r, c, a);
}
// a filled polygon in the plane of the mathematics (n <= 16 points)
static void fpoly(const float *pts, int n, uint16_t c, int a)
{
    float p[32];
    for (int i = 0; i < n && i < 16; i++) {
        p[2 * i] = VX(pts[2 * i]);
        p[2 * i + 1] = VY(pts[2 * i + 1]);
    }
    la_poly(p, n, c, a);
}
// a closed curve: the image of the unit circle under M (a, b, c, d), as 64 parts
static void ellipse(float a, float b, float c, float d, float ox, float oy, float w, uint16_t col, int al)
{
    float px = ox + a, py = oy + c;
    for (int i = 1; i <= 64; i++) {
        float t = i * 6.2831853f / 64;
        float cs = fcos(t), sn = fsin(t);
        float nx = ox + a * cs + b * sn, ny = oy + c * cs + d * sn;
        seg(px, py, nx, ny, w, col, al);
        px = nx;
        py = ny;
    }
}

// ---------------------------------------------------------------- the background, the grids, the labels
static void pic_bg(rect_t r)
{
    tfd_noclip();
    tfd_rrect(r.x, r.y, r.w, r.h, 12, PIC_BG, 256);
    tfd_clip(r.x + 1, r.y + 1, r.x + r.w - 1, r.y + r.h - 1);
}
// the grid of the plane: the lines x = k and y = k (k from -n to n), through the map (a b; c d) of each point
static void grid_map(float a, float b, float c, float d, int n, uint16_t col, int al)
{
    float R = (float)n;
    for (int k = -n; k <= n; k++) {
        int aa = k == 0 ? al * 2 : al;
        if (aa > 256) {
            aa = 256;
        }
        // the line x = k: (k, t) -> (a k + b t, c k + d t)
        seg(a * k - b * R, c * k - d * R, a * k + b * R, c * k + d * R, 1, col, aa);
        // the line y = k
        seg(-a * R + b * k, -c * R + d * k, a * R + b * k, c * R + d * k, 1, col, aa);
    }
}
static void grid_plain(int n, uint16_t col, int al)
{
    grid_map(1, 0, 0, 1, n, col, al);
}
// a small formula at a point of the plane (dx, dy: px from the point)
static void label(float x, float y, int dx, int dy, const char *math, uint16_t col)
{
    if (s_mini) {
        return;
    }
    int keep = s_mbase, ka = s_talpha;
    bool kd = s_mdisp;
    s_mbase = s_tsz > 1 ? s_tsz - 1 : 0;
    s_mdisp = false;
    bx_t b = m_measure(math);
    int px = (int)VX(x) + dx, py = (int)VY(y) + dy;
    // keep the label in the box
    int w = b.w / 4;
    if (px - w / 2 < s_pr.x + 2) {
        px = s_pr.x + 2 + w / 2;
    }
    if (px + w / 2 > s_pr.x + s_pr.w - 2) {
        px = s_pr.x + s_pr.w - 2 - w / 2;
    }
    if (py - b.a < s_pr.y + 2) {
        py = s_pr.y + 2 + b.a;
    }
    if (py + b.d > s_pr.y + s_pr.h - 2) {
        py = s_pr.y + s_pr.h - 2 - b.d;
    }
    int x0c = s_clx0, y0c = s_cly0, x1c = s_clx1, y1c = s_cly1;
    ty_clip(s_pr.x, s_pr.y, s_pr.x + s_pr.w, s_pr.y + s_pr.h);
    const char *p = math;
    m_list(&p, 0, px * 4 - b.w / 2, py, true, col);
    s_clx0 = x0c;
    s_cly0 = y0c;
    s_clx1 = x1c;
    s_cly1 = y1c;
    tfd_clip(s_pr.x, s_pr.y, s_pr.x + s_pr.w, s_pr.y + s_pr.h);
    s_mbase = keep;
    s_mdisp = kd;
    s_talpha = ka;
}
// a text label in the box of the picture (top left in English, top right in Hebrew)
static void pic_caption(const char *math, uint16_t col)
{
    if (s_mini) {
        return;
    }
    int keep = s_mbase;
    s_mbase = s_tsz > 1 ? s_tsz - 1 : 0;
    bx_t b = m_measure(math);
    int x4 = s_rtl ? (s_pr.x + s_pr.w - 6) * 4 - b.w : (s_pr.x + 6) * 4;
    int x0c = s_clx0, y0c = s_cly0, x1c = s_clx1, y1c = s_cly1;
    ty_clip(s_pr.x, s_pr.y, s_pr.x + s_pr.w, s_pr.y + s_pr.h);
    const char *p = math;
    m_list(&p, 0, x4, s_pr.y + 5 + b.a, true, col);
    s_clx0 = x0c;
    s_cly0 = y0c;
    s_clx1 = x1c;
    s_cly1 = y1c;
    tfd_clip(s_pr.x, s_pr.y, s_pr.x + s_pr.w, s_pr.y + s_pr.h);
    s_mbase = keep;
}
// a colour scale for a value v in [-1, 1]: blue - dark - orange
static uint16_t heat(float v)
{
    v = v < -1 ? -1 : v > 1 ? 1 : v;
    if (v >= 0) {
        return ui_mix(rgb(30, 34, 56), rgb(255, 170, 70), (int)(v * 255));
    }
    return ui_mix(rgb(30, 34, 56), rgb(70, 170, 255), (int)(-v * 255));
}
// the cells of a matrix as colours (a heat map) in the box (x, y, w, h); vmax = the value of the full colour
static void heatmap(const float *m, int n, int cols, rect_t r, float vmax)
{
    int cw = r.w / cols, ch = r.h / n;
    int c = cw < ch ? cw : ch;
    int x0 = r.x + (r.w - c * cols) / 2, y0 = r.y + (r.h - c * n) / 2;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < cols; j++) {
            float v = m[i * cols + j] / (vmax > 0 ? vmax : 1);
            tfd_rrect(x0 + j * c + 1, y0 + i * c + 1, c - 2, c - 2, c > 12 ? 3 : 1, heat(v), 256);
        }
    }
}
// ---------------------------------------------------------------- 3D (a slow turn round the vertical axis)
static float s_yaw = 0.6f, s_pitch = 0.42f;
static void p3(float x, float y, float z, float *sx, float *sy)
{
    float cy = fcos(s_yaw), sy_ = fsin(s_yaw), cp = fcos(s_pitch), sp = fsin(s_pitch);
    float X = cy * x - sy_ * y, Y = sy_ * x + cy * y;  // (z is up)
    float Zs = cp * z - sp * Y;
    *sx = VX(X);
    *sy = VY(Zs);
}
static void seg3(const float *a, const float *b, float w, uint16_t c, int al)
{
    float x0, y0, x1, y1;
    p3(a[0], a[1], a[2], &x0, &y0);
    p3(b[0], b[1], b[2], &x1, &y1);
    if (w <= 1.01f) {
        wu(x0, y0, x1, y1, c, al);
    } else if (clip_seg(&x0, &y0, &x1, &y1)) {
        tfd_line(x0, y0, x1, y1, w, c, al);
    }
}
static void arrow3(const float *v, uint16_t c, float w)
{
    float x0, y0, x1, y1;
    p3(0, 0, 0, &x0, &y0);
    p3(v[0], v[1], v[2], &x1, &y1);
    float kx = s_vcx, ky = s_vcy;
    // (arrow() works in the plane: give it the screen points as a plane with a unit of one px)
    float ks = s_vs;
    s_vcx = 0;
    s_vcy = 0;
    s_vs = 1;
    arrow(x0, -y0, x1, -y1, c, w, 256);
    s_vcx = kx;
    s_vcy = ky;
    s_vs = ks;
}
// a flat part of a plane: the parallelogram o + s u + t v (s, t in [-1, 1])
static void plane3(const float *u, const float *v, uint16_t c, int al)
{
    float p[8], q[3];
    const float sg[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 3; i++) {
            q[i] = sg[k][0] * u[i] + sg[k][1] * v[i];
        }
        p3(q[0], q[1], q[2], &p[2 * k], &p[2 * k + 1]);
    }
    la_poly(p, 4, c, al);
}
static void axes3(float L)
{
    const float o[3] = {0, 0, 0};
    float e[3][3] = {{L, 0, 0}, {0, L, 0}, {0, 0, L}};
    float m[3][3] = {{-L, 0, 0}, {0, -L, 0}, {0, 0, -L}};
    for (int i = 0; i < 3; i++) {
        seg3(m[i], o, 1, rgb(60, 68, 100), 200);
        seg3(o, e[i], 1, rgb(110, 120, 160), 230);
    }
}
