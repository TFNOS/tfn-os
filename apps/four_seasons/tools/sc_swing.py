# FOUR SEASONS: THE SWING (a close view): the foot of the big tree on the top of the hill, near. The trunk at the left
# with a hollow, a red squirrel lives in it; the long limb goes over the top to the right, the tire swing hangs from
# it on its rope; behind, the valley falls away (the far pond, the red shed, the ridge, the mountains).
#   squirrel 0: it looks out of its hollow; 1: fed, it sits on the lip and holds out its paws to the ball of string;
#   2: gone. string 1: a ball of white kite string lies on the lip of the hollow; 0: taken. The ball has its own
#   pixels (the layers of the two things must not touch the same pixels), so it shows with all three squirrel values.
#   Winter: snow closes the hollow, the squirrel sleeps: no squirrel and no ball show.
import math
import numpy as np
from fsart import Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline, m_union
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fsparts_c as C

W, H = 672, 180
VARS = {'squirrel': [0, 1, 2], 'string': [1, 0]}
HOT = {
    'squirrel': (108, 40, 104, 86),
    'swing': (388, 60, 90, 104),
}

HX, HY, HRX, HRY = 150, 80, 17, 25          # the hollow
BX, BY, BR = 157, 99, 7.5                    # the ball of string
SX, SY = 432, 114                            # the middle of the tire
GROUND = 156


def xl(y):
    y = np.asarray(y, dtype=np.float64)
    return 104 + np.sin(y * 0.05) * 1.5 - np.maximum(0, y - 132) ** 2 * 0.03


def xr(y):
    y = np.asarray(y, dtype=np.float64)
    return 196 + np.sin(y * 0.04 + 1) * 1.5 + np.maximum(0, y - 134) ** 2 * 0.028


def gy(x):
    return GROUND + 3 * math.sin(x * 0.013 + 0.4) + 1.5 * math.sin(x * 0.05)


LIMB = ([(186, 30), (260, 20), (350, 15), (440, 12), (530, 8), (620, 3), (690, -2)], 28, 15)
LIMB2 = ([(118, 22), (98, 4), (86, -8)], 26, 20)
_GROWN = None


def grown():
    global _GROWN
    if _GROWN is None:
        br, tips = C.grow([LIMB], 41, per=10, sub_len=(20, 40), sub_w=0.4, depth=2, up=0.55, t_range=(0.08, 0.98))
        rng = RNG(77)
        clumps = []

        def add(cx, cy, r, fr):
            if abs(cx - SX) < 22 and cy > 16:
                return                                       # the rope stays clear
            clumps.append((cx, cy, r, rng(), rng(), fr))
        for (x, y, a) in tips:
            for k in range(2):
                add(x + rng.uni(-9, 9), y + rng.uni(-6, 8), rng.uni(10, 16), rng() < 0.6)
        for k in range(70):
            x = rng.uni(196, 690)
            add(x, 14 - (x - 190) * 0.015 + rng.uni(-16, 22), rng.uni(10, 17), rng() < 0.5)
        for k in range(16):
            add(rng.uni(-6, 110), rng.uni(-10, 14), rng.uni(11, 17), rng() < 0.4)
        _GROWN = (br, clumps)
    return _GROWN


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    C.sky(c, p, 140, nclouds=5, seed=23)
    P.mountains(c, p, 126, 34, 0.007, 8)
    P.far_ridge(c, p, 136, 9, 0.012, 21, step=8, tree_h=(3, 6))
    valley_below(c, p, s)
    br, clumps = grown()
    C.leaves_list(c, p, clumps, front=False, seed=300)
    for i, (pts, w0, w1, lvl) in enumerate(sorted(br, key=lambda b: -b[1])):
        C.limb(c, p, pts, w0, w1, seed=i * 5)
    # the near ground
    gm = m_poly([(-4, H + 4)] + [(x, gy(x)) for x in range(-4, W + 6, 4)] + [(W + 4, H + 4)])
    if s == WINTER:
        C.snowfield(c, p, gm, 17, GROUND, H, stalks=50)
    else:
        C.turf(c, p, gm, 17, GROUND, H, n=1100, hmax=8)
    swing_ground(c, p, s)
    if s != WINTER:
        P.flowers(c, p, gm - m_ellipse(SX, 167, 50, 9), 140, 18)
        C.litter(c, p, gm, 90, 19)
    # the tree: the limbs, the trunk with its roots and the hollow
    C.limb(c, p, LIMB[0], LIMB[1], LIMB[2], body=False)
    C.limb(c, p, LIMB2[0], LIMB2[1], LIMB2[2], body=False)
    roots(c, p, s)
    C.trunk(c, p, xl, xr, -6, 170, seed=29, cracks=7)
    C.limb(c, p, LIMB2[0], LIMB2[1], LIMB2[2], outline=False, seed=7)
    C.limb(c, p, LIMB[0], LIMB[1], LIMB[2], outline=False, seed=9)
    hollow(c, p, s, st)
    swing(c, p, s)
    foot_things(c, p, s)
    C.leaves_list(c, p, clumps, front=True, seed=400)
    P.particles_baked(c, p, (0, 0, W, 150), 36, 71)
    return c


