# FOUR SEASONS: THE GARDEN (a close view): the garden bed of the farm, near: a frame of boards, rows of soil with the
# vegetables of the season, the scarecrow (blue checked shirt, a sack head, a straw hat with a red band), the ditch
# from the pond along the right side, a low fence behind and the far pond with its sluice.
#   plant 0: the bare spot; 1: planted (a mound and the seed packet on a stick); 2: a sprout; 3: a big sunflower full
#   of seeds; 4: the head taken (the stalk is cut). A plant shows as its season can show it: value 3 in spring is a
#   sprout, in autumn a dry stalk with a bent head, in winter the dry stalk with snow; value 2 in autumn is a dry
#   small sprout, in winter a dry stem in the snow.
import math
import numpy as np
from fsart import Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline, m_union
from fspal import pal, ramp_of, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fsparts_c as C

W, H = 672, 180
VARS = {'plant': [0, 1, 2, 3, 4]}
HOT = {
    'bed': (398, 30, 112, 136),
}

PX, PY = 454, 136                  # the spot of the plant (its foot)
SCX = 200                          # the scarecrow
BACK, FRONT = 112, 150             # the soil of the bed: its far edge and its front edge
WHITE = (255, 255, 255)


def bed_x(v):
    """the left and the right edge of the soil at the depth v (0 far, 1 near)"""
    return lerp(170, 124, v), lerp(566, 606, v)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    C.sky(c, p, 112, nclouds=5, seed=131)
    P.mountains(c, p, 92, 30, 0.007, 31)
    P.far_ridge(c, p, 100, 8, 0.012, 33, step=8, tree_h=(3, 6))
    far_land(c, p, s)
    fence(c, p, s)
    ground(c, p, s)
    ditch(c, p, s)
    bed(c, p, s)
    rows(c, p, s)
    scarecrow(c, p, s, SCX, 142)
    plant(c, p, s, st.get('plant', 0))
    can(c, p, s)
    P.particles_baked(c, p, (0, 0, W, H), 30, 133)
    return c


def far_land(c, p, s):
    """the meadow behind the garden, the far pond with its sluice gate (the water of the ditch comes from there)"""
    hm = m_poly([(-4, H)] + [(x, 98 + 4 * math.sin(x * 0.01 + 1)) for x in range(-4, W + 6, 4)] + [(W + 4, H)])
    c.paint(hm, p.hill)
    P.grass_marks(c, p, hm, 1200, 134)
    pm = m_ellipse(600, 104, 70, 4.2)
    if s == WINTER:
        c.paint(pm, p.ice[2])
        c.paint(m_rect(560, 103, 30, 1) & pm, WHITE)
    else:
        c.paint(pm, mix(p.water, p.skyB, 0.3))
        c.paint(m_rect(566, 103, 26, 1) & pm, mix(p.water, WHITE, 0.5))
        c.paint(m_rect(610, 105, 18, 1) & pm, mix(p.water, WHITE, 0.4))
    # the sluice gate (far): two posts, a board, the wheel
    x = 612
    c.paint(m_rect(x - 5, 98, 2, 10), p.woodr[1])
    c.paint(m_rect(x + 4, 98, 2, 10), p.woodr[1])
    c.paint(m_rect(x - 4, 102, 9, 4), p.woodr[2])
    c.paint(m_disc(x + 0.5, 98, 2.4) - m_disc(x + 0.5, 98, 1.2), p.iron[2])
    for (bx, by, r) in [(40, 104, 7), (96, 102, 5), (520, 102, 6)]:
        P.bush(c, p, bx, by, r, seed=bx)


def fence(c, p, s):
    """a low fence of split rails behind the bed"""
    y0 = 108
    for k in range(9):
        x = -6 + k * 86 + (k % 3) * 4
        pm = m_rect(x, y0 - 22, 5, 24)
        c.outline(pm, mul(p.woodD, 0.6))
        c.paint(pm, p.woodr[2])
        c.paint(m_rect(x, y0 - 22, 1, 24), p.woodr[3])
        c.paint(m_rect(x + 4, y0 - 22, 1, 24), p.woodr[1])
        if s == WINTER:
            c.paint(m_ellipse(x + 2.5, y0 - 23, 4, 1.6), WHITE)
    for dy in (-17, -8):
        rm = m_polyline([(-6, y0 + dy), (W + 6, y0 + dy + 2)], 3)
        c.paint(rm, p.woodr[2])
        c.paint(m_polyline([(-6, y0 + dy - 1), (W + 6, y0 + dy + 1)], 1), p.woodr[3])
        if s == WINTER:
            c.paint(m_polyline([(-6, y0 + dy - 2), (W + 6, y0 + dy)], 1.4), WHITE)
    if s == SUMMER:
        # a climbing bean on the fence
        rng = RNG(141)
        for k in range(14):
            x = 40 + k * 7 + rng.uni(-2, 2)
            y = y0 - 20 + rng.uni(0, 14)
            C.vine_leaf(c, p, x, y, 3.4, rng.uni(0, 6.28), p.leaf)
        for k in range(5):
            c.paint(m_rect(52 + k * 15, y0 - 12 + (k % 2) * 4, 1.4, 6), hx('#7fb84a'))


