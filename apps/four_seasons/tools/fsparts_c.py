# FOUR SEASONS: the parts of the close views (the camera is near: about 4-5 x the scale of the valley): limbs with
# bark, a near trunk, branches that grow from the limbs, clumps of near leaves with blossom, vine leaves, near grass, a
# near snow field, fallen leaves, boards, a rope, small sprites. All take the palette p of the season (fspal.pal). The
# scenes sc_tree, sc_swing, sc_vines, sc_shed and sc_garden use them. Each part takes its own seed: a part never uses
# the random numbers of another part (the layers of the baker must not change when one thing changes).
import math
import numpy as np
from fsart import (Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline,
                   m_union, fbm2)
from fspal import SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P

WHITE = (255, 255, 255)
AUT_R = [hx('#6a1a10'), hx('#9a2a18'), hx('#d23a26'), hx('#ff7a4a'), hx('#ffb090')]
AUT_Y = [hx('#8a5a10'), hx('#c8861c'), hx('#f2c33a'), hx('#ffe27a'), hx('#fff4c0')]


def ldir(p):
    """the direction to the light in whole pixels (dx, dy): for the lit and the dark rims"""
    lx = -1 if p.L[0] < -0.3 else (1 if p.L[0] > 0.3 else 0)
    ly = -1 if p.L[1] < -0.3 else (1 if p.L[1] > 0.3 else 0)
    return lx, ly


def sun_xy(p, w):
    """the place of the sun in a close view of the width w"""
    return p.sun_xy[0] * w / 640.0, p.sun_xy[1] * 0.85 + 2


def sky(c, p, y1, nclouds=5, seed=7, sun=True):
    s = sun_xy(p, c.w) if sun else None
    P.sky(c, p, 0, y1, s, sun_r=10)
    P.clouds(c, p, 8, max(12, y1 * 0.42), nclouds, seed)
    return s


def sprite(c, x, y, rows, cmap):
    """a small hand sprite: rows of characters, cmap = {char: colour}; other characters are empty"""
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch in cmap:
                c.px(x + i, y + j, cmap[ch])


def sub(m, sel):
    """the pixels of the mask m where the bool array sel (the box of m) is true"""
    return Mask(m.x0, m.y0, m.m & sel)


# ---------------------------------------------------------------- wood: limbs, branches and a near trunk
def limb(c, p, pts, w0, w1=None, outline=True, body=True, snow=True, seed=0):
    """a near limb (a thick polyline) with a dark outline, a lit and a dark rim, bark marks, snow on top in winter"""
    w1 = w0 if w1 is None else w1
    m = m_polyline(pts, w0, w1)
    if outline:
        c.outline(m, mul(p.bark[0], 0.78))
    if not body:
        return m
    lx, ly = ldir(p)
    c.paint(m, p.bark[1])
    wx = max(w0, w1)
    if wx >= 3:
        n = max(1, int(round(wx * 0.28)))
        c.paint(m.side(-lx, -ly, n), p.bark[0])
        c.paint(m.side(lx, ly, n), p.bark[2])
    if wx >= 7:
        c.paint(m.side(lx, ly, 1), p.bark[3])
        rng = RNG(seed + 31)
        for i in range(len(pts) - 1):
            ax, ay = pts[i]
            bx, by = pts[i + 1]
            L = math.hypot(bx - ax, by - ay)
            if L < 1:
                continue
            ux, uy = (bx - ax) / L, (by - ay) / L
            ww = lerp(w0, w1, i / max(1, len(pts) - 1))
            for k in range(int(L / 5)):
                t = rng()
                off = rng.uni(-0.32, 0.32) * ww
                x = ax + (bx - ax) * t - uy * off
                y = ay + (by - ay) * t + ux * off
                ln = rng.uni(3, 8)
                c.paint(m_line(x, y, x + ux * ln, y + uy * ln, 1) & m, p.bark[0])
    if snow and p.s == WINTER:
        c.snow_cap(crop(m, c), max(1, int(round(wx * 0.3))), p.snow, rough=1 if wx > 5 else 0)
    return m


def crop(m, c):
    """the part of the mask m in the canvas c (its origin is not negative)"""
    x0, y0 = max(0, m.x0), max(0, m.y0)
    x1, y1 = min(c.w, m.x0 + m.w), min(c.h, m.y0 + m.h)
    if x0 >= x1 or y0 >= y1:
        return Mask(0, 0, np.zeros((1, 1), bool))
    return Mask(x0, y0, m.m[y0 - m.y0:y1 - m.y0, x0 - m.x0:x1 - m.x0])


