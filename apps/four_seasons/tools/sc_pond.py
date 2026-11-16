# FOUR SEASONS: THE POND (a wide view, near): the pond of the valley with its small island and the weeping willow,
# the sluice gate on the far shore with the ditch to the garden, the reeds on the right bank, the big tree on its hill
# far behind. The log: 0 on the near bank; 1 out on the ice (the app shows it only in winter); 2 jammed between the
# bank and the island (a bridge).
import math
import numpy as np
from fsart import (Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline,
                   m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fswater as FW

W, H = 720, 184
OX, OY = 40, 6                      # the 640 x 172 view of the board in this picture

VARS = {'log': [0, 1, 2]}
HOT = {
    'shore': (560, 40, 80, 62),      # the sluice gate on the far shore
    'island': (420, 34, 116, 86),    # the island and its willow
    'log': (232, 118, 176, 54),      # the log on the bank / on the ice
}

IX, IY = 478, 114                    # the island (the foot of the willow)
SLX, SLY = 598, 84                   # the sluice gate (its sill on the far waterline)
WILLOW = None


def y_far(x):
    """the far waterline"""
    return 84 + 1.6 * math.sin(x * 0.031) + 1.0 * math.sin(x * 0.11 + 1)


def y_near(x):
    """the near waterline: a point of the bank goes out to the island"""
    return 160 + 3 * math.sin(x * 0.021 + 0.4) - 9 * math.exp(-((x - 392) / 34) ** 2) + 6 * math.exp(-((x - 60) / 50) ** 2)


def y_hill(x):
    return 83 - 30 * math.exp(-((x - 150) / 120) ** 2) - 3 * math.sin(x * 0.02)


def willow():
    global WILLOW
    if WILLOW is None:
        WILLOW = FW.Willow(IX - 4, IY - 2, 70, 52, seed=77, sc=0.42, lean=-0.06, bottom=IY - 3)
    return WILLOW


def water_mask():
    pts = [(x, y_far(x)) for x in range(18, 706, 4)]
    pts += [(712, 92), (716, 120), (704, 150)]
    pts += [(x, y_near(x)) for x in range(696, 10, -4)]
    pts += [(4, 140), (2, 110), (10, 90)]
    return m_poly(pts)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    sun = (p.sun_xy[0] + OX, int(p.sun_xy[1] * 0.68) + 4)
    P.sky(c, p, 0, 86, sun)
    P.clouds(c, p, 6, 30, 7, 41)
    P.mountains(c, p, 74, 24, 0.008, 5)
    P.far_ridge(c, p, 80, 9, 0.012, 11, step=6, tree_h=(2, 5))
    before = c.rgb.copy()
    hill_and_tree(c, p, s)
    far_bank(c, p, s)
    src_far = FW.changed(c, before)
    src_far[86:, :] = False
    wm = water_mask()
    wa = FW.full(c, wm)
    # the ground under the banks at the left and the right
    gm = m_rect(0, 86, W, H - 86) - wm
    c.paint(gm, p.hill if s != WINTER else p.snow[2])
    if s != WINTER:
        P.grass_marks(c, p, gm, 500, 19, hmin=1, hmax=3)
    if s == WINTER:
        FW.ice(c, p, wm, 84, 164, 5, n_crack=14, n_snow=16)
    else:
        FW.water(c, p, wm, 84, 164, 9, sun_x=sun[0], n=260)
        # (the far bank, the hill and its tree in the water)
        FW.reflect(c, src_far, 86, wa, [mix(p.waterD, p.hillS, 0.3), mix(p.water, p.hillS, 0.32), mix(p.water, p.hillL, 0.3)],
                   ripple=3, wob=1, hmax=34)
        FW.bank_band(c, p, wm, lambda x: y_far(x) + 1, 0, W, mix(p.hillS, p.waterD, 0.45), 2, 3)
    lily_and_floaters(c, p, s, wm)
    shallows(c, p, s)
    # the island and its willow, with its picture in the water
    before = c.rgb.copy()
    island(c, p, s)
    if s != WINTER:
        src = FW.changed(c, before)
        src[IY + 6:, :] = False
        FW.reflect(c, src, IY + 6, wa & ~FW.full(c, m_ellipse(IX, IY + 3, 48, 9)),
                   [mix(p.waterD, p.bark[0], 0.3), mix(p.waterD, p.lfD if s != AUTUMN else hx('#a07a30'), 0.3),
                    mix(p.water, p.lf if s != AUTUMN else hx('#e0b050'), 0.3)], ripple=3, wob=1, hmax=30)
    life(c, p, s)
    reeds_right(c, p, s)
    near_bank(c, p, s)
    lg = st.get('log', 0)
    if lg == 0:
        FW.log(c, p, 262, 166, 338, 163, 6.5, 3, snow=(s == WINTER))
    elif lg == 1:
        FW.log(c, p, 300, 140, 372, 136, 6.0, 3, snow=(s == WINTER))
        if s != WINTER:
            c.paint(m_rect(296, 146, 82, 1), mix(p.water, (255, 255, 255), 0.5))
            c.paint(m_rect(306, 148, 60, 1), mul(p.waterD, 0.85))
    else:
        FW.log(c, p, 372, 162, 438, 121, 5.6, 3, snow=(s == WINTER), end_at=0)
        if s != WINTER:
            c.paint(m_rect(392, 151, 14, 1), mix(p.water, (255, 255, 255), 0.5))
            c.paint(m_rect(418, 135, 10, 1), mix(p.water, (255, 255, 255), 0.5))
    foreground(c, p, s, lg)
    return c


def hill_and_tree(c, p, s):
    """the hill of the big tree far behind on the left, the path down to the pond"""
    pts = [(-4, 100)] + [(x, y_hill(x)) for x in range(-4, 380, 3)] + [(380, 100)]
    hm = m_poly(pts)
    c.paint(hm, mix(p.hill, p.ridge, 0.35))
    P.grass_marks(c, p, hm, 700, 13, hmin=1, hmax=2, cols=(mix(p.hillL, p.ridge, 0.3), mix(p.hillS, p.ridge, 0.3), p.grassL))
    if s == WINTER:
        c.paint(hm.top(2), (255, 255, 255))
    t = P.Tree(150, 57, seed=404, depth=5, length=8.5, width=3.4, crown=(25, 13), nclump=150, clump_r=(1.6, 3.4))
    t.draw_wood(c, p)
    t.draw_leaves(c, p, seed=3)
    # the tire swing
    c.paint(m_line(168, 47, 168, 56, 1), hx('#e2cea0'))
    c.paint(m_ellipse(168, 58, 2, 2.4), hx('#26262b'))
    # the path from the tree to the far bank
    path = m_polyline(bezier((160, 59), (210, 64), (260, 72), (330, 82), 20), 1.4, 3.0)
    c.paint(path, p.pathr[2] if s != WINTER else p.snow[1])


def far_bank(c, p, s):
    """the far bank: a strip of grass with stones at the water, bushes, a fence, the sluice and the ditch"""
    pts = [(x, y_far(x) - 7 - 3 * math.sin(x * 0.017)) for x in range(-4, W + 6, 4)] + [(W + 4, 96), (-4, 96)]
    bm = m_poly(pts)
    c.paint(bm, p.hill)
    P.grass_marks(c, p, bm, 900, 17, hmin=1, hmax=2)
    if s == WINTER:
        c.paint(bm, p.snow[2])
        c.paint_dither(bm, p.snow[1], 0.2)
        c.paint(bm.top(1), (255, 255, 255))
    # the ditch from the sluice to the garden (to the right and back), the roof of the farm far away
    dpts = bezier((SLX + 10, SLY - 2), (640, 79), (680, 74), (W + 4, 72), 16)
    dm = m_polyline(dpts, 4, 2)
    c.paint(dm, p.soilr[1] if s != WINTER else p.snow[1])
    c.paint(dm.top(1), p.soilr[0] if s != WINTER else p.snow[0])
    if s == SPRING:
        c.paint(m_polyline(dpts[:6], 1), p.water)
    c.paint(m_poly([(676, 70), (700, 60), (724, 70)]), p.tile[2] if s != WINTER else (255, 255, 255))
    c.paint(m_rect(704, 58, 3, 6), p.brick[1])
    # stones along the waterline
    rng = RNG(21)
    x = 6
    while x < W:
        r = rng.uni(1.4, 2.6)
        if not (SLX - 14 < x < SLX + 16):
            P.rock(c, p, x, y_far(x) - 0.5, r)
        x += rng.uni(7, 19)
    for bx, br in [(40, 5), (232, 6), (366, 4.5), (520, 6), (664, 5)]:
        P.bush(c, p, bx, y_far(bx) - 2, br, seed=bx)
    P.fence(c, p, 252, y_far(252) - 3, 336, y_far(336) - 4, 6, rail_dy=(-7, -3), post_h=9)
    P.flowers(c, p, bm, 70, 23)
    sluice_small(c, p, s)


def sluice_small(c, p, s, refl_only=False):
    """the sluice gate on the far shore: two stone pillars, the board, the wheel on the beam"""
    x, y = SLX, SLY
    if refl_only:
        c.paint(m_rect(x - 9, y + 1, 4, 5), mix(p.stone[1], p.waterD, 0.5))
        c.paint(m_rect(x + 7, y + 1, 4, 5), mix(p.stone[1], p.waterD, 0.5))
        c.paint(m_rect(x - 5, y + 2, 12, 3), mix(p.woodr[1], p.waterD, 0.5))
        return
    lp = m_rect(x - 10, y - 15, 6, 16)
    rp = m_rect(x + 6, y - 15, 6, 16)
    for pm in (lp, rp):
        c.paint(pm, p.stone[2])
        c.paint(pm.side(-1, 0), p.stone[3])
        c.paint(pm.side(1, 0), p.stone[1])
        for yy in range(y - 12, y, 4):
            c.paint(m_rect(pm.x0, yy, pm.w, 1), p.stone[1])
    bd = m_rect(x - 4, y - 11, 10, 11)
    c.paint(bd, p.woodr[2])
    for xx in range(x - 4, x + 6, 3):
        c.paint(m_rect(xx, y - 11, 1, 11), p.woodr[1])
    c.paint(m_rect(x - 4, y - 7, 10, 1), p.iron[1])
    beam = m_rect(x - 12, y - 18, 26, 3)
    c.paint(beam, p.woodr[3])
    c.paint(beam.bottom(1), p.woodr[1])
    c.paint(m_line(x + 1, y - 18, x + 1, y - 24, 1), p.iron[2])
    wm = m_ellipse(x + 1, y - 25, 5, 5) - m_ellipse(x + 1, y - 25, 3.4, 3.4)
    c.paint(wm, p.iron[1])
    c.paint(m_line(x - 3, y - 25, x + 5, y - 25, 1), p.iron[1])
    c.paint(m_line(x + 1, y - 29, x + 1, y - 21, 1), p.iron[1])
    c.px(x - 2, y - 28, p.iron[3])
    if s == WINTER:
        c.paint(m_rect(x - 13, y - 20, 28, 2), (255, 255, 255))
        c.paint(m_rect(x - 10, y - 17, 6, 2), (255, 255, 255))
        c.paint(m_rect(x + 6, y - 17, 6, 2), (255, 255, 255))
        c.paint(m_rect(x - 2, y - 31, 7, 1), (255, 255, 255))


def lily_and_floaters(c, p, s, wm):
    pads = [(120, 104, 5, 1), (138, 110, 6.5, 0), (104, 118, 7, 1), (190, 134, 8, 0), (214, 128, 6, 1), (560, 132, 8, 1),
            (588, 140, 9.5, 0), (612, 126, 6, 1), (530, 146, 7, 0), (254, 98, 4, 0), (660, 110, 5, 1)]
    if s != WINTER:
        k = {SPRING: 7, SUMMER: 11, AUTUMN: 6}[s]
        for i, (x, y, r, fl) in enumerate(pads[:k]):
            FW.lily_pad(c, p, x, y, r * (0.75 if s == SPRING else 1.0), 100 + i, flower=bool(fl))
    n = {SPRING: 70, SUMMER: 18, AUTUMN: 110, WINTER: 0}[s]
    FW.floaters(c, p, wm - m_ellipse(IX, IY, 60, 14), n, 77, front=1.0)


def shallows(c, p, s):
    """the shallow water at the left: a clump of cattails in the water, a heron in summer"""
    FW.reeds(c, p, 150, 128, 7, 26, 81, spread=1.0)
    FW.reeds(c, p, 112, 104, 5, 18, 83, spread=0.9)
    if s != WINTER:
        c.paint(m_rect(138, 129, 26, 1), mix(p.water, (255, 255, 255), 0.4))
    if s == SUMMER:
        # a grey heron that stands in the water
        hx0, hy0 = 196, 124
        c.paint(m_line(hx0 - 1, hy0, hx0 - 1, hy0 - 7, 1), hx('#c8a040'))
        c.paint(m_line(hx0 + 1, hy0, hx0 + 1, hy0 - 7, 1), hx('#c8a040'))
        bm = m_ellipse(hx0, hy0 - 10, 5, 3.2, rot=-0.35)
        c.paint(bm, hx('#9aa2b0'))
        c.paint(bm.bottom(1), hx('#6a7280'))
        c.paint(m_line(hx0 - 4, hy0 - 9, hx0 - 7, hy0 - 6, 1), hx('#4a5260'))
        c.paint(m_polyline([(hx0 + 3, hy0 - 12), (hx0 + 4, hy0 - 16), (hx0 + 3, hy0 - 19)], 1.4), hx('#e8eaf0'))
        c.paint(m_disc(hx0 + 4, hy0 - 20, 1.6), hx('#e8eaf0'))
        c.paint(m_line(hx0 + 3, hy0 - 21, hx0 + 1, hy0 - 22, 1), hx('#2a2a32'))
        c.paint(m_line(hx0 + 5, hy0 - 20, hx0 + 10, hy0 - 19, 1), hx('#e8b030'))
        c.paint(m_rect(hx0 - 4, hy0 + 1, 9, 1), mix(p.water, (255, 255, 255), 0.45))


def island(c, p, s):
    """the island: a mound of earth and stones with grass, the willow on it"""
    rng = RNG(5)
    gm = m_ellipse(IX, IY + 1, 46, 9)
    if s != WINTER:
        c.paint(m_ellipse(IX, IY + 4, 48, 8), mul(p.waterD, 0.8))
    c.paint(m_ellipse(IX, IY + 2.5, 47, 8.5), p.soilr[1] if s != WINTER else p.snow[1])
    c.paint(gm, p.hill if s != WINTER else p.snow[2])
    P.grass_marks(c, p, gm, 160, 31, hmin=1, hmax=3)
    if s == WINTER:
        c.paint(gm, p.snow[2])
        c.paint(gm.top(2), (255, 255, 255))
    w = willow()
    w.wood(c, p)
    if s in (SPRING, SUMMER):
        w.curtain(c, p)
    elif s == AUTUMN:
        w.twigs(c, p, wind=0.12, leaves=0.13)
    else:
        w.twigs(c, p, wind=0.0)
        w.snow(c, p)
    # stones at the edge of the island
    for k in range(9):
        a = math.pi * (0.05 + 0.9 * k / 8) + rng.uni(-0.08, 0.08)
        P.rock(c, p, IX + math.cos(a) * 44, IY + 3 + math.sin(a) * 6.5, rng.uni(1.6, 2.8))
    if s == AUTUMN:
        for k in range(26):
            c.paint(m_rect(IX + rng.uni(-40, 40), IY + rng.uni(-2, 7), 2, 1), rng.pick([hx('#f2c33a'), hx('#e8a030'), hx('#ffe27a')]))
    if s == SPRING:
        for k in range(10):
            x = IX + rng.uni(-38, 38)
            c.px(x, IY + rng.uni(-1, 6), rng.pick(p.flower))


def life(c, p, s):
    """the life of the pond in each season"""
    if s == SPRING:
        # a duck and her four ducklings
        duck(c, p, 214, 112, 1.0)
        for k in range(4):
            duck(c, p, 230 + k * 9, 114 + (k % 2), 0.55, young=True)
    elif s == SUMMER:
        # a frog on a pad, dragonflies
        fx, fy = 588, 138
        c.paint(m_ellipse(fx, fy - 2, 3, 2), hx('#4a8a2a'))
        c.px(fx - 2, fy - 4, hx('#2a5a1a'))
        c.px(fx + 1, fy - 4, hx('#2a5a1a'))
        c.px(fx - 1, fy - 3, hx('#9ad866'))
        for (dx, dy) in [(160, 100), (420, 140)]:
            c.paint(m_rect(dx, dy, 5, 1), hx('#2a6ad8'))
            c.paint(m_rect(dx + 1, dy - 1, 1, 1), hx('#cfe8ff'))
            c.paint(m_rect(dx + 3, dy - 1, 1, 1), hx('#cfe8ff'))
    elif s == AUTUMN:
        # wild geese far in the sky
        for k in range(7):
            gx = 420 + k * 9 - (abs(k - 3)) * 0
            gy = 22 + abs(k - 3) * 3
            c.paint(m_line(gx - 2, gy + 1, gx, gy, 1), hx('#4a3a4a'))
            c.paint(m_line(gx, gy, gx + 2, gy + 1, 1), hx('#4a3a4a'))
    else:
        # a crow on the ice, the tracks of a fox
        cx, cy = 214, 128
        c.paint(m_ellipse(cx, cy, 4, 2.4), hx('#2a2a32'))
        c.paint(m_disc(cx + 4, cy - 2, 1.8), hx('#2a2a32'))
        c.px(cx + 6, cy - 2, hx('#6a6a6a'))
        c.paint(m_line(cx - 4, cy - 1, cx - 7, cy + 1, 1), hx('#2a2a32'))
        c.px(cx - 1, cy + 3, hx('#5a4a3a'))
        c.px(cx + 1, cy + 3, hx('#5a4a3a'))
        for k in range(12):
            tx = 120 + k * 7
            ty = 150 - k * 2.1 + (k % 2) * 2
            c.px(tx, ty, p.snow[0])
            c.px(tx + 1, ty, p.snow[0])


def duck(c, p, x, y, sc, young=False):
    if young:
        body = hx('#e8c84a')
        head = hx('#e8c84a')
    else:
        body = hx('#8a6a4a')
        head = hx('#2a7a4a')
    c.paint(m_rect(x - 6 * sc, y + 2 * sc, 12 * sc, 1), mul(p.waterD, 0.8))
    bm = m_ellipse(x, y, 6 * sc, 2.6 * sc)
    c.paint(bm, body)
    c.paint(bm.top(1), mix(body, (255, 255, 255), 0.3))
    c.paint(m_line(x + 3 * sc, y - 1, x + 4 * sc, y - 4 * sc, max(1, 2 * sc)), body)
    c.paint(m_disc(x + 4.5 * sc, y - 4.5 * sc, 1.8 * sc + 0.3), head)
    c.paint(m_rect(x + 6 * sc, y - 4.5 * sc, 2, 1), hx('#f0a020'))
    if not young:
        c.px(x + 4, y - 5, (255, 255, 255))
        c.paint(m_rect(x - 3, y - 1, 5, 1), hx('#3a5aa8'))


def reeds_right(c, p, s):
    FW.reeds(c, p, 636, 116, 9, 30, 41, spread=1.1)
    FW.reeds(c, p, 670, 108, 7, 26, 43, spread=1.0)
    FW.reeds(c, p, 696, 130, 8, 34, 45, spread=1.0, lean=-0.1)
    FW.reeds(c, p, 26, 120, 6, 24, 47, spread=1.0, lean=0.1)


def near_bank(c, p, s):
    pts = [(x, y_near(x)) for x in range(-4, W + 6, 3)] + [(W + 4, H + 4), (-4, H + 4)]
    bm = m_poly(pts)
    if s == WINTER:
        c.paint(bm, p.snow[2])
        c.paint_dither(bm, p.snow[1], 0.18)
        c.paint(bm.top(2), (255, 255, 255))
        c.paint(bm.top(1).shift(0, 2) & bm, p.snow[1])
    else:
        c.paint(bm, p.hill)
        c.paint(bm.top(2), p.soilr[1])
        c.paint(bm.top(1), p.soilr[2])
        P.grass_marks(c, p, bm, 1500, 51, hmin=1, hmax=4)
    rng = RNG(61)
    for k in range(14):
        x = rng.uni(0, W)
        if 250 < x < 450:
            continue
        P.rock(c, p, x, y_near(x) + 1.5, rng.uni(1.8, 3.4))
    P.flowers(c, p, bm - m_rect(240, 150, 220, 40), 90, 63)


def foreground(c, p, s, lg):
    rng = RNG(71)
    # tall grass and cattails at the very front (out of the hot spots)
    FW.reeds(c, p, 70, 182, 8, 34, 73, spread=1.2, lean=0.08)
    FW.reeds(c, p, 690, 184, 7, 30, 75, spread=1.2, lean=-0.08)
    for x in range(-4, W + 4, 2):
        h = 3 + (x * 7 % 5)
        col = p.grassL if (x // 2) % 3 else mul(p.fg, 0.8)
        if s == WINTER:
            col = (255, 255, 255) if (x // 2) % 3 else p.snow[1]
        c.paint(m_rect(x, H - h, 1, h), col)
    if s == WINTER and lg == 0:
        # (the tracks in the snow go to the log)
        for k in range(8):
            c.px(230 + k * 4, 176 - k * 1.2, p.snow[0])
