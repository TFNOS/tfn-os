# FOUR SEASONS: THE VINES (a close view): the vineyard on the slope of the valley, near: the front row with its
# posts, wires, old twisted vine trunks, canes and leaves; two rows behind; up the hill the big tree. An old iron key
# hangs on the upper wire: spring and summer leaves cover it; in autumn the leaves fell and it hangs clear; in winter
# snow covers it (a lump of snow only).
import math
import numpy as np
from fsart import Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline, m_union
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fsparts_c as C

W, H = 672, 180
VARS = {'key': [1, 0]}
HOT = {
    'key': (368, 40, 64, 84),
    'vines': (150, 44, 160, 100),
}

POSTS = [34, 286, 538]
WIRE_UP, WIRE_LO = 58, 102
SOIL = 166
KX, KY = 402, 62                    # the place where the key hangs on the upper wire
TRUNKS = [(96, 0), (192, 1), (360, 2), (468, 3), (618, 4)]


def wire_y(x, y0, sag=4.0):
    for a, b in zip([-260] + POSTS, POSTS + [W + 260]):
        if a <= x <= b:
            f = (x - a) / (b - a)
            return y0 + sag * math.sin(math.pi * f)
    return y0


def hill_y(x):
    return 96 - 58 * math.exp(-((x - 560) / 190) ** 2) - 3 * math.sin(x * 0.02)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    C.sky(c, p, 110, nclouds=5, seed=91)
    P.mountains(c, p, 92, 30, 0.007, 14)
    P.far_ridge(c, p, 100, 8, 0.012, 15, step=8, tree_h=(3, 6))
    # the hill of the big tree, up to the right
    hm = m_poly([(-4, H + 4)] + [(x, hill_y(x)) for x in range(-4, W + 6, 3)] + [(W + 4, H + 4)])
    c.paint(hm, p.hill)
    sdir = 1 if p.L[0] > 0 else -1

    def face(x):
        return -(hill_y(x + 2) - hill_y(x - 2)) / 4 * -sdir
    P.grass_marks(c, p, hm, 1500, 33, face_fn=face)
    big_tree(c, p, s)
    # the two rows behind (small, far), then the front row
    back_row(c, p, s, 126, 0.34, 5)
    back_row(c, p, s, 150, 0.5, 6)
    front_ground(c, p, s)
    rose(c, p, s)
    front_row(c, p, s, st)
    P.particles_baked(c, p, (0, 0, W, H), 30, 93)
    return c


def big_tree(c, p, s):
    """the big tree of the valley on the top of the hill (far)"""
    C.far_tree(c, p, 566, hill_y(566) + 2)


def back_row(c, p, s, yb, k, seed):
    """a row of vines behind: posts, a wire, a band of leaves (the size k of the front row)"""
    rng = RNG(seed)
    ph = 110 * k
    dens = {SPRING: 0.7, SUMMER: 1.0, AUTUMN: 0.35, WINTER: 0.0}[s]
    soil = m_rect(0, yb - 2, W, 5 * k + 3)
    c.paint(soil, mix(p.soil, p.hill, 0.35) if s != WINTER else p.snow[1])
    c.paint(soil.bottom(1), mul(p.soil, 0.8) if s != WINTER else p.snow[0])
    x = -10 + rng.uni(0, 30)
    while x < W + 10:
        c.paint(m_rect(x, yb - ph, 3 * k + 1, ph), p.woodr[2])
        c.paint(m_rect(x, yb - ph, 1, ph), p.woodr[3])
        if s == WINTER:
            c.paint(m_rect(x - 1, yb - ph - 1, 3 * k + 3, 2), (255, 255, 255))
        x += 250 * k + rng.uni(-6, 6)
    for wy in (0.62, 0.32):
        c.paint(m_line(0, yb - ph * wy, W, yb - ph * wy, 1), mix(p.woodD, p.hill, 0.45))
    if dens <= 0:
        for x in range(4, W, int(40 * k) + 3):
            c.paint(m_line(x, yb, x + 2, yb - ph * 0.35, 1.2), p.woodr[1])
            c.paint(m_line(x + 2, yb - ph * 0.35, x + 8 * k, yb - ph * 0.5, 1), p.woodr[1])
            c.px(x + 2, yb - ph * 0.35 - 1, (255, 255, 255))
        return
    # (the trunks of the row)
    x = rng.uni(0, 40)
    while x < W:
        c.paint(m_line(x, yb, x + rng.uni(-2, 2), yb - ph * 0.36, max(1.0, 5 * k)), p.bark[1])
        x += rng.uni(70, 110) * k
    for i in range(int(W / (7 * k))):
        x = i * 7 * k + rng.uni(-2, 2)
        if rng() > dens:
            continue
        y = yb - ph * rng.uni(0.36, 0.85)
        r = rng.uni(4, 7) * k * 1.6
        hv = rng()
        cols = C.leaf_ramp(p, hv) if s == AUTUMN else [mul(p.vine, 0.6), mul(p.vine, 0.8), p.vine,
                                                        mix(p.vine, (255, 255, 255), 0.3), (255, 255, 255)]
        if s == AUTUMN:
            cols = [mix(cc, p.vine, 0.3) for cc in cols]
        m = m_ellipse(x, y, r, r * 0.75)
        c.shade_sphere(m, x, y, r, r * 0.75, cols[:4], p.L, amb=0.25)
        if s == SUMMER and rng() < 0.3:
            c.paint(m_rect(x, y + r * 0.5, 2 * k + 1, 3 * k + 1), hx('#5d2f86'))