def along(pts, w0, w1, t):
    """the point at the part t (0..1) of the length of a polyline: (x, y, the angle, the width there)"""
    segs = []
    tot = 0.0
    for i in range(len(pts) - 1):
        L = math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
        segs.append(L)
        tot += L
    d = t * tot
    for i, L in enumerate(segs):
        if d <= L or i == len(segs) - 1:
            f = clamp(d / max(1e-6, L), 0, 1)
            ax, ay = pts[i]
            bx, by = pts[i + 1]
            return ax + (bx - ax) * f, ay + (by - ay) * f, math.atan2(by - ay, bx - ax), lerp(w0, w1, t)
        d -= L


def _turn(a, b, k):
    d = (b - a + math.pi) % (2 * math.pi) - math.pi
    return a + d * k


def grow(limbs, seed, per=5, sub_len=(26, 52), sub_w=0.42, depth=2, up=0.3, t_range=(0.15, 0.97)):
    """the branches that grow from the limbs (pts, w0, w1): a list of (pts, w0, w1, level) and the tips (x, y, angle)"""
    rng = RNG(seed)
    out = []
    tips = []

    def rec(x, y, a, ln, w, d):
        bend = rng.uni(-0.3, 0.3)
        mx, my = x + math.cos(a) * ln * 0.5, y + math.sin(a) * ln * 0.5
        a2 = a + bend
        x2, y2 = mx + math.cos(a2) * ln * 0.5, my + math.sin(a2) * ln * 0.5
        out.append(([(x, y), (mx, my), (x2, y2)], w, max(1.0, w * 0.68), d))
        if d == 0:
            tips.append((x2, y2, a2))
            return
        for s in (-1, 1):
            a3 = _turn(a2 + s * rng.uni(0.3, 0.75), -math.pi / 2, up * 0.4)
            rec(x2, y2, a3, ln * rng.uni(0.55, 0.78), max(1.0, w * 0.62), d - 1)

    for (pts, w0, w1) in limbs:
        for k in range(per):
            t = lerp(t_range[0], t_range[1], (k + rng.uni(0.15, 0.85)) / per)
            x, y, ang, w = along(pts, w0, w1, t)
            s = 1 if rng() < 0.5 else -1
            a = _turn(ang + s * rng.uni(0.5, 1.1), -math.pi / 2, up)
            rec(x, y, a, rng.uni(*sub_len), max(2.0, w * sub_w), depth)
        x, y, ang, w = along(pts, w0, w1, 1.0)
        tips.append((x, y, ang))
    return out, tips


def trunk_mask(xl, xr, y0, y1):
    ys = np.arange(y0, y1 + 1, 2.0)
    pts = [(float(xl(y)), y) for y in ys] + [(float(xr(y)), y) for y in ys[::-1]]
    return m_poly(pts)


