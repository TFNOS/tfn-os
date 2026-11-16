# FOUR SEASONS: THE HIVE (a close view): the old wooden bee hive under the apple tree of the farm, near. Two painted
# boxes on a bottom board with its door slot and landing board, a lid of tin with a stone on it, the stand of two
# cinder blocks; the second hive behind on the right. Spring and summer: many bees; autumn: a few bees; winter: snow on
# the lid, the bees sleep inside, and small mouse tracks in the snow go to a gnawed hole at the bottom left corner:
# when nest = 1 the straw of a mouse nest shows in the hole.
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER, ramp_of
import fsparts as P
import fsparts_fahm as F

W, H = 672, 180
VARS = {'nest': [1, 0]}
HOT = {'hive': (176, 38, 236, 132)}

WHITE = (255, 255, 255)
HX0, HX1 = 194, 352               # the front of the hive (left, right)
HD = (40, -16)                    # the depth of the hive (the side goes to the back right)
GY = 172                          # the ground at the hive
BOT = 150                         # the bottom of the bottom board (the top of the blocks)
HOLE = (212, 143)                 # the gnawed hole of the mouse (centre)
TREE = {}


def ground_y(x):
    return 128 + 1.5 * math.sin(x * 0.017 + 0.3)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    sunx = p.sun_xy[0] * W / 640
    suny = p.sun_xy[1] * 1.1 + 6
    F.close_sky(c, p, 132, sunx, suny, 47, 4)
    P.far_ridge(c, p, 110, 9, 0.01, 31, step=8, tree_h=(4, 9))
    field(c, p, s)
    ground(c, p, s)
    apple_tree(c, p, s, back=True)
    water_dish(c, p, s)
    hive2(c, p, s)
    hive(c, p, s, st)
    apple_tree(c, p, s, back=False)
    front_plants(c, p, s)
    bees(c, p, s)
    P.particles_baked(c, p, (20, 20, 660, 160), {SPRING: 18, SUMMER: 0, AUTUMN: 22, WINTER: 34}[s], 23)
    return c


def field(c, p, s):
    top = [(x, 112 + 1.4 * math.sin(x * 0.021)) for x in range(-4, W + 5, 4)]
    fm = m_poly(top + [(W + 4, 140), (-4, 140)])
    cols = {SPRING: [mix(p.soil, p.hill, 0.25), mix(p.lf, p.hill, 0.45)],
            SUMMER: [hx('#c8a040'), hx('#e8c860')],
            AUTUMN: [hx('#9a7a3e'), hx('#c0a060')],
            WINTER: [p.snow[2], p.snow[1]]}[s]
    c.paint(fm, cols[0])

    def fn(x, y, bay):
        k = np.floor((y - 110) / 3.0 + np.sin(x * 0.021) * 0.5).astype(int)
        return np.where(k % 2 == 0, 1, -1)
    F.paint_idx(c, fm, cols, fn)
    if s == WINTER:
        c.speckle(fm, [hx('#a8987a')], 0.05, 5)


def ground(c, p, s):
    gm = m_poly([(-4, H + 4)] + [(x, ground_y(x)) for x in range(-4, W + 5, 4)] + [(W + 4, H + 4)])
    if s != WINTER:
        c.paint(gm, p.hill)
        P.grass_marks(c, p, gm, 2600, 17, hmin=2, hmax=5)
        # clover leaves in the grass (spring, summer)
        if s in (SPRING, SUMMER):
            rng = RNG(9)
            for k in range(90):
                x, y = rng.uni(0, W), rng.uni(140, 178)
                for a in (0, 2.1, 4.2):
                    c.paint(m_disc(x + math.cos(a) * 1.6, y + math.sin(a) * 1.2, 1.3), mix(p.lf, p.lfD, 0.3))
        if s == AUTUMN:
            rng = RNG(19)
            for k in range(160):
                x, y = rng.uni(0, W), rng.uni(134, 179)
                col = rng.pick([hx('#e8582a'), hx('#c9351f'), hx('#ffb23d'), hx('#a3542a'), hx('#f2c33a')])
                c.paint(m_ellipse(x, y, 2.2, 1.2, rot=rng.uni(-1, 1)), col)
                c.px(x - 1, y, mul(col, 0.7))
    else:
        c.paint(gm, p.snow[2])
        c.paint_values(gm, [p.snow[1], p.snow[2], p.snow[3]], lambda x, y: np.clip(0.55 + 0.35 * np.sin(x * 0.02 + y * 0.05) * np.sin(x * 0.007), 0, 1), 0.35)
        c.speckle(gm, [WHITE, p.snow[1]], 0.03, 3)
        tracks(c, p)