def ground(c, p, s):
    gm = m_poly([(-4, H + 4), (-4, 116)] + [(x, 116 + 2 * math.sin(x * 0.03)) for x in range(0, W + 6, 6)] + [(W + 4, H + 4)])
    if s == WINTER:
        C.snowfield(c, p, gm, 151, 116, H, stalks=50)
        return
    C.turf(c, p, gm, 151, 116, H, n=900, hmax=7)
    path = m_poly([(-4, 166), (300, 164), (680, 170), (680, H + 4), (-4, H + 4)])
    c.paint_values(path, p.pathr, lambda x, y: np.clip(0.45 + (y - 164) * 0.03, 0, 1), 0.4)
    c.paint(path.top(1), p.pathr[0])
    C.litter(c, p, gm, 70, 152)
    P.flowers(c, p, gm - path, 50, 153)


def ditch(c, p, s):
    """the ditch from the pond: it comes down from the right and runs along the right end of the bed"""
    pts = [(690, 120), (660, 126), (632, 134), (618, 146), (612, 160), (600, 180)]
    bank = m_polyline(pts, 13, 15)
    ch = m_polyline(pts, 8, 10)
    c.paint(bank, p.mud[1] if s != WINTER else p.snow[1])
    c.paint(bank.side(0, -1, 2), p.mud[2] if s != WINTER else WHITE)
    if s == SPRING:
        c.vgrad(ch, p.waterr[1:], 120, 180, 0.3)
        for k in range(6):
            x, y = pts[k % 5]
            c.paint(m_line(x - 3, y + 2, x + 1, y + 1, 1) & ch, p.waterr[4])
    elif s == SUMMER:
        c.paint(ch, p.mud[1])
        c.paint_dither(ch, p.mud[2], 0.4)
        rng = RNG(161)
        for k in range(10):
            x, y = lerp(690, 604, k / 9), lerp(122, 172, k / 9)
            c.paint(m_line(x - 3, y, x + rng.uni(-2, 2), y + rng.uni(2, 4), 1) & ch, p.mud[0])
    elif s == AUTUMN:
        c.paint(ch, p.waterr[1])
        c.paint(m_line(640, 130, 650, 128, 1) & ch, p.waterr[3])
        for (x, y, col) in [(628, 139, C.AUT_R[2]), (616, 156, C.AUT_Y[2]), (660, 127, C.AUT_R[3])]:
            c.paint(m_ellipse(x, y, 2.4, 1.3), col)
    else:
        c.paint(ch, p.ice[2])
        c.paint(m_line(650, 128, 640, 133, 1) & ch, WHITE)
    # the small board gate where the ditch meets the bed
    c.paint(m_rect(606, 140, 12, 4), p.woodr[2])
    c.paint(m_rect(606, 140, 12, 1), p.woodr[3])


def bed(c, p, s):
    """the frame of boards and the soil of the bed"""
    lx, ly = C.ldir(p)
    soil = m_poly([(124, FRONT), (170, BACK), (566, BACK), (606, FRONT)])
    c.darken(m_poly([(118, FRONT + 12), (612, FRONT + 12), (612 + p.slant * 10, FRONT + 15), (118 + p.slant * 10, FRONT + 15)]),
             0.75, p.cool)
    # the back board and the side boards
    c.paint(m_polyline([(166, BACK - 1), (570, BACK - 1)], 4), p.woodr[1])
    c.paint(m_polyline([(166, BACK - 3), (570, BACK - 3)], 1), p.woodr[3])
    c.paint(m_poly([(118, FRONT), (166, BACK - 3), (172, BACK - 3), (126, FRONT)]), p.woodr[2])
    c.paint(m_poly([(606, FRONT), (566, BACK - 3), (572, BACK - 3), (614, FRONT)]), p.woodr[1])
    if s == WINTER:
        c.paint_values(soil, p.snow, lambda x, y: np.clip(0.55 + 0.35 * np.sin(((y - BACK) / (FRONT - BACK)) * 4 * math.pi), 0, 1), 0.4)
        c.speckle(soil, [hx('#8a7a5e'), hx('#6a5a48')], 0.02, 7)
    else:
        def val(x, y):
            v = (y - BACK) / (FRONT - BACK)
            f = (v * 4 + 0.1) % 1.0
            return np.where(f < 0.32, 0.15, np.where(f < 0.5, 0.95, 0.5))
        c.paint_values(soil, p.soilr, val, 0.3)
        c.speckle(soil, [p.soilr[3], p.soilr[0]], 0.05, 9)
    # the front board (near, big): planks with nails
    C.boards(c, p, 116, FRONT, 616, FRONT + 12, 500, p.woodr, 171, nails=False, knots=True)
    fb = m_rect(116, FRONT, 500, 12)
    c.outline(fb, mul(p.woodD, 0.6))
    c.paint(m_rect(116, FRONT, 500, 1), p.woodr[4])
    c.paint(m_rect(116, FRONT + 11, 500, 1), p.woodr[0])
    for x in range(126, 612, 60):
        c.paint(m_rect(x, FRONT + 5, 2, 2), hx('#3a3a40'))
        c.px(x, FRONT + 5, hx('#9a9aa4'))
    if s == WINTER:
        c.paint(m_rect(114, FRONT - 2, 504, 3), WHITE)
        c.paint(m_rect(114, FRONT + 1, 504, 1), p.snow[1])


