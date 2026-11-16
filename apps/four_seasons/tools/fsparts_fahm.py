# FOUR SEASONS: the parts of the scenes FARM, ATTIC, MAILBOX and HIVE (bigger parts than fsparts.py: the close views
# show boards with their grain, stones, tin, tall grass, bees, icicles). All take the palette p of the season (fspal.pal).
import math
import numpy as np
from fsart import (Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline,
                   m_union, bezier)
from fspal import SPRING, SUMMER, AUTUMN, WINTER, shade, ramp_of

WHITE = (255, 255, 255)


def light_of(p, n):
    """the light (0..1) on a flat face with the normal n (x right, y down, z to the eye)"""
    L = p.L
    d = n[0] * L[0] + n[1] * L[1] + n[2] * L[2]
    return clamp(0.18 + 0.82 * d, 0.0, 1.0)


def face(p, ramp, n, bias=0.0):
    """the colour of the ramp for a flat face with the normal n"""
    v = clamp(light_of(p, n) + bias, 0.0, 0.999)
    return ramp[int(v * len(ramp))]


def paint_idx(c, m, cols, fn):
    """the colours cols on the pixels of m by an index: fn(x, y, bayer) gives an int array (-1 = no paint)"""
    if m is None:
        return
    r = c._clip(m)
    if r is None:
        return
    sl, mm, _ = r
    ys, xs = np.mgrid[sl[0], sl[1]]
    idx = np.asarray(fn(xs.astype(np.float32) + 0.5, ys.astype(np.float32) + 0.5, c.bayer[sl]))
    if idx.shape != mm.shape:
        idx = np.broadcast_to(idx, mm.shape)
    pal = np.array(cols, np.uint8)
    sel = mm & (idx >= 0)
    sub = c.rgb[sl]
    sub[sel] = pal[np.clip(idx, 0, len(cols) - 1)[sel]]


def patches(c, m, col, scale, thr, seed, dither=0.0):
    """col on the parts of m where a smooth noise is over thr (worn plaster, damp, moss); dither > 0 gives a short
    Bayer step at the edge of each patch"""
    from fsart import fbm2
    if m is None or m.empty():
        return
    n = fbm2(m.w, m.h, scale, seed, 2)
    hard = Mask(m.x0, m.y0, m.m & (n > thr))
    c.paint(hard, col)
    if dither > 0:
        soft = Mask(m.x0, m.y0, m.m & (n > thr - dither) & (n <= thr))
        c.paint_dither(soft, col, 0.4)


def uv_of(x, y, O, A, B):
    """the place (u, v) of the points x, y in the parallelogram O + u (A - O) + v (B - O)"""
    ax, ay = A[0] - O[0], A[1] - O[1]
    bx, by = B[0] - O[0], B[1] - O[1]
    det = ax * by - ay * bx
    dx, dy = x - O[0], y - O[1]
    u = (dx * by - dy * bx) / det
    v = (ax * dy - ay * dx) / det
    return u, v


def para(O, A, B):
    """the polygon of the parallelogram O, A, A + B - O, B"""
    return m_poly([O, A, (A[0] + B[0] - O[0], A[1] + B[1] - O[1]), B])


# ---------------------------------------------------------------- wood
def wood_ramp(p, base=None, s=None):
    """a ramp of 5 tones of a wood (dark gap, grain, board, light, edge)"""
    base = p.wood if base is None else base
    return ramp_of(base, p.s, 5, -0.85, 0.5)