def front_ground(c, p, s):
    """the soil under the front row and the grass in front"""
    gm = m_poly([(-4, H + 4)] + [(x, SOIL - 4 + 2 * math.sin(x * 0.03)) for x in range(-4, W + 6, 4)] + [(W + 4, H + 4)])
    if s == WINTER:
        C.snowfield(c, p, gm, 81, SOIL - 4, H, stalks=60)
        return
    nz = C.fbm2(gm.w, gm.h, 8, 83, 2)
    c.paint_values(gm, p.soilr, np.clip(0.5 + nz * 0.6, 0, 1), 0.4)
    rng = RNG(84)
    for k in range(70):
        x = rng.uni(0, W)
        y = rng.uni(SOIL, H)
        c.paint(m_ellipse(x, y, rng.uni(1.5, 3), rng.uni(1, 1.6)), p.soilr[rng.int(2, 3)])
        c.px(x, y + 1, p.soilr[0])
    front = m_poly([(-4, H + 4)] + [(x, 174 + 2 * math.sin(x * 0.07)) for x in range(-4, W + 6, 4)] + [(W + 4, H + 4)])
    C.turf(c, p, front, 85, 172, H, n=400, hmax=6)
    C.litter(c, p, gm, 40 if s != AUTUMN else 10, 86)
    if s == AUTUMN:
        fallen(c, p, gm)


def fallen(c, p, m):
    """autumn: the fallen vine leaves on the soil under the row"""
    rng = RNG(87)
    xs, ys = m.coords()
    for k in range(70):
        i = int(rng() * len(xs))
        x, y = xs[i], ys[i]
        cols = C.AUT_Y if rng() < 0.6 else C.AUT_R
        cols = [mix(cc, hx('#8a6a3a'), 0.25) for cc in cols]
        C.vine_leaf(c, p, x, y, rng.uni(3.4, 5.2), rng.uni(0, 2 * math.pi), cols)


def rose(c, p, s):
    """the rose bush at the end of the row (as in old vineyards): roses, hips in autumn, snow in winter"""
    x, y = 16, SOIL + 2
    rng = RNG(95)
    for k in range(7):
        a = -math.pi / 2 + rng.uni(-0.8, 0.8)
        L = rng.uni(18, 34)
        c.paint(m_line(x + k * 2, y, x + k * 2 + math.cos(a) * L, y + math.sin(a) * L, 1.4), hx('#4a5a2a') if s != WINTER else hx('#5a4a3a'))
    if s == WINTER:
        c.paint(m_ellipse(x + 6, y - 2, 14, 3), (255, 255, 255))
        for k in range(4):
            c.paint(m_ellipse(x + rng.uni(-6, 18), y - rng.uni(10, 28), 2.2, 1), (255, 255, 255))
        return
    for k in range(10):
        bx, by = x + rng.uni(-8, 22), y - rng.uni(6, 34)
        cols = p.leaf if s != AUTUMN else [mix(cc, hx('#8a6a2a'), 0.4) for cc in p.leaf]
        m = m_ellipse(bx, by, 6, 4.6)
        c.shade_sphere(m, bx, by, 6, 4.6, cols, p.L, amb=0.25)
    for k in range(6):
        bx, by = x + rng.uni(-6, 20), y - rng.uni(10, 34)
        if s == SPRING and k < 3:
            c.paint(m_disc(bx, by, 2.4), hx('#ff7a9a'))
            c.px(bx, by, hx('#c8304a'))
        elif s == SUMMER:
            c.paint(m_disc(bx, by, 3.2), hx('#d82a3a'))
            c.paint(m_disc(bx - 0.5, by - 0.5, 1.6), hx('#ff6a6a'))
            c.px(bx + 1, by + 1, hx('#8a1020'))
        elif s == AUTUMN:
            c.paint(m_ellipse(bx, by, 1.8, 2.2), hx('#d8401a'))
            c.px(bx - 0.5, by - 1, hx('#ffa060'))


