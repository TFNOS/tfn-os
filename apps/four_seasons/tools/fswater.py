# FOUR SEASONS: the parts of the water scenes (POND, SHORE, ISLAND, WELL): open water with ripples and the glitter of
# the sun, ice with cracks and snow, reflections, reeds, lily pads, things that float, the weeping willow of the
# island, logs, the iron crank handle, dry cracked mud. All take the palette p of the season (fspal.pal).
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier)
from fspal import SPRING, SUMMER, AUTUMN, WINTER, shade, ramp_of

WHITE = (255, 255, 255)


# ---------------------------------------------------------------- masks of the full canvas
def full(c, m):
    """the pixels of the mask m as a bool array of the size of the canvas"""
    a = np.zeros((c.h, c.w), bool)
    if m is None:
        return a
    r = c._clip(m)
    if r:
        sl, mm, _ = r
        a[sl] = mm
    return a


def as_mask(a):
    return Mask(0, 0, a)


def changed(c, before):
    """the pixels that a drawing changed (before = a copy of c.rgb)"""
    return np.any(c.rgb != before, axis=2)


# ---------------------------------------------------------------- water and ice
def water_cols(p):
    return [mix(p.skyB, p.water, 0.45), mix(p.skyB, p.water, 0.75), p.water, mix(p.water, p.waterD, 0.5), p.waterD]


def water(c, p, m, ytop, ybot, seed, sun_x=None, n=120, glint=True):
    """open water on m: lighter far (the sky in it), darker near; short ripple marks that grow to the front, and the
    glitter column of the sun"""
    c.vgrad(m, water_cols(p), ytop, ybot, 0.3)
    rng = RNG(seed)
    xs, ys = m.coords()
    if len(xs) == 0:
        return
    lite = mix(p.water, WHITE, 0.45)
    dark = mul(p.waterD, 0.86)
    for i in range(n):
        k = int(rng() * len(xs))
        x, y = xs[k], ys[k]
        f = clamp((y - ytop) / max(1, ybot - ytop), 0, 1)
        ln = 2 + f * 12 * rng.uni(0.5, 1.2)
        col = lite if rng() < 0.55 else dark
        c.paint(m_rect(x, y, ln, 1) & m, col)
        if f > 0.5 and rng() < 0.4:
            c.paint(m_rect(x + ln * 0.3, y + 1, ln * 0.5, 1) & m, mix(col, p.water, 0.4))
    if sun_x is not None and glint:
        for i in range(int(n * 0.5)):
            y = rng.uni(ytop + 1, ybot)
            f = clamp((y - ytop) / max(1, ybot - ytop), 0, 1)
            x = sun_x + rng.uni(-1, 1) * (6 + f * 22) * rng()
            ln = 1 + f * 6 * rng()
            c.paint(m_rect(x, y, ln, 1) & m, p.sunG if rng() < 0.5 else mix(p.sun, WHITE, 0.5))


def ice(c, p, m, ytop, ybot, seed, n_crack=8, n_snow=10):
    """the ice of winter on m: a light ramp, cracks, snow drifts, scratches of light"""
    c.paint_values(m, p.ice, lambda x, y: np.clip(1 - (y - ytop) / max(1, ybot - ytop) * 0.85, 0, 1), 0.3)
    rng = RNG(seed)
    xs, ys = m.coords()
    if len(xs) == 0:
        return
    for i in range(n_snow):
        k = int(rng() * len(xs))
        x, y = xs[k], ys[k]
        f = clamp((y - ytop) / max(1, ybot - ytop), 0, 1)
        rx = (8 + 26 * f) * rng.uni(0.5, 1.2)
        ry = rx * 0.16
        sm = m_ellipse(x, y, rx, ry) & m
        c.paint(sm, p.snow[2])
        c.paint(sm.top(1), WHITE)
        c.paint(sm.bottom(1), p.snow[1])
    for i in range(n_crack):
        k = int(rng() * len(xs))
        x, y = xs[k], ys[k]
        f = clamp((y - ytop) / max(1, ybot - ytop), 0, 1)
        pts = [(x, y)]
        a = rng.uni(-0.4, 0.4)
        for j in range(5):
            a += rng.uni(-0.7, 0.7)
            stp = (4 + 10 * f) * rng.uni(0.6, 1.3)
            pts.append((pts[-1][0] + math.cos(a) * stp, pts[-1][1] + math.sin(a) * stp * 0.35))
        cm = m_polyline(pts, 1) & m
        c.paint(cm, p.ice[0])
        c.paint(cm.shift(0, 1) & m, WHITE)
        c.paint(cm, p.ice[0])
    for i in range(n_crack * 2):
        k = int(rng() * len(xs))
        x, y = xs[k], ys[k]
        c.paint(m_rect(x, y, 2 + rng() * 6, 1) & m, WHITE)


def bank_band(c, p, m_water, y_edge_fn, x0, x1, col, depth=2, seed=1):
    """a band of the colour col on the water just under its far edge (the reflection of the bank), with ripple gaps"""
    rng = RNG(seed)
    for x in range(int(x0), int(x1)):
        y = y_edge_fn(x)
        d = depth + (1 if rng() < 0.4 else 0)
        if rng() < 0.15:
            continue
        c.paint(m_rect(x, y, 1, d) & m_water, col)