def valley_below(c, p, s):
    """the valley under the hill, far: its floor, the pond, the red shed, the path"""
    m = m_poly([(-4, H)] + [(x, 140 + 4 * math.sin(x * 0.008 + 2)) for x in range(-4, W + 6, 6)] + [(W + 4, H)])
    c.paint(m, mix(p.hill, p.hz, 0.25) if s != WINTER else p.snow[2])
    c.paint_dither(m, mix(p.hillS, p.hz, 0.3) if s != WINTER else p.snow[1], 0.2)
    pm = m_ellipse(552, 150, 30, 3.2)
    c.paint(pm, mix(p.water, p.skyB, 0.3) if s != WINTER else p.ice[2])
    c.paint(m_rect(534, 149, 18, 1), mix(p.water, (255, 255, 255), 0.5) if s != WINTER else p.ice[3])
    x0, y0 = 616, 139
    c.paint(m_rect(x0, y0, 12, 7), p.shed)
    c.paint(m_rect(x0 + 12, y0 + 1, 4, 6), p.shedD)
    c.paint(m_poly([(x0 - 1, y0), (x0 + 6, y0 - 4), (x0 + 17, y0 - 3), (x0 + 13, y0 + 1)]),
            p.roof if s != WINTER else (255, 255, 255))
    c.px(x0 + 3, y0 + 3, p.trim)
    c.px(x0 + 8, y0 + 3, mix(hx('#2c3a4a'), hx('#ffcf6a'), 0.8 if s >= AUTUMN else 0.0))
    c.paint(m_polyline([(480, 154), (520, 147), (580, 145), (616, 146)], 1.4), p.pathr[2] if s != WINTER else p.snow[1])


def roots(c, p, s):
    for (pts, w0, w1) in [([(118, 154), (96, 164), (72, 170), (46, 172)], 14, 4),
                          ([(186, 156), (210, 166), (236, 171), (262, 173)], 15, 4),
                          ([(150, 164), (144, 174), (136, 182)], 16, 10)]:
        C.limb(c, p, pts, w0, w1, seed=int(pts[0][0]))


def hollow(c, p, s, st):
    """the hollow in the trunk: a thick lip of bark, the dark inside; the squirrel and the ball of string"""
    lx, ly = C.ldir(p)
    lip = m_ellipse(HX, HY, HRX + 5, HRY + 5)
    c.outline(lip, mul(p.bark[0], 0.7))
    c.shade_sphere(lip, HX, HY, HRX + 5, HRY + 5, [p.bark[0], p.bark[1], p.bark[2], p.bark[3]], p.L, amb=0.3)
    for k in range(9):
        a = k / 9 * 2 * math.pi + 0.3
        c.paint(m_line(HX + math.cos(a) * (HRX + 1), HY + math.sin(a) * (HRY + 1), HX + math.cos(a) * (HRX + 4.5),
                       HY + math.sin(a) * (HRY + 4.5), 1), p.bark[1])
    hole = m_ellipse(HX, HY, HRX, HRY)
    c.outline(hole, mul(p.bark[0], 0.55))
    deep = [hx('#0e0907'), hx('#1c120c'), hx('#2e2016')]
    c.vgrad(hole, deep, HY - HRY, HY + HRY, 0.3)
    c.paint(hole.side(-lx, 0, 2), hx('#3a2a1e'))
    if s == WINTER:
        # snow closes the hollow (a drift up to near its top): the squirrel sleeps inside
        top = HY - HRY + 7
        drift = m_poly([(HX - HRX - 2, HY + HRY + 2)] + [(x, top + 2.5 * math.sin((x - HX) * 0.3) + (x - HX) * 0.1)
                                                       for x in range(HX - HRX - 2, HX + HRX + 3, 2)]
                       + [(HX + HRX + 2, HY + HRY + 2)]) & m_ellipse(HX, HY, HRX + 1, HRY + 1)
        # (a hollow filled with snow: the inner side away from the sun is lit, the lip shades the top)
        c.paint_values(drift, p.snow, lambda x, y: np.clip(0.62 - (x - HX) / HRX * 0.3 - (y < top + 6) * 0.35
                                                          + (y > HY + 6) * 0.15, 0, 1), 0.35)
        c.paint(drift.top(1), p.snow[2])
        for k in range(5):
            c.px(HX - 10 + k * 5, HY + (k * 7) % 14, (255, 255, 255))
        c.paint(lip.top(4), p.snow[2])
        c.paint(lip.top(2), (255, 255, 255))
        for k in range(3):
            c.paint(m_rect(HX - 8 + k * 7, HY + HRY + 4, 1, 3 + k % 2 * 2), hx('#dff0ff'))
        return
    sq = st.get('squirrel', 0)
    if sq == 0:
        squirrel_peek(c, p)
    elif sq == 1:
        squirrel_out(c, p)
    if st.get('string', 1):
        ball(c, p)