VLEAF = None


def leaf_cols(p, s, hv):
    if s == AUTUMN:
        return C.AUT_Y if hv < 0.55 else C.AUT_R
    if s == SPRING:
        return [mul(p.vine, 0.62), mul(p.vine, 0.8), p.vine, mix(p.vine, (255, 255, 255), 0.3), mix(p.vine, (255, 255, 255), 0.6)]
    return [mul(p.vine, 0.55), mul(p.vine, 0.75), p.vine, mix(p.vine, hx('#c8f080'), 0.45), mix(p.vine, (255, 255, 255), 0.5)]


def front_row(c, p, s, st):
    """the front row: the posts, the old trunks, the cordons on the lower wire, the canes, the wires, the leaves, the
    grapes and the key"""
    lx, ly = C.ldir(p)
    # the posts
    for i, x in enumerate(POSTS):
        pm = m_rect(x - 5, 22 + i % 2 * 3, 10, SOIL + 6 - 22 - i % 2 * 3)
        c.outline(pm, mul(p.woodD, 0.7))
        C.boards(c, p, x - 5, 22 + i % 2 * 3, x + 5, SOIL + 6, 10, p.woodr, 600 + i, nails=False)
        c.paint(m_rect(x - 5, 22 + i % 2 * 3, 10, 2), p.woodr[1])
        if s == WINTER:
            c.paint(m_ellipse(x, 21 + i % 2 * 3, 7, 2.6), (255, 255, 255))
    # the tin plate with the number of the row on the middle post
    c.paint(m_rect(POSTS[1] - 4, 120, 8, 9), p.metal[2])
    c.paint(m_rect(POSTS[1] - 4, 120, 8, 1), p.metal[3])
    C.sprite(c, POSTS[1] - 2, 122, ['XXX', '..X', '.X.', '.X.', '.X.'], {'X': hx('#2a2a30')})
    # the wires
    wire_c = [hx('#5a5a62'), hx('#9a9aa4')]
    for wy in (WIRE_UP, WIRE_LO):
        pts = [(x, wire_y(x, wy)) for x in range(-4, W + 6, 6)]
        c.paint(m_polyline(pts, 1), wire_c[0])
        c.paint(m_polyline([(x, y - 1) for x, y in pts], 1) & m_rect(0, 0, W, H), wire_c[0])
        if s == WINTER:
            c.paint(m_polyline([(x, y - 2) for x, y in pts], 1.4), (255, 255, 255))
    canes = []
    rng = RNG(400)
    for (tx, seed) in TRUNKS:
        r2 = RNG(410 + seed)
        # the old trunk: twisted, from the soil to the lower wire, then two cordons on the wire
        amp = r2.uni(2, 8)
        fq = r2.uni(0.7, 1.7)
        lean = r2.uni(-8, 8)
        w0 = r2.uni(9, 14)
        pts = [(tx - lean + r2.uni(-3, 3), SOIL + 4)]
        for k in range(1, 7):
            f = k / 6
            pts.append((tx - lean * (1 - f) + math.sin(k * fq + seed) * amp * (1 - f * 0.6) + r2.uni(-1.5, 1.5),
                        SOIL + 4 - f * (SOIL + 4 - WIRE_LO - 2)))
        C.limb(c, p, pts, w0, w0 * 0.6, seed=seed)
        # a knot, and the dark scar of an old cut arm
        kx, ky, _, _ = C.along(pts, w0, w0, r2.uni(0.3, 0.7))
        km = m_ellipse(kx + r2.uni(-2, 2), ky, w0 * 0.55, w0 * 0.4)
        c.paint(km, p.bark[2])
        c.paint(km.side(*C.ldir(p), 1), p.bark[3])
        c.paint(m_ellipse(km.x0 + km.w / 2, ky + 0.5, w0 * 0.22, w0 * 0.16), p.bark[0])
        sx_, sy_, _, _ = C.along(pts, w0, w0, r2.uni(0.55, 0.85))
        sd = 1 if r2() < 0.5 else -1
        C.limb(c, p, [(sx_, sy_), (sx_ + sd * 7, sy_ - 4)], 5, 4, seed=seed + 50)
        c.paint(m_ellipse(sx_ + sd * 8, sy_ - 4.5, 2.4, 2.4), p.woodr[3])
        c.paint(m_ellipse(sx_ + sd * 8, sy_ - 4.5, 1.2, 1.2), p.woodr[1])
        for d in (-1, 1):
            cp = [(tx, wire_y(tx, WIRE_LO) - 1)]
            for k in range(1, 6):
                xx = tx + d * k * 13
                cp.append((xx, wire_y(xx, WIRE_LO) - 2 - math.sin(k + seed) * 1.5))
            C.limb(c, p, cp, 6, 3.5, seed=seed * 7 + d)
            for k in range(1, 6, 2 if s == WINTER else 1):
                x0, y0 = cp[k]
                canes.append((x0, y0, d, r2.uni(-0.25, 0.25), r2.uni(46, 64)))
    # the canes (one year old wood): up past the upper wire; in winter cut short (spurs)
    cane_c = {SPRING: hx('#8a6a3a'), SUMMER: hx('#7a6a34'), AUTUMN: hx('#a8783a'), WINTER: hx('#6a4a2a')}[s]
    for (x0, y0, d, a, L) in canes:
        if s == WINTER:
            c.paint(m_line(x0, y0, x0 + d * 3, y0 - 8, 2), cane_c)
            c.paint(m_line(x0 + d * 3, y0 - 9, x0 + d * 4, y0 - 10, 2), (255, 255, 255))
            continue
        x1, y1 = x0 + d * 6 + a * L, y0 - L
        c.paint(m_polyline([(x0, y0), (x0 + d * 3 + a * L * 0.5, y0 - L * 0.5), (x1, y1)], 1.6), cane_c)
        # a tendril
        c.paint(m_polyline([(x1 - 2, y1 + 10), (x1 - 6, y1 + 6), (x1 - 4, y1 + 3), (x1 - 7, y1 + 1)], 1), mix(cane_c, p.vine, 0.6))
    if st.get('key', 1):
        key(c, p, s)
    # the leaves along the canes and the cordons
    dens = {SPRING: 0.85, SUMMER: 1.0, AUTUMN: 0.16, WINTER: 0.0}[s]
    rr = {SPRING: 0.74, SUMMER: 1.0, AUTUMN: 0.85, WINTER: 1.0}[s]
    leaves = []
    rng = RNG(450)
    for (x0, y0, d, a, L) in canes:
        for k in range(5):
            t = rng.uni(0.15, 1.0)
            x = x0 + d * 3 * t + a * L * t + rng.uni(-3, 3)
            y = y0 - L * t + rng.uni(-2, 4)
            R, ang, hv, th = rng.uni(9, 13), rng.uni(0, 2 * math.pi), rng(), rng()
            if s == AUTUMN and abs(x - KX) < 28 and y < WIRE_UP + 66:
                continue                                     # the key hangs clear in autumn
            leaves.append((x, y, R, ang, hv, th))
    # (the leaves that cover the key in spring and summer)
    for (x, y, R, ang) in [(394, 64, 12, 2.3), (410, 66, 12, 0.6), (398, 80, 13, 2.0), (412, 84, 12, 1.0),
                           (396, 96, 12, 2.6), (408, 102, 12, 1.3), (402, 112, 11, 1.6), (388, 82, 10, 3.0),
                           (418, 74, 10, 0.2), (402, 58, 10, 1.8), (390, 104, 10, 2.8), (416, 98, 10, 0.4),
                           (403, 88, 12, 1.5), (400, 72, 11, 2.2), (388, 60, 10, 0.1), (390, 116, 10, -0.3),
                           (410, 112, 9, 2.6)]:
        leaves.append((x, y, R / rr if s == SPRING else R, ang, 0.5, 0.5 if s != AUTUMN else 1.5))
    leaves.sort(key=lambda q: q[1])
    for (x, y, R, ang, hv, th) in leaves:
        if th >= dens:
            continue
        C.vine_leaf(c, p, x, y, R * rr, ang, leaf_cols(p, s, hv))
    grapes(c, p, s)