def tracks(c, p):
    """the tracks of the mouse in the snow: small groups of four dots, from the left to the hole of the hive"""
    pts = bezier((20, 178), (90, 150), (150, 182), (HOLE[0] - 6, HOLE[1] + 10), 26)
    # the thin line of the tail between them
    c.paint(m_polyline(pts, 1.0), p.snow[1])
    dark = mix(p.snow[0], p.cool, 0.6)
    for i, (x, y) in enumerate(pts[:-1]):
        nx, ny = pts[i + 1]
        a = math.atan2(ny - y, nx - x)
        ca, sa = math.cos(a), math.sin(a)
        # two hind feet (big) and two front feet (small) a little in front of them
        for side in (-1, 1):
            hxp, hyp = x - sa * side * 2.6, y + ca * side * 2.6
            c.paint(m_rect(hxp, hyp, 2, 2), dark)
            fxp, fyp = x + ca * 4 - sa * side * 1.5, y + sa * 4 + ca * side * 1.5
            c.px(fxp, fyp, p.snow[0])


def water_dish(c, p, s):
    """a clay dish with water and stones for the bees (left, near the fence)"""
    x, y = 132, 160
    c.darken(m_ellipse(x + p.slant * 4, y + 3, 22, 4), 0.72, p.cool)
    clay = ramp_of(hx('#c06a3a'), s, 4, -0.6, 0.4)
    c.paint(m_ellipse(x, y + 1, 20, 6), clay[1])
    c.paint(m_ellipse(x, y - 1, 20, 5.4), clay[2])
    inner = m_ellipse(x, y - 1, 17, 4.2)
    if s == WINTER:
        c.paint(inner, p.ice[2])
        c.paint(m_ellipse(x - 3, y - 2, 9, 1.6), WHITE)
        c.paint(m_ellipse(x + 4, y - 4, 14, 2.6), WHITE)
    else:
        c.paint(inner, p.waterr[2])
        c.paint(m_line(x - 10, y - 2, x + 2, y - 2, 1), p.waterr[4])
        for (sx, sy) in ((x - 6, y), (x + 5, y - 2), (x + 10, y + 1)):
            P.rock(c, p, sx, sy, 1.6)
        if s == AUTUMN:
            c.paint(m_ellipse(x + 2, y, 3, 1.4, rot=0.4), hx('#e8582a'))
    c.paint(m_ellipse(x, y - 1, 20, 5.4).top(1), clay[3])


def apple_tree(c, p, s, back):
    """the apple tree on the right: its trunk and its crown over the top right"""
    if 'apple' not in TREE:
        TREE['apple'] = P.Tree(600, 190, seed=228, depth=4, length=56, width=24, angle=-math.pi / 2 - 0.12,
                               crown=(170, 46), nclump=420, clump_r=(4.5, 9.5), spread=0.75)
    t = TREE['apple']
    if back:
        c.darken(m_ellipse(560 + p.slant * 20, 170, 120, 9), 0.76, p.cool)
        t.draw_wood(c, p)
        tm = m_poly([(578, 182), (586, 120), (592, 90), (612, 90), (616, 120), (626, 182)])
        rng = RNG(5)
        for k in range(9):
            x = rng.uni(584, 620)
            c.paint(m_polyline([(x, 182), (x + rng.uni(-3, 3), 150), (x + rng.uni(-4, 4), 110)], 1) & tm, p.bark[0])
        if s == WINTER:
            c.paint(m_polyline([(578, 140), (584, 112), (590, 90)], 2) & tm.grow(1), WHITE)
        return
    t.draw_leaves(c, p, seed=6)
    if s in (SUMMER, AUTUMN):
        n = 0
        for (cx, cy, r0, th, hv, ph) in t.clumps:
            if hv > 0.66 and n < 30 and cy > 10:
                col = p.apple
                m = m_disc(cx + 2, cy + r0 * 0.45, 3.4)
                c.shade_sphere(m, cx + 2, cy + r0 * 0.45, 3.4, 3.4, col if s == AUTUMN or hv > 0.83 else ramp_of(hx('#9acb3a'), s, 4, -0.6, 0.4), p.L, amb=0.3)
                c.paint(m_line(cx + 2, cy + r0 * 0.45 - 3, cx + 3, cy + r0 * 0.45 - 5, 1), p.bark[1])
                n += 1
    if s == AUTUMN:
        rng = RNG(15)
        for k in range(8):
            x = rng.uni(470, 660)
            y = rng.uni(150, 172)
            m = m_disc(x, y, 3.2)
            c.shade_sphere(m, x, y, 3.2, 3.2, p.apple, p.L, amb=0.3)