def trunk(c, p, xl, xr, y0, y1, seed=1, cracks=7, outline=True, m=None):
    """a near trunk from y0 to y1: xl(y), xr(y) give the left and the right edge (they take numpy arrays); a cylinder
    light across, bark plates with cracks, moss on the dark side, snow in the cracks on the side of the wind"""
    m = trunk_mask(xl, xr, y0, y1) if m is None else m
    if outline:
        c.outline(m, mul(p.bark[0], 0.72))
    X, Y = m.grid()
    l = xl(Y)
    r = xr(Y)
    u = np.clip((X - l) / np.maximum(1.0, r - l), 0, 1)
    nx = u * 2 - 1
    nz = np.sqrt(np.clip(1 - nx * nx, 0, 1))
    d = nx * p.L[0] * 1.15 + nz * 0.7
    v = 0.1 + 0.82 * np.clip(d, 0, 1)
    n1 = fbm2(m.w, m.h, 16, seed, 3)
    n2 = fbm2(m.w, m.h, 5, seed + 5, 2)
    t = u * cracks + n1 * 1.2 + n2 * 0.2
    fi = np.floor(t)
    fr = t - fi
    yb = (Y + n1 * 14 + fi * 9.0) / 21.0
    hb = (yb - np.floor(yb)) < 0.07
    crack = (fr < 0.15) | hb
    if p.L[0] < 0:
        lit = (fr >= 0.15) & (fr < 0.27) & ~hb
    else:
        lit = (fr > 0.87) & ~hb
    v = v + n2 * 0.09
    v = np.where(lit, v + 0.16, v)
    v = np.where(crack, v * 0.28, v)
    cols = [mul(p.bark[0], 0.7), p.bark[0], p.bark[1], p.bark[2], p.bark[3]]
    c.paint_values(m, cols, v, band=0.3)
    # (moss low on the dark side; in winter snow in the cracks on the side of the wind)
    dark_side = nx * (1 if p.L[0] < 0 else -1) > 0.55
    low = Y > m.y0 + m.h * 0.45
    if p.s != WINTER:
        mc = {SPRING: hx('#6f9a44'), SUMMER: hx('#4a7232'), AUTUMN: hx('#6a6e34')}[p.s]
        mm = sub(m, dark_side & low & (n1 > 0.12) & ~crack)
        c.speckle(mm, [mc, mc, mix(mc, WHITE, 0.25)], 0.3, seed=seed + 9)
    else:
        c.paint(sub(m, (nx < -0.6) & crack & (n2 > 0.1)), p.snow[1])
    return m


# ---------------------------------------------------------------- near leaves
def leaf_ramp(p, hv):
    if p.s == AUTUMN:
        if hv > 0.62:
            return AUT_R
        if hv < 0.24:
            return AUT_Y
    return list(p.leaf)


def clump(c, p, cx, cy, r, hv=0.5, seed=0, dark=False):
    """a near clump of leaves: a lit ball with leaves at its rim, leaf marks inside, a shadow on what is behind"""
    cols = leaf_ramp(p, hv)
    if dark:
        cols = [mul(cols[0], 0.8)] + cols[:-1]
    rng = RNG(seed)
    lx, ly = ldir(p)
    parts = [m_ellipse(cx, cy, r, r * 0.82)]
    n = max(7, int(r * 1.0))
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uni(-0.25, 0.25)
        rr = rng.uni(0.86, 1.06)
        parts.append(m_ellipse(cx + math.cos(a) * r * rr, cy + math.sin(a) * r * 0.82 * rr, rng.uni(2.2, 3.8),
                               rng.uni(1.2, 2.0), rot=a + rng.uni(-0.7, 0.7)))
    full = m_union(parts)
    c.paint(full.shift(-lx * 2, -ly * 2 + 1), mul(cols[0], 0.78))
    c.shade_sphere(full, cx, cy, r * 1.05, r * 0.9, cols, p.L, amb=0.14, band=0.3)
    nm = int(r * r * 0.09) + 3
    for k in range(nm):
        a = rng.uni(0, 2 * math.pi)
        d = math.sqrt(rng()) * r * 0.85
        x = cx + math.cos(a) * d
        y = cy + math.sin(a) * d * 0.82
        lit = (math.cos(a) * p.L[0] + math.sin(a) * p.L[1]) * d / r
        col = cols[3] if lit > 0.2 else (cols[0] if lit < -0.15 else cols[1])
        c.paint(m_ellipse(x, y, rng.uni(1.4, 2.5), rng.uni(0.7, 1.1), rot=rng.uni(0, math.pi)), col)
    return full


FLOWER_S = ['.P.', 'PCP', '.S.']
FLOWER_B = ['.PP.', 'PCCP', 'PCCS', '.SS.']


def blossoms(c, p, cx, cy, r, n, seed):
    rng = RNG(seed)
    for k in range(n):
        a = rng.uni(0, 2 * math.pi)
        d = math.sqrt(rng()) * r * 0.92
        x = int(cx + math.cos(a) * d)
        y = int(cy + math.sin(a) * d * 0.8 - r * 0.12)
        pet = p.blossom[2] if rng() < 0.55 else WHITE
        heart = p.blossom[0] if rng() < 0.6 else hx('#ffe36b')
        rows = FLOWER_B if rng() < 0.5 else FLOWER_S
        sprite(c, x - 1, y - 1, rows, {'P': pet, 'C': heart, 'S': p.blossom[1]})


DENS = {SPRING: 0.86, SUMMER: 1.0, AUTUMN: 0.55, WINTER: 0.0}


