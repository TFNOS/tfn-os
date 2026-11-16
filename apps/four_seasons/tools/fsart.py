# FOUR SEASONS: the pixel-art raster library of the PC baker (numpy). Flat colours with no anti-aliasing, ramps with
# short Bayer dither steps, sphere light on round shapes, outlines,
# snow caps, cast shadows. A scene function draws into a Canvas; bake.py turns the pictures into the files of the SD card.
import math
import numpy as np

BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32)
BAYER = (BAYER4 + 0.5) / 16.0            # thresholds in (0, 1)


def clamp(v, a, b):
    return a if v < a else b if v > b else v


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    t = clamp(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def hx(h):
    n = int(h[1:], 16)
    return ((n >> 16) & 255, (n >> 8) & 255, n & 255)


def mix(a, b, t):
    return (int(round(a[0] + (b[0] - a[0]) * t)), int(round(a[1] + (b[1] - a[1]) * t)), int(round(a[2] + (b[2] - a[2]) * t)))


def mul(c, k):
    return (int(clamp(round(c[0] * k), 0, 255)), int(clamp(round(c[1] * k), 0, 255)), int(clamp(round(c[2] * k), 0, 255)))


def add(c, d):
    return (int(clamp(c[0] + d[0], 0, 255)), int(clamp(c[1] + d[1], 0, 255)), int(clamp(c[2] + d[2], 0, 255)))


def ramp(*cols):
    """a ramp of colours from dark to light (hex strings or tuples)"""
    return [hx(c) if isinstance(c, str) else tuple(c) for c in cols]


def ramp_between(a, b, n):
    return [mix(a, b, i / (n - 1)) for i in range(n)]


class RNG:
    """the same small generator as the look page (mulberry32)"""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def __call__(self):
        self.s = (self.s + 0x6D2B79F5) & 0xFFFFFFFF
        t = self.s
        t = ((t ^ (t >> 15)) * (t | 1)) & 0xFFFFFFFF
        t ^= (t + (((t ^ (t >> 7)) * (t | 61)) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return ((t ^ (t >> 14)) & 0xFFFFFFFF) / 4294967296.0

    def uni(self, a, b):
        return a + (b - a) * self()

    def int(self, a, b):
        return int(a + (b - a + 1) * self()) if b >= a else a

    def pick(self, seq):
        return seq[min(len(seq) - 1, int(self() * len(seq)))]


def _hash(i):
    i = (i ^ 0x9e3779b9) & 0xFFFFFFFF
    i = (i * 0x85ebca6b) & 0xFFFFFFFF
    i ^= i >> 13
    i = (i * 0xc2b2ae35) & 0xFFFFFFFF
    i ^= i >> 16
    return i / 4294967296.0


def noise1(x):
    i = math.floor(x)
    f = x - i
    u = f * f * (3 - 2 * f)
    return lerp(_hash(i), _hash(i + 1), u) * 2 - 1


def fbm1(x, o=3):
    s, a, f = 0.0, 0.5, 1.0
    for k in range(o):
        s += a * noise1(x * f + k * 17.3)
        f *= 2.03
        a *= 0.5
    return s


def vnoise2(w, h, scale, seed=1):
    """smooth value noise of the size w x h, -1..1 (one octave)"""
    gw, gh = int(w / scale) + 3, int(h / scale) + 3
    rng = np.random.default_rng(seed)
    g = rng.random((gh, gw)).astype(np.float32) * 2 - 1
    ys = np.arange(h, dtype=np.float32) / scale
    xs = np.arange(w, dtype=np.float32) / scale
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    fy = ys - y0
    fx = xs - x0
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = g[y0][:, x0]
    b = g[y0][:, x0 + 1]
    c = g[y0 + 1][:, x0]
    d = g[y0 + 1][:, x0 + 1]
    top = a + (b - a) * fx[None, :]
    bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def fbm2(w, h, scale, seed=1, octaves=3):
    s = np.zeros((h, w), np.float32)
    a, sc = 0.5, scale
    for k in range(octaves):
        s += a * vnoise2(w, h, max(1.0, sc), seed + k * 101)
        a *= 0.5
        sc /= 2.0
    return s


class Mask:
    """a set of pixels: the bool array m at the place (x0, y0) of the canvas"""
    __slots__ = ('x0', 'y0', 'm')

    def __init__(self, x0, y0, m):
        self.x0, self.y0, self.m = int(x0), int(y0), m

    @property
    def w(self):
        return self.m.shape[1]

    @property
    def h(self):
        return self.m.shape[0]

    def empty(self):
        return not self.m.any()

    def coords(self):
        ys, xs = np.nonzero(self.m)
        return xs + self.x0, ys + self.y0

    def grid(self):
        """the x and y of each element of the box (float arrays, pixel centres)"""
        ys, xs = np.mgrid[0:self.h, 0:self.w]
        return xs.astype(np.float32) + self.x0 + 0.5, ys.astype(np.float32) + self.y0 + 0.5

    def shift(self, dx, dy):
        return Mask(self.x0 + dx, self.y0 + dy, self.m)

    def bbox(self):
        return self.x0, self.y0, self.x0 + self.w, self.y0 + self.h

    def _union_box(self, o):
        x0 = min(self.x0, o.x0)
        y0 = min(self.y0, o.y0)
        x1 = max(self.x0 + self.w, o.x0 + o.w)
        y1 = max(self.y0 + self.h, o.y0 + o.h)
        return x0, y0, x1, y1

    def _place(self, x0, y0, x1, y1):
        a = np.zeros((y1 - y0, x1 - x0), bool)
        a[self.y0 - y0:self.y0 - y0 + self.h, self.x0 - x0:self.x0 - x0 + self.w] = self.m
        return a

    def __or__(self, o):
        if o is None:
            return self
        x0, y0, x1, y1 = self._union_box(o)
        return Mask(x0, y0, self._place(x0, y0, x1, y1) | o._place(x0, y0, x1, y1))

    def __and__(self, o):
        x0, y0, x1, y1 = self._union_box(o)
        return Mask(x0, y0, self._place(x0, y0, x1, y1) & o._place(x0, y0, x1, y1))

    def __sub__(self, o):
        if o is None:
            return self
        x0, y0, x1, y1 = self._union_box(o)
        return Mask(x0, y0, self._place(x0, y0, x1, y1) & ~o._place(x0, y0, x1, y1))

    def grow(self, n=1):
        """the mask with n pixels more on each side (4-neighbours, n times)"""
        a = np.pad(self.m, n)
        for _ in range(n):
            b = a.copy()
            b[1:, :] |= a[:-1, :]
            b[:-1, :] |= a[1:, :]
            b[:, 1:] |= a[:, :-1]
            b[:, :-1] |= a[:, 1:]
            a = b
        return Mask(self.x0 - n, self.y0 - n, a)

    def grow8(self, n=1):
        a = np.pad(self.m, n)
        for _ in range(n):
            b = a.copy()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx or dy:
                        b |= np.roll(np.roll(a, dy, 0), dx, 1)
            a = b
        return Mask(self.x0 - n, self.y0 - n, a)

    def edge(self):
        """the pixels of the mask next to a pixel out of it"""
        a = np.pad(self.m, 1)
        inner = a[1:-1, 1:-1] & a[:-2, 1:-1] & a[2:, 1:-1] & a[1:-1, :-2] & a[1:-1, 2:]
        return Mask(self.x0, self.y0, self.m & ~inner)

    def side(self, dx, dy, n=1):
        """the pixels of the mask whose neighbour n px in the direction (dx, dy) is out of the mask (a lit or dark rim)"""
        a = np.pad(self.m, n)
        h, w = self.m.shape
        nb = a[n + dy * n:n + dy * n + h, n + dx * n:n + dx * n + w]
        return Mask(self.x0, self.y0, self.m & ~nb)

    def top(self, depth=1):
        """the top pixels of each column (depth px down from each top edge)"""
        out = np.zeros_like(self.m)
        a = self.m
        prev_out = np.ones(a.shape[1], bool)
        run = np.zeros(a.shape[1], int)
        for y in range(a.shape[0]):
            row = a[y]
            run = np.where(row, np.where(prev_out, 1, run + 1), 0)
            out[y] = row & (run <= depth) & (run > 0)
            prev_out = ~row
        return Mask(self.x0, self.y0, out)

    def bottom(self, depth=1):
        f = Mask(self.x0, self.y0, self.m[::-1, :]).top(depth)
        return Mask(self.x0, self.y0, f.m[::-1, :])


# ---------------------------------------------------------------- shapes (they give a Mask)
def m_rect(x, y, w, h):
    x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h))
    return Mask(x, y, np.ones((max(0, h), max(0, w)), bool))


def m_ellipse(cx, cy, rx, ry, rot=0.0):
    rx, ry = max(0.5, rx), max(0.5, ry)
    r = max(rx, ry) + 1
    x0, y0 = int(math.floor(cx - r)), int(math.floor(cy - r))
    x1, y1 = int(math.ceil(cx + r)) + 1, int(math.ceil(cy + r)) + 1
    ys, xs = np.mgrid[y0:y1, x0:x1]
    dx = xs + 0.5 - cx
    dy = ys + 0.5 - cy
    if rot:
        c, s = math.cos(rot), math.sin(rot)
        dx, dy = dx * c + dy * s, -dx * s + dy * c
    return Mask(x0, y0, (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0)


def m_disc(cx, cy, r):
    return m_ellipse(cx, cy, r, r)


def m_poly(pts):
    """the pixels whose centre is in the polygon (the even-odd rule)"""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, x1 = int(math.floor(min(xs))), int(math.ceil(max(xs))) + 1
    y0, y1 = int(math.floor(min(ys))), int(math.ceil(max(ys))) + 1
    m = np.zeros((max(1, y1 - y0), max(1, x1 - x0)), bool)
    n = len(pts)
    for yy in range(y0, y1):
        yc = yy + 0.5
        cross = []
        for i in range(n):
            ax, ay = pts[i]
            bx, by = pts[(i + 1) % n]
            if (ay <= yc < by) or (by <= yc < ay):
                cross.append(ax + (yc - ay) * (bx - ax) / (by - ay))
        cross.sort()
        for k in range(0, len(cross) - 1, 2):
            a = int(math.ceil(cross[k] - 0.5))
            b = int(math.floor(cross[k + 1] - 0.5))
            if b >= a:
                m[yy - y0, max(0, a - x0):min(m.shape[1], b - x0 + 1)] = True
    return Mask(x0, y0, m)


def m_line(x0, y0, x1, y1, w=1.0):
    """a thick line (a quad with round ends when w >= 2)"""
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    if L < 1e-6:
        return m_disc(x0, y0, max(0.5, w / 2))
    if w <= 1.01:
        n = int(max(abs(dx), abs(dy))) + 1
        ts = np.linspace(0, 1, n + 1)
        px = np.floor(x0 + dx * ts).astype(int)
        py = np.floor(y0 + dy * ts).astype(int)
        bx0, by0 = px.min(), py.min()
        m = np.zeros((py.max() - by0 + 1, px.max() - bx0 + 1), bool)
        m[py - by0, px - bx0] = True
        return Mask(bx0, by0, m)
    nx, ny = -dy / L * w / 2, dx / L * w / 2
    q = m_poly([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])
    if w >= 2.5:
        q = q | m_disc(x0, y0, w / 2) | m_disc(x1, y1, w / 2)
    return q


def bezier(p0, p1, p2, p3=None, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        if p3 is None:
            x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
            y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        else:
            x = (1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0] + t ** 3 * p3[0]
            y = (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1] + t ** 3 * p3[1]
        out.append((x, y))
    return out


def m_polyline(pts, w=1.0, w1=None):
    """a thick polyline (the width goes from w to w1)"""
    w1 = w if w1 is None else w1
    m = None
    n = len(pts) - 1
    for i in range(n):
        a = lerp(w, w1, i / max(1, n))
        b = lerp(w, w1, (i + 1) / max(1, n))
        seg = m_line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], (a + b) / 2)
        m = seg if m is None else m | seg
    return m


def m_union(ms):
    out = None
    for m in ms:
        if m is not None:
            out = m if out is None else out | m
    return out


# ---------------------------------------------------------------- the canvas
class Canvas:
    def __init__(self, w, h, bg=(0, 0, 0)):
        self.w, self.h = w, h
        self.rgb = np.zeros((h, w, 3), np.uint8)
        self.rgb[:, :] = bg
        by = np.arange(h) & 3
        bx = np.arange(w) & 3
        self.bayer = BAYER[by[:, None], bx[None, :]]

    def copy(self):
        c = Canvas.__new__(Canvas)
        c.w, c.h, c.rgb, c.bayer = self.w, self.h, self.rgb.copy(), self.bayer
        return c

    # the part of a mask in the canvas: (canvas slices, mask slice)
    def _clip(self, m):
        x0, y0 = m.x0, m.y0
        x1, y1 = x0 + m.w, y0 + m.h
        cx0, cy0, cx1, cy1 = max(0, x0), max(0, y0), min(self.w, x1), min(self.h, y1)
        if cx0 >= cx1 or cy0 >= cy1:
            return None
        return (slice(cy0, cy1), slice(cx0, cx1)), m.m[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0], (cx0, cy0)

    def paint(self, m, col, alpha=1.0):
        if m is None:
            return
        r = self._clip(m)
        if r is None:
            return
        sl, mm, _ = r
        if alpha >= 1.0:
            self.rgb[sl][mm] = col
        else:
            sub = self.rgb[sl]
            v = sub[mm].astype(np.float32)
            sub[mm] = np.clip(v + (np.array(col, np.float32) - v) * alpha, 0, 255).astype(np.uint8)

    def paint_dither(self, m, col, level):
        """col on the pixels of m whose Bayer threshold is under level (0..1): a flat dither of two colours"""
        if m is None:
            return
        r = self._clip(m)
        if r is None:
            return
        sl, mm, _ = r
        sel = mm & (self.bayer[sl] < level)
        self.rgb[sl][sel] = col

    def paint_values(self, m, cols, val, band=0.3):
        """the ramp cols on m by the value val (0..1, an array of the box of m or a function of x, y): flat steps,
        and a short Bayer dither at each step (the width band of a step)"""
        if m is None:
            return
        r = self._clip(m)
        if r is None:
            return
        sl, mm, (cx0, cy0) = r
        if callable(val):
            ys, xs = np.mgrid[sl[0], sl[1]]
            v = val(xs.astype(np.float32) + 0.5, ys.astype(np.float32) + 0.5)
        else:
            v = val[cy0 - m.y0:cy0 - m.y0 + mm.shape[0], cx0 - m.x0:cx0 - m.x0 + mm.shape[1]]
        n = len(cols)
        f = np.clip(v, 0, 0.9999) * (n - 1)
        i0 = np.floor(f).astype(int)
        fr = f - i0
        t = np.clip((fr - (1 - band)) / max(1e-6, band), 0, 1)
        idx = np.where(self.bayer[sl] < t, np.minimum(i0 + 1, n - 1), i0)
        pal = np.array(cols, np.uint8)
        sub = self.rgb[sl]
        sub[mm] = pal[idx[mm]]

    def fill_rect(self, x, y, w, h, col):
        self.paint(m_rect(x, y, w, h), col)

    def fill_poly(self, pts, col):
        self.paint(m_poly(pts), col)

    def fill_ellipse(self, cx, cy, rx, ry, col):
        self.paint(m_ellipse(cx, cy, rx, ry), col)

    def line(self, x0, y0, x1, y1, col, w=1.0):
        self.paint(m_line(x0, y0, x1, y1, w), col)

    def polyline(self, pts, col, w=1.0, w1=None):
        self.paint(m_polyline(pts, w, w1), col)

    def px(self, x, y, col):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.rgb[y, x] = col

    def get(self, x, y):
        x, y = int(clamp(x, 0, self.w - 1)), int(clamp(y, 0, self.h - 1))
        return tuple(int(v) for v in self.rgb[y, x])

    def vgrad(self, m, cols, ya, yb, band=0.35, power=1.0):
        """a vertical ramp from cols[0] at ya to cols[-1] at yb, in flat bands with dither steps"""
        def val(x, y):
            return np.clip((y - ya) / max(1e-6, yb - ya), 0, 1) ** power
        self.paint_values(m, cols, val, band)

    def outline(self, m, col, grow=1):
        """col on the pixels round m (out of it)"""
        if m is None:
            return
        g = m.grow(grow) - m
        self.paint(g, col)

    def darken(self, m, k=0.7, tint=None):
        """make the pixels of m darker (a cast shadow: the colours times k, toward tint)"""
        if m is None:
            return
        r = self._clip(m)
        if r is None:
            return
        sl, mm, _ = r
        sub = self.rgb[sl]
        v = sub[mm].astype(np.float32)
        if tint is not None:
            t = np.array(tint, np.float32)
            v = v * k + t * (1 - k) * 0.55 * (v / 255.0 + 0.4)
        else:
            v = v * k
        sub[mm] = np.clip(v, 0, 255).astype(np.uint8)

    def tint(self, m, col, a):
        self.paint(m, col, a)

    def shade_sphere(self, m, cx, cy, rx, ry, cols, L, amb=0.18, band=0.35, bias=0.0):
        """a round shape lit as a half ball: the value from the normal and the light L (x right, y down, z to the eye)"""
        def val(x, y):
            nx = (x - cx) / max(0.5, rx)
            ny = (y - cy) / max(0.5, ry)
            nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
            d = nx * L[0] + ny * L[1] + nz * L[2]
            return np.clip(amb + (1 - amb) * np.clip(d, 0, 1) + bias, 0, 1)
        self.paint_values(m, cols, val, band)

    def shade_dir(self, m, cols, lx, ly, x0, y0, x1, y1, band=0.35):
        """a flat ramp along a direction (a cylinder or a slope): the value from the projection of (x, y) on the box"""
        def val(x, y):
            u = (x - x0) / max(1e-6, x1 - x0)
            v = (y - y0) / max(1e-6, y1 - y0)
            return np.clip(u * lx + v * ly + (0.5 - 0.5 * (lx + ly)), 0, 1)
        self.paint_values(m, cols, val, band)

    def speckle(self, m, cols, density, seed=1, size=1):
        """dots of the colours cols on about density of the pixels of m"""
        if m is None:
            return
        xs, ys = m.coords()
        if len(xs) == 0:
            return
        rng = np.random.default_rng(seed)
        sel = rng.random(len(xs)) < density
        xs, ys = xs[sel], ys[sel]
        ci = rng.integers(0, len(cols), len(xs))
        for x, y, c in zip(xs, ys, ci):
            if size == 1:
                self.px(x, y, cols[c])
            else:
                self.fill_rect(x, y, size, size, cols[c])

    def rings(self, cx, cy, r, col, levels=3, k=0.17, sy=1.0, mask=None):
        """the halo of the sun: rings of a light colour, mixed in, with dither steps (the look page)"""
        x0, y0 = int(max(0, cx - r * levels - 2)), int(max(0, cy - r * levels / sy - 2))
        x1, y1 = int(min(self.w, cx + r * levels + 3)), int(min(self.h, cy + r * levels / sy + 3))
        if x0 >= x1 or y0 >= y1:
            return
        ys, xs = np.mgrid[y0:y1, x0:x1]
        dd = np.sqrt((xs + 0.5 - cx) ** 2 + ((ys + 0.5 - cy) * sy) ** 2)
        rv = np.clip(levels + 0.4 - dd / r, 0, levels)
        rb = np.floor(rv)
        rt = np.clip((rv - rb - 0.7) / 0.3, 0, 1)
        ring = rb + (self.bayer[y0:y1, x0:x1] < rt)
        sub = self.rgb[y0:y1, x0:x1].astype(np.float32)
        a = (ring * k)[..., None]
        if mask is not None:
            mm = np.zeros((y1 - y0, x1 - x0), bool)
            r2 = self._clip(mask)
            mfull = np.zeros((self.h, self.w), bool)
            if r2:
                sl, mk, _ = r2
                mfull[sl] = mk
            mm = mfull[y0:y1, x0:x1]
            a = a * mm[..., None]
        out = sub + (np.array(col, np.float32) - sub) * a
        self.rgb[y0:y1, x0:x1] = np.clip(out, 0, 255).astype(np.uint8)

    def glow(self, cx, cy, r, col, a=0.5, sy=1.0, levels=3):
        self.rings(cx, cy, r / levels, col, levels, a / levels, sy)

    def snow_cap(self, m, depth, snow, under=None, rough=0):
        """snow on the top of each column of m: depth px of the ramp snow (light at the top); under = the soft
        blue line under the snow (or None)"""
        if m is None:
            return
        t = m.top(depth)
        if rough:
            rng = np.random.default_rng(m.x0 * 31 + m.y0)
            # (a ragged lower edge: some columns get one px less or more)
            ys, xs = np.nonzero(t.m)
            keep = np.ones(len(xs), bool)
            colrnd = rng.integers(-rough, rough + 1, t.m.shape[1])
            depth_at = np.cumsum(t.m, axis=0)
            dd = depth_at[ys, xs]
            keep = dd <= depth + colrnd[xs]
            mm = np.zeros_like(t.m)
            mm[ys[keep], xs[keep]] = True
            t = Mask(t.x0, t.y0, mm)
        top1 = m.top(1)
        self.paint(t, snow[min(1, len(snow) - 1)])
        self.paint(top1, snow[-1])
        if under is not None:
            b = (t.shift(0, 1) & m) - t
            self.paint(b, under)

    def blit(self, other, x, y, key=None):
        """the picture other (a Canvas) at (x, y); key = a colour that is not copied"""
        h, w = other.h, other.w
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(self.w, x + w), min(self.h, y + h)
        if x0 >= x1 or y0 >= y1:
            return
        src = other.rgb[y0 - y:y1 - y, x0 - x:x1 - x]
        if key is None:
            self.rgb[y0:y1, x0:x1] = src
        else:
            sel = ~np.all(src == np.array(key, np.uint8), axis=2)
            self.rgb[y0:y1, x0:x1][sel] = src[sel]

    def save_png(self, path, scale=1):
        from PIL import Image
        im = Image.fromarray(self.rgb, 'RGB')
        if scale != 1:
            im = im.resize((self.w * scale, self.h * scale), Image.NEAREST)
        im.save(path)


# ---------------------------------------------------------------- common things of the art
def cast_shadow(c, m, dx, dy_scale, k=0.72, tint=None):
    """the shadow of a standing shape m on the ground: its pixels moved and flattened (a shear to the side)"""
    if m is None:
        return
    xs, ys = m.coords()
    if len(xs) == 0:
        return
    base = ys.max()
    hgt = base - ys
    sx = (xs + hgt * dx).astype(int)
    sy = (base + hgt * dy_scale).astype(int)
    x0, y0 = sx.min(), sy.min()
    a = np.zeros((sy.max() - y0 + 1, sx.max() - x0 + 1), bool)
    a[sy - y0, sx - x0] = True
    sm = Mask(x0, y0, a).grow(1)
    c.darken(sm, k, tint)