# ---------------------------------------------------------------- the hives
def box_front(c, p, s, x0, x1, y0, y1, ramp, seed, wood):
    """one box of the hive: the painted front, the side, the hand holds, the corner joints, worn paint"""
    nf = (-0.25, 0.0, 0.97)
    fm = m_rect(x0, y0, x1 - x0, y1 - y0)
    fc = F.face(p, ramp, nf)
    # (painted boards: the grain of the wood shows under the paint)
    i = ramp.index(fc)
    pc = [ramp[max(0, i - 2)], ramp[max(0, i - 1)], fc, ramp[min(3, i + 1)], mix(ramp[min(3, i + 1)], WHITE, 0.3)]
    nb = 2 if y1 - y0 > 40 else 1
    F.boards(c, p, fm, (x0, y0), (x1, y0), (x0, y1), nb, pc, seed=seed, knots=1, nails=False, grain=0.55, tones=False)
    # (worn paint: the wood shows at the lower edge and in a few flakes)
    F.patches(c, fm & m_rect(x0, y1 - 6, x1 - x0, 6), mix(wood[2], fc, 0.2), 3, 0.2, seed + 1, 0.0)
    F.patches(c, fm, mix(wood[2], fc, 0.35), 4, 0.5, seed, 0.0)
    c.paint(m_rect(x0, y0, x1 - x0, 1), ramp[min(3, ramp.index(fc) + 1)])
    c.paint(m_rect(x0, y1 - 1, x1 - x0, 1), ramp[0])
    # the side
    sm = m_poly([(x1, y0), (x1 + HD[0], y0 + HD[1]), (x1 + HD[0], y1 + HD[1]), (x1, y1)])
    sc = F.face(p, ramp, (1, 0, 0), 0.05)
    c.paint(sm, sc)
    F.patches(c, sm, mix(wood[2], sc, 0.35), 4, 0.5, seed + 2, 0.0)
    c.paint(m_line(x1, y1 - 1, x1 + HD[0], y1 - 1 + HD[1], 1), ramp[0])
    # the corner joints (the ends of the side boards show at the front corners)
    for yy in range(int(y0) + 2, int(y1) - 2, 8):
        c.paint(m_rect(x0, yy, 3, 4), mix(wood[3], fc, 0.3))
        c.paint(m_rect(x1 - 3, yy + 4, 3, 4), mix(wood[3], fc, 0.3))
    # the hand hold on the front and on the side
    hm = (x0 + x1) / 2
    hy = y0 + (y1 - y0) * 0.32
    c.paint(m_rect(hm - 18, hy, 36, 5), ramp[0])
    c.paint(m_rect(hm - 18, hy, 36, 1), mul(ramp[0], 0.7))
    c.paint(m_rect(hm - 18, hy + 5, 36, 1), ramp[3])
    sx, sy = x1 + HD[0] * 0.5, hy + HD[1] * 0.5
    c.paint(m_poly([(sx - 8, sy + 3), (sx + 8, sy - 3), (sx + 8, sy + 1), (sx - 8, sy + 7)]), ramp[0])
    # nails
    for (nx, ny) in ((x0 + 6, y0 + 4), (x1 - 6, y0 + 4), (x0 + 6, y1 - 5), (x1 - 6, y1 - 5)):
        c.px(nx, ny, p.iron[0])
        c.px(nx - 1, ny - 1, p.iron[3])