RED = [hx('#5a2410'), hx('#9a3c18'), hx('#d0662a'), hx('#f29a4e')]
CREAM = hx('#f4e6cc')
EYE = hx('#141010')


def ears(c, x, y, sp):
    for ex in (-sp, sp):
        c.paint(m_poly([(x + ex - 3, y + 2), (x + ex + 1, y - 9), (x + ex + 3, y + 2)]), RED[1])
        c.paint(m_poly([(x + ex - 1, y), (x + ex + 1, y - 6), (x + ex + 2, y)]), RED[3])
        c.paint(m_line(x + ex + 0.5, y - 8, x + ex + 2, y - 12, 1.4), RED[0])


def squirrel_peek(c, p):
    """the squirrel looks out of the hollow: its head, its ear tufts, its chest, its paws on the lip (left of the
    ball)"""
    x, y = 147, 74
    c.paint(m_ellipse(x, y + 14, 7, 6), RED[1])
    c.paint(m_ellipse(x, y + 15, 4.5, 4.5), CREAM)
    ears(c, x, y - 6, 6)
    head = m_ellipse(x, y, 10, 8.5)
    c.outline(head, RED[0])
    c.shade_sphere(head, x, y, 10, 8.5, RED, p.L, amb=0.3)
    c.paint(m_ellipse(x + 1, y + 4.5, 5, 3), CREAM)
    for ex in (-5, 3):
        c.paint(m_rect(x + ex, y - 2, 3, 3), EYE)
        c.px(x + ex, y - 2, (255, 255, 255))
    c.paint(m_rect(x - 1, y + 3, 3, 1.4), hx('#2a1410'))
    for px_ in (136, 143):
        c.paint(m_ellipse(px_, 103, 3, 2), RED[2])
        c.px(px_ - 1, 104, RED[0])
        c.px(px_ + 1, 104, RED[0])


def squirrel_out(c, p):
    """fed: the squirrel sits on the right lip of the hollow, its tail up, its paws out to the ball; seed husks"""
    lx, ly = C.ldir(p)
    tail = [(190, 110), (202, 98), (206, 80), (200, 64), (188, 58), (182, 66)]
    tm = m_polyline(tail, 11, 15)
    rng = RNG(5150)
    tufts = []
    for k in range(22):
        i = rng.int(0, 4)
        t = rng()
        x = lerp(tail[i][0], tail[i + 1][0], t)
        y = lerp(tail[i][1], tail[i + 1][1], t)
        a = rng.uni(0, 2 * math.pi)
        rr = 5 + i * 0.5
        tufts.append(m_ellipse(x + math.cos(a) * rr, y + math.sin(a) * rr, 3.2, 1.6, rot=a + 0.8))
    tm = tm | m_union(tufts)
    c.outline(tm, RED[0])
    c.paint(tm, RED[2])
    c.paint(tm.side(lx, ly, 3), RED[3])
    c.paint(tm.side(-lx, -ly, 3), RED[1])
    for k in range(14):
        x, y = tail[k % 6]
        c.px(x + (k % 3) * 2 - 2, y + (k % 4) - 1, RED[3] if k % 2 else RED[1])
    body = m_ellipse(180, 96, 10, 14)
    c.outline(body, RED[0])
    c.shade_sphere(body, 180, 96, 10, 14, RED, p.L, amb=0.3)
    c.paint(m_ellipse(176, 99, 5, 9), CREAM)
    ears(c, 176, 70, 5)
    head = m_ellipse(175, 76, 9.5, 8)
    c.outline(head, RED[0])
    c.shade_sphere(head, 175, 76, 9.5, 8, RED, p.L, amb=0.3)
    c.paint(m_ellipse(170, 80, 5, 3), CREAM)
    c.paint(m_rect(169, 73, 3, 3), EYE)
    c.px(169, 73, (255, 255, 255))
    c.paint(m_rect(166, 78, 2, 1.4), hx('#2a1410'))
    # the paws hold out to the ball (they touch its right side)
    c.paint(m_line(176, 92, 167, 96, 3), RED[2])
    c.paint(m_line(176, 100, 167, 102, 3), RED[2])
    c.paint(m_rect(166, 95, 2, 2), RED[0])
    c.paint(m_rect(166, 101, 2, 2), RED[0])
    c.paint(m_ellipse(182, 110, 7, 2.4), RED[1])
    # the husks of the sunflower seeds on the lip
    for (x, y) in [(138, 107), (144, 109), (132, 104), (174, 113), (182, 115), (168, 110)]:
        c.paint(m_ellipse(x, y, 2.4, 1.2), hx('#2a2a2e'))
        c.px(x, y, hx('#d8d4c8'))