def boards(c, p, m, O, A, B, n, cols, seed=1, knots=2, nails=True, gap=True, grain=1.0, edge_lit=1, tones=True):
    """boards on the mask m: the boards go along A - O (u), n boards side by side along B - O (v); cols = 5 tones
    (gap, grain, board, light, edge). Each board has its own tone, a grain that bends round its knots, a dark gap
    and a lit edge (edge_lit = +1 the lit edge on the high v side, -1 on the low side)"""
    rng = RNG(seed)
    tone = [rng.pick([0.0, 0.0, 0.25, 0.5]) if tones else 0.0 for _ in range(n)]
    ph = [rng.uni(0, 6.28) for _ in range(n)]
    kn = []
    for i in range(n):
        for k in range(knots if rng() < 0.7 else 0):
            kn.append((i, rng.uni(0.1, 0.9), rng.uni(0.3, 0.7)))
    Lu = math.hypot(A[0] - O[0], A[1] - O[1])
    Lv = math.hypot(B[0] - O[0], B[1] - O[1]) / n

    def fn(x, y, bay):
        u, v = uv_of(x, y, O, A, B)
        vv = v * n
        i = np.clip(np.floor(vv).astype(int), 0, n - 1)
        f = vv - np.floor(vv)
        out = np.full(x.shape, 2, int)
        t = np.array(tone)[i]
        out = np.where(bay < t, 3, out)
        # the grain: wavy lines along the board
        uu = u * Lu
        phase = np.array(ph)[i]
        w = f * Lv + 1.3 * np.sin(uu * 0.05 + phase) + 0.7 * np.sin(uu * 0.17 + phase * 2)
        for (ki, ku, kv) in kn:
            du = (u - ku) * Lu
            dv = (f - kv) * Lv
            bump = np.exp(-(du * du) / 40.0 - (dv * dv) / 6.0) * (i == ki)
            w = w + bump * 3.0
            ring = (du * du / 9.0 + dv * dv / 2.2) < 1.0
            out = np.where(ring & (i == ki), 1, out)
            out = np.where(((du * du / 2.5 + dv * dv / 0.8) < 1.0) & (i == ki), 0, out)
        g = np.sin(w * 2.1)
        # (the grain lines are solid dashes: a slow wave along the board breaks them)
        gr = (g > 0.9) & (np.sin(uu * 0.09 + phase * 3 + f * 5) + np.sin(uu * 0.23 + phase) * 0.5 > 1.0 - 1.6 * grain)
        out = np.where(gr & (out >= 2), 1, out)
        if gap:
            gpx = 1.0 / max(1.0, Lv)
            if edge_lit > 0:
                out = np.where(f < gpx, 0, out)
                out = np.where((f > 1 - gpx) & (out >= 2), 4, out)
            else:
                out = np.where(f > 1 - gpx, 0, out)
                out = np.where((f < gpx) & (out >= 2), 4, out)
        return out
    paint_idx(c, m, cols, fn)
    if nails:
        for i in range(n):
            for uu in (0.06, 0.94):
                vv = (i + 0.5) / n
                x = O[0] + (A[0] - O[0]) * uu + (B[0] - O[0]) * vv
                y = O[1] + (A[1] - O[1]) * uu + (B[1] - O[1]) * vv
                c.px(x, y, mul(cols[0], 0.8))
                c.px(x - 1, y - 1, cols[4])


# ---------------------------------------------------------------- stone
def stones(c, p, m, cols, rh=5, rw=(7, 14), seed=1, mortar=None, lit=True):
    """a wall of stones (rows of the height rh, blocks of random width): each stone a flat tone, lit at its top
    edge, dark at its bottom edge; mortar between them"""
    rng = RNG(seed)
    mortar = cols[0] if mortar is None else mortar
    x0, y0, x1, y1 = m.bbox()
    c.paint(m, mortar)
    y = y0
    row = 0
    while y < y1:
        x = x0 - rng.uni(0, rw[1])
        h = rh + (rng.int(-1, 1) if rh > 4 else 0)
        while x < x1:
            w = rng.uni(*rw)
            sm = m_rect(x + 1, y + 1, w - 1, h - 1) & m
            if not sm.empty():
                t = rng.pick([1, 2, 2, 3])
                c.paint(sm, cols[t])
                if lit:
                    c.paint(sm.top(1), cols[min(len(cols) - 1, t + 1)])
                    c.paint(sm.bottom(1), cols[max(1, t - 1)])
            x += w
        y += h
        row += 1