def row_y(v):
    return BACK + v * (FRONT - BACK)


def rows(c, p, s):
    """the vegetables in the rows (away from the plant spot and the scarecrow)"""
    rng = RNG(181)
    for r, v in enumerate((0.2, 0.45, 0.7, 0.92)):
        y = row_y(v)
        x0, x1 = bed_x(v)
        x = x0 + 14
        while x < x1 - 10:
            step = rng.uni(17, 34)
            if abs(x - PX) < 34 or abs(x - SCX) < 24:
                x += step
                continue
            veg(c, p, s, r, x + rng.uni(-3, 3), y, rng)
            x += step


def veg(c, p, s, r, x, y, rng):
    kind = rng()
    sz = rng.uni(0.8, 1.35)
    if s == WINTER:
        if kind < 0.22:
            c.paint(m_line(x, y, x, y - 14 * sz, 1.4), p.woodr[1])
            c.paint(m_ellipse(x, y - 15 * sz, 2, 1.2), WHITE)
        elif kind < 0.36:
            m = m_ellipse(x, y - 2, 6 * sz, 3.4 * sz)
            c.paint(m, hx('#d87a28'))
            c.paint(m_ellipse(x, y - 2 - 2 * sz, 6 * sz, 2.4 * sz), WHITE)
        elif kind < 0.5:
            for k in range(3):
                c.paint(m_line(x + k * 3, y, x + k * 3 + rng.uni(-2, 2), y - rng.uni(4, 9), 1), hx('#7a6a50'))
        return
    if s == SPRING:
        if kind < 0.2:
            return
        for k in range(rng.int(1, 3)):
            sx = x + (k - 1) * rng.uni(5, 8)
            hh = 3 * sz
            c.paint(m_line(sx, y, sx, y - hh, 1), hx('#5a9a3a'))
            c.paint(m_ellipse(sx - 1.8 * sz, y - hh - 1, 2 * sz, 1.1), p.lfL)
            c.paint(m_ellipse(sx + 1.8 * sz, y - hh - 1, 2 * sz, 1.1), p.lf)
        return
    if s == SUMMER:
        if kind < 0.1:
            return
        if r == 0 and kind < 0.75:
            # tomatoes on a stake
            c.paint(m_line(x, y, x, y - 26, 1.4), p.woodr[2])
            for k in range(6):
                lx_ = x + rng.uni(-6, 6)
                ly_ = y - rng.uni(4, 24)
                m = m_ellipse(lx_, ly_, 4, 3)
                c.shade_sphere(m, lx_, ly_, 4, 3, p.leaf, p.L, amb=0.3)
            for k in range(3):
                tx, ty = x + rng.uni(-5, 5), y - rng.uni(6, 20)
                m = m_disc(tx, ty, 2.4)
                c.shade_sphere(m, tx, ty, 2.4, 2.4, p.apple, p.L, amb=0.3)
        elif kind < 0.55:
            # a lettuce
            rx, ry = 8 * sz, 5.4 * sz
            m = m_ellipse(x, y - ry * 0.7, rx, ry)
            c.outline(m, mul(p.lfD, 0.7))
            c.shade_sphere(m, x, y - ry * 0.7, rx, ry, [p.lfD, p.lf, p.lfL, mix(p.lfL, WHITE, 0.4)], p.L, amb=0.3)
            for k in range(5):
                a = k * 1.26 + kind
                c.paint(m_line(x, y - ry * 0.7, x + math.cos(a) * rx * 0.75, y - ry * 0.7 + math.sin(a) * ry * 0.75, 1), p.lfD)
        elif kind < 0.85:
            # carrot tops
            for k in range(rng.int(4, 6)):
                a = -math.pi / 2 + (k - 2) * 0.35 + rng.uni(-0.1, 0.1)
                L = 9 * sz * rng.uni(0.8, 1.1)
                c.paint(m_line(x, y, x + math.cos(a) * L, y + math.sin(a) * L, 1), p.lf)
                c.px(x + math.cos(a) * L, y + math.sin(a) * L, p.lfL)
            c.paint(m_ellipse(x, y, 2.4, 1.2), hx('#e87a28'))
        else:
            # a red beet
            c.paint(m_ellipse(x, y - 1, 3 * sz, 2.4 * sz), hx('#8a1a3a'))
            for k in range(3):
                c.paint(m_line(x, y - 2, x + (k - 1) * 4, y - 9 * sz, 1), hx('#a8304a'))
                c.paint(m_ellipse(x + (k - 1) * 4, y - 9 * sz, 2.4, 1.4), p.lf)
        return
    # autumn: pumpkins, dry stalks, a cabbage
    if kind < 0.4:
        rr = rng.uni(5, 10)
        m = m_ellipse(x, y - rr * 0.6, rr, rr * 0.7)
        c.outline(m, hx('#6a2a0a'))
        c.shade_sphere(m, x, y - rr * 0.6, rr, rr * 0.7, [hx('#8a3a0a'), hx('#c85a14'), hx('#f08a28'), hx('#ffc070')], p.L, amb=0.3)
        for k in (-1, 1):
            c.paint(m_line(x + k * rr * 0.4, y - rr * 1.2, x + k * rr * 0.5, y, 1) & m, hx('#b04a10'))
        c.paint(m_line(x, y - rr * 1.2, x + 2, y - rr * 1.5, 2), hx('#5a6a2a'))
    elif kind < 0.6:
        m = m_ellipse(x, y - 4 * sz, 7 * sz, 5 * sz)
        c.outline(m, hx('#2a1a3a'))
        c.shade_sphere(m, x, y - 4 * sz, 7 * sz, 5 * sz, [hx('#3a2a4a'), hx('#5a4a6a'), hx('#8a7a9a'), hx('#b8c0a0')], p.L, amb=0.3)
        c.paint(m_line(x - 4 * sz, y - 4 * sz, x + 4 * sz, y - 6 * sz, 1) & m, hx('#3a2a4a'))
    elif kind < 0.85:
        for k in range(3):
            c.paint(m_line(x + k * 3, y, x + k * 3 + rng.uni(-2, 2), y - rng.uni(8, 16), 1), hx('#8a6a3a'))