def ball(c, p):
    """the ball of white kite string on the lip of the hollow, its loose end over the lip"""
    m = m_disc(BX, BY, BR)
    cols = [hx('#9a9282'), hx('#cfc6b2'), hx('#ede6d6'), hx('#ffffff')]
    c.outline(m, hx('#5a5048'))
    c.shade_sphere(m, BX, BY, BR, BR, cols, p.L, amb=0.3)
    for k in range(5):
        a = 0.4 + k * 0.6
        c.paint(m_line(BX - math.cos(a) * BR, BY - math.sin(a) * BR * 0.7, BX + math.cos(a) * BR,
                       BY + math.sin(a) * BR * 0.7, 1) & m, cols[1])
    c.paint(m_polyline([(BX + 3, BY + BR), (BX + 4, BY + 11), (BX + 2, BY + 16), (BX + 4, BY + 21)], 1), cols[2])


def swing(c, p, s):
    """the tire swing: a knot round the limb, the rope, the tire (a ring seen a little from the side)"""
    ky = 11
    C.rope(c, p, [(SX - 7, ky - 10), (SX - 8, ky + 2), (SX - 3, ky + 11)], 3.4)
    C.rope(c, p, [(SX + 7, ky - 11), (SX + 7, ky + 2), (SX + 2, ky + 11)], 3.4)
    C.rope(c, p, [(SX - 8, ky - 12), (SX + 8, ky - 13)], 3.4)
    C.rope(c, p, [(SX, ky + 9), (SX, 50), (SX, SY - 30)], 4.4)
    lx, ly = C.ldir(p)
    rx, ry = 33, 39
    outer = m_ellipse(SX, SY, rx, ry)
    inner = m_ellipse(SX + 5, SY + 1, 10, 22)
    ring = outer - inner
    c.outline(ring, hx('#08080a'))
    c.shade_sphere(ring, SX, SY, rx, ry, p.tire, p.L, amb=0.2, band=0.3)
    # the hole: the land behind, the inner wall of the tire (lit on the far side)
    c.paint(inner, mix(p.hillS, p.skyB, 0.3) if s != WINTER else p.snow[1])
    c.paint(m_rect(SX - 6, SY - 2, 24, 30) & inner, mix(p.hill, p.hz, 0.25) if s != WINTER else p.snow[2])
    c.paint(m_ellipse(SX + 5, SY + 1, 10, 22) - m_ellipse(SX + 8, SY + 2, 9, 21), p.tire[1])
    c.paint(m_ellipse(SX + 5, SY + 1, 10, 22).side(-lx if lx else -1, 0, 3) & inner, p.tire[2])
    # the tread: short marks on the rim, a white letter band of the old tire
    for k in range(34):
        a = k / 34 * 2 * math.pi
        x0, y0 = SX + math.cos(a) * (rx - 3), SY + math.sin(a) * (ry - 3)
        x1, y1 = SX + math.cos(a) * (rx + 0.5), SY + math.sin(a) * (ry + 0.5)
        lit = (math.cos(a) * p.L[0] + math.sin(a) * p.L[1]) > 0.3
        c.paint(m_line(x0, y0, x1, y1, 1) & ring, p.tire[3] if lit else p.tire[0])
    c.paint(ring.side(lx, ly, 1), p.tire[3])
    c.paint((m_ellipse(SX - 1, SY, 22, 28) - m_ellipse(SX - 1, SY, 21, 27)) & ring & m_rect(SX - 30, SY - 12, 16, 26),
            hx('#6a6a72'))
    # the rope goes round the top of the tire and through the hole
    C.rope(c, p, [(SX - 2, SY - 42), (SX + 1, SY - 33), (SX + 4, SY - 20)], 4.4)
    if s == WINTER:
        c.paint(m_ellipse(SX - 2, SY - 38, 19, 3), (255, 255, 255))
        c.paint(m_ellipse(SX - 2, SY - 37, 16, 1.4), p.snow[1])
        c.paint(m_ellipse(SX + 5, SY + 21, 7, 2), (255, 255, 255))
        for k in range(5):
            c.paint(m_rect(SX - 16 + k * 8, SY + ry - 1, 1, 3 + (k % 2) * 3), hx('#dff0ff'))
    elif s == AUTUMN:
        c.paint(m_ellipse(SX + 6, SY + 21, 3.4, 1.5), C.AUT_R[2])
        c.paint(m_ellipse(SX + 2, SY + 21, 2.4, 1.2), C.AUT_Y[2])
    elif s == SPRING:
        for k in range(5):
            c.px(SX + 1 + k * 2, SY + 22 - (k % 2), p.blossom[2] if k % 2 else (255, 255, 255))