# ---------------------------------------------------------------- the plants of a close view
def tall_grass(c, p, x0, x1, ybase, n, hmin, hmax, seed, lean=0.0, cols=None, mask=None):
    """long blades of grass that bend (a close view): in winter dry stalks that come out of the snow"""
    rng = RNG(seed)
    if cols is None:
        if p.s == WINTER:
            cols = [hx('#8a7a5e'), hx('#a8987a'), hx('#c8b898')]
        elif p.s == AUTUMN:
            cols = [hx('#7a6a2e'), hx('#b0943c'), hx('#dcbf5b'), hx('#f4d67e')]
        else:
            cols = [p.grass, p.hillS, p.hill, p.hillL]
    for i in range(n):
        x = rng.uni(x0, x1)
        yb = ybase(x) if callable(ybase) else ybase
        h = rng.uni(hmin, hmax)
        bend = (rng.uni(-0.5, 0.5) + lean) * h * 0.5
        pts = bezier((x, yb), (x + bend * 0.2, yb - h * 0.6), (x + bend, yb - h), None, 6)
        col = cols[int(rng() * len(cols))]
        bm = m_polyline(pts, 1.6 if h > 10 else 1.0, 1.0)
        if mask is not None:
            bm = bm & mask
        c.paint(bm, col)


def close_flowers(c, p, pts, seed, size=1.0):
    """flowers of a close view at the points pts: daisies and dandelions in spring, poppies and clover in summer,
    asters and seed heads in autumn; none in winter"""
    rng = RNG(seed)
    for (x, y) in pts:
        k = rng()
        if p.s == WINTER:
            continue
        stem = mix(p.grass, p.hillS, 0.5)
        h = rng.uni(5, 12) * size
        c.paint(m_line(x, y, x + rng.uni(-1.5, 1.5), y - h, 1), stem)
        fx, fy = x, y - h
        if p.s == SPRING:
            if k < 0.55:          # a daisy
                for a in range(6):
                    c.paint(m_disc(fx + math.cos(a * 1.05) * 2.2 * size, fy + math.sin(a * 1.05) * 1.6 * size, 1.1 * size), WHITE)
                c.paint(m_disc(fx, fy, 1.2 * size), hx('#ffd23f'))
            elif k < 0.85:        # a dandelion
                c.paint(m_ellipse(fx, fy, 2.4 * size, 1.8 * size), hx('#ffcf2a'))
                c.paint(m_ellipse(fx - 0.6, fy - 0.8, 1.2 * size, 0.8 * size), hx('#fff07a'))
            else:                 # a violet
                c.paint(m_disc(fx, fy, 1.6 * size), hx('#9a72e0'))
                c.px(fx, fy, hx('#fff07a'))
        elif p.s == SUMMER:
            if k < 0.45:          # a poppy
                c.paint(m_ellipse(fx, fy, 2.8 * size, 2.2 * size), hx('#e8342a'))
                c.paint(m_ellipse(fx - 0.8, fy - 0.8, 1.4 * size, 1.0 * size), hx('#ff6a4a'))
                c.px(fx, fy, hx('#2a1a1a'))
            elif k < 0.8:         # clover
                c.paint(m_ellipse(fx, fy, 2.0 * size, 2.4 * size), hx('#e07ab0'))
                c.paint(m_ellipse(fx - 0.5, fy - 1, 1.0 * size, 1.0 * size), hx('#ffb0d8'))
            else:                 # a daisy
                for a in range(6):
                    c.paint(m_disc(fx + math.cos(a * 1.05) * 2.2 * size, fy + math.sin(a * 1.05) * 1.6 * size, 1.1 * size), WHITE)
                c.paint(m_disc(fx, fy, 1.2 * size), hx('#ffd23f'))
        else:
            if k < 0.5:           # an aster
                c.paint(m_disc(fx, fy, 2.0 * size), hx('#8a5ac8'))
                c.px(fx, fy, hx('#ffc54c'))
            else:                 # a dry seed head
                c.paint(m_disc(fx, fy, 1.6 * size), hx('#c8a878'))
                c.px(fx - 1, fy - 1, hx('#efe0c0'))