def hive(c, p, s, st):
    wood = F.wood_ramp(p)
    blue = p.paint_blue
    cream = p.paint_white
    # the shadow on the ground
    c.darken(m_poly([(HX0 - 6, GY), (HX1 + HD[0], GY + HD[1] + 2), (HX1 + HD[0] + p.slant * 36, GY + 6), (HX0 - 6 + p.slant * 36, GY + 8)]), 0.72, p.cool)
    # the stand: two cinder blocks
    conc = ramp_of(hx('#a8a49c'), s, 4, -0.6, 0.45)
    for bx in (HX0 + 6, HX1 - 50):
        fm = m_rect(bx, BOT, 44, GY - BOT)
        c.paint(fm, F.face(p, conc, (-0.25, 0, 0.97)))
        c.speckle(fm, [conc[1], conc[3]], 0.12, bx)
        for hx_ in (bx + 5, bx + 25):
            c.paint(m_rect(hx_, BOT + 5, 14, GY - BOT - 10), conc[0])
            c.paint(m_rect(hx_, BOT + 5, 14, 2), mul(conc[0], 0.7))
        c.paint(m_poly([(bx + 44, BOT), (bx + 44 + 10, BOT - 4), (bx + 54, GY - 4), (bx + 44, GY)]), F.face(p, conc, (1, 0, 0)))
        if s == WINTER:
            c.paint(m_rect(bx - 2, GY - 6, 60, 8), p.snow[2])
            c.paint(m_rect(bx - 2, GY - 6, 60, 1), WHITE)
    # the bottom board with the door slot, and the landing board in front of it
    bb = m_rect(HX0 - 2, BOT - 12, HX1 - HX0 + 4, 12)
    c.paint(bb, F.face(p, wood, (-0.25, 0, 0.97)))
    c.paint(m_rect(HX0 - 2, BOT - 12, HX1 - HX0 + 4, 1), wood[3])
    c.paint(m_poly([(HX1 + 2, BOT - 12), (HX1 + 2 + HD[0], BOT - 12 + HD[1]), (HX1 + 2 + HD[0], BOT + HD[1]), (HX1 + 2, BOT)]), F.face(p, wood, (1, 0, 0)))
    slot = m_rect(HX0 + 40, BOT - 11, HX1 - HX0 - 70, 6)
    c.paint(slot, hx('#120c08'))
    c.paint(m_rect(HX0 + 40, BOT - 11, HX1 - HX0 - 70, 1), hx('#2a1e14'))
    land = m_poly([(HX0 + 30, BOT - 5), (HX1 - 20, BOT - 5), (HX1 - 14, BOT + 3), (HX0 + 24, BOT + 3)])
    c.paint(land, wood[3])
    c.paint(m_rect(HX0 + 24, BOT + 2, HX1 - HX0 - 38, 2), wood[1])
    c.paint(m_line(HX0 + 30, BOT - 5, HX1 - 20, BOT - 5, 1), wood[4])
    # the gnawed hole of the mouse at the bottom left corner (rough edges, dark inside)
    hxc, hyc = HOLE
    hole = m_poly([(hxc + dx * 1.5, hyc + dy * 1.5) for dx, dy in
                   ((-10, 4), (-9, -3), (-5, -6), (-1, -5), (3, -7), (7, -4), (10, 1), (9, 4))])
    c.paint(hole.grow(1), mix(wood[3], wood[4], 0.5))
    c.paint(hole, hx('#0c0806'))
    c.paint(hole.top(2) - hole.top(1), hx('#1e140c'))
    # the two boxes (the deep box of the brood: blue; the box of the honey: cream) and the lid
    y_b = BOT - 12
    box_front(c, p, s, HX0, HX1, y_b - 50, y_b, blue, 11, wood)
    box_front(c, p, s, HX0, HX1, y_b - 50 - 34, y_b - 50, cream, 13, wood)
    ly = y_b - 84
    lid_f = m_rect(HX0 - 6, ly - 10, HX1 - HX0 + 12, 10)
    tin = ramp_of(hx('#9aa4aa'), s, 4, -0.7, 0.55)
    c.paint(lid_f, F.face(p, tin, (-0.25, 0, 0.97)))
    c.paint(m_rect(HX0 - 6, ly - 10, HX1 - HX0 + 12, 1), tin[3])
    c.paint(m_rect(HX0 - 6, ly - 1, HX1 - HX0 + 12, 1), tin[0])
    top = m_poly([(HX0 - 6, ly - 10), (HX1 + 6, ly - 10), (HX1 + 6 + HD[0], ly - 10 + HD[1]), (HX0 - 6 + HD[0], ly - 10 + HD[1])])
    c.paint(top, F.face(p, tin, (0, -1, 0), 0.1))
    c.paint(m_poly([(HX1 + 6, ly - 10), (HX1 + 6 + HD[0], ly - 10 + HD[1]), (HX1 + 6 + HD[0], ly + HD[1]), (HX1 + 6, ly)]), F.face(p, tin, (1, 0, 0)))
    F.patches(c, lid_f | top, mix(tin[1], hx('#a0522d'), 0.45), 5, 0.4, 3, 0.05)
    # the stone on the lid
    sx, sy = HX0 + 100, ly - 18
    sm = m_ellipse(sx, sy, 16, 8)
    c.shade_sphere(sm, sx, sy, 16, 8, p.stone, p.L, amb=0.25)
    c.paint(sm.bottom(1), p.stone[0])
    if s == WINTER:
        winter_hive(c, p, ly, sx, sy)
    # the straw of the mouse nest in the hole (only winter shows it: the grass hides the hole in the other seasons)
    if st.get('nest', 1) and s == WINTER:
        straw = ramp_of(hx('#e0c060'), s, 4, -0.55, 0.5)
        rng = RNG(77)
        tuft = hole - hole.top(4)
        c.shade_sphere(tuft, hxc - 2, hyc + 2, 14, 8, straw, p.L, amb=0.4)
        # strands that stick out of the hole and hang on the board
        for k in range(34):
            x = rng.uni(hxc - 12, hxc + 12)
            y = rng.uni(hyc - 3, hyc + 5)
            L = rng.uni(4, 12)
            a = rng.uni(-3.0, -0.2) if k % 3 else rng.uni(0.2, 2.9)
            q = bezier((x, y), (x + math.cos(a) * L * 0.5, y + math.sin(a) * L * 0.3 - 1), (x + math.cos(a) * L, y + math.sin(a) * L * 0.6 + 1), None, 4)
            c.paint(m_polyline(q, 1.0), rng.pick([straw[1], straw[2], straw[3], straw[3]]))
        for (a0, b0) in (((hxc - 20, hyc + 8), (hxc - 8, hyc + 3)), ((hxc + 6, hyc + 4), (hxc + 20, hyc + 9))):
            c.paint(m_line(a0[0], a0[1], b0[0], b0[1], 1), straw[3])
        # bits of fluff (wool, paper) in the straw
        c.paint(m_ellipse(hxc + 3, hyc, 3.2, 2.0), hx('#f4ece0'))
        c.paint(m_ellipse(hxc - 6, hyc + 3, 2.2, 1.4), hx('#e8dccc'))
        c.paint(m_rect(hxc - 2, hyc + 2, 4, 2), hx('#c8d8e8'))


