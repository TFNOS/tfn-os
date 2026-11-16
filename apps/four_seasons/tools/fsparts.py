# FOUR SEASONS: the parts that more than one scene draws: the sky with its bands and the sun, the clouds, the far
# mountains, a ridge of small trees, a hill with its grass marks, the big tree (branches and clumps of leaves), bushes,
# flowers, stones, fences, the falling snow on objects. All take the palette p of the season (fspal.pal).
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, smooth, hx, mix, mul, add, m_rect, m_ellipse, m_disc, m_poly,
                   m_line, m_polyline, m_union, bezier, fbm1, noise1, fbm2, vnoise2, cast_shadow)
from fspal import SPRING, SUMMER, AUTUMN, WINTER, shade, ramp_of

WHITE = (255, 255, 255)


# ---------------------------------------------------------------- the sky
def sky(c, p, y0, y1, sun=None, nb=8, sun_r=8.5, rings=True):
    """the banded sky from y0 to y1 (flat bands with short dither steps), the sun at sun (x, y) with its rings"""
    H = y1 - y0
    cols = []
    for b in range(nb + 1):
        f = b / nb
        cc = mix(p.skyT, p.skyB, f ** 1.25)
        cols.append(mix(cc, p.hz, max(0.0, f - 0.6) * 1.2))
    m = m_rect(0, y0, c.w, H)
    c.paint_values(m, cols, lambda x, y: np.clip((y - y0) / H, 0, 1), band=0.3)
    if sun is not None and rings:
        c.rings(sun[0], sun[1], 18, p.sunG, levels=3, k=0.17, sy=1.5)
    if sun is not None:
        c.paint(m_disc(sun[0], sun[1], sun_r), p.sun)
        c.paint(m_disc(sun[0] - 2, sun[1] - 2, sun_r * 0.42), mix(p.sun, WHITE, 0.6))


def cloud(c, p, cx, cy, cw, kind=1, seed=0):
    """a cloud: kind 1 = a puffy cloud of bumps (spring, summer); 2 = a long low cloud lit from under (autumn, winter)"""
    if kind == 2:
        h = cw * 0.11
        for i in range(4):
            f = i / 3
            c.paint(m_ellipse(cx - cw * 0.45 + f * cw * 0.9, cy - math.sin(f * math.pi) * h * 0.6, cw * 0.22,
                              h * (0.6 + math.sin(f * math.pi) * 0.5)), p.clS)
        c.paint(m_ellipse(cx + 2, cy + h * 0.25, cw * 0.48, h * 0.38), p.cl)
        c.paint(m_ellipse(cx + 4, cy + h * 0.45, cw * 0.36, h * 0.18), p.clH)
        return
    bumps = 5
    h = cw * 0.26
    rng = RNG(1000 + seed)
    pts = []
    for i in range(bumps):
        f = i / (bumps - 1)
        pts.append((cx - cw * 0.5 + f * cw + rng.uni(-2, 2), cy - math.sin(f * math.pi) * h * 0.8 + rng.uni(-1, 1),
                    h * (0.55 + math.sin(f * math.pi) * 0.6)))
    sh = m_union([m_ellipse(px + 2, py + 2.4, r, r * 0.85) for px, py, r in pts] + [m_rect(cx - cw * 0.5 + 2, cy - h * 0.2 + 2, cw, h * 0.55)])
    c.paint(sh, p.clS)
    body = m_union([m_ellipse(px, py, r, r * 0.85) for px, py, r in pts] + [m_rect(cx - cw * 0.5, cy - h * 0.2, cw, h * 0.5)])
    c.paint(body, p.cl)
    hi = m_union([m_ellipse(px - r * 0.3, py - r * 0.3, r * 0.5, r * 0.4) for px, py, r in pts])
    c.paint(hi, p.clH)
    # (a mid tone line under the light top: the volume of the cloud)
    c.paint((body.shift(0, 2) & body) - body.shift(0, -1) - hi, mix(p.cl, p.clS, 0.35))