def swing_ground(c, p, s):
    """the bare earth under the swing (the feet of the children), the shadow of the tire"""
    m = m_ellipse(SX, 168, 50, 8)
    if s != WINTER:
        c.paint_values(m, p.soilr, lambda x, y: np.clip(0.75 - ((x - SX) / 50) ** 2 * 0.5 + (y - 168) * 0.03, 0, 1), 0.4)
        c.paint_dither(m.edge(), p.grassr[1], 0.5)
        for k in range(16):
            c.px(SX - 40 + k * 5.3, 165 + (k * 7) % 6, p.soilr[3])
    else:
        for k in range(8):
            x = SX - 46 + k * 13
            y = 164 + (k % 2) * 7
            c.paint(m_ellipse(x, y, 3.4, 1.8), p.snow[0])
            c.paint(m_ellipse(x, y + 0.6, 2.2, 1.0), mix(p.snow[0], p.cool, 0.3))
    c.darken(m_ellipse(SX + p.slant * 16, 166, 26, 3.4), 0.75, p.cool)


def foot_things(c, p, s):
    """small things at the foot of the tree: mushrooms and nut shells (autumn), snowdrops (spring), a snail (summer)"""
    if s == AUTUMN:
        for (x, y, r) in [(226, 168, 6), (236, 170, 4), (70, 170, 4.5)]:
            c.paint(m_rect(x - 1.2, y - r * 0.6, 2.6, r * 0.9), hx('#e8dcc0'))
            cap = m_ellipse(x, y - r * 0.7, r, r * 0.55)
            c.outline(cap, hx('#3a1a0a'))
            c.shade_sphere(cap, x, y - r * 0.7, r, r * 0.55, [hx('#5a2a14'), hx('#8a4a22'), hx('#b8743a'), hx('#e0a868')], p.L)
        for (x, y) in [(204, 174), (110, 176), (250, 175)]:
            c.paint(m_ellipse(x, y, 2.6, 2), hx('#8a5a2a'))
            c.paint(m_ellipse(x, y - 1.6, 2.6, 1.1), hx('#5a3a1a'))
    elif s == SPRING:
        for (x, y) in [(72, 170), (80, 173), (230, 169), (240, 172), (62, 175)]:
            c.paint(m_line(x, y, x, y - 8, 1), hx('#3f8a3a'))
            c.paint(m_ellipse(x + 1, y - 8, 1.8, 2.4), (255, 255, 255))
            c.px(x + 1, y - 6, hx('#8ad070'))
    elif s == SUMMER:
        x, y = 228, 162
        c.paint(m_ellipse(x - 2, y + 4, 8, 1.8), hx('#8a7a5a'))
        sh = m_disc(x + 1, y, 4.4)
        c.outline(sh, hx('#4a2a10'))
        c.shade_sphere(sh, x + 1, y, 4.4, 4.4, [hx('#6a4220'), hx('#a8702e'), hx('#e0b060'), hx('#fff0b0')], p.L)
        c.paint(m_line(x - 7, y + 3, x - 9, y - 1, 1), hx('#8a7a5a'))