def reflect(c, src, yline, water_a, cols, ripple=4, wob=1, hmax=None, x_shift=0):
    """a reflection of the pixels src (a full bool array) in the water (a full bool array) under the line yline:
    the source colours go to the few colours cols by their light (dark, middle, light); a row in each ripple rows is
    out, and the rows move a little left and right"""
    hmax = hmax or c.h
    lum = c.rgb.astype(np.int32).sum(axis=2)
    cols = [np.array(cc, np.uint8) for cc in cols]
    th = [230, 470]
    y0 = int(yline)
    for dy in range(0, hmax):
        yd = y0 + dy
        ys = y0 - 1 - dy
        if yd >= c.h or ys < 0:
            break
        if ripple and dy % ripple == ripple - 1:
            continue
        sh = x_shift + (wob if (dy // 2) % 2 else -wob) * (1 if dy > 2 else 0)
        row = np.roll(src[ys], sh)
        lrow = np.roll(lum[ys], sh)
        sel = row & water_a[yd]
        if not sel.any():
            continue
        idx = np.where(lrow < th[0], 0, np.where(lrow < th[1], 1, 2))
        for k in range(3):
            s2 = sel & (idx == k)
            c.rgb[yd][s2] = cols[min(k, len(cols) - 1)]


# ---------------------------------------------------------------- plants of the water
def reeds(c, p, x, y, n, h, seed, spread=1.0, heads=True, lean=0.0):
    """a clump of cattails: thin leaves and stems with brown heads; young and green in spring, tall in summer, tan in
    autumn, straw with snow in winter"""
    rng = RNG(seed)
    s = p.s
    if s == SPRING:
        cols = [mix(p.lfD, p.lf, 0.3), p.lf, p.lfL]
        hk = 0.62
    elif s == SUMMER:
        cols = [p.lfD, mix(p.lfD, p.lf, 0.6), p.lfL]
        hk = 1.0
    elif s == AUTUMN:
        cols = [hx('#7a6430'), hx('#a8873e'), hx('#d8b860')]
        hk = 0.95
    else:
        cols = [hx('#9a8a70'), hx('#bfae8c'), hx('#e2d6bc')]
        hk = 0.8
    for k in range(n):
        bx = x + rng.uni(-1, 1) * spread * n * 0.9
        hh = h * hk * rng.uni(0.55, 1.05)
        bend = (rng.uni(-0.35, 0.35) + lean) * hh
        col = cols[int(rng() * 3)]
        if s == WINTER and rng() < 0.3:
            # (a broken stem: it bends down)
            pts = [(bx, y), (bx + bend * 0.2, y - hh * 0.6), (bx + bend * 0.2 + hh * 0.25 * (1 if bend > 0 else -1), y - hh * 0.45)]
        else:
            pts = bezier((bx, y), (bx + bend * 0.1, y - hh * 0.5), (bx + bend, y - hh), n=6)
        c.paint(m_polyline(pts, 1), col)
        if heads and rng() < 0.5 and s != SPRING:
            tx, ty = pts[-1]
            hm = m_rect(tx - 1, ty + 1, 2.4, max(3, hh * 0.18))
            hc = hx('#6a4228') if s != WINTER else hx('#7a5a40')
            if s == AUTUMN and rng() < 0.5:
                hc = hx('#8a6a4a')
            c.paint(hm, hc)
            c.px(tx - 1, ty + 1, mix(hc, WHITE, 0.3))
            c.px(tx, ty, col)
            if s == WINTER:
                c.paint(m_rect(tx - 1, ty, 3, 1.4), WHITE)
            if s == AUTUMN and rng() < 0.4:
                # (the head breaks into white fluff)
                for j in range(3):
                    c.px(tx + 1 + rng.uni(0, 3), ty + rng.uni(0, 4), hx('#f4ead8'))
    # the long flat leaves (they arch out)
    for k in range(int(n * 0.7)):
        bx = x + rng.uni(-1, 1) * spread * n * 0.8
        hh = h * hk * rng.uni(0.35, 0.8)
        side = 1 if rng() < 0.5 else -1
        pts = bezier((bx, y), (bx + side * hh * 0.1, y - hh * 0.7), (bx + side * hh * 0.45, y - hh * 0.75), n=6)
        c.paint(m_polyline(pts, 1.6 if hh > 8 else 1), cols[int(rng() * 3)])
        if s == WINTER and rng() < 0.5:
            c.px(pts[3][0], pts[3][1] - 1, WHITE)


def lily_pad(c, p, x, y, r, seed, flower=False):
    """a lily pad (a disc with a notch), seen at a low angle; a flower in summer"""
    rng = RNG(seed)
    s = p.s
    if s == WINTER:
        return
    if s == SPRING:
        cols = [hx('#4f6a2e'), hx('#7a8a3a'), hx('#a8b850')]
    elif s == SUMMER:
        cols = [mul(p.lfD, 0.85), p.lfD, mix(p.lf, p.lfL, 0.4)]
    else:
        cols = [hx('#5a4a22'), hx('#8a7a3a'), hx('#b8a050')]
    m = m_ellipse(x, y, r, r * 0.38)
    a = rng.uni(0, 2 * math.pi)
    notch = m_poly([(x, y), (x + math.cos(a) * r * 1.3, y + math.sin(a) * r * 0.5 - r * 0.1),
                    (x + math.cos(a + 0.5) * r * 1.3, y + math.sin(a + 0.5) * r * 0.5)])
    m = m - notch
    c.paint(m.shift(0, 1), mul(p.waterD, 0.8))
    c.paint(m, cols[1])
    c.paint(m.top(1), cols[2])
    c.paint(m.bottom(1), cols[0])
    c.px(x, y, cols[0])
    if s == AUTUMN:
        c.px(x + r * 0.3, y, hx('#c8502a'))
    if flower and s == SUMMER:
        fc = hx('#ffffff') if seed % 2 else hx('#ffb0c8')
        fm = m_poly([(x - r * 0.45, y - 1), (x - r * 0.2, y - r * 0.55 - 1), (x, y - r * 0.3 - 1), (x + r * 0.2, y - r * 0.55 - 1),
                     (x + r * 0.45, y - 1)])
        c.paint(fm, fc)
        c.paint(fm.bottom(1), mix(fc, hx('#c87aa0'), 0.4))
        c.px(x, y - r * 0.25 - 1, hx('#ffd23f'))
        c.px(x - 1, y - r * 0.2 - 1, hx('#ffd23f'))


def floaters(c, p, m, n, seed, front=1.0):
    """things that float on the water m: blossom petals in spring, small leaves in autumn, seeds in summer"""
    rng = RNG(seed)
    xs, ys = m.coords()
    if len(xs) == 0 or p.s == WINTER:
        return
    for i in range(n):
        k = int(rng() * len(xs))
        x, y = xs[k], ys[k]
        if p.s == SPRING:
            c.paint(m_rect(x, y, 2, 1), p.blossom[2] if rng() < 0.6 else WHITE)
            c.px(x, y + 1, p.blossom[0])
        elif p.s == AUTUMN:
            col = rng.pick([hx('#f2c33a'), hx('#e8682a'), hx('#c9351f'), hx('#a8541f'), hx('#ffb23d')])
            sz = 1 + int(rng() * 2 * front)
            c.paint(m_rect(x, y + 1, 2 + sz, 1), mul(p.waterD, 0.75))
            c.paint(m_rect(x, y, 2 + sz, 1), col)
            if sz > 1:
                c.paint(m_rect(x + 1, y - 1, sz, 1), mix(col, WHITE, 0.25))
        else:
            if rng() < 0.4:
                c.px(x, y, hx('#e8e0b0'))


# ---------------------------------------------------------------- a log
def log(c, p, x0, y0, x1, y1, r, seed, snow=False, end_at=0):
    """a log of the length (x0, y0)-(x1, y1) and the radius r: bark lit from the top, lines in the bark, a stub of a
    branch, a cut end with rings at the end end_at (0 = the first point)"""
    rng = RNG(seed)
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux
    if ny > 0:
        nx, ny = -nx, -ny          # the normal points up (to the light)
    body = m_line(x0, y0, x1, y1, 2 * r)
    # (the shadow under it)
    c.darken(m_line(x0 + 1, y0 + r * 0.8, x1 + 1, y1 + r * 0.8, 2 * r * 0.7), 0.7, p.cool)

    def val(x, y):
        d = (x - x0) * nx + (y - y0) * ny
        return np.clip(0.5 + d / (2 * r) * 1.0 + 0.1, 0, 1)
    cols = [p.bark[0], p.bark[1], p.bark[2], p.bark[3]]
    c.paint_values(body, cols, val, 0.35)
    # lines in the bark (along the log)
    for k in range(int(L / 4)):
        t = rng.uni(0.05, 0.95)
        o = rng.uni(-0.7, 0.7) * r
        ln = rng.uni(4, 12)
        ax, ay = x0 + ux * L * t + nx * o, y0 + uy * L * t + ny * o
        c.paint(m_line(ax, ay, ax + ux * ln, ay + uy * ln, 1) & body, p.bark[0] if o < 0.2 * r else p.bark[1])
    for k in range(int(L / 10)):
        t = rng.uni(0.05, 0.95)
        ax, ay = x0 + ux * L * t + nx * r * 0.55, y0 + uy * L * t + ny * r * 0.55
        c.paint(m_line(ax, ay, ax + ux * 3, ay + uy * 3, 1) & body, mix(p.bark[3], p.warm, 0.3))
    # a stub of a branch
    t = 0.62
    sx, sy = x0 + ux * L * t + nx * r * 0.6, y0 + uy * L * t + ny * r * 0.6
    c.paint(m_line(sx, sy, sx + nx * r * 0.9 + ux * 3, sy + ny * r * 0.9 + uy * 3, max(2, r * 0.5)), p.bark[2])
    c.px(sx + nx * r * 0.9 + ux * 3, sy + ny * r * 0.9 + uy * 3, p.bark[3])
    # the cut end with its rings
    ex, ey = (x0, y0) if end_at == 0 else (x1, y1)
    ang = math.atan2(uy, ux)
    em = m_ellipse(ex, ey, r * 0.5, r, rot=ang)
    c.paint(em, hx('#d8b07a') if p.s != WINTER else hx('#c8b49a'))
    c.paint(m_ellipse(ex, ey, r * 0.32, r * 0.64, rot=ang) - m_ellipse(ex, ey, r * 0.22, r * 0.44, rot=ang), hx('#a87a4a'))
    c.paint(m_ellipse(ex, ey, r * 0.08 + 0.6, r * 0.15 + 0.6, rot=ang), hx('#8a5a32'))
    c.outline(em, p.bark[0])
    if snow:
        top = body.top(max(2, int(r * 0.45)))
        c.paint(top, p.snow[2])
        c.paint(body.top(1), WHITE)
    return body


# ---------------------------------------------------------------- the iron crank handle (the well, the sluice)
def crank(c, p, x, y, ang, L, sc=1.0, lit=1.0, under=0.0, grip_under=None):
    """the iron crank handle: a square boss with a square hole at (x, y), a flat iron arm of the length L at the angle
    ang, and a round wooden grip at its end (at a right angle, seen from above). lit: the light of the place (0..1);
    under: how deep it is under water (0 = dry, 1 = deep: the colours go to the water); grip_under: the same for the
    wooden grip (it floats), None = as under"""
    iron = [mul(p.iron[0], 0.7 + 0.3 * lit), p.iron[1], p.iron[2], p.iron[3]]
    rust = [hx('#5a2e1a'), hx('#8a4a24'), hx('#b8642e')]
    wood = [hx('#7a5230'), hx('#b0804a'), hx('#dcb07a'), hx('#f0d0a0')]
    if under > 0:
        wc = mix(p.waterD, p.water, 0.3)
        iron = [mix(cc, wc, under) for cc in iron]
        rust = [mix(cc, wc, under) for cc in rust]
    ex, ey = x + math.cos(ang) * L, y + math.sin(ang) * L
    w = 4.2 * sc
    arm = m_line(x, y, ex, ey, w)
    c.paint(arm.shift(1, 2), mul(p.cool, 0.6) if under == 0 else mix(p.waterD, p.cool, 0.4))
    c.paint(arm, iron[1])
    c.paint(arm.side(0, -1), iron[3])
    c.paint(arm.side(0, 1), iron[0])
    # rust spots
    for k in range(int(L / 6)):
        t = 0.15 + 0.7 * k / max(1, int(L / 6))
        c.paint(m_rect(x + math.cos(ang) * L * t, y + math.sin(ang) * L * t - 0.5, 2, 1.2), rust[k % 3])
    # the boss
    bs = 5 * sc
    bm = m_rect(x - bs, y - bs, bs * 2, bs * 2)
    c.paint(bm, iron[2])
    c.paint(bm.side(0, -1) | bm.side(-1, 0), iron[3])
    c.paint(bm.side(0, 1) | bm.side(1, 0), iron[0])
    c.paint(m_rect(x - bs * 0.45, y - bs * 0.45, bs * 0.9 + 0.5, bs * 0.9 + 0.5), mul(iron[0], 0.6))
    c.px(x - bs + 1, y - bs + 1, rust[2])
    # the grip: a short wooden cylinder at the end (seen at an angle)
    gl = 11 * sc
    gx, gy = ex + math.cos(ang + 1.2) * 1.0, ey
    gm = m_line(gx, gy, gx + math.cos(ang - math.pi / 2) * gl * 0.35, gy - gl * 0.9, 5.6 * sc)
    wd = wood
    gu = under if grip_under is None else grip_under
    if gu > 0:
        wd = [mix(cc, mix(p.waterD, p.water, 0.3), gu * 0.6) for cc in wood]
    c.paint(gm.shift(1, 1), mul(wd[0], 0.7))
    c.paint(gm, wd[1])
    c.paint(gm.side(-1, 0), wd[2])
    c.paint(gm.side(0, -1) & gm.side(-1, 0), wd[3])
    c.paint(gm.side(1, 0), wd[0])
    c.paint(m_rect(gx - 2 * sc, gy - 1.5 * sc, 4 * sc, 2 * sc), iron[1])
    return arm | bm | gm


# ---------------------------------------------------------------- dry cracked mud
def cracked_mud(c, p, m, seed, ytop, ybot, cell=(7, 16)):
    """dry mud on m: light clay with a net of cracks; the cells grow to the front"""
    cols = [mix(p.mud[2], p.path, 0.45), mix(p.mud[3], p.path, 0.6), mix(p.mud[3], WHITE, 0.25)]
    c.paint_values(m, cols, lambda x, y: np.clip(0.2 + 0.5 * (1 - (y - ytop) / max(1, ybot - ytop)), 0, 1), 0.35)
    r = c._clip(m)
    if r is None:
        return
    sl, mm, (cx0, cy0) = r
    h, w = mm.shape
    rng = RNG(seed)
    pts = []
    y = ytop - 4
    while y < ybot + 8:
        f = clamp((y - ytop) / max(1, ybot - ytop), 0, 1)
        cs = lerp(cell[0], cell[1], f)
        x = cx0 - cs + rng.uni(0, cs)
        while x < cx0 + w + cs:
            pts.append((x + rng.uni(-0.35, 0.35) * cs, y + rng.uni(-0.3, 0.3) * cs * 0.5))
            x += cs
        y += cs * 0.5
    P = np.array(pts, np.float32)
    ys, xs = np.mgrid[cy0:cy0 + h, cx0:cx0 + w]
    X = xs.astype(np.float32) + 0.5
    Y = ys.astype(np.float32) + 0.5
    d1 = np.full((h, w), 1e9, np.float32)
    d2 = np.full((h, w), 1e9, np.float32)
    for (px, py) in P:
        d = np.sqrt((X - px) ** 2 + ((Y - py) * 2.0) ** 2)
        nd2 = np.where(d < d1, d1, np.minimum(d2, d))
        d1 = np.minimum(d1, d)
        d2 = nd2
    crack = (d2 - d1 < 1.25) & mm
    lip = (d2 - d1 >= 1.25) & (d2 - d1 < 2.3) & mm
    sub = c.rgb[sl]
    lipm = lip & np.roll(crack, 1, axis=0)
    sub[crack] = mul(p.mud[0], 0.9)
    sub[lipm] = cols[2]


# ---------------------------------------------------------------- the weeping willow
class Willow:
    """a weeping willow: a gnarled trunk, main branches that arch up and out, and a curtain of long strands that hang
    down. Spring: fresh yellow-green with catkins; summer: deep green, silver light; autumn: the leaves fell (bare
    golden twigs, a few yellow leaves); winter: bare golden twigs, snow on the branches."""

    def __init__(self, x, y, ht, wd, seed, sc=1.0, lean=0.0, nstrand=None, bottom=None):
        rng = RNG(seed)
        self.rng_seed = seed
        self.x, self.y, self.ht, self.wd, self.sc = x, y, ht, wd, sc
        self.fork = (x + lean * ht * 0.3, y - ht * 0.36)
        fx, fy = self.fork
        self.cx, self.cy = x + lean * ht * 0.4, y - ht * 0.66
        self.rx, self.ry = wd, ht * 0.36
        self.tw = 7.5 * sc
        self.branches = []           # (points, w0, w1)
        nb = 6
        for k in range(nb):
            a = math.pi * (1.06 + 0.88 * k / (nb - 1)) + rng.uni(-0.07, 0.07)
            ex = self.cx + math.cos(a) * self.rx * rng.uni(0.78, 0.95)
            ey = self.cy + math.sin(a) * self.ry * rng.uni(0.62, 0.8)
            sx = fx + rng.uni(-0.25, 0.25) * self.tw
            p1 = (sx + (ex - sx) * 0.12, fy - (fy - ey) * 0.75)
            p2 = (ex - (ex - sx) * 0.25, ey - self.ry * 0.3)
            pts = bezier((sx, fy + 2 * sc), p1, p2, (ex, ey), n=18)
            self.branches.append((pts, self.tw * rng.uni(0.6, 0.85), 1.4 * sc))
            # a side branch from its middle, outward
            j = 9 + int(rng() * 4)
            bx, by = pts[j]
            side = 1 if ex > fx else -1
            ex2 = bx + side * self.rx * rng.uni(0.3, 0.45)
            ey2 = by + rng.uni(-0.1, 0.25) * self.ry
            pts2 = bezier((bx, by), (bx + side * self.rx * 0.12, by - self.ry * 0.35), (ex2, ey2), n=10)
            self.branches.append((pts2, self.tw * 0.32, 1.0 * sc))
        # the strands: from points on the arch of the crown and on the outer parts of the branches
        ns = nstrand or int(wd * 1.1)
        self.strands = []
        bot = (y - ht * 0.06) if bottom is None else bottom
        for i in range(ns):
            u = (i + rng.uni(0.1, 0.9)) / ns * 2 - 1        # -1 .. 1 across the crown
            ax = self.cx + u * self.rx * 0.98
            top = self.cy - self.ry * math.sqrt(max(0.0, 1 - u * u)) + rng.uni(0, 0.25) * self.ry
            yb = bot + rng.uni(-0.12, 0.05) * ht - (abs(u) ** 3) * ht * 0.06
            out = u * self.rx * 0.14 * rng.uni(0.6, 1.3)
            self.strands.append((ax, top, out, yb, rng.uni(0.75, 1.25), rng()))
        self.strands.sort(key=lambda q: q[5])
        self.inner = []
        for i in range(int(ns * 0.6)):
            u = rng.uni(-0.8, 0.8)
            ax = self.cx + u * self.rx * 0.8
            top = self.cy - self.ry * 0.6 * math.sqrt(max(0.0, 1 - u * u)) + rng.uni(0, 0.4) * self.ry
            yb = bot - rng.uni(0.05, 0.25) * ht
            self.inner.append((ax, top, u * self.rx * 0.05, yb, rng.uni(0.75, 1.2), rng()))

    def strand_pts(self, q, wind=0.0):
        ax, top, out, yb, wk, ph = q
        ln = yb - top
        return bezier((ax, top), (ax + out * 1.4, top + ln * 0.18), (ax + out + wind * ln * 0.5, top + ln * 0.6),
                      (ax + out * 0.8 + wind * ln, yb), n=10)

    def trunk_mask(self):
        x, y, fx, fy, tw = self.x, self.y, self.fork[0], self.fork[1], self.tw
        pts = [(x - tw * 1.9, y + 1), (x - tw * 1.1, y - tw * 0.6), (x - tw * 1.0, y - tw * 1.6),
               (fx - tw * 0.9, fy + tw * 0.4), (fx - tw * 1.3, fy - tw * 0.2), (fx + tw * 1.2, fy - tw * 0.3),
               (fx + tw * 0.85, fy + tw * 0.5), (x + tw * 0.9, y - tw * 1.4), (x + tw * 1.1, y - tw * 0.5),
               (x + tw * 2.0, y + 1)]
        return m_poly(pts)

    def wood(self, c, p):
        """the trunk and the main branches"""
        lit = 1 if p.L[0] > 0 else -1
        sc = self.sc
        tm = self.trunk_mask()
        x0, y0, x1, y1 = tm.bbox()
        cols = [p.bark[0], p.bark[1], p.bark[2], p.bark[3]]
        bm = m_union([m_polyline(pts, w0, w1) for (pts, w0, w1) in self.branches])
        allw = tm | bm
        c.paint(allw, p.bark[1])
        # the lit side of the trunk and of each branch
        c.shade_dir(tm, cols, lit, 0, x0, y0, x1, y1, 0.3)
        for (pts, w0, w1) in self.branches:
            sh = [(px + lit * w0 * 0.22, py - w0 * 0.2) for (px, py) in pts]
            c.paint(m_polyline(sh, w0 * 0.45, max(1, w1 * 0.5)) & bm, p.bark[2])
        c.paint(allw.side(lit, 0), p.bark[3])
        c.paint(allw.side(-lit, 0), p.bark[0])
        # the fissures of the trunk (long wavy lines) and a knot hole
        rng = RNG(self.rng_seed + 9)
        for k in range(int(5 + self.tw * 0.8)):
            fx = rng.uni(x0 + 3, x1 - 3)
            yy = rng.uni(y0 + 2, y1 - 6)
            ln = rng.uni(6, 18) * sc
            pts = [(fx + math.sin(j * 0.9 + k) * 1.2, yy + j * ln / 5) for j in range(6)]
            c.paint(m_polyline(pts, 1) & tm, p.bark[0])
        kx, ky = self.x + lit * -self.tw * 0.2, self.y - self.tw * 2.6
        km = m_ellipse(kx, ky, self.tw * 0.32 + 0.5, self.tw * 0.45 + 0.5)
        c.paint(km, mul(p.bark[0], 0.7))
        c.paint(km.side(lit, 0), p.bark[3])
        # roots into the ground
        for k, (dx, ln) in enumerate([(-1.7, 1.6), (-0.9, 1.0), (1.0, 1.2), (1.8, 1.8)]):
            rx = self.x + dx * self.tw
            c.paint(m_line(rx, self.y - 1, rx + dx * self.tw * 0.6, self.y + 2 * sc, max(1.5, self.tw * 0.45)), p.bark[1])
            c.paint(m_line(rx, self.y - 2, rx + dx * self.tw * 0.6, self.y + 1 * sc, 1), p.bark[2 if dx * lit > 0 else 0])
        self.wood_mask = allw
        return allw

    def curtain(self, c, p, cover=None, wind=0.0):
        """the hanging leaves (spring, summer): one mass of strands, lit as a dome, with dark gaps and light streaks"""
        sc = self.sc
        s = p.s
        if s == SPRING:
            cols = [p.lfD, mix(p.lfD, p.lf, 0.6), p.lf, mix(p.lf, p.lfL, 0.6), mix(p.lfL, WHITE, 0.35)]
        else:
            cols = [mul(p.lfD, 0.82), p.lfD, mix(p.lfD, p.lf, 0.6), mix(p.lf, hx('#b8d8a0'), 0.35), hx('#d8ecc8')]
        sm = []
        for q in self.strands:
            pts = self.strand_pts(q, wind)
            sm.append(m_polyline(pts, 6.5 * sc * q[4], 2.2 * sc * q[4]))
        cap = m_ellipse(self.cx, self.cy + self.ry * 0.15, self.rx * 0.92, self.ry * 0.95)
        mask = m_union(sm) | cap
        if cover is not None:
            mask = mask | cover
        c.paint(mask.shift(0, 1) - mask, mul(cols[0], 0.8))
        c.shade_sphere(mask, self.cx - p.L[0] * self.rx * 0.1, self.cy + self.ry * 0.25, self.rx * 1.08, self.ry * 2.0, cols, p.L,
                       amb=0.12, band=0.3)
        rng = RNG(self.rng_seed + 31)
        # dark gaps between the strands (more at the bottom)
        for q in self.strands + self.inner:
            if rng() < 0.45:
                continue
            pts = self.strand_pts(q, wind)
            off = -2.6 * sc * q[4]
            gp = [(px + off, py) for (px, py) in pts[2:]]
            c.paint(m_polyline(gp, 1) & mask, cols[0] if rng() < 0.6 else cols[1])
        # light streaks and leaf tips
        for q in self.strands:
            pts = self.strand_pts(q, wind)
            if rng() < 0.6:
                off = 1.5 * sc * q[4] * (1 if p.L[0] > 0 else -1)
                lp = [(px + off, py) for (px, py) in pts[1:8]]
                c.paint(m_polyline(lp, 1) & mask, cols[3] if rng() < 0.7 else cols[4])
            tx, ty = pts[-1]
            for j in range(3):
                lx = tx + rng.uni(-2, 2) * sc
                ly = ty - rng.uni(0, 6) * sc
                c.paint(m_rect(lx, ly, 1, 2), cols[rng.pick([1, 2, 3])])
            c.px(tx, ty + 1, cols[2])
        if s == SPRING:
            # catkins: small yellow tassels
            for q in self.strands[::2]:
                pts = self.strand_pts(q, wind)
                for j in (3, 6):
                    if rng() < 0.6:
                        c.paint(m_rect(pts[j][0] + rng.uni(-2, 2), pts[j][1], 1, 2 + sc), hx('#f0e070'))
        return mask

    def twigs(self, c, p, wind=0.0, leaves=0.0, seed=0):
        """the bare strands (autumn, winter): thin golden twigs; a few yellow leaves on them in autumn"""
        sc = self.sc
        rng = RNG(self.rng_seed + 51 + seed)
        if p.s == WINTER:
            tc = [hx('#a88a50'), hx('#c9a868'), hx('#e0c890')]
        else:
            tc = [hx('#8a6a30'), hx('#b08a3a'), hx('#d0a850')]
        lc = [hx('#f2c33a'), hx('#e8a030'), hx('#ffe27a'), hx('#c8861c')]
        for q in self.inner + self.strands:
            pts = self.strand_pts(q, wind)
            col = tc[int(rng() * 3)]
            c.paint(m_polyline(pts, 1), col)
            if sc > 0.7 and rng() < 0.4:
                pts2 = [(px + 1, py) for (px, py) in pts[:5]]
                c.paint(m_polyline(pts2, 1), tc[0])
            if leaves > 0:
                for j in range(1, len(pts)):
                    if rng() < leaves:
                        lx, ly = pts[j]
                        c.paint(m_rect(lx, ly, 1 + sc, 2 + sc), rng.pick(lc))
            if p.s == WINTER and rng() < 0.35:
                j = int(rng.uni(2, 9))
                c.px(pts[j][0], pts[j][1], WHITE)

    def snow(self, c, p):
        """snow on the top of the main branches and in the fork (winter)"""
        sc = self.sc
        for (pts, w0, w1) in self.branches:
            for i in range(len(pts) - 1):
                (ax, ay), (bx, by) = pts[i], pts[i + 1]
                if abs(by - ay) > abs(bx - ax) * 1.6:
                    continue
                w = lerp(w0, w1, i / (len(pts) - 1))
                c.paint(m_line(ax, ay - w * 0.5 - sc * 0.6, bx, by - w * 0.5 - sc * 0.6, max(1, sc * 1.6)), WHITE)
        fx, fy = self.fork
        c.paint(m_ellipse(fx, fy - self.tw * 0.25, self.tw * 1.1, self.tw * 0.35), WHITE)
        c.paint(m_ellipse(fx, fy - self.tw * 0.1, self.tw * 1.0, self.tw * 0.2), p.snow[2])


class WillowNear:
    """the weeping willow seen near (a close view): a thick gnarled trunk, main branches that arch up and out, side
    branches, and locks of long strands that hang from the branches. Leaves: a dark back layer (the inside of the
    crown), then the wood, then the lit front locks with gaps (the trunk shows through them)."""

    def __init__(self, x, y, ht, wd, seed, tw=16.0, bottom=None):
        rng = RNG(seed)
        self.seed = seed
        self.x, self.y, self.ht, self.wd, self.tw = x, y, ht, wd, tw
        self.fork = (x + 3, y - ht * 0.42)
        fx, fy = self.fork
        self.cx, self.cy = x + 6, y - ht * 0.72
        self.rx, self.ry = wd, ht * 0.27
        self.bottom = (y - 6) if bottom is None else bottom
        self.branches = []
        nb = 7
        for k in range(nb):
            a = math.pi * (1.05 + 0.9 * k / (nb - 1)) + rng.uni(-0.05, 0.05)
            ex = self.cx + math.cos(a) * self.rx * rng.uni(0.8, 0.95)
            ey = self.cy + math.sin(a) * self.ry * rng.uni(0.75, 1.0)
            sx = fx + (k - 3) * tw * 0.3
            if k == 3:
                ex += self.rx * 0.18          # (the middle limb leans a little)
            pts = bezier((sx, fy + 4), (sx + (ex - sx) * 0.12, fy - (fy - ey) * 0.8), (ex - (ex - sx) * 0.3, ey - self.ry * 0.4),
                         (ex, ey), n=20)
            # (the end of the branch droops)
            side = 1 if ex > fx else -1
            pts += [(ex + side * 6, ey + 2), (ex + side * 10, ey + 7)]
            self.branches.append((pts, tw * rng.uni(0.36, 0.5), 1.6, 1))
            for t in (0.42, 0.68):
                j = int(t * 20)
                bx, by = pts[j]
                ln = self.rx * rng.uni(0.22, 0.36)
                ex2, ey2 = bx + side * ln, by + rng.uni(-0.05, 0.3) * self.ry
                pts2 = bezier((bx, by), (bx + side * ln * 0.4, by - self.ry * 0.3), (ex2, ey2), n=10) + [(ex2 + side * 4, ey2 + 4)]
                self.branches.append((pts2, tw * 0.22, 1.0, 0))
        # the locks of strands: anchors along the outer part of each branch
        self.locks = []
        for (pts, w0, w1, main) in self.branches:
            n = len(pts)
            t = 0.3 if main else 0.15
            while t < 1.0:
                ax, ay = pts[min(n - 1, int(t * (n - 1)))]
                self.locks.append(self._lock(rng, ax, ay))
                t += rng.uni(0.07, 0.12) if main else rng.uni(0.12, 0.2)
        # locks from the top of the crown (they fill the dome)
        self.dome_pts = []
        for i in range(26):
            u = (i + rng.uni(0.1, 0.9)) / 26 * 2 - 1
            ax = self.cx + u * self.rx * 0.9
            ay = self.cy - self.ry * math.sqrt(max(0.0, 1 - u * u)) * 0.9 + rng.uni(0, 6)
            self.dome_pts.append((ax, ay))
            self.locks.append(self._lock(rng, ax, ay))
        self.locks.sort(key=lambda L: L['z'])

    def _lock(self, rng, ax, ay):
        side = 1 if ax > self.cx else -1
        u = clamp((ax - self.cx) / self.rx, -1.2, 1.2)
        strands = []
        short = rng() < 0.22
        for k in range(rng.int(3, 6)):
            ox = rng.uni(-4, 4)
            yb = self.bottom + rng.uni(-0.12, 0.03) * self.ht - (abs(u) ** 2) * self.ht * 0.2
            ay2 = ay + rng.uni(0, 3)
            if short:
                yb = ay2 + (yb - ay2) * rng.uni(0.45, 0.7)
            strands.append((ax + ox, ay2, side * rng.uni(2, 8) * (0.3 + 1.6 * abs(u) ** 2), yb, rng.uni(0.8, 1.2)))
        return {'x': ax, 'y': ay, 'z': rng(), 'b': rng.uni(-0.12, 0.12), 'st': strands}

    def strand_pts(self, q, wind=0.0):
        ax, ay, out, yb, wk = q
        L = yb - ay
        return bezier((ax, ay), (ax + out * 0.5, ay - 4), (ax + out + wind * L * 0.5, ay + L * 0.45), (ax + out * 1.1 + wind * L, yb), n=12)

    def _front(self, L):
        """a lock in front (lit) or in the back (dark): the low locks in front of the trunk are left out"""
        fx, fy = self.fork
        if abs(L['x'] - self.x) < self.tw * 2.4 and L['y'] > fy - 34:
            return False
        if abs(L['x'] - self.x) < self.tw * 1.3:
            return False
        return L['z'] > 0.38

    def trunk_mask(self):
        x, y, fx, fy, tw = self.x, self.y, self.fork[0], self.fork[1], self.tw
        pts = [(x - tw * 1.9, y + 2), (x - tw * 1.05, y - tw * 0.7), (x - tw * 0.85, y - tw * 1.8), (x - tw * 0.95, y - tw * 2.8),
               (fx - tw * 0.8, fy + tw * 0.6), (fx - tw * 1.25, fy - tw * 0.1), (fx + tw * 1.2, fy - tw * 0.2),
               (fx + tw * 0.9, fy + tw * 0.7), (x + tw * 0.75, y - tw * 2.6), (x + tw * 0.95, y - tw * 1.5),
               (x + tw * 1.15, y - tw * 0.6), (x + tw * 2.1, y + 2)]
        return m_poly(pts)

    def wood(self, c, p, snow=False):
        lit = 1 if p.L[0] > 0 else -1
        tw = self.tw
        tm = self.trunk_mask()
        x0, y0, x1, y1 = tm.bbox()
        cols = [p.bark[0], p.bark[1], p.bark[2], p.bark[3]]
        bm = m_union([m_polyline(pts, w0, w1) for (pts, w0, w1, main) in self.branches])
        allw = tm | bm
        c.paint(allw, p.bark[1])
        c.shade_dir(tm, cols, lit, 0, x0, y0, x1, y1, 0.3)
        for (pts, w0, w1, main) in self.branches:
            sh = [(px + lit * w0 * 0.2, py - w0 * 0.22) for (px, py) in pts]
            c.paint(m_polyline(sh, w0 * 0.45, max(1, w1 * 0.5)) & bm, p.bark[2])
        c.paint(allw.side(lit, 0), p.bark[3])
        c.paint(allw.side(-lit, 0), p.bark[0])
        c.paint(bm.side(0, 1) - tm, p.bark[0])
        # deep fissures up the trunk, a knot hole, the bark plates lit at their edges
        rng = RNG(self.seed + 9)
        for k in range(16):
            fx = rng.uni(x0 + 4, x1 - 4)
            yy = rng.uni(y0 + 4, y1 - 10)
            ln = rng.uni(10, 26)
            pts = [(fx + math.sin(j * 0.8 + k) * 1.6, yy + j * ln / 6) for j in range(7)]
            fm = m_polyline(pts, 1) & tm
            c.paint(fm, p.bark[0])
            c.paint(fm.shift(lit, 0) & tm, p.bark[3] if rng() < 0.4 else p.bark[2])
            c.paint(fm, p.bark[0])
        kx, ky = self.x - lit * tw * 0.3, self.y - tw * 2.2
        km = m_ellipse(kx, ky, tw * 0.28, tw * 0.42)
        c.paint(km.grow(1), p.bark[2])
        c.paint(km, mul(p.bark[0], 0.6))
        c.paint(km.side(-lit, 0), p.bark[0])
        # the roots
        for dx, ln in [(-1.7, 1.4), (-0.8, 0.9), (0.9, 1.0), (1.8, 1.6)]:
            rx = self.x + dx * tw
            c.paint(m_line(rx, self.y - 2, rx + dx * tw * 0.7, self.y + 4, tw * 0.45), p.bark[1])
            c.paint(m_line(rx, self.y - 3, rx + dx * tw * 0.7, self.y + 3, 1), p.bark[2 if dx * lit > 0 else 0])
        if snow:
            for (pts, w0, w1, main) in self.branches:
                for i in range(len(pts) - 1):
                    (ax, ay), (bx, by) = pts[i], pts[i + 1]
                    if abs(by - ay) > abs(bx - ax) * 1.4:
                        continue
                    w = lerp(w0, w1, i / (len(pts) - 1))
                    c.paint(m_line(ax, ay - w * 0.5 - 0.6, bx, by - w * 0.5 - 0.6, max(1.0, min(3.0, w * 0.45))), WHITE)
            fx, fy = self.fork
            c.paint(m_ellipse(fx, fy - tw * 0.2, tw * 1.15, tw * 0.3), WHITE)
            c.paint(m_ellipse(fx, fy - tw * 0.05, tw * 1.0, tw * 0.15), p.snow[2])
        return allw

    def cap_mask(self):
        """the leaf mass on the top of the crown: puffs along the arches of the branches (a lumpy dome)"""
        ms = [m_ellipse(ax, ay + 4, 13, 9) for (ax, ay) in self.dome_pts]
        for (pts, w0, w1, main) in self.branches:
            n = len(pts)
            for j in range(int(n * 0.35), n - 1, 2):
                px, py = pts[j]
                r = 12 if main else 9
                ms.append(m_ellipse(px, py - 3, r, r * 0.7))
        return m_union(ms)

    def leaf_cols(self, p):
        if p.s == SPRING:
            return [p.lfD, mix(p.lfD, p.lf, 0.6), p.lf, mix(p.lf, p.lfL, 0.6), mix(p.lfL, WHITE, 0.35)]
        return [mul(p.lfD, 0.82), p.lfD, mix(p.lfD, p.lf, 0.6), mix(p.lf, hx('#b8d8a0'), 0.35), hx('#d8ecc8')]

    def back(self, c, p):
        """the back layer of the leaves (spring, summer): the inside of the crown, in shade"""
        cols = self.leaf_cols(p)
        ms = [self.cap_mask().grow(1), m_ellipse(self.cx, self.cy + self.ry * 0.7, self.rx * 0.8, self.ry * 1.45)]
        for L in self.locks:
            for q in L['st']:
                ms.append(m_polyline(self.strand_pts(q), 4.0, 2.0))
        bm = m_union(ms)
        c.paint(bm, mul(cols[0], 0.8))
        c.paint_dither(bm, cols[0], 0.45)
        rng = RNG(self.seed + 3)
        for L in self.locks[::3]:
            q = L['st'][0]
            pts = self.strand_pts(q)
            c.paint(m_polyline(pts[3:], 1) & bm, cols[1])
        return bm

    def front(self, c, p, cover=None, wind=0.0):
        """the lit front locks (spring, summer) and the leaf mass of the top of the crown"""
        cols = self.leaf_cols(p)
        scx, scy, srx, sry = self.cx - p.L[0] * self.rx * 0.1, self.cy + self.ry * 0.5, self.rx * 1.08, self.ry * 3.4
        cap = self.cap_mask()
        fronts = [L for L in self.locks if self._front(L)]
        lms = []
        for L in fronts:
            lms.append(m_union([m_polyline(self.strand_pts(q, wind), 5.0 * q[4], 2.2 * q[4]) for q in L['st']]))
        fm = m_union([cap] + lms)
        if cover is not None:
            fm = fm | cover
        c.paint(fm.shift(0, 1) - fm, mul(cols[0], 0.7))
        c.shade_sphere(fm, scx, scy, srx, sry, cols, p.L, amb=0.14, band=0.3)
        # (each lock has its own tone: the curtain is in locks, not one sheet)
        for L, lm in zip(fronts, lms):
            if abs(L['b']) > 0.05:
                c.shade_sphere(lm - cap, scx, scy, srx, sry, cols, p.L, amb=0.14, band=0.3, bias=L['b'])
        if cover is not None:
            c.shade_sphere(cover, scx, scy, srx, sry, cols, p.L, amb=0.14, band=0.3)
        rng = RNG(self.seed + 31)
        lit = 1 if p.L[0] > 0 else -1
        for L in fronts:
            for k, q in enumerate(L['st']):
                pts = self.strand_pts(q, wind)
                # the dark line between two strands, the light edge of a strand, the leaves at its tip
                if k % 2 == 0:
                    c.paint(m_polyline([(px - 2.4 * lit, py) for (px, py) in pts[2:]], 1) & fm, cols[1] if rng() < 0.6 else cols[0])
                if rng() < 0.35:
                    j0 = 2 + int(rng() * 3)
                    c.paint(m_polyline([(px + 1.4 * lit, py) for (px, py) in pts[j0:j0 + 4]], 1) & fm, cols[3] if rng() < 0.7 else cols[4])
                tx, ty = pts[-1]
                for j in range(4):
                    c.paint(m_rect(tx + rng.uni(-2.5, 2.5), ty - rng.uni(0, 8), 1, 2), cols[rng.pick([1, 2, 3])])
                c.paint(m_rect(tx - 1, ty, 2, 2), cols[2])
                c.px(tx, ty + 2, cols[1])
        if p.s == SPRING:
            for L in fronts[::2]:
                q = L['st'][0]
                pts = self.strand_pts(q, wind)
                for j in (4, 8):
                    if rng() < 0.7:
                        c.paint(m_rect(pts[j][0] + rng.uni(-3, 3), pts[j][1], 1, 3), hx('#f0e070'))
        return fm

    def twigs(self, c, p, wind=0.0, leaves=0.0, layer='front'):
        """the bare strands (autumn, winter): thin golden twigs in their locks; a few yellow leaves in autumn.
        layer 'back': the locks behind the trunk (draw them before the wood), 'front': the others"""
        rng = RNG(self.seed + 51 + (7 if layer == 'back' else 0))
        if p.s == WINTER:
            tc = [hx('#a88a50'), hx('#c9a868'), hx('#e0c890')]
        else:
            tc = [hx('#8a6a30'), hx('#b08a3a'), hx('#d4ac52')]
        lc = [hx('#f2c33a'), hx('#e8a030'), hx('#ffe27a'), hx('#c8861c')]
        for L in self.locks:
            back = not self._front(L)
            if back != (layer == 'back'):
                continue
            for k, q in enumerate(L['st']):
                if (back and k % 3) or (not back and k % 3 == 2):
                    continue
                pts = self.strand_pts(q, wind)
                col = tc[0] if back else tc[1 + int(rng() * 2)]
                c.paint(m_polyline(pts, 1), col)
                if leaves > 0 and not back:
                    for j in range(2, len(pts)):
                        if rng() < leaves:
                            lx, ly = pts[j]
                            c.paint(m_rect(lx, ly, 2, 3), rng.pick(lc))
                if p.s == WINTER and rng() < 0.4:
                    j = int(rng.uni(2, 10))
                    c.px(pts[j][0], pts[j][1], WHITE)
                    c.px(pts[j][0], pts[j][1] + 1, p.ice[2])