def clouds(c, p, y_lo, y_hi, n, seed, x0=-60, x1=None):
    x1 = c.w + 60 if x1 is None else x1
    rng = RNG(seed)
    kind = 2 if p.s in (AUTUMN, WINTER) else 1
    for i in range(n):
        cx = x0 + (x1 - x0) * (i + rng.uni(0.1, 0.9)) / n
        cy = rng.uni(y_lo, y_hi)
        cw = rng.uni(36, 70) * (1.5 if kind == 2 else 1)
        cloud(c, p, cx, cy, cw, kind, seed + i)


# ---------------------------------------------------------------- the far land
def ridge_y(x, base, amp, fq, seed, jag):
    if jag:
        n = abs(fbm1(x * fq + seed, 4)) * 1.3 + abs(noise1(x * fq * 4 + seed)) * 0.25
    else:
        n = fbm1(x * fq + seed, 3) * 0.5 + 0.5
    return base - n * amp


def ridge_mask(c, base, amp, fq, seed, jag, bottom=None, x_off=0):
    bottom = c.h + 4 if bottom is None else bottom
    pts = [(-4, bottom)]
    for x in range(-4, c.w + 5, 2):
        pts.append((x, ridge_y(x + x_off, base, amp, fq, seed, jag)))
    pts.append((c.w + 4, bottom))
    return m_poly(pts)


def mountains(c, p, base, amp=34, fq=0.007, seed=3, x_off=0, cap_line=None, haze_y=None):
    """far mountains (the look page: a jagged ridge, snow above the cap line, a haze band at the foot); the cap line
    is high in summer and low in winter; the slopes that face the sun are lighter"""
    m = ridge_mask(c, base, amp, fq, seed, True, x_off=x_off)
    c.paint(m, p.mtn)
    sx = 1 if p.L[0] > 0 else -1
    ys = np.arange(m.h)[:, None] + m.y0
    xr = np.arange(m.x0, m.x0 + m.w)
    ry = np.array([ridge_y(x + x_off, base, amp, fq, seed, True) for x in xr])
    sl = np.gradient(ry)
    face = (-sl * sx) > 0.2
    dark = (sl * sx) > 0.35
    c.paint(Mask(m.x0, m.y0, m.m & face[None, :]), mix(p.mtn, p.warm, 0.2))
    c.paint(Mask(m.x0, m.y0, m.m & dark[None, :] & (ys > ry[None, :] + 2)), mix(p.mtn, p.cool, 0.2))
    if cap_line is None:
        cap_line = base - amp + {SPRING: 12, SUMMER: 6, AUTUMN: 11, WINTER: 22}[p.s]
    capm = Mask(m.x0, m.y0, m.m & (ys < cap_line))
    c.paint(capm, mix(p.cap, p.cool, 0.12))
    c.paint(Mask(capm.x0, capm.y0, capm.m & face[None, :]), p.cap)
    # (a ragged foot of the snow: dots of snow and rock)
    foot = Mask(m.x0, m.y0, m.m & (ys >= cap_line) & (ys < cap_line + 3))
    c.paint_dither(foot, mix(p.cap, p.mtn, 0.5), 0.45)
    hy = base - 6 if haze_y is None else haze_y
    c.paint(m_rect(0, hy, c.w, 60) & m, mix(p.mtn, p.hz, 0.35))
    c.paint_dither(m_rect(0, hy - 3, c.w, 3) & m, mix(p.mtn, p.hz, 0.35), 0.5)


def far_ridge(c, p, base, amp=14, fq=0.0055, seed=9, step=7, tree_h=(3, 7), x_off=0):
    m = ridge_mask(c, base, amp, fq, seed, False, x_off=x_off)
    c.paint(m, p.ridge)
    rng = RNG(seed * 7 + 1)
    for x in range(-6, c.w + 6, step):
        if rng() < 0.3:
            continue
        y = ridge_y(x + x_off, base, amp, fq, seed, False)
        th = rng.uni(*tree_h)
        tx = x + rng.uni(-2, 2)
        c.paint(m_rect(tx, y - 2, 1, 3), mul(p.farT, 0.6))
        crown = m_ellipse(tx + 0.5, y - 2 - th * 0.8, th * 0.75, th)
        c.paint(crown, mul(p.farT, 0.82))
        c.paint(m_ellipse(tx + 0.5 - th * 0.25, y - 2.4 - th, th * 0.42, th * 0.55), mix(p.farT, p.hz, 0.25))
        if p.s == WINTER:
            c.paint(crown.top(2), WHITE)
    return m