def bee(c, x, y, k=0):
    """a bee (5 x 4 px): the body with stripes and the light wings"""
    x, y = int(x), int(y)
    c.paint(m_rect(x, y, 4, 2), hx('#f2b830'))
    c.px(x + 1, y, hx('#2a1e10'))
    c.px(x + 3, y + 1, hx('#2a1e10'))
    c.px(x + 4, y, hx('#2a1e10'))
    c.px(x + 1 + (k % 2), y - 1, hx('#eef6ff'))
    c.px(x + 2, y - 1 - (k % 2), hx('#cfe0f0'))


def icicles(c, p, x0, x1, y, seed, lmax=10, step=3.0, dens=0.7):
    """icicles that hang from the line y between x0 and x1"""
    rng = RNG(seed)
    x = x0
    while x < x1:
        if rng() < dens:
            L = rng.uni(2, lmax)
            w = 1.6 if L > 5 else 1.0
            c.paint(m_poly([(x - w, y), (x + w + 0.6, y), (x + 0.3, y + L)]), hx('#cfe6f6'))
            c.paint(m_line(x, y, x, y + L * 0.7, 1), WHITE)
            c.px(x + 1, y + L * 0.5, hx('#9cc6dc'))
        x += rng.uni(step * 0.6, step * 1.5)


def snow_on(c, p, m, depth, rough=1, under=True):
    """a layer of snow on the top of the mask m (lit at the top, a soft blue line under it)"""
    c.snow_cap(m, depth, p.snow, under=p.snow[0] if under else None, rough=rough)


def snow_slab(c, p, x0, x1, y, h, seed=1, droop=1.5):
    """a soft slab of snow that lies on a flat top from x0 to x1 at y (its round ends hang a little)"""
    rng = RNG(seed)
    pts = [(x0 - droop, y + 1)]
    n = max(3, int((x1 - x0) / 6))
    for i in range(n + 1):
        f = i / n
        pts.append((lerp(x0, x1, f), y - h * (0.75 + 0.25 * math.sin(f * math.pi)) - rng.uni(0, h * 0.25)))
    pts.append((x1 + droop, y + 1))
    m = m_poly(pts)
    c.paint(m, p.snow[2])
    c.paint(m.top(1), WHITE)
    c.paint(m.bottom(1), p.snow[1])
    return m


def smoke(c, p, x, y, n, seed, drift=1.0, rise=14):
    """the puffs of smoke of a chimney (they rise and go with the wind, light at the top)"""
    rng = RNG(seed)
    dark = mix(p.clS, (90, 90, 100), 0.3)
    body = mix(p.clS, WHITE, 0.45)
    light = mix(p.cl, WHITE, 0.6)
    puffs = []
    for i in range(n):
        f = i / max(1, n - 1)
        r = 2.6 + f * 5.5
        px = x + drift * (f * 40 + math.sin(f * 5) * 3)
        py = y - f * rise - 2 + rng.uni(-1, 1)
        puffs.append((px, py, r))
    for (px, py, r) in puffs:
        c.paint(m_ellipse(px + 1, py + 1.5, r * 1.2, r * 0.85), dark)
    for (px, py, r) in puffs:
        c.paint(m_ellipse(px, py, r * 1.2, r * 0.85), body)
    for (px, py, r) in puffs:
        c.paint(m_ellipse(px - r * 0.3, py - r * 0.35, r * 0.6, r * 0.4), light)


def close_sky(c, p, y1, sunx, suny, seed, nclouds=4):
    """the sky of a close view: the bands, the sun of the season with its rings, a few clouds"""
    import fsparts as P
    P.sky(c, p, 0, y1, (sunx, suny), sun_r=11)
    P.clouds(c, p, 10, max(14, y1 * 0.45), nclouds, seed)