def leaves_list(c, p, clumps, front, seed=0, dens=None):
    """clumps: (cx, cy, r, th, hv, front); a clump shows when th < the density of the season"""
    if dens is None:
        dens = DENS[p.s]
    for i, (cx, cy, r, th, hv, fr) in enumerate(clumps):
        if fr != front or th >= dens:
            continue
        if p.s == AUTUMN:
            r = r * 0.9
        clump(c, p, cx, cy, r, hv, seed + i * 13, dark=not front)
        if p.s == SPRING and hv > 0.12:
            blossoms(c, p, cx, cy, r, int(r * (0.9 if front else 0.5)), seed + i * 7 + 1)


def vine_leaf(c, p, x, y, R, ang, cols, seed=0):
    """a vine leaf with five lobes: its stem point at (x, y), its tip in the direction ang; cols = a ramp of 5"""
    cx, cy = x + math.cos(ang) * R * 0.95, y + math.sin(ang) * R * 0.95
    pts = []
    for k in range(36):
        th = 2 * math.pi * k / 36
        rr = R * (0.66 + 0.34 * abs(math.cos(2.5 * th)))
        dth = (th - math.pi + math.pi) % (2 * math.pi) - math.pi
        rr *= 1 - 0.5 * math.exp(-(dth / 0.32) ** 2)
        pts.append((cx + math.cos(th + ang) * rr, cy + math.sin(th + ang) * rr))
    m = m_poly(pts)
    lx, ly = ldir(p)
    c.paint(m.shift(-lx, -ly + 1) - m, mul(cols[0], 0.8))
    c.paint(m, cols[2])
    # (the half of the leaf that faces the light)
    nxv, nyv = -math.sin(ang), math.cos(ang)
    side = 1 if (nxv * p.L[0] + nyv * p.L[1]) > 0 else -1
    ux, uy = math.cos(ang), math.sin(ang)
    half = m_poly([(cx - ux * R * 2, cy - uy * R * 2), (cx + ux * R * 2, cy + uy * R * 2),
                   (cx + ux * R * 2 + nxv * side * R * 2, cy + uy * R * 2 + nyv * side * R * 2),
                   (cx - ux * R * 2 + nxv * side * R * 2, cy - uy * R * 2 + nyv * side * R * 2)]) & m
    c.paint(half, cols[3])
    c.paint(m.side(lx, ly, 1), cols[4])
    c.paint(m.side(-lx, -ly, 1), cols[1])
    # the veins: from the stem point to the five lobes
    sx, sy = x + ux * R * 0.45, y + uy * R * 0.45
    for th in (0, 1.257, -1.257, 2.4, -2.4):
        tx = cx + math.cos(th + ang) * R * 0.8
        ty = cy + math.sin(th + ang) * R * 0.8
        c.paint(m_line(sx, sy, tx, ty, 1) & m, cols[1])
    c.paint(m_line(x, y, sx, sy, 1), cols[0])
    return m


# ---------------------------------------------------------------- the near ground
def turf(c, p, m, seed, y_far, y_near, n=900, hmax=9):
    """near grass on the mask m: patches of the grass ramp, then blades (short far, long near)"""
    nz = fbm2(m.w, m.h, 20, seed, 3)
    X, Y = m.grid()
    f = np.clip((Y - y_far) / max(1, y_near - y_far), 0, 1)
    v = 0.52 + nz * 0.5 - f * 0.12
    c.paint_values(m, p.grassr[:4], v, band=0.35)
    rng = np.random.default_rng(seed)
    xs, ys = m.coords()
    if len(xs) == 0:
        return
    pick = rng.integers(0, len(xs), n)
    hs = rng.random(n)
    lean = rng.uniform(-0.45, 0.45, n)
    tone = rng.random(n)
    lit = p.L[0]
    for k in range(n):
        x, y = xs[pick[k]], ys[pick[k]]
        ff = clamp((y - y_far) / max(1, y_near - y_far), 0, 1)
        h = 2 + (hmax * (0.35 + 0.65 * ff)) * (0.4 + 0.6 * hs[k])
        tx, ty = x + lean[k] * h, y - h
        mx, my = x + lean[k] * h * 0.35, y - h * 0.55
        side = lean[k] * (-1 if lit < 0 else 1)
        col = p.grassr[3] if (tone[k] > 0.55 or side > 0.2) else p.grassr[0]
        if tone[k] > 0.94:
            col = p.grassr[4]
        c.paint(m_polyline([(x, y), (mx, my), (tx, ty)], 1), col)