def winter_hive(c, p, ly, sx, sy):
    """snow on the lid and the stone, on the landing board; icicles from the lid"""
    F.snow_slab(c, p, HX0 - 8, HX1 + 8, ly - 10, 9, seed=3, droop=2)
    top_snow = m_poly([(HX0 - 8, ly - 12), (HX0 - 6 + HD[0], ly - 12 + HD[1] - 6), (HX1 + 8 + HD[0], ly - 10 + HD[1] - 6), (HX1 + 8, ly - 12),
                       (HX1 + 6, ly - 19), (HX0 - 6, ly - 19)])
    c.paint(top_snow, p.snow[2])
    c.paint(top_snow.top(1), WHITE)
    cap = m_ellipse(sx, sy - 5, 15, 5)
    c.paint(cap, WHITE)
    c.paint(cap.bottom(1), p.snow[1])
    F.icicles(c, p, HX0 - 4, HX1 + 4, ly, 3, lmax=12, step=5, dens=0.7)
    c.paint(m_poly([(HX0 + 30, BOT - 7), (HX1 - 20, BOT - 7), (HX1 - 16, BOT - 4), (HX0 + 26, BOT - 4)]), p.snow[2])
    c.paint(m_line(HX0 + 30, BOT - 7, HX1 - 20, BOT - 7, 1), WHITE)
    # frost on the lower edge of the boxes
    c.paint_dither(m_rect(HX0, BOT - 15, HX1 - HX0, 3), WHITE, 0.5)