# ---------------------------------------------------------------- the ground
def grass_marks(c, p, mask, n, seed, ytop_fn=None, depth=46, hmin=1, hmax=3, face_fn=None, cols=None):
    """short vertical marks of grass on the mask: lit or dark by the slope (face_fn(x) > 0: the slope faces the sun)"""
    rng = np.random.default_rng(seed)
    xs, ys = mask.coords()
    if len(xs) == 0:
        return
    pick = rng.integers(0, len(xs), n)
    hs = rng.uniform(hmin, hmax + 0.99, n).astype(int)
    tone = rng.random(n)
    cols = cols or (p.hillL, p.hillS, p.grassL)
    for k in range(n):
        x, y = xs[pick[k]], ys[pick[k]]
        f = face_fn(x) if face_fn else 0
        if f > 0.12:
            col = cols[0]
        elif f < -0.12:
            col = cols[1]
        else:
            col = cols[0] if tone[k] > 0.5 else cols[1]
        if tone[k] > 0.93:
            col = cols[2]
        c.paint(m_rect(x, y - hs[k] + 1, 1, hs[k]), col)


def flowers(c, p, mask, n, seed, avoid=None):
    rng = RNG(seed)
    xs, ys = mask.coords()
    if len(xs) == 0 or p.s == WINTER:
        return
    vis = {SPRING: 0.85, SUMMER: 1.0, AUTUMN: 0.5}[p.s]
    for i in range(n):
        k = int(rng() * len(xs))
        x, y = xs[k], ys[k]
        th = rng()
        ci = int(rng() * 4)
        if th > vis:
            continue
        if avoid is not None and avoid(x, y):
            continue
        col = p.flower[ci]
        c.px(x, y, col)
        c.px(x + 1, y, col)
        c.px(x, y - 1, col)
        c.px(x + 1, y - 1, mix(col, WHITE, 0.4))
        c.px(x, y + 1, mix(p.grass, p.hillS, 0.5))


def rock(c, p, x, y, r, seed=0):
    """a stone on the ground: lit on the side of the sun, with a snow cap in winter"""
    m = m_ellipse(x, y, r * 1.35, r * 0.85)
    c.shade_sphere(m, x, y, r * 1.35, r * 0.85, p.stone, p.L, amb=0.2)
    c.paint(m.bottom(1), p.stone[0])
    if p.s == WINTER:
        c.snow_cap(m, max(1, int(r * 0.45)), p.snow)
    return m


def bush(c, p, x, y, r, seed=0, kind='flower'):
    """a round bush: pink in spring (blossom), green in summer, red in autumn, white in winter"""
    rng = RNG(seed + 77)
    sd = p.slant
    c.darken(m_ellipse(x + sd * r * 0.6, y + 1, r * 1.3, r * 0.32), 0.72, p.cool)
    if p.s == SPRING and kind == 'flower':
        cols = [hx('#b84f7e'), hx('#d86b9a'), p.bush, p.bushL, WHITE]
    elif p.s == SUMMER:
        cols = [mul(p.lfD, 0.85), p.lfD, p.lf, p.lfL, mix(p.lfL, WHITE, 0.3)]
    elif p.s == AUTUMN:
        cols = [hx('#6a1e14'), hx('#9a2c1c'), p.bush, p.bushL, hx('#ffb070')]
    elif p.s == SPRING:
        cols = p.leaf
    else:
        cols = [hx('#8a7a6a'), hx('#6a5a4a'), hx('#a0a8b8'), p.snow[2], WHITE]
    blobs = []
    for k in range(7):
        a = rng.uni(0, 2 * math.pi)
        rr = rng.uni(0.2, 0.75) * r
        bx = x + math.cos(a) * rr
        by = y - r * 0.6 + math.sin(a) * rr * 0.55
        br = r * rng.uni(0.45, 0.7)
        blobs.append((bx, by, br))
    blobs.sort(key=lambda b: b[1])
    for bx, by, br in blobs:
        m = m_ellipse(bx, by, br, br * 0.82)
        c.shade_sphere(m, bx, by, br, br * 0.82, cols, p.L, amb=0.25)
    if p.s == WINTER:
        allm = m_union([m_ellipse(bx, by, br, br * 0.82) for bx, by, br in blobs])
        c.snow_cap(allm, 3, p.snow, rough=1)
        # (the twigs that come out of the snow)
        for k in range(5):
            tx = x + rng.uni(-r, r)
            c.line(tx, y - r * 0.4, tx + rng.uni(-2, 2), y - r * 1.2, hx('#5e4433'))
    if p.s == SPRING and kind == 'flower':
        for k in range(int(r * 1.5)):
            c.px(x + rng.uni(-r, r), y - r * 0.6 + rng.uni(-r * 0.6, r * 0.4), WHITE)
    if p.s == AUTUMN:
        for k in range(int(r)):
            c.px(x + rng.uni(-r * 1.4, r * 1.4), y + rng.uni(-1, 2), rng.pick([p.bush, p.bushL, hx('#e8a040')]))