def snowfield(c, p, m, seed, y_far, y_near, stalks=40):
    """a near field of snow: soft drifts (the snow ramp), blue hollows, glints, dry stalks"""
    nz = fbm2(m.w, m.h, 26, seed, 3)
    X, Y = m.grid()
    f = np.clip((Y - y_far) / max(1, y_near - y_far), 0, 1)
    v = 0.66 + nz * 0.45 + (0.5 - f) * 0.1
    c.paint_values(m, p.snow, v, band=0.4)
    c.paint(m.top(1), WHITE)
    rng = np.random.default_rng(seed + 3)
    xs, ys = m.coords()
    if len(xs) == 0:
        return
    for k in rng.integers(0, len(xs), len(xs) // 160):
        c.px(xs[k], ys[k], WHITE)
    for k in rng.integers(0, len(xs), stalks):
        x, y = xs[k], ys[k]
        h = 3 + rng.random() * 7
        lean = rng.uniform(-0.4, 0.4)
        c.paint(m_line(x, y, x + lean * h, y - h, 1), hx('#a8956e') if rng.random() < 0.6 else hx('#7a6a50'))


def litter(c, p, m, n, seed):
    """things on the ground of the season: petals (spring), fallen leaves (autumn), a few seeds (summer)"""
    rng = RNG(seed)
    xs, ys = m.coords()
    if len(xs) == 0 or p.s == WINTER:
        return
    for k in range(n):
        i = int(rng() * len(xs))
        x, y = int(xs[i]), int(ys[i])
        if p.s == SPRING:
            col = p.blossom[2] if rng() < 0.6 else WHITE
            c.px(x, y, col)
            c.px(x + 1, y, col)
            c.px(x, y + 1, p.blossom[1])
        elif p.s == AUTUMN:
            col = rng.pick([AUT_R[2], AUT_R[3], AUT_Y[2], AUT_Y[1], hx('#a3542a')])
            c.paint(m_ellipse(x, y, rng.uni(1.6, 2.6), rng.uni(0.9, 1.4), rot=rng.uni(0, 3)), col)
            c.px(x, y, mul(col, 0.7))
        elif rng() < 0.3:
            c.px(x, y, p.grassr[4])


# ---------------------------------------------------------------- wood: boards, a rope
def boards(c, p, x0, y0, x1, y1, bw, cols, seed, clip=None, nails=True, knots=True):
    """vertical boards from x0 to x1 (about bw px each); cols = a ramp of 4 or 5 (dark .. light): a gap on one side, a
    lit edge on the other, grain lines, knots, nails"""
    rng = RNG(seed)
    lx, _ = ldir(p)
    x = x0
    out = []
    while x < x1 - 1:
        w = min(bw + rng.int(-2, 2), x1 - x)
        bm = m_rect(x, y0, w, y1 - y0)
        if clip is not None:
            bm = bm & clip
        if bm.empty():
            x += w
            continue
        tone = rng()
        c.paint(bm, cols[2] if tone > 0.25 else mix(cols[2], cols[1], 0.35))
        for g in range(max(1, w // 5)):
            gx = x + rng.uni(2, w - 2)
            ph = rng.uni(0, 6)
            pts = [(gx + math.sin(yy * 0.07 + ph) * 1.2, yy) for yy in range(int(y0), int(y1) + 1, 6)]
            c.paint_dither(m_polyline(pts, 1) & bm, cols[1], 0.75)
        if knots and rng() < 0.45:
            ky = rng.uni(y0 + 6, y1 - 6)
            kx = x + rng.uni(3, w - 3)
            c.paint(m_ellipse(kx, ky, 2.2, 3.2) & bm, cols[1])
            c.paint(m_ellipse(kx, ky, 1.0, 1.6) & bm, cols[0])
        c.paint(m_rect(x + (w - 1 if lx <= 0 else 0), y0, 1, y1 - y0) & bm, cols[0])
        c.paint(m_rect(x + (0 if lx <= 0 else w - 1), y0, 1, y1 - y0) & bm, cols[-1])
        if nails:
            for ny in (y0 + 5, y1 - 7):
                nx_ = x + w // 2
                c.paint(m_rect(nx_, ny, 2, 2) & bm, hx('#3a3a40'))
                c.px(nx_, ny, hx('#9a9aa4'))
                if p.s != WINTER:
                    c.paint_dither(m_rect(nx_, ny + 2, 2, 4) & bm, hx('#8a4a2a'), 0.4)
        out.append(bm)
        x += w
    return out


def rope(c, p, pts, w=3.0):
    """a near rope: an outline, a lit rim, twist marks"""
    m = m_polyline(pts, w)
    lx, ly = ldir(p)
    c.outline(m, mul(p.rope[0], 0.6))
    c.paint(m, p.rope[1])
    c.paint(m.side(lx, ly, 1), p.rope[2])
    acc = 0.0
    for i in range(len(pts) - 1):
        ax, ay = pts[i]
        bx, by = pts[i + 1]
        L = math.hypot(bx - ax, by - ay)
        if L < 0.5:
            continue
        ux, uy = (bx - ax) / L, (by - ay) / L
        t = 3.0 - acc
        while t < L:
            x, y = ax + ux * t, ay + uy * t
            c.paint(m_line(x - uy * w * 0.5 - ux, y + ux * w * 0.5 - uy, x + uy * w * 0.5 + ux, y - ux * w * 0.5 + uy, 1)
                    & m, p.rope[0])
            t += 3.0
        acc = (acc + L) % 3.0
    return m


def far_tree(c, p, x, y, seed=12):
    """the big tree of the valley far away on the top of its hill (its foot at x, y), with its tire swing"""
    s = p.s
    c.paint(m_line(x, y, x, y - 16, 3), p.bark[1])
    c.paint(m_line(x, y - 10, x + 9, y - 18, 1.4), p.bark[1])
    c.paint(m_line(x, y - 12, x - 8, y - 20, 1.4), p.bark[1])
    if s == WINTER:
        for (a, b, cc, d) in [(x, y - 16, x - 4, y - 26), (x, y - 16, x + 5, y - 27), (x + 9, y - 18, x + 13, y - 24),
                              (x - 8, y - 20, x - 12, y - 25)]:
            c.paint(m_line(a, b, cc, d, 1), p.bark[1])
        c.paint(m_rect(x - 9, y - 21, 3, 1), WHITE)
        c.paint(m_rect(x + 7, y - 19, 4, 1), WHITE)
    else:
        rng = RNG(seed)
        for k in range(9):
            cx = x + rng.uni(-14, 14)
            cy = y - 24 + rng.uni(-6, 5)
            cols = leaf_ramp(p, rng())
            m = m_ellipse(cx, cy, 6.5, 5.5)
            c.shade_sphere(m, cx, cy, 6.5, 5.5, cols, p.L, amb=0.2)
            if s == SPRING:
                c.px(cx - 1, cy - 2, p.blossom[2])
                c.px(cx + 2, cy, WHITE)
    c.paint(m_line(x + 10, y - 16, x + 10, y - 9, 1), hx('#e2cea0'))
    c.paint(m_ellipse(x + 10, y - 7, 1.8, 2.2), hx('#26262b'))


def robin(c, p, x, y, face=1):
    """a small robin on a branch (its feet at x, y)"""
    f = face
    c.paint(m_ellipse(x, y - 6, 6, 4.6), hx('#6a5644'))
    c.paint(m_ellipse(x + f * 2.5, y - 5, 3.6, 3.4), hx('#e0602c'))
    c.paint(m_disc(x + f * 4, y - 10, 3.4), hx('#6a5644'))
    c.paint(m_ellipse(x + f * 4.6, y - 8.6, 2.2, 1.8), hx('#e0602c'))
    c.px(x + f * 5, y - 11, hx('#141418'))
    c.px(x + f * 7.5, y - 10, hx('#3a2a1a'))
    c.px(x + f * 8.5, y - 10, hx('#3a2a1a'))
    c.paint(m_line(x - f * 5, y - 7, x - f * 10, y - 4, 2), hx('#4f3f32'))
    c.paint(m_ellipse(x - f * 1, y - 8, 3, 1.6), hx('#8a7258'))
    c.px(x - 1, y, hx('#3a2a1a'))
    c.px(x + 1, y, hx('#3a2a1a'))
    if p.s == WINTER:
        c.px(x + f * 3, y - 13, WHITE)
