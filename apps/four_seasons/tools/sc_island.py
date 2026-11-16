# FOUR SEASONS: THE ISLAND (a close view): the weeping willow of the small island in the pond, near. A long red
# ribbon (the tail of the kite) is tied on a branch at the right. Spring and summer: the long green leaves hang to the
# ground and hide the ribbon; autumn: the leaves fell, the ribbon flutters in the wind, clear; winter: snow, the ribbon
# hangs frozen in a coat of ice (not clear). ribbon 1: on the willow, 0: taken.
import math
import numpy as np
from fsart import (Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline,
                   m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fswater as FW

W, H = 672, 180
OX, OY = 16, 4
WHITE = (255, 255, 255)

TX, TY = 318, 150                     # the foot of the willow
WILLOW = FW.WillowNear(TX, TY, 140, 196, seed=91, tw=17, bottom=TY - 6)


def _knot():
    """the knot of the ribbon: the point of a branch nearest to (TX + 104, 68)"""
    best = None
    for (pts, w0, w1, main) in WILLOW.branches:
        for (x, y) in pts[2:-3]:
            d = (x - TX - 104) ** 2 + (y - 68) ** 2
            if best is None or d < best[0]:
                best = (d, x, y)
    return int(best[1]), int(best[2])


KX, KY = _knot()

VARS = {'ribbon': [1, 0]}
HOT = {
    'ribbon': (KX - 26, KY - 18, 84, 76),
    'willow': (TX - 46, 96, 82, 62),
}


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    sun = (p.sun_xy[0] + OX, int(p.sun_xy[1] * 0.62) + 4)
    P.sky(c, p, 0, 92, sun)
    P.clouds(c, p, 4, 30, 6, 63)
    P.mountains(c, p, 66, 20, 0.008, 9)
    P.far_ridge(c, p, 74, 9, 0.012, 15, step=6, tree_h=(2, 5))
    before = c.rgb.copy()
    far_bank(c, p, s)
    src = FW.changed(c, before)
    src[96:, :] = False
    wm = m_rect(-4, 94, W + 8, H)
    wa = FW.full(c, wm)
    if s == WINTER:
        FW.ice(c, p, wm, 94, H, 11, n_crack=12, n_snow=12)
    else:
        FW.water(c, p, wm, 94, H, 19, sun_x=sun[0], n=200)
        FW.reflect(c, src, 96, wa, [mix(p.waterD, p.hillS, 0.3), mix(p.water, p.hillS, 0.32), mix(p.water, p.hillL, 0.3)],
                   ripple=3, wob=1, hmax=24)
        FW.bank_band(c, p, wm, lambda x: 95, 0, W, mix(p.hillS, p.waterD, 0.45), 2, 5)
    water_life(c, p, s, wm)
    island_ground(c, p, s)
    tree(c, p, s, st.get('ribbon', 1))
    front(c, p, s)
    return c


def far_bank(c, p, s):
    """the far bank: the hill of the big tree at the left, the path, a fence, the red shed at the right"""
    def yh(x):
        return 86 - 26 * math.exp(-((x - 90) / 150) ** 2) - 2 * math.sin(x * 0.03)
    hm = m_poly([(-4, 100)] + [(x, yh(x)) for x in range(-4, W + 6, 3)] + [(W + 4, 100)])
    c.paint(hm, mix(p.hill, p.ridge, 0.3))
    P.grass_marks(c, p, hm, 900, 81, hmin=1, hmax=2, cols=(mix(p.hillL, p.ridge, 0.25), mix(p.hillS, p.ridge, 0.25), p.grassL))
    if s == WINTER:
        c.paint(hm, p.snow[2])
        c.paint_dither(hm, p.snow[1], 0.18)
        c.paint(hm.top(1), WHITE)
    t = P.Tree(84, 62, seed=404, depth=5, length=7, width=3.0, crown=(21, 11), nclump=120, clump_r=(1.5, 3.0))
    t.draw_wood(c, p)
    t.draw_leaves(c, p, seed=3)
    path = m_polyline(bezier((96, 63), (160, 70), (300, 78), (420, 90), 20), 1.2, 2.6)
    c.paint(path, p.pathr[2] if s != WINTER else p.snow[1])
    # the red shed far at the right
    x0, gy = 560, 86
    c.paint(m_rect(x0, gy - 12, 22, 12), p.shed)
    for xx in range(x0 + 3, x0 + 22, 3):
        c.paint(m_rect(xx, gy - 11, 1, 11), mul(p.shed, 0.82))
    c.paint(m_poly([(x0 - 2, gy - 12), (x0 + 11, gy - 19), (x0 + 24, gy - 12)]), p.roof if s != WINTER else WHITE)
    c.paint(m_rect(x0 + 4, gy - 8, 5, 8), p.shedD)
    c.paint(m_rect(x0 + 13, gy - 9, 5, 4), mix(hx('#2c3a4a'), hx('#ffcf6a'), 0.8 if s == WINTER else 0.0))
    P.fence(c, p, 430, 93, 640, 91, 9, rail_dy=(-7, -3), post_h=9)
    for bx, br in [(250, 5), (520, 6), (650, 5)]:
        P.bush(c, p, bx, 94, br, seed=bx)
    # stones of the far waterline
    rng = RNG(83)
    x = 2
    while x < W:
        P.rock(c, p, x, 94, rng.uni(1.3, 2.4))
        x += rng.uni(8, 20)


def water_life(c, p, s, wm):
    pads = [(40, 118, 7, 1), (66, 128, 9, 0), (30, 150, 11, 1), (600, 112, 6, 0), (626, 124, 9, 1), (648, 146, 11, 0),
            (580, 136, 7, 1), (96, 104, 5, 0)]
    if s != WINTER:
        k = {SPRING: 5, SUMMER: 8, AUTUMN: 5}[s]
        for i, (x, y, r, fl) in enumerate(pads[:k]):
            FW.lily_pad(c, p, x, y, r * (0.75 if s == SPRING else 1.0), 200 + i, flower=bool(fl))
    n = {SPRING: 80, SUMMER: 20, AUTUMN: 160, WINTER: 0}[s]
    FW.floaters(c, p, wm, n, 87, front=1.5)
    if s == SPRING:
        # a duck with ducklings at the right
        for k, (dx, dy, sc) in enumerate([(540, 104, 1.2), (556, 106, 0.6), (566, 105, 0.6), (575, 107, 0.6)]):
            young = k > 0
            body = hx('#e8c84a') if young else hx('#8a6a4a')
            c.paint(m_rect(dx - 7 * sc, dy + 3 * sc, 14 * sc, 1), mul(p.waterD, 0.8))
            bm = m_ellipse(dx, dy, 6 * sc, 2.8 * sc)
            c.paint(bm, body)
            c.paint(bm.top(1), mix(body, WHITE, 0.3))
            c.paint(m_disc(dx + 4.5 * sc, dy - 4 * sc, 2 * sc + 0.3), body if young else hx('#2a7a4a'))
            c.paint(m_rect(dx + 6 * sc, dy - 4 * sc, 2, 1), hx('#f0a020'))
    elif s == AUTUMN:
        for k in range(6):
            gx, gy = 470 + k * 10, 20 + abs(k - 2) * 3
            c.paint(m_line(gx - 2, gy + 1, gx, gy, 1), hx('#4a3a4a'))
            c.paint(m_line(gx, gy, gx + 2, gy + 1, 1), hx('#4a3a4a'))


def island_ground(c, p, s):
    """the island: a low mound of earth with grass, stones at its edge, roots of the willow"""
    gm = m_ellipse(TX + 10, TY + 12, 250, 28)
    edge = m_ellipse(TX + 10, TY + 15, 254, 28)
    if s != WINTER:
        c.paint(m_ellipse(TX + 10, TY + 18, 258, 27), mul(p.waterD, 0.8))
    c.paint(edge, p.soilr[1] if s != WINTER else p.snow[1])
    c.paint(gm, p.hill if s != WINTER else p.snow[2])
    if s != WINTER:
        c.paint_values(gm, [p.hillS, p.hill], lambda x, y: np.clip((y - (TY - 16)) / 8, 0, 1), 0.4)
        P.grass_marks(c, p, gm, 1400, 89, hmin=1, hmax=4)
    else:
        c.paint_dither(gm, p.snow[1], 0.15)
        c.paint(gm.top(2), WHITE)
    # the shadow of the crown on the ground
    c.darken(m_ellipse(TX + 10 - p.L[0] * 30, TY + 2, 150, 9) & gm, 0.8, p.cool)
    rng = RNG(97)
    for k in range(22):
        a = math.pi * (0.03 + 0.94 * k / 21) + rng.uni(-0.03, 0.03)
        P.rock(c, p, TX + 10 + math.cos(a) * 248, TY + 14 + math.sin(a) * 25, rng.uni(2.6, 5.0))
    for k in range(6):
        a = math.pi * (1.05 + 0.9 * k / 5)
        P.rock(c, p, TX + 10 + math.cos(a) * 246, TY + 14 + math.sin(a) * 26, rng.uni(2.0, 3.4))
    FW.reeds(c, p, TX - 210, TY + 8, 7, 30, 99, spread=1.1, lean=-0.1)
    FW.reeds(c, p, TX + 236, TY + 6, 8, 34, 101, spread=1.1, lean=0.1)


def ribbon_shapes(s):
    """the masks of the ribbon: the bow, the two tails (with their light and dark parts)"""
    kx, ky = KX, KY
    out = []
    bow = m_ellipse(kx - 6, ky - 1, 6.4, 4.0, rot=0.3) | m_ellipse(kx + 6, ky - 1, 6.4, 4.0, rot=-0.3)
    hole = m_ellipse(kx - 6, ky - 1, 2.6, 1.4, rot=0.3) | m_ellipse(kx + 6, ky - 1, 2.6, 1.4, rot=-0.3)
    tails = []
    for i, (L, ph) in enumerate([(74, 0.0), (60, 1.7)]):
        pts = []
        for j in range(15):
            t = j / 14
            if s == AUTUMN:
                # (the wind blows the tails out to the right)
                x = kx + (2 if i else -2) + t * L * 0.86 + math.sin(t * 7 + ph) * 2.2 * t
                y = ky + 2 + t * L * 0.42 + math.sin(t * 9 + ph) * 4.5 * t
            elif s == WINTER:
                x = kx + (3 if i else -3) + t * 4 + math.sin(t * 2 + ph) * 1.2
                y = ky + 2 + t * L * 0.66
            else:
                x = kx + (3 if i else -3) + t * 6 + math.sin(t * 4 + ph) * 2
                y = ky + 2 + t * L * 0.8
            pts.append((x, y))
        tails.append(pts)
    return bow, hole, tails


def draw_ribbon(c, p, s):
    bow, hole, tails = ribbon_shapes(s)
    if s == WINTER:
        red = [mix(hx('#8a1820'), p.ice[1], 0.5), mix(hx('#c8303a'), p.ice[2], 0.5), mix(hx('#f07070'), p.ice[3], 0.5)]
    else:
        red = [hx('#7a1018'), hx('#d0202c'), hx('#ff6a5a')]
    allm = bow
    for i, pts in enumerate(tails):
        tm = m_polyline(pts, 5.4, 3.8)
        allm = allm | tm
        c.paint(tm, red[1])
        # (the twist of the ribbon: dark and light parts)
        for j in range(0, len(pts) - 1):
            if (j + i) % 4 == 0:
                c.paint(m_line(pts[j][0], pts[j][1], pts[j + 1][0], pts[j + 1][1], 3.2) & tm, red[0])
            elif (j + i) % 4 == 2:
                c.paint(m_line(pts[j][0], pts[j][1] - 1, pts[j + 1][0], pts[j + 1][1] - 1, 1) & tm, red[2])
        # the end of the tail is cut in a V
        ex, ey = pts[-1]
        c.paint(m_poly([(ex - 2, ey + 1), (ex + 2, ey + 1), (ex, ey - 1)]), red[0] if s != WINTER else p.ice[3])
    c.paint(bow, red[1])
    c.paint(bow.top(1), red[2])
    c.paint(bow.bottom(1), red[0])
    c.paint(hole, red[0])
    c.paint(m_rect(KX - 2, KY - 3, 4, 4), red[0])
    c.px(KX - 1, KY - 3, red[2])
    if s == WINTER:
        # a coat of ice over all of it, icicles at the ends, snow on the bow
        c.outline(allm, p.ice[3])
        c.paint_dither(allm, p.ice[3], 0.35)
        for pts in tails:
            ex, ey = pts[-1]
            c.paint(m_poly([(ex - 2, ey + 1), (ex + 2, ey + 1), (ex, ey + 7)]), p.ice[3])
            c.px(ex, ey + 2, p.ice[1])
        c.paint(m_ellipse(KX, KY - 4, 9, 2.2), WHITE)
    return allm


def tree(c, p, s, rib):
    w = WILLOW
    if s in (SPRING, SUMMER):
        w.back(c, p)
        w.wood(c, p)
        # (the leaves cover the place of the ribbon in both states: the hidden ribbon changes no pixel)
        bow, hole, tails = ribbon_shapes(s)
        cover = m_union([bow] + [m_polyline(pts, 5.4, 3.8) for pts in tails]).grow(3)
        if rib:
            draw_ribbon(c, p, s)
        w.front(c, p, cover=cover)
    elif s == AUTUMN:
        w.twigs(c, p, wind=0.1, leaves=0.05, layer='back')
        w.wood(c, p)
        w.twigs(c, p, wind=0.1, leaves=0.05)
        if rib:
            draw_ribbon(c, p, s)
    else:
        w.twigs(c, p, wind=0.0, layer='back')
        w.wood(c, p, snow=True)
        w.twigs(c, p, wind=0.0)
        if rib:
            draw_ribbon(c, p, s)


def front(c, p, s):
    """things on the island in front of the tree, the life of the season"""
    rng = RNG(103)
    gm = m_ellipse(TX + 10, TY + 12, 246, 25) - m_rect(TX - 60, TY - 20, 120, 24)
    if s == SPRING:
        # daffodils and small white flowers
        for k in range(14):
            x = rng.uni(TX - 220, TX + 230)
            if abs(x - TX) < 50:
                continue
            y = TY + 4 + rng.uni(0, 14)
            c.paint(m_line(x, y, x, y - 6, 1), p.lfD)
            c.paint(m_rect(x - 1, y - 8, 3, 2), hx('#ffe14d'))
            c.px(x, y - 7, hx('#f0a020'))
        P.flowers(c, p, gm, 60, 105)
    elif s == SUMMER:
        P.flowers(c, p, gm, 90, 107)
        # a kingfisher on a reed at the right
        kx, ky = TX + 232, TY - 22
        c.paint(m_ellipse(kx, ky, 3.4, 2.4), hx('#2a8ad8'))
        c.paint(m_ellipse(kx, ky + 1.5, 2.4, 1.2), hx('#f08a2a'))
        c.paint(m_disc(kx + 2.5, ky - 2.5, 1.8), hx('#2a7ac8'))
        c.paint(m_line(kx + 4, ky - 2.5, kx + 8, ky - 2, 1), hx('#2a2a32'))
        c.paint(m_line(kx - 3, ky + 1, kx - 6, ky + 3, 1), hx('#1a5a98'))
    elif s == AUTUMN:
        # fallen willow leaves (yellow) in heaps on the ground
        for k in range(260):
            x = rng.uni(TX - 230, TX + 240)
            y = TY + rng.uni(-4, 18)
            if not gm.m[min(gm.h - 1, max(0, int(y) - gm.y0)), min(gm.w - 1, max(0, int(x) - gm.x0))]:
                continue
            c.paint(m_rect(x, y, 2 + (k % 2), 1), rng.pick([hx('#f2c33a'), hx('#e8a030'), hx('#ffe27a'), hx('#c8861c')]))
    else:
        # the tracks of a fox across the snow of the island
        for k in range(16):
            tx = TX - 200 + k * 26
            ty = TY + 12 + math.sin(k * 0.7) * 4
            c.paint(m_rect(tx, ty, 2, 1), p.snow[0])
            c.paint(m_rect(tx + 5, ty + 2, 2, 1), p.snow[0])
        P.particles_baked(c, p, (20, 10, 650, 140), 60, 109)