def hive2(c, p, s):
    """the second hive behind on the right (white and yellow), smaller: it stands further back"""
    x0, x1, gy = 410, 500, 148
    wood = F.wood_ramp(p)
    D = (26, -10)
    c.darken(m_poly([(x0 - 4, gy), (x1 + D[0], gy + D[1]), (x1 + D[0] + p.slant * 22, gy + 4), (x0 - 4 + p.slant * 22, gy + 6)]), 0.74, p.cool)
    for lx in (x0 + 6, x1 - 12):
        c.paint(m_rect(lx, gy - 12, 6, 12), wood[1])
        c.paint(m_rect(lx, gy - 12, 2, 12), wood[3])
    c.paint(m_rect(x0 - 2, gy - 18, x1 - x0 + 4, 6), F.face(p, wood, (0, 0, 1)))
    c.paint(m_rect(x0 + 24, gy - 17, 40, 3), hx('#120c08'))
    ys = gy - 18
    for k, (h, ramp) in enumerate(((30, p.paint_yel), (22, p.paint_white))):
        fm = m_rect(x0, ys - h, x1 - x0, h)
        fc = F.face(p, ramp, (-0.25, 0, 0.97))
        c.paint(fm, fc)
        F.patches(c, fm, mix(wood[2], fc, 0.3), 6, 0.4, 40 + k, 0.05)
        c.paint(m_rect(x0, ys - h, x1 - x0, 1), ramp[3])
        c.paint(m_rect(x0, ys - 1, x1 - x0, 1), ramp[0])
        c.paint(m_poly([(x1, ys - h), (x1 + D[0], ys - h + D[1]), (x1 + D[0], ys + D[1]), (x1, ys)]), F.face(p, ramp, (1, 0, 0)))
        c.paint(m_rect((x0 + x1) / 2 - 12, ys - h * 0.68, 24, 3), ramp[0])
        ys -= h
    tin = ramp_of(hx('#9aa4aa'), s, 4, -0.7, 0.55)
    c.paint(m_rect(x0 - 4, ys - 7, x1 - x0 + 8, 7), F.face(p, tin, (0, 0, 1)))
    c.paint(m_poly([(x0 - 4, ys - 7), (x1 + 4, ys - 7), (x1 + 4 + D[0], ys - 7 + D[1]), (x0 - 4 + D[0], ys - 7 + D[1])]), F.face(p, tin, (0, -1, 0), 0.1))
    c.paint(m_poly([(x1 + 4, ys - 7), (x1 + 4 + D[0], ys - 7 + D[1]), (x1 + 4 + D[0], ys + D[1]), (x1 + 4, ys)]), F.face(p, tin, (1, 0, 0)))
    if s == WINTER:
        F.snow_slab(c, p, x0 - 6, x1 + 6, ys - 7, 6, seed=8)
        c.paint(m_poly([(x0 - 6, ys - 9), (x0 - 4 + D[0], ys - 9 + D[1] - 4), (x1 + 6 + D[0], ys - 7 + D[1] - 4), (x1 + 6, ys - 9), (x1 + 4, ys - 13), (x0 - 4, ys - 13)]), p.snow[2])
        F.icicles(c, p, x0 - 2, x1 + 2, ys, 4, lmax=7, step=5)