def scarecrow(c, p, s, x, gy):
    """the scarecrow of the farm, near: a pole and a cross bar, a blue checked shirt, straw, a sack head, a hat"""
    lx, ly = C.ldir(p)
    c.darken(m_ellipse(x + p.slant * 16, gy + 2, 22, 3), 0.75, p.cool)
    c.paint(m_rect(x - 2.5, gy - 112, 5, 112), p.woodr[1])
    c.paint(m_rect(x - 2.5, gy - 112, 1, 112), p.woodr[3])
    bar = m_line(x - 46, gy - 86, x + 46, gy - 89, 5)
    c.paint(bar, p.woodr[2])
    c.paint(m_line(x - 46, gy - 88, x + 46, gy - 91, 1), p.woodr[3])
    # the trousers (patched), then the shirt
    for d in (-1, 1):
        lm = m_poly([(x + d * 2, gy - 52), (x + d * 13, gy - 52), (x + d * 12, gy - 24), (x + d * 4, gy - 24)])
        c.outline(lm, hx('#2a2018'))
        c.paint(lm, hx('#6a5a3a'))
        c.paint(lm.side(lx or 1, 0, 2), hx('#8a7a52'))
        for k in range(4):
            c.paint(m_line(x + d * 4 + d * k * 2.6, gy - 24, x + d * 3 + d * k * 3, gy - 16, 1), p.straw[2 if k % 2 else 1])
    c.paint(m_rect(x + 3, gy - 44, 7, 7), hx('#b8342a'))
    c.paint(m_rect(x + 3, gy - 44, 7, 1), hx('#e86a5a'))
    shirt = m_poly([(x - 44, gy - 92), (x + 44, gy - 95), (x + 44, gy - 82), (x + 17, gy - 80), (x + 15, gy - 48),
                    (x - 15, gy - 48), (x - 17, gy - 80), (x - 44, gy - 78)])
    c.outline(shirt, hx('#1a2030'))
    blue = p.paint_blue
    c.paint(shirt, blue[2])
    c.paint_values(shirt, blue, lambda xx, yy: np.where(((xx.astype(int) // 4 + yy.astype(int) // 4) % 2) == 0, 0.3, 0.7), 0.1)
    c.paint(shirt.side(lx or 1, ly, 2), blue[3])
    c.paint(shirt.side(-(lx or 1), -ly, 2), blue[0])
    c.paint(m_line(x, gy - 92, x, gy - 50, 1), blue[0])
    for k in range(4):
        c.paint(m_disc(x + 2, gy - 86 + k * 9, 1.2), WHITE)
    # straw out of the sleeves and the collar
    for sx, d in ((x - 45, -1), (x + 45, 1)):
        for k in range(5):
            c.paint(m_line(sx, gy - 88, sx + d * 6, gy - 94 + k * 3, 1), p.straw[1 + k % 3])
    for k in range(6):
        c.paint(m_line(x - 6 + k * 2.4, gy - 94, x - 8 + k * 3, gy - 99, 1), p.straw[2])
    # the sack head with a stitched face
    hm = m_ellipse(x, gy - 106, 12, 12)
    c.outline(hm, hx('#3a2a1a'))
    c.shade_sphere(hm, x, gy - 106, 12, 12, ramp_of(hx('#d8c090'), s, 4, -0.6, 0.4), p.L, amb=0.3)
    for ex in (-5, 4):
        c.paint(m_line(ex + x - 2, gy - 110, ex + x + 2, gy - 106, 1), hx('#3a2a1a'))
        c.paint(m_line(ex + x + 2, gy - 110, ex + x - 2, gy - 106, 1), hx('#3a2a1a'))
    c.paint(m_polyline([(x - 6, gy - 100), (x - 2, gy - 98), (x + 2, gy - 98), (x + 6, gy - 100)], 1), hx('#3a2a1a'))
    for k in range(5):
        c.px(x - 5 + k * 2.5, gy - 99 + (k % 2), hx('#3a2a1a'))
    c.paint(m_line(x - 8, gy - 96, x + 8, gy - 96, 2), hx('#8a6a3a'))            # the cord at the neck
    # the straw hat with its red band
    brim = m_ellipse(x, gy - 116, 21, 4.4)
    crown = m_ellipse(x, gy - 122, 11, 8) & m_rect(x - 12, gy - 131, 24, 12)
    c.outline(brim | crown, hx('#5a4220'))
    c.paint(brim, p.straw[2])
    c.paint(brim.side(0, 1, 1), p.straw[0])
    c.paint(crown, p.straw[2])
    c.paint(crown.side(lx or 1, 0, 2), p.straw[3])
    c.paint(m_rect(x - 11, gy - 120, 22, 3), hx('#c8342a'))
    for k in range(6):
        c.paint(m_line(x - 18 + k * 7, gy - 115, x - 17 + k * 7, gy - 113, 1), p.straw[1])
    if s == WINTER:
        c.paint(m_ellipse(x, gy - 129, 10, 3), WHITE)
        c.paint(m_ellipse(x - 2, gy - 118, 20, 1.6), WHITE)
        c.paint(m_line(x - 46, gy - 92, x + 46, gy - 95, 2), WHITE)
        c.paint(m_line(x - 44, gy - 95, x + 44, gy - 98, 1), p.snow[2])
    elif s == AUTUMN:
        crow(c, x + 34, gy - 95, -1)
    elif s == SPRING:
        # a small bird on the hat
        c.paint(m_ellipse(x + 6, gy - 133, 3.4, 2.4), hx('#7a6a5a'))
        c.paint(m_disc(x + 9, gy - 135, 1.8), hx('#7a6a5a'))
        c.px(x + 11, gy - 135, hx('#e8b040'))
        c.paint(m_ellipse(x + 6, gy - 132, 2, 1.2), hx('#e8d8b0'))


def crow(c, x, y, d):
    k = hx('#16161e')
    c.paint(m_ellipse(x, y - 4, 6, 4), k)
    c.paint(m_disc(x + d * 6, y - 8, 3), k)
    c.paint(m_poly([(x + d * 8, y - 9), (x + d * 13, y - 8), (x + d * 8, y - 7)]), hx('#3a3a40'))
    c.px(x + d * 7, y - 9, hx('#e8e8f0'))
    c.paint(m_poly([(x - d * 4, y - 6), (x - d * 12, y - 2), (x - d * 4, y - 2)]), k)
    c.paint(m_ellipse(x - d * 1, y - 5, 4, 1.6), hx('#2a2a3a'))
    c.px(x - 1, y, hx('#3a3a40'))
    c.px(x + 1, y, hx('#3a3a40'))


def can(c, p, s):
    """the watering can in front of the bed"""
    x, y = 566, 172
    body = m_rect(x - 9, y - 14, 18, 14) | m_ellipse(x, y, 9, 2)
    c.darken(m_ellipse(x + p.slant * 10, y + 1, 14, 2.4), 0.75, p.cool)
    c.paint(m_line(x + 8, y - 4, x + 20, y - 16, 2.4), p.metal[1])
    c.paint(m_poly([(x + 18, y - 19), (x + 24, y - 17), (x + 21, y - 13)]), p.metal[2])
    c.outline(body, hx('#2a2e34'))
    c.shade_dir(body, p.metal, -1 if p.L[0] < 0 else 1, 0, x - 9, 0, x + 9, 1)
    c.paint(m_rect(x - 9, y - 9, 18, 1), p.metal[0])
    c.paint(m_ellipse(x, y - 14, 9, 2), p.metal[0])
    handle = m_ellipse(x - 3, y - 17, 7, 6) - m_ellipse(x - 3, y - 17, 5, 4) - m_rect(x - 12, y - 16, 20, 10)
    c.paint(handle, p.metal[2])
    if s == WINTER:
        c.paint(m_ellipse(x, y - 15, 10, 2.4), WHITE)


# ---------------------------------------------------------------- the plant of the puzzle
GREEN = None


def plant(c, p, s, v):
    x, y = PX, PY
    # the spot: loosened soil (or snow) round the foot of the plant
    if s == WINTER:
        c.paint(m_ellipse(x, y + 1, 18, 4), p.snow[2])
        c.paint(m_ellipse(x, y, 16, 3), p.snow[3])
    else:
        sm = m_ellipse(x, y + 1, 18, 4.4)
        c.paint(sm, p.soilr[1])
        c.speckle(sm, [p.soilr[0], p.soilr[3]], 0.12, 191)
    if v == 0:
        if s != WINTER:
            c.paint(m_ellipse(x, y + 1, 6, 1.8), p.soilr[0])
        return
    if v == 1:
        mound(c, p, s, x, y)
        return
    if v == 2:
        if s in (SPRING, SUMMER):
            sprout(c, p, x, y, 1.0 if s == SPRING else 1.25)
        elif s == AUTUMN:
            sprout(c, p, x, y, 1.0, dry=True)
        else:
            c.paint(m_line(x, y, x - 1, y - 9, 1.4), hx('#7a6a50'))
            c.paint(m_line(x - 1, y - 9, x - 5, y - 11, 1), hx('#7a6a50'))
            c.paint(m_ellipse(x - 1, y - 2, 6, 1.6), WHITE)
        return
    # 3: a big sunflower (as its season shows it); 4: the head taken
    if s == SPRING:
        sprout(c, p, x, y, 1.5)
    elif s == SUMMER:
        sunflower(c, p, x, y, head=(v == 3))
    else:
        dry_stalk(c, p, s, x, y, head=(v == 3))


def mound(c, p, s, x, y):
    """planted: a mound of fresh soil, the seed packet on a stick"""
    m = m_ellipse(x, y - 1, 12, 4)
    if s == WINTER:
        c.paint(m, p.snow[2])
        c.paint(m.top(1), WHITE)
    else:
        c.shade_sphere(m, x, y - 1, 12, 4, p.soilr, p.L, amb=0.3)
    c.paint(m_rect(x + 5, y - 24, 2, 22), p.woodr[3])
    pk = m_rect(x - 1, y - 30, 14, 17)
    c.outline(pk, hx('#3a3a40'))
    c.paint(pk, hx('#f4ecd8') if s != WINTER else hx('#e4e0d8'))
    c.paint(m_disc(x + 6, y - 23, 4.4), hx('#f2c33a'))
    c.paint(m_disc(x + 6, y - 23, 2), hx('#6a3a1a'))
    c.paint(m_rect(x, y - 16, 12, 2), hx('#4a8a3a'))
    c.paint(m_rect(x, y - 29, 12, 1), hx('#c8342a'))
    if s == WINTER:
        c.paint(m_rect(x - 2, y - 32, 16, 2), WHITE)


def sprout(c, p, x, y, k=1.0, dry=False):
    """a sunflower sprout: a stem, two seed leaves, a pair of true leaves (dry: wilted, brown)"""
    stem = hx('#5a9a3a') if not dry else hx('#8a7a40')
    leaf = [p.lfD, p.lf, p.lfL, mix(p.lfL, WHITE, 0.4)] if not dry else [hx('#6a5a2a'), hx('#8a7a3a'), hx('#b0a050'), hx('#d0c070')]
    h = 16 * k
    tip = (x + (4 if dry else 0), y - h * (0.7 if dry else 1))
    c.paint(m_polyline([(x, y), (x + (1 if dry else 0), y - h * 0.5), tip], 2 * k), stem)
    for d in (-1, 1):
        a = -math.pi / 2 + d * (1.2 if not dry else 1.9)
        cx, cy = x + math.cos(a) * 5 * k, y - h * 0.45 + math.sin(a) * 3 * k
        m = m_ellipse(cx, cy, 5 * k, 2.4 * k, rot=a)
        c.outline(m, mul(leaf[0], 0.7))
        c.shade_sphere(m, cx, cy, 5 * k, 3 * k, leaf, p.L, amb=0.3)
    for d in (-1, 1):
        a = -math.pi / 2 + d * (0.7 if not dry else 1.6)
        cx, cy = tip[0] + math.cos(a) * 6 * k, tip[1] + math.sin(a) * 4 * k
        m = m_ellipse(cx, cy, 6.4 * k, 3.4 * k, rot=a)
        c.outline(m, mul(leaf[0], 0.7))
        c.shade_sphere(m, cx, cy, 6.4 * k, 3.4 * k, leaf, p.L, amb=0.3)
        c.paint(m_line(tip[0], tip[1], cx + math.cos(a) * 4 * k, cy + math.sin(a) * 3 * k, 1) & m, leaf[0])


def big_leaf(c, p, x, y, ang, L, cols):
    """a big heart-shaped sunflower leaf: its stem from (x, y) in the direction ang"""
    sx, sy = x + math.cos(ang) * 6, y + math.sin(ang) * 6
    c.paint(m_line(x, y, sx, sy, 1.6), cols[1])
    cx, cy = sx + math.cos(ang) * L * 0.5, sy + math.sin(ang) * L * 0.5
    m = m_ellipse(cx, cy, L * 0.55, L * 0.36, rot=ang)
    tipm = m_poly([(cx + math.cos(ang) * L * 0.4 - math.sin(ang) * L * 0.2, cy + math.sin(ang) * L * 0.4 + math.cos(ang) * L * 0.2),
                   (cx + math.cos(ang) * L * 0.75, cy + math.sin(ang) * L * 0.75 + 2),
                   (cx + math.cos(ang) * L * 0.4 + math.sin(ang) * L * 0.2, cy + math.sin(ang) * L * 0.4 - math.cos(ang) * L * 0.2)])
    lm = m | tipm
    c.outline(lm, mul(cols[0], 0.7))
    c.shade_sphere(lm, cx, cy, L * 0.6, L * 0.45, cols, p.L, amb=0.3)
    c.paint(m_line(sx, sy, cx + math.cos(ang) * L * 0.6, cy + math.sin(ang) * L * 0.6 + 1, 1) & lm, cols[0])
    for k in (-1, 1):
        for t in (0.3, 0.6):
            bx = sx + math.cos(ang) * L * t
            by = sy + math.sin(ang) * L * t
            c.paint(m_line(bx, by, bx + math.cos(ang + k * 0.9) * L * 0.25, by + math.sin(ang + k * 0.9) * L * 0.25, 1) & lm, cols[1])


def sunflower(c, p, x, y, head=True):
    """summer: the tall sunflower; head = its big head full of seeds (else the top is cut)"""
    lx, ly = C.ldir(p)
    top = (x - 6, 38)
    stem_pts = [(x, y), (x + 2, y - 40), (x - 2, y - 70), top]
    sm = m_polyline(stem_pts, 6, 4.4)
    c.outline(sm, hx('#1e3a14'))
    c.paint(sm, hx('#4a8a2a'))
    c.paint(sm.side(lx or 1, 0, 2), hx('#7ab84a'))
    c.paint(sm.side(-(lx or 1), 0, 1), hx('#2e5a1e'))
    cols = [p.lfD, mix(p.lfD, p.lf, 0.5), p.lf, p.lfL, mix(p.lfL, WHITE, 0.35)]
    for (t, ang, L) in [(0.15, 0.5, 24), (0.28, 2.7, 26), (0.45, 0.3, 24), (0.6, 2.9, 22), (0.75, 0.1, 18), (0.86, 3.0, 16)]:
        lx_, ly_, _, _ = C.along(stem_pts, 6, 4, t)
        big_leaf(c, p, lx_, ly_, ang, L, cols)
    if not head:
        c.paint(m_ellipse(top[0], top[1], 3, 1.4), hx('#c8e090'))
        c.paint(m_ellipse(top[0], top[1], 1.6, 0.8), hx('#8ab050'))
        return
    hx_, hy_ = x - 8, 30
    # the petals (two rings), the disc full of seeds (black and white striped seeds in a spiral)
    yel = [hx('#a86a08'), hx('#e0a018'), hx('#f8cc30'), hx('#fff080')]
    for ring, (R, n, sz) in enumerate([(25, 22, 8.4), (22, 18, 7.4)]):
        for k in range(n):
            a = 2 * math.pi * k / n + ring * 0.14
            px_, py_ = hx_ + math.cos(a) * R, hy_ + math.sin(a) * R * 0.92
            pm = m_ellipse(px_, py_, sz, 3.0, rot=a)
            c.outline(pm, hx('#7a4a08'))
            lit = math.cos(a) * p.L[0] + math.sin(a) * p.L[1]
            c.paint(pm, yel[2] if lit > -0.2 else yel[1])
            c.paint(m_ellipse(px_ - math.cos(a) * 2, py_ - math.sin(a) * 2, sz * 0.5, 1.2, rot=a), yel[3] if lit > 0.2 else yel[2])
    disc = m_ellipse(hx_, hy_, 17, 15.6)
    c.outline(disc, hx('#2a1606'))
    c.shade_sphere(disc, hx_, hy_, 17, 15.6, [hx('#2a1606'), hx('#4a2a0e'), hx('#6a4018'), hx('#8a5a24')], p.L, amb=0.35)
    for n in range(1, 170):
        r = 1.25 * math.sqrt(n)
        if r > 15.5:
            break
        a = n * 2.39996
        sx, sy = hx_ + math.cos(a) * r, hy_ + math.sin(a) * r * 0.92
        c.px(sx, sy, hx('#16100c'))
        if n % 2 == 0:
            c.px(sx + 1, sy, hx('#c8c0b0'))
    c.paint(m_ellipse(hx_ - 1, hy_ - 1, 4, 3.6), hx('#6a5a20'))
    c.paint(m_ellipse(hx_ - 1.6, hy_ - 1.6, 2, 1.6), hx('#9a8a3a'))
    # bees at the flower (the hive is free: they pollinated it)
    for (bx, by) in [(hx_ + 32, hy_ - 10), (hx_ - 34, hy_ + 6), (hx_ + 26, hy_ + 22)]:
        c.paint(m_ellipse(bx, by, 3, 2), hx('#f2c030'))
        c.paint(m_rect(bx - 0.6, by - 2, 1.4, 4), hx('#1a1a1a'))
        c.paint(m_ellipse(bx - 1, by - 3, 2, 1.2), hx('#e8f4ff'))
        c.px(bx + 3, by, hx('#1a1a1a'))


def dry_stalk(c, p, s, x, y, head=True):
    """autumn and winter: the dry stalk of the sunflower; head = its bent dry head (else the stalk is cut)"""
    brown = [hx('#3a2a14'), hx('#5a4220'), hx('#7a5e30'), hx('#a08048')]
    if s == WINTER:
        brown = [hx('#3a3028'), hx('#5a4c3a'), hx('#7a6a54'), hx('#9a8a74')]
    top = (x - 4, 44)
    stem_pts = [(x, y), (x + 2, y - 40), (x - 1, y - 70), top]
    sm = m_polyline(stem_pts, 5.6, 4)
    c.outline(sm, mul(brown[0], 0.7))
    c.paint(sm, brown[2])
    c.paint(sm.side(C.ldir(p)[0] or 1, 0, 1), brown[3])
    for (t, ang, L) in [(0.2, 1.3, 16), (0.38, 1.9, 18), (0.58, 1.2, 14), (0.74, 2.0, 12)]:
        lx_, ly_, _, _ = C.along(stem_pts, 6, 4, t)
        tip = (lx_ + math.cos(ang) * L, ly_ + math.sin(ang) * L)
        lm = m_polyline([(lx_, ly_), ((lx_ + tip[0]) / 2 + 3, (ly_ + tip[1]) / 2), tip], 3.4, 1.2)
        c.paint(lm, brown[1])
        c.paint(m_polyline([(lx_, ly_), tip], 1), brown[0])
    if s == WINTER:
        c.snow_cap(sm, 2, p.snow)
    if not head:
        c.paint(m_ellipse(top[0], top[1], 2.6, 1.2), brown[3])
        if s == WINTER:
            c.paint(m_ellipse(top[0], top[1] - 1, 3.4, 1.4), WHITE)
        return
    # the head bends down (its back to us), a fringe of dry petals
    neck = m_polyline([top, (top[0] - 6, top[1] - 6), (top[0] - 14, top[1] - 4)], 4)
    c.paint(neck, brown[2])
    hc = (top[0] - 20, top[1] + 4)
    hm = m_ellipse(hc[0], hc[1], 13, 15, rot=0.5)
    c.outline(hm, mul(brown[0], 0.6))
    c.shade_sphere(hm, hc[0], hc[1], 13, 15, brown, p.L, amb=0.3)
    for k in range(12):
        a = 0.5 + k * 0.52
        px_, py_ = hc[0] + math.cos(a) * 14, hc[1] + math.sin(a) * 15
        c.paint(m_line(px_, py_, px_ + math.cos(a) * 3, py_ + math.sin(a) * 4 + 2, 1.4), hx('#8a5a1a') if s != WINTER else brown[1])
    for k in range(6):
        c.paint(m_line(hc[0] - 8 + k * 3, hc[1] - 10, hc[0] - 9 + k * 3.4, hc[1] + 10, 1) & hm, brown[1])
    if s == WINTER:
        cap = m_ellipse(hc[0] + 2, hc[1] - 12, 11, 4)
        c.paint(cap, p.snow[2])
        c.paint(cap.top(1), WHITE)
    elif s == AUTUMN:
        # a few seeds dropped, a sparrow pecks at them
        for k in range(5):
            c.px(x - 14 + k * 4, y + 1 + (k % 2), hx('#1a1410'))