# ---------------------------------------------------------------- the big tree
class Tree:
    """a tree of branches (made one time from a seed) and clumps of leaves in the crown"""

    def __init__(self, x, y, seed=404, depth=5, length=20.0, width=7.5, angle=-math.pi / 2, crown=(58, 31), nclump=260,
                 clump_r=(3.0, 7.0), spread=0.66, extra=()):
        self.x, self.y = x, y
        rng = RNG(seed)
        self.branches = []

        def grow(bx, by, a, ln, wd, d):
            x2, y2 = bx + math.cos(a) * ln, by + math.sin(a) * ln
            self.branches.append((bx, by, x2, y2, wd, a, d))
            if d <= 0:
                return
            n = 3 if d >= depth - 1 else 2
            for i in range(n):
                sp = (i - 1) * spread if n == 3 else (0.5 if i else -0.46)
                grow(x2, y2, a + sp + (rng() - 0.5) * 0.34, ln * (0.68 + rng() * 0.12), wd * 0.64, d - 1)
        grow(x, y, angle, length, width, depth)
        for b in extra:
            self.branches.append(b)
        # the crown: clumps in an ellipse round the top of the branches
        self.cx = sum(b[2] for b in self.branches if b[6] <= 2) / max(1, sum(1 for b in self.branches if b[6] <= 2))
        self.cy = sum(b[3] for b in self.branches if b[6] <= 2) / max(1, sum(1 for b in self.branches if b[6] <= 2))
        self.crx, self.cry = crown
        self.clumps = []
        for i in range(4000):
            if len(self.clumps) >= nclump:
                break
            a = rng() * 2 * math.pi
            r = math.sqrt(rng())
            cx = self.cx + math.cos(a) * r * self.crx
            cy = self.cy + math.sin(a) * r * self.cry
            if cy > self.cy + self.cry * 0.62 + math.cos(a) * 4:
                continue
            self.clumps.append((cx, cy, clump_r[0] + rng() * (clump_r[1] - clump_r[0]), rng(), rng(), rng() * 6.28))
        self.clumps.sort(key=lambda q: q[1])

    def wood_mask(self, min_w=0.0):
        return m_union([m_line(b[0], b[1], b[2], b[3], max(1.0, b[4])) for b in self.branches if b[4] >= min_w])

    def draw_wood(self, c, p, skip_twigs=False):
        lit = 1 if p.L[0] > 0 else -1
        for (x1, y1, x2, y2, w, a, d) in self.branches:
            if skip_twigs and d == 0:
                continue
            c.paint(m_line(x1, y1, x2, y2, max(1.0, w)), p.bark[1])
            if w > 2.2:
                c.paint(m_line(x1 + lit * w * 0.25, y1, x2 + lit * w * 0.25, y2, w * 0.4), p.bark[2])
            if w > 4.5:
                c.paint(m_line(x1 + lit * w * 0.36, y1, x2 + lit * w * 0.36, y2, 1.0), p.bark[3])
        if p.s == WINTER:
            for (x1, y1, x2, y2, w, a, d) in self.branches:
                flat = 1 - abs(math.sin(a))
                if flat < 0.22 or w < 0.8:
                    continue
                c.paint(m_line(x1, y1 - w * 0.55, x2, y2 - w * 0.55, max(1.0, w * 0.5)), WHITE)

    def density(self, s):
        return {SPRING: 0.84, SUMMER: 1.0, AUTUMN: 0.72, WINTER: 0.0}[s]

    def draw_leaves(self, c, p, front=True, seed=0):
        dens = self.density(p.s)
        if dens <= 0:
            return
        L = p.L
        sdx, sdy = L[0], L[1]
        rng = RNG(seed + 5)
        for (cx, cy, r0, th, hv, ph) in self.clumps:
            vis = clamp((dens - th * 0.9) * 5, 0, 1)
            if vis <= 0:
                continue
            r = r0 * (0.45 + 0.55 * vis)
            cols = list(p.leaf)
            if p.s == AUTUMN:
                if hv > 0.62:
                    cols = [hx('#6a1a10'), hx('#9a2a18'), hx('#d23a26'), hx('#ff7a4a'), hx('#ffb090')]
                elif hv < 0.24:
                    cols = [hx('#8a5a10'), hx('#c8861c'), hx('#f2c33a'), hx('#ffe27a'), hx('#fff4c0')]
            m = m_ellipse(cx, cy, r, r * 0.84)
            c.shade_sphere(m, cx, cy, r, r * 0.84, cols, L, amb=0.16, band=0.3)
            # (leaf pixels at the edge: the crown is not smooth)
            c.px(cx + r * 0.9, cy + (ph % 2) - 1, cols[2])
            c.px(cx - r * 0.95, cy + 1, cols[1])
            c.px(cx + rng.uni(-r, r), cy - r * 0.8, cols[3])
            if p.s == SPRING:
                bl = 1.0 if hv > 0.15 else 0.4
                if rng() < bl:
                    pm = m_ellipse(cx - r * 0.1 - sdx * r * 0.2, cy - r * 0.25, r * 0.55, r * 0.4)
                    c.paint(pm, p.blossom[1])
                    c.paint(pm.top(1), p.blossom[2])
                for k in range(3):
                    c.px(cx + rng.uni(-r * 0.7, r * 0.7), cy + rng.uni(-r * 0.6, r * 0.3), WHITE if hv > 0.6 else p.blossom[2])

    def snow(self, c, p):
        """snow on the branches (winter): drawn by draw_wood"""
        pass