# ---------------------------------------------------------------- the plants and the bees
def front_plants(c, p, s):
    """the plants at the foot of the hive and in front (the grass hides the gnawed hole outside the winter)"""
    if s != WINTER:
        # a thick tuft in front of the hole of the mouse
        F.tall_grass(c, p, HOLE[0] - 28, HOLE[0] + 26, lambda x: GY + 4, 90, 22, 40, 51, lean=0.1)
        if s == SPRING:
            # dandelions in the tuft
            F.close_flowers(c, p, [(HOLE[0] - 12, GY - 18), (HOLE[0] + 8, GY - 22), (HOLE[0] - 2, GY - 12)], 3, 1.4)
        F.tall_grass(c, p, 0, W, lambda x: H + 2, 160, 8, 22, 52, lean=-0.05)
        F.tall_grass(c, p, HX1 - 20, HX1 + 50, lambda x: GY + 2, 18, 8, 18, 53)
        rng = RNG(4)
        pts = [(rng.uni(20, 660), rng.uni(150, 178)) for _ in range(34)]
        F.close_flowers(c, p, pts, 6, 1.3)
        if s == SUMMER:
            lavender(c, p, 40, 150)
            lavender(c, p, 74, 156)
        if s == AUTUMN:
            c.paint(m_ellipse(HOLE[0] + 4, GY + 2, 24, 4), hx('#a3542a'))
            rng = RNG(55)
            for k in range(30):
                x, y = rng.uni(HOLE[0] - 22, HOLE[0] + 30), rng.uni(GY - 4, GY + 6)
                c.paint(m_ellipse(x, y, 2.6, 1.4, rot=rng.uni(-1, 1)), rng.pick([hx('#e8582a'), hx('#c9351f'), hx('#ffb23d'), hx('#a3542a')]))
    else:
        F.tall_grass(c, p, 0, W, lambda x: H + 2, 50, 6, 16, 52)
        # a small drift of snow at the foot of the hive (it leaves the hole open: the mouse goes in and out)
        c.paint(m_ellipse(HX1 - 30, BOT + 1, 60, 5) - m_rect(HX0 - 10, BOT - 10, 60, 20), p.snow[2])
        c.paint((m_ellipse(HX1 - 30, BOT + 1, 60, 5) - m_rect(HX0 - 10, BOT - 10, 60, 20)).top(1), WHITE)


def lavender(c, p, x, y):
    rng = RNG(int(x))
    for k in range(14):
        a = rng.uni(-0.5, 0.5)
        L = rng.uni(14, 26)
        x1, y1 = x + math.sin(a) * L, y - math.cos(a) * L
        c.paint(m_line(x, y, x1, y1, 1), mix(p.lfD, hx('#8a9a7a'), 0.4))
        for j in range(5):
            c.paint(m_disc(x1 - math.sin(a) * j * 1.8, y1 + math.cos(a) * j * 1.8, 1.4), hx('#8a62c8') if j % 2 else hx('#a884e0'))


def bee_big(c, x, y, k):
    """a bee of the close view (about 8 x 6 px): striped body, head, light wings"""
    body = m_ellipse(x, y, 3.6, 2.2)
    c.paint(body, hx('#f2b830'))
    c.paint(m_rect(x - 1, y - 2, 1, 5) & body, hx('#2a1e10'))
    c.paint(m_rect(x + 1, y - 2, 1, 5) & body, hx('#2a1e10'))
    c.paint(m_disc(x + 4, y, 1.4), hx('#2a1e10'))
    c.paint(m_line(x - 4, y, x - 5, y, 1), hx('#2a1e10'))
    wy = y - 3 - (k % 2)
    c.paint(m_ellipse(x - 1, wy, 2.4, 1.4, rot=-0.3), hx('#eef6ff'))
    c.paint(m_ellipse(x + 1.5, wy + 0.5, 1.8, 1.2, rot=0.3), hx('#cfe0f0'))


def bees(c, p, s):
    n = {SPRING: 22, SUMMER: 34, AUTUMN: 5, WINTER: 0}[s]
    rng = RNG(71)
    for i in range(n):
        # (most bees fly near the door of the hive and to the flowers)
        if rng() < 0.5:
            x = rng.uni(HX0 + 20, HX1 + 40)
            y = rng.uni(BOT - 40, BOT + 10)
            if HX0 - 2 < x < HX1 + 2 and BOT - 140 < y < BOT - 12:
                y = BOT - 14 - rng.uni(0, 30) if rng() < 0.5 else BOT + rng.uni(2, 12)
        else:
            x = rng.uni(20, 650)
            y = rng.uni(60, 168)
        bee_big(c, x, y, i)
        # the path of the flight (three dots)
        if i % 3 == 0:
            for j in range(1, 4):
                c.px(x - 5 - j * 4, y + math.sin(j + i) * 2, mix(p.hill, WHITE, 0.4) if y > 130 else mix(p.skyB, WHITE, 0.5))
    if s in (SPRING, SUMMER):
        # bees that walk on the landing board
        for k in range(6 if s == SUMMER else 3):
            bee_big(c, HX0 + 50 + k * 17, BOT - 3, k + 1)