def grapes(c, p, s):
    """the bunches under the cordons: green flowers (spring), purple bunches (summer), a few dry ones (autumn)"""
    if s == WINTER:
        return
    rng = RNG(470)
    ripe = [hx('#2a1440'), hx('#4a2468'), hx('#6a3a94'), hx('#a080c8')]
    green = [hx('#4a6a20'), hx('#7a9a34'), hx('#a8c850'), hx('#e0f0a0')]
    dry = [hx('#2a1420'), hx('#3a2030'), hx('#5a3448'), hx('#8a6070')]
    for (tx, seed) in TRUNKS:
        for d in (-1, 1):
            for k in range(1, 6):
                if rng() < 0.55:
                    continue
                x = tx + d * k * 13 + rng.uni(-5, 5)
                if abs(x - KX) < 24:
                    continue
                y = wire_y(x, WIRE_LO) + rng.uni(1, 5)
                if s == SPRING:
                    for j in range(6):
                        c.px(x + rng.uni(-2, 2), y + j * 1.4, hx('#c8e070') if j % 2 else hx('#a8d050'))
                    continue
                size = rng.int(3, 6)
                cols = (ripe if rng() < 0.75 else green) if s == SUMMER else dry
                if s == AUTUMN and rng() < 0.55:
                    continue
                if s == AUTUMN:
                    size = min(size, 3)
                c.paint(m_line(x, y - 3, x + rng.uni(-2, 2), y + 1, 1), hx('#6a5a2a'))
                for row in range(size):
                    n = max(1, 4 - row * 3 // size)
                    for j in range(n):
                        if rng() < 0.12:
                            continue
                        bx = x + (j - (n - 1) / 2) * 3.4 + (row % 2) * 0.8
                        by = y + 3 + row * 3
                        m = m_disc(bx, by, 1.9)
                        c.shade_sphere(m, bx, by, 1.9, 1.9, cols, p.L, amb=0.3)


def key(c, p, s):
    """the old iron key on a loop of twine on the upper wire; in winter a lump of snow covers it"""
    wy = wire_y(KX, WIRE_UP)
    if s == WINTER:
        lump = m_union([m_ellipse(KX, wy + 18, 9, 16), m_ellipse(KX - 1, wy + 6, 7, 6), m_ellipse(KX + 1, wy + 32, 6, 6)])
        c.outline(lump, p.snow[0])
        c.shade_sphere(lump, KX, wy + 18, 9, 22, p.snow, p.L, amb=0.4)
        c.paint(lump.top(1), (255, 255, 255))
        for k in range(3):
            c.paint(m_rect(KX - 4 + k * 4, wy + 38, 1, 2 + k), hx('#dff0ff'))
        return
    twine = hx('#c8b080')
    c.paint(m_polyline([(KX - 3, wy - 1), (KX - 4, wy + 3), (KX, wy + 7), (KX + 4, wy + 3), (KX + 3, wy - 1)], 1), twine)
    c.paint(m_line(KX, wy + 7, KX, wy + 10, 1), twine)
    iron = p.iron
    rust = hx('#8a4a24')
    by = wy + 18
    bow = m_disc(KX, by, 8.5) - m_disc(KX, by, 4)
    bow = bow | m_disc(KX - 6, by - 3, 3) | m_disc(KX + 6, by - 3, 3) | m_disc(KX, by - 8, 3)
    bow = bow - m_disc(KX, by, 4)
    shank = m_rect(KX - 2, by + 7, 4, 28)
    collar = m_rect(KX - 3.5, by + 10, 7, 3)
    bit = m_poly([(KX + 2, by + 25), (KX + 12, by + 25), (KX + 12, by + 28), (KX + 9, by + 28), (KX + 9, by + 31),
                  (KX + 12, by + 31), (KX + 12, by + 35), (KX + 2, by + 35)])
    keym = bow | shank | collar | bit
    c.outline(keym, hx('#141418'))
    c.shade_sphere(keym, KX, by + 10, 14, 26, iron, p.L, amb=0.25)
    c.paint(keym.side(*C.ldir(p), 1), iron[3])
    c.speckle(keym, [rust, mix(rust, iron[1], 0.4)], 0.18, seed=97)
    c.paint(m_disc(KX, by, 4) - m_disc(KX, by, 3), iron[0])
    if s == AUTUMN:
        # the low sun catches the iron: a glint
        gx, gy = KX + 5, by - 4
        c.px(gx, gy, (255, 255, 255))
        c.paint(m_line(gx - 3, gy, gx + 3, gy, 1), mix(p.sun, (255, 255, 255), 0.6))
        c.paint(m_line(gx, gy - 3, gx, gy + 3, 1), mix(p.sun, (255, 255, 255), 0.6))
        c.px(gx, gy, (255, 255, 255))