def fence(c, p, x0, y0, x1, y1, posts, rail_dy=(-12, -6), post_h=15, seed=0):
    """a wooden fence with posts and two rails, from (x0, y0) to (x1, y1) on the ground"""
    for k in range(posts):
        f = k / max(1, posts - 1)
        px = lerp(x0, x1, f)
        py = lerp(y0, y1, f)
        c.paint(m_rect(px, py - post_h, 3, post_h), p.woodr[2])
        c.paint(m_rect(px, py - post_h, 1, post_h), p.woodr[3])
        c.paint(m_rect(px + 2, py - post_h, 1, post_h), p.woodr[1])
        if p.s == WINTER:
            c.paint(m_rect(px - 1, py - post_h - 2, 5, 2), WHITE)
    for dy in rail_dy:
        c.paint(m_line(x0, y0 + dy, x1 + 3, y1 + dy, 2), p.woodr[2])
        c.paint(m_line(x0, y0 + dy - 1, x1 + 3, y1 + dy - 1, 1), p.woodr[3])
        if p.s == WINTER:
            c.paint(m_line(x0, y0 + dy - 2, x1 + 3, y1 + dy - 2, 1), WHITE)


def particles_baked(c, p, region, n, seed):
    """a few still leaves, petals or snow flakes in the air (the live ones come from the game)"""
    rng = RNG(seed)
    x0, y0, x1, y1 = region
    for i in range(n):
        x = rng.uni(x0, x1)
        y = rng.uni(y0, y1)
        if p.s == SPRING:
            c.paint(m_rect(x, y, 2, 1), p.blossom[2] if rng() < 0.5 else WHITE)
        elif p.s == AUTUMN:
            c.paint(m_rect(x, y, 2, 1), rng.pick([hx('#ffb23d'), hx('#e8582a'), hx('#c9351f')]))
        elif p.s == WINTER:
            c.px(x, y, WHITE)
