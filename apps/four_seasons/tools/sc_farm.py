# FOUR SEASONS: THE FARM (a wide view): the farmyard behind the valley. From the left: the gate and the way back to
# the valley, the mailbox on its post, the farmhouse with the attic window in its gable, the stone well, the two bee
# hives under the apple tree, the garden bed with its scarecrow. The seasons as the valley (blossom, green, red
# leaves, snow). No things change here (no VARS): the puzzle is in the close views.
import math
import numpy as np
from fsart import (Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline,
                   m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER, ramp_of
import fsparts as P
import fsparts_fahm as F

W, H = 720, 184
OX, OY = 40, 6                     # the 640 x 172 view of the board in this picture
GY = 156                           # the ground line of the house

VARS = {}
HOT = {
    'mailbox': (152, 98, 52, 58),
    'house': (214, 40, 162, 114),
    'well': (398, 82, 66, 70),
    'hive': (474, 98, 96, 54),
    'garden': (584, 104, 92, 60),
}

WHITE = (255, 255, 255)
TREE = {}


def yard_y(x):
    """the top of the yard (behind it the field)"""
    return 140 + OY * 0 + 1.6 * math.sin(x * 0.019 + 1.0) + 1.0 * math.sin(x * 0.051)


def tree(key, *a, **k):
    if key not in TREE:
        TREE[key] = P.Tree(*a, **k)
    return TREE[key]


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    sun = (p.sun_xy[0] + OX, p.sun_xy[1] + OY)
    P.sky(c, p, 0, 124, sun)
    P.clouds(c, p, 12, 50, 7, 57)
    P.mountains(c, p, 112, 30, 0.008, 5)
    P.far_ridge(c, p, 126, 12, 0.012, 13)
    field(c, p, s)
    # the yard
    yard = m_poly([(-4, H + 4)] + [(x, yard_y(x)) for x in range(-4, W + 5, 3)] + [(W + 4, H + 4)])
    c.paint(yard, p.hill)
    P.grass_marks(c, p, yard, 2600, 31)
    path(c, p, s)
    # the big oak at the gate (left) and the apple tree over the hives (right)
    oak = tree('oak', 70, 150, seed=811, depth=5, length=17, width=6.5, crown=(46, 26), nclump=220, clump_r=(3.0, 7.0))
    c.darken(m_ellipse(70 + p.slant * 10, 151, 30 + abs(p.slant) * 6, 3.4), 0.74, p.cool)
    oak.draw_wood(c, p)
    oak.draw_leaves(c, p, seed=8)
    gate(c, p, s)
    mailbox(c, p, s, 180, GY - 2)
    house(c, p, s)
    well(c, p, s, 431, 152)
    apple = tree('apple', 590, 151, seed=227, depth=4, length=15, width=4.6, crown=(30, 18), nclump=120, clump_r=(2.6, 5.2))
    c.darken(m_ellipse(590 + p.slant * 8, 152, 22, 2.6), 0.74, p.cool)
    apple.draw_wood(c, p)
    apple.draw_leaves(c, p, seed=4)
    apples(c, p, s, apple)
    hive(c, p, s, 482, 151, 0)
    hive(c, p, s, 528, 153, 1)
    bees(c, p, s)
    garden(c, p, s)
    yard_life(c, p, s)
    foreground(c, p, s)
    P.particles_baked(c, p, (40, 30, 680, 150), 14 if s != SUMMER else 0, 77)
    return c


def field(c, p, s):
    """the field behind the yard: rows of the crop of the season"""
    top = [(x, 124 + 2.0 * math.sin(x * 0.013)) for x in range(-4, W + 5, 4)]
    fm = m_poly(top + [(W + 4, 150), (-4, 150)])
    cols = {SPRING: [mix(p.soil, p.hill, 0.2), mix(p.lf, p.hill, 0.5)],
            SUMMER: [hx('#c8a040'), hx('#e8c860')],
            AUTUMN: [hx('#9a7a3e'), hx('#c0a060')],
            WINTER: [p.snow[2], p.snow[1]]}[s]
    c.paint(fm, cols[0])

    def fn(x, y, bay):
        k = np.floor((y - 124) / 2.0 + np.sin(x * 0.013) * 1.0).astype(int)
        return np.where(k % 2 == 0, 1, -1)
    F.paint_idx(c, fm, cols, fn)
    if s == SUMMER:
        c.speckle(fm, [hx('#f6e08a'), hx('#b08a30')], 0.06, 3)
    if s == AUTUMN:
        # round hay bales on the stubble
        for bx in (120, 210, 470, 652):
            by = 131 + 2.0 * math.sin(bx * 0.013)
            m = m_ellipse(bx, by, 5, 4)
            c.darken(m_ellipse(bx - 4, by + 3, 6, 1.5), 0.7, p.cool)
            c.shade_sphere(m, bx, by, 5, 4, p.straw, p.L, amb=0.25)
            c.paint(m_ellipse(bx - 1.6, by, 1.6, 2.4), mul(p.straw[1], 0.9))
    if s == WINTER:
        c.speckle(fm, [hx('#a8987a')], 0.04, 5)


def path(c, p, s):
    main = bezier((-6, 172), (90, 168), (190, 166), (262, 158), 30)
    to_well = bezier((262, 160), (330, 162), (380, 158), (424, 154), 20)
    to_garden = bezier((330, 163), (450, 170), (540, 164), (600, 162), 24)
    pm = m_polyline(main, 9.0, 5.0) | m_polyline(to_well, 4.0, 3.0) | m_polyline(to_garden, 4.0, 3.4)
    if s != WINTER:
        c.paint(pm, p.pathr[2])
        c.paint(pm.bottom(1), p.pathr[0])
        c.paint(pm.top(1), p.pathr[1])
        c.paint_dither(pm, p.pathr[3], 0.1)
        c.speckle(pm, [p.pathr[1], p.pathr[0]], 0.05, 9)
    else:
        c.paint(pm, p.snow[1])
        c.paint_dither(pm, p.snow[0], 0.18)
        # the footsteps of grandpa in the snow
        for i in range(10):
            f = i / 9
            x, y = main[int(f * 29) + 0]
            c.px(x + (i % 2) * 2, y - 1 + (i % 2), p.snow[0])


def gate(c, p, s):
    """the gate of the yard (open) and the fence to it"""
    P.fence(c, p, -6, 150, 120, 148, 6, rail_dy=(-10, -5), post_h=13)
    # the open gate: a frame of boards with a cross
    x, y = 126, 150
    c.paint(m_rect(x, y - 16, 3, 16), p.woodr[2])
    c.paint(m_rect(x, y - 16, 1, 16), p.woodr[3])
    gm = m_poly([(x + 3, y - 14), (x + 15, y - 9), (x + 15, y - 1), (x + 3, y - 4)])
    c.paint(gm, p.woodr[2])
    c.paint(m_line(x + 3, y - 12, x + 15, y - 7, 1), p.woodr[3])
    c.paint(m_line(x + 3, y - 6, x + 15, y - 3, 1), p.woodr[1])
    c.paint(m_line(x + 3, y - 4, x + 15, y - 9, 1), p.woodr[1])
    if s == WINTER:
        c.paint(m_rect(x - 1, y - 18, 5, 2), WHITE)
        c.paint(m_line(x + 3, y - 15, x + 15, y - 10, 1), WHITE)


def mailbox(c, p, s, x, gy):
    """the tin mailbox on its post by the path (small: the close view is MAILBOX)"""
    c.darken(m_ellipse(x + 1 + p.slant * 6, gy + 1, 6, 1.4), 0.72, p.cool)
    c.paint(m_rect(x - 1, gy - 24, 3, 24), p.woodr[2])
    c.paint(m_rect(x - 1, gy - 24, 1, 24), p.woodr[3])
    c.paint(m_rect(x + 1, gy - 24, 1, 24), p.woodr[1])
    c.paint(m_rect(x - 6, gy - 26, 14, 2), p.woodr[1])
    body = m_rect(x - 8, gy - 33, 18, 7) | m_ellipse(x + 1, gy - 33, 9, 4)
    body = body & m_rect(x - 8, gy - 38, 18, 12)
    c.shade_dir(body, p.metal, 0, -1, x - 8, gy - 38, x + 10, gy - 26)
    c.paint(m_rect(x - 8, gy - 37, 3, 11) & body, p.metal[1])   # the front end (the door)
    c.paint(m_line(x - 8, gy - 36, x - 8, gy - 27, 1), p.metal[0])
    c.paint(m_rect(x + 7, gy - 36, 1, 6), hx('#d8342a'))           # the flag
    c.paint(m_rect(x + 7, gy - 37, 4, 3), hx('#e8483a'))
    if s == WINTER:
        c.paint(m_ellipse(x + 1, gy - 37, 9, 2), WHITE)
        c.paint(m_rect(x - 7, gy - 26, 1, 3), hx('#cfe6f6'))
        c.paint(m_rect(x - 5, gy - 26, 1, 2), hx('#cfe6f6'))
    if s in (SPRING, SUMMER):
        # the wren on the box
        c.paint(m_ellipse(x - 3, gy - 39, 2.4, 1.6), hx('#8a5a32'))
        c.px(x - 6, gy - 40, hx('#8a5a32'))
        c.px(x - 6, gy - 41, hx('#6a4228'))
        c.px(x - 1, gy - 40, hx('#3a2a1a'))
    if s != WINTER:
        F.tall_grass(c, p, x - 7, x + 8, gy + 1, 9, 3, 7, 5)


def house(c, p, s):
    gl, gr, gt = 222, 300, 96               # the gable wall: left, right, the top of the wall
    peak = (261, 54)
    D = (72, -14)                           # the depth of the house (to the back right)
    pl = p.plaster
    # the shadow of the house on the yard
    sh = m_poly([(gl, GY), (gr + D[0], GY + D[1]), (gr + D[0] + p.slant * 22, GY + D[1] + 5), (gl + p.slant * 22, GY + 5)])
    c.darken(sh, 0.72, p.cool)
    # the side wall (to the right)
    side = m_poly([(gr, gt), (gr + D[0], gt + D[1]), (gr + D[0], GY + D[1]), (gr, GY)])
    sc = F.face(p, pl, (1, 0, 0), 0.3)
    c.paint(side, sc)
    F.patches(c, side, mix(sc, p.stoneW[1], 0.35), 5, 0.36, 5, 0.06)
    # the front (the gable)
    front = m_poly([(gl, gt), (peak[0], peak[1] + 2), (gr, gt), (gr, GY), (gl, GY)])
    fc = F.face(p, pl, (0, 0, 1), 0.2)
    c.paint(front, fc)
    F.patches(c, front, mix(fc, p.stoneW[1], 0.35), 5, 0.36, 6, 0.06)
    # the stone base
    sb = m_rect(gl, GY - 9, gr - gl, 9)
    F.stones(c, p, sb, p.stoneW, rh=3, rw=(5, 9), seed=3)
    sbs = m_poly([(gr, GY - 9), (gr + D[0], GY - 9 + D[1]), (gr + D[0], GY + D[1]), (gr, GY)])
    c.paint(sbs, p.stoneW[1])
    for k in range(0, D[0], 6):
        c.paint(m_line(gr + k, GY - 9 - k * 14 / 72, gr + k, GY - k * 14 / 72, 1), p.stoneW[0])
    c.paint(m_line(gr, GY - 5, gr + D[0], GY - 5 + D[1], 1), p.stoneW[0])
    # the timber posts at the corners and a beam
    for x in (gl, gr - 2):
        c.paint(m_rect(x, gt, 3, GY - 9 - gt), p.woodr[1])
        c.paint(m_rect(x + 1, gt, 1, GY - 9 - gt), p.woodr[2])
    c.paint(m_rect(gl, gt - 1, gr - gl, 3), p.woodr[1])
    c.paint(m_line(gr + D[0] - 1, gt + D[1], gr + D[0] - 1, GY + D[1], 2), p.woodr[1])
    # the windows of the front
    warm = {SPRING: 0.0, SUMMER: 0.0, AUTUMN: 0.6, WINTER: 0.9}[s]
    for wx in (230, 276):
        window(c, p, s, wx, 108, 15, 15, warm, shutters=True)
    # the door
    dm = m_rect(252, 126, 18, 30)
    F.boards(c, p, dm, (252, 126), (252, 156), (270, 126), 4, F.wood_ramp(p, hx('#6a8a5a') if s != WINTER else hx('#5a7a5a')), seed=7, knots=1, nails=False)
    c.outline(dm, p.woodr[0])
    c.paint(m_rect(266, 141, 2, 2), hx('#d8b040'))
    c.paint(m_rect(250, 156, 22, 2), p.stone[2])
    # the attic window in the gable
    attic_window(c, p, s, 253, 68)
    # the windows of the side wall
    for k, ux in enumerate((14, 44)):
        x0 = gr + ux
        y0 = 112 - ux * 14 / 72
        wm = m_poly([(x0, y0), (x0 + 13, y0 - 2.5), (x0 + 13, y0 + 12.5), (x0, y0 + 15)])
        c.paint(wm, mix(p.glass[1], hx('#ffcf6a'), warm) if warm else p.glass[1])
        c.paint(m_poly([(x0 + 1, y0 + 1), (x0 + 6, y0), (x0 + 1, y0 + 8)]), mix(p.glass[3], p.skyB, 0.4) if not warm else mix(p.glass[3], hx('#ffe9a8'), 0.6))
        c.outline(wm, p.trim)
        c.paint(m_line(x0 + 6, y0 - 1, x0 + 6, y0 + 14, 1), p.trim)
        c.paint(m_line(x0, y0 + 15.5, x0 + 13, y0 + 13, 2), p.woodr[3])
        if warm:
            c.glow(x0 + 6, y0 + 7, 14, hx('#ffc35a'), 0.3 * warm)
    # the roof: the right plane with rows of tiles, the gable edge with its boards
    O, A, B = (peak[0] - 1, peak[1] - 3), (peak[0] - 1 + D[0], peak[1] - 3 + D[1]), (gr + 7, gt + 2)
    roof = F.para(O, A, B)
    rr = p.tile if s != WINTER else p.snow
    tl = [mul(rr[0], 0.9), rr[0], rr[1], rr[2], rr[3]]

    def tiles(x, y, bay):
        u, v = F.uv_of(x, y, O, A, B)
        row = np.floor(v * 9).astype(int)
        fv = v * 9 - row
        fu = u * 15 + (row % 2) * 0.5
        out = np.full(x.shape, 3, int)
        out = np.where(fv > 0.72, 1, out)
        out = np.where((fu - np.floor(fu)) < 0.09, 2, out)
        out = np.where((fv < 0.22) & (out == 3), 4, out)
        return out
    if s != WINTER:
        F.paint_idx(c, roof, tl, tiles)
        c.paint_dither(roof, rr[1], 0.06)
        # moss on the old tiles
        c.speckle(roof, [mix(rr[1], p.lfD, 0.5)], 0.03, 11)
    else:
        c.paint(roof, p.snow[2])

        def sn(x, y, bay):
            u, v = F.uv_of(x, y, O, A, B)
            out = np.where(v < 0.08, 3, -1)
            out = np.where(((v * 9) % 1.0 > 0.86) & (bay < 0.4), 1, out)
            return out
        F.paint_idx(c, roof, p.snow, sn)
        c.paint(m_line(B[0], B[1] + 1, B[0] + D[0], B[1] + 1 + D[1], 2), tl[2])
        F.icicles(c, p, B[0] - 2, B[0] + D[0], B[1] + 3, 5, lmax=7)
    # the eave line (the gutter) and the board of the gable
    c.paint(m_line(B[0], B[1] + 1, B[0] + D[0], B[1] + 1 + D[1], 1), p.woodr[0])
    barge = m_polyline([(gl - 6, gt + 3), (peak[0], peak[1] - 3), (gr + 7, gt + 3)], 3.0)
    c.paint(barge, p.woodr[2])
    c.paint(m_polyline([(gl - 6, gt + 2), (peak[0], peak[1] - 4), (gr + 7, gt + 2)], 1.0), p.woodr[3])
    if s == WINTER:
        c.paint(m_polyline([(gl - 6, gt), (peak[0], peak[1] - 6), (gr + 7, gt)], 2.0), WHITE)
        F.icicles(c, p, gl - 4, gl + 14, gt + 4, 9, lmax=5)
    # the chimney (bricks) on the ridge, smoke in autumn and winter
    cx = peak[0] + D[0] * 0.72
    cy = peak[1] - 3 + D[1] * 0.72
    cm = m_rect(cx, cy - 16, 10, 22)
    c.paint(cm, p.brick[2])

    def bricks(x, y, bay):
        r = np.floor(y / 2).astype(int)
        out = np.where((y % 2) < 1, 1, 2)
        out = np.where(((x + (r % 2) * 2.5) % 5) < 1, 1, out)
        out = np.where(x > cx + 7, np.where(out == 2, 1, 0), out)
        return out
    F.paint_idx(c, cm, p.brick, bricks)
    c.paint(m_rect(cx - 1, cy - 18, 12, 2), p.stone[1])
    if s == WINTER:
        c.paint(m_rect(cx - 1, cy - 20, 12, 2), WHITE)
    if s in (AUTUMN, WINTER):
        F.smoke(c, p, cx + 5, cy - 19, 8, 3, drift=-1.5, rise=3)
    # the side bench and the woodpile
    woodpile(c, p, s, gr + 46, GY - 14 + 2)
    # the window boxes and the flowers by the door
    if s in (SPRING, SUMMER):
        for wx in (230, 276):
            for k in range(6):
                c.paint(m_disc(wx + 1 + k * 2.6, 123, 1.4), p.flower[k % 3 if s == SUMMER else (k % 4)])
                c.px(wx + 1 + k * 2.6, 124, p.lfD)
        F.tall_grass(c, p, gl - 4, gl + 26, GY + 1, 12, 3, 6, 9)
    if s == AUTUMN:
        for (px, py, r) in ((244, 153, 3.4), (278, 154, 2.6)):
            m = m_ellipse(px, py, r * 1.3, r)
            c.shade_sphere(m, px, py, r * 1.3, r, ramp_of(hx('#e87a28'), s, 4, -0.6, 0.4), p.L, amb=0.3)
            c.paint(m_rect(px, py - r - 1, 1, 2), p.lfD)
    if s == WINTER:
        c.paint(m_ellipse(252 - 6, GY - 1, 8, 2.2), WHITE)
        c.paint(m_ellipse(gr + 4, GY - 1, 7, 2), WHITE)


def window(c, p, s, x, y, w, h, warm, shutters=False):
    wm = m_rect(x, y, w, h)
    c.paint(wm, mix(p.glass[1], hx('#ffcf6a'), warm) if warm else p.glass[1])
    c.paint(m_poly([(x, y), (x + w * 0.5, y), (x, y + h * 0.6)]), mix(p.glass[2], p.skyB, 0.3) if not warm else mix(p.glass[3], hx('#ffe9a8'), 0.6))
    c.outline(wm, p.trim)
    c.paint(m_line(x + w // 2, y, x + w // 2, y + h - 1, 1), p.trim)
    c.paint(m_line(x, y + h // 2, x + w - 1, y + h // 2, 1), p.trim)
    if warm:
        c.glow(x + w / 2, y + h / 2, 14, hx('#ffc35a'), 0.3 * warm)
    if shutters:
        sg = p.paint_green
        for sx in (x - 6, x + w + 1):
            sm = m_rect(sx, y - 1, 5, h + 2)
            c.paint(sm, sg[2])
            for k in range(1, h + 2, 2):
                c.paint(m_rect(sx, y - 1 + k, 5, 1), sg[1])
            c.paint(m_rect(sx, y - 1, 1, h + 2), sg[3])
    c.paint(m_rect(x - 1, y + h + 1, w + 2, 2), p.woodr[3])
    if s == WINTER:
        c.paint(m_rect(x - 1, y + h, w + 2, 1), WHITE)
        c.paint(m_rect(x - 1, y - 2, w + 2, 1), WHITE)


def attic_window(c, p, s, x, y):
    """the small window under the peak of the gable (the attic): a frame of 4 panes; the winter sun lights it"""
    w, h = 16, 14
    wm = m_rect(x, y, w, h)
    c.paint(m_rect(x - 2, y - 2, w + 4, h + 4), p.woodr[1])
    glass = p.glass[0] if s != WINTER else mix(p.glass[1], hx('#ffe8b0'), 0.5)
    c.paint(wm, glass)
    c.paint(m_poly([(x, y), (x + 7, y), (x, y + 6)]), mix(p.glass[2], p.skyB, 0.4))
    if s == WINTER:
        c.paint(m_poly([(x + 9, y + 1), (x + 15, y + 1), (x + 15, y + 9)]), WHITE)
    c.paint(m_line(x + w // 2, y, x + w // 2, y + h - 1, 1), p.trim)
    c.paint(m_line(x, y + h // 2, x + w - 1, y + h // 2, 1), p.trim)
    c.outline(wm, p.trim)
    if s == WINTER:
        c.paint(m_rect(x - 2, y + h + 2, w + 4, 2), WHITE)
    # a dove under the gable in spring and summer
    if s in (SPRING, SUMMER):
        c.paint(m_ellipse(x + w + 6, y + h + 1, 2.6, 1.6), hx('#e8e8f0'))
        c.px(x + w + 8, y + h - 1, hx('#e8e8f0'))
        c.px(x + w + 9, y + h - 1, hx('#e89a3a'))


def woodpile(c, p, s, x, y):
    """the logs stacked on the side wall (their round ends to us)"""
    rng = RNG(41)
    for row in range(4):
        for k in range(5 - (row // 2)):
            lx = x + k * 4.4 + (row % 2) * 2.2
            ly = y - row * 3.6 - (lx - x) * 14 / 72
            m = m_ellipse(lx, ly, 2.2, 1.9)
            c.paint(m, rng.pick([hx('#b08050'), hx('#c89a68'), hx('#a07040')]))
            c.px(lx, ly, hx('#7a5030'))
            c.paint(m.bottom(1), hx('#6a4228'))
    if s == WINTER:
        c.paint(m_poly([(x - 3, y - 14), (x + 22, y - 18.5), (x + 22, y - 15.5), (x - 3, y - 11)]), WHITE)


def well(c, p, s, x, gy):
    """the stone well: a round wall of stones, two posts and a little roof of shingles, the axle with its rope and
    the bucket on the rim (no crank: the crank handle fell into the well)"""
    rw = 19
    c.darken(m_ellipse(x + p.slant * 10, gy + 1, rw + 6, 3), 0.72, p.cool)
    body = m_rect(x - rw, gy - 16, rw * 2, 16) | m_ellipse(x, gy, rw, 3.4)
    F.stones(c, p, body, p.stone, rh=4, rw=(6, 10), seed=19)
    # (the round wall: the side away from the sun is darker)
    sx = -1 if p.L[0] > 0 else 1
    c.darken(body & m_rect(x + (sx * rw if sx > 0 else -rw - 1) - (rw * 0.6 if sx > 0 else 0), gy - 20, rw * 0.6 + 1, 30), 0.82, p.cool)
    rim = m_ellipse(x, gy - 16, rw + 1, 4.2)
    c.paint(rim, p.stone[3])
    c.paint(m_ellipse(x, gy - 16, rw - 3, 2.6), hx('#141820'))
    c.paint(rim.top(1), p.stone[4])
    if s == WINTER:
        c.paint(m_ellipse(x, gy - 17, rw + 1, 2.8) - m_ellipse(x, gy - 15.5, rw - 3, 2.4), WHITE)
    # the posts and the roof
    for px in (x - rw + 2, x + rw - 4):
        c.paint(m_rect(px, gy - 44, 3, 28), p.woodr[2])
        c.paint(m_rect(px, gy - 44, 1, 28), p.woodr[3])
        c.paint(m_rect(px + 2, gy - 44, 1, 28), p.woodr[1])
    c.paint(m_line(x - rw + 2, gy - 34, x + rw - 1, gy - 34, 2), p.woodr[1])     # the axle
    c.paint(m_rect(x + rw - 1, gy - 35, 3, 3), p.iron[1])                        # (the bare end of the axle)
    c.paint(m_rect(x - 5, gy - 36, 10, 4), p.rope[1])
    for k in range(5):
        c.px(x - 4 + k * 2, gy - 35, p.rope[2])
    c.paint(m_line(x + 1, gy - 32, x + 1, gy - 19, 1), p.rope[0])
    roof = m_poly([(x - rw - 6, gy - 42), (x, gy - 56), (x + rw + 6, gy - 42), (x + rw + 6, gy - 40), (x - rw - 6, gy - 40)])
    c.paint(roof, p.woodr[1])

    def shingles(xx, yy, bay):
        r = np.floor((yy - (gy - 56)) / 3).astype(int)
        out = np.where(((xx + (r % 2) * 2) % 4) < 1, 1, 2)
        out = np.where((yy - (gy - 56)) % 3 < 1, 0, out)
        out = np.where(xx > x, np.maximum(out - 1, 0) if p.L[0] < 0 else np.minimum(out + 1, 3), out)
        return out
    F.paint_idx(c, roof, p.woodr, shingles)
    if s == WINTER:
        c.paint(m_poly([(x - rw - 6, gy - 43), (x, gy - 57), (x + rw + 6, gy - 43), (x, gy - 54)]), WHITE)
        c.paint(m_poly([(x - rw - 7, gy - 42), (x, gy - 58), (x + rw + 7, gy - 42), (x + rw + 7, gy - 44), (x, gy - 60), (x - rw - 7, gy - 44)]), WHITE)
        F.icicles(c, p, x - rw - 4, x + rw + 4, gy - 40, 13, lmax=4)
    # the bucket on the rim
    bx, by = x - rw + 1, gy - 18
    bm = m_poly([(bx - 4, by - 6), (bx + 4, by - 6), (bx + 3, by + 1), (bx - 3, by + 1)])
    c.paint(bm, p.woodr[2])
    c.paint(m_rect(bx - 4, by - 4, 8, 1), p.iron[1])
    c.paint(m_rect(bx - 3, by - 1, 7, 1), p.iron[1])
    c.paint(m_line(bx - 4, by - 6, bx - 4, by + 1, 1), p.woodr[3])
    if s != WINTER:
        F.tall_grass(c, p, x - rw - 4, x + rw + 4, gy + 2, 14, 3, 7, 21)
    else:
        c.paint(m_ellipse(x, gy + 1, rw + 3, 2.4), WHITE)


def hive(c, p, s, x, gy, k):
    """a small bee hive on its stand: two boxes, the roof, the slot of the door"""
    ramps = [p.paint_white, p.paint_blue]
    rr = ramps[k]
    w, bh = 28, 11
    c.darken(m_ellipse(x + w / 2 + p.slant * 8, gy + 1, 19, 2.4), 0.72, p.cool)
    for lx in (x + 2, x + w - 4):
        c.paint(m_rect(lx, gy - 5, 2, 5), p.woodr[1])
        c.paint(m_rect(lx, gy - 5, 1, 5), p.woodr[3])
    c.paint(m_rect(x - 1, gy - 7, w + 2, 2), p.woodr[2])
    c.paint(m_rect(x - 1, gy - 7, w + 2, 1), p.woodr[3])
    D = (6, -4)
    for b in range(2):
        y0 = gy - 7 - (b + 1) * bh
        fm = m_rect(x, y0, w, bh)
        col = rr if b == 0 else ramps[1 - k]
        c.paint(fm, F.face(p, col, (0, 0, 1)))
        sm = m_poly([(x + w, y0), (x + w + D[0], y0 + D[1]), (x + w + D[0], y0 + bh + D[1]), (x + w, y0 + bh)])
        c.paint(sm, F.face(p, col, (1, 0, 0)))
        c.paint(m_rect(x, y0 + bh - 1, w, 1), col[0])
        c.paint(m_rect(x + 9, y0 + 4, 10, 2), col[0])   # the hand hold
        c.paint(m_rect(x + 9, y0 + 4, 10, 1), mul(col[0], 0.8))
        c.paint(m_rect(x, y0, w, 1), col[3])
    y0 = gy - 7 - 2 * bh
    c.paint(m_rect(x - 2, y0 - 4, w + 4, 4), F.face(p, p.metal, (0, 0, 1)))
    c.paint(m_poly([(x - 2, y0 - 4), (x + w + 2, y0 - 4), (x + w + 2 + D[0], y0 - 4 + D[1]), (x - 2 + D[0], y0 - 4 + D[1])]), p.metal[3])
    c.paint(m_poly([(x + w + 2, y0 - 4), (x + w + 2 + D[0], y0 - 4 + D[1]), (x + w + 2 + D[0], y0 + D[1]), (x + w + 2, y0)]), F.face(p, p.metal, (1, 0, 0)))
    c.paint(m_rect(x - 2, y0 - 1, w + 4, 1), p.metal[0])
    # a stone on the lid against the wind
    c.paint(m_ellipse(x + w * 0.6, y0 - 6, 4, 2), p.stone[2])
    c.paint(m_ellipse(x + w * 0.6 - 1, y0 - 7, 2, 1), p.stone[3])
    # the slot of the door with its board
    c.paint(m_rect(x + 6, gy - 10, 16, 2), hx('#1a140e'))
    c.paint(m_rect(x + 3, gy - 8, 22, 1), p.woodr[3])
    if s == WINTER:
        c.paint(m_poly([(x - 3, y0 - 4), (x + w + 3 + D[0], y0 - 4 + D[1]), (x + w + 3 + D[0], y0 - 8 + D[1]), (x + w / 2, y0 - 10), (x - 3, y0 - 7)]), WHITE)
        c.paint(m_rect(x - 3, y0 - 4, w + 6, 1), p.snow[1])
    if s != WINTER:
        F.tall_grass(c, p, x - 3, x + w + 6, gy + 1, 10, 3, 6, 33 + k)


def apples(c, p, s, t):
    if s not in (SUMMER, AUTUMN):
        return
    rng = RNG(5)
    n = 0
    for (cx, cy, r0, th, hv, ph) in t.clumps:
        if hv > 0.7 and n < 22:
            col = hx('#d23a26') if s == AUTUMN or hv > 0.85 else hx('#9acb3a')
            c.paint(m_disc(cx + 1, cy + r0 * 0.4, 1.3), col)
            c.px(cx + 0.5, cy + r0 * 0.4 - 1, mix(col, WHITE, 0.5))
            n += 1
    if s == AUTUMN:
        for k in range(7):
            ax = 575 + rng.uni(0, 34)
            c.paint(m_disc(ax, 153 + rng.uni(0, 3), 1.2), hx('#c8352a'))


def bees(c, p, s):
    n = {SPRING: 9, SUMMER: 14, AUTUMN: 3, WINTER: 0}[s]
    rng = RNG(61)
    for i in range(n):
        bx = rng.uni(480, 570)
        by = rng.uni(110, 146)
        c.px(bx, by, hx('#2a1e10'))
        c.px(bx + 1, by, hx('#f2b830'))
        c.px(bx, by - 1, hx('#e8f2ff'))


def garden(c, p, s):
    """the garden bed: a frame of boards, the rows of soil, the scarecrow; the ditch from the pond on its right"""
    O = (596, 146)
    A = (676, 146)
    B = (590, 164)
    bed = F.para(O, A, B)
    c.darken(F.para((O[0], O[1] + 1), (A[0] + 4, A[1] + 1), (B[0] - 2, B[1] + 2)), 0.8, p.cool)
    if s != WINTER:
        c.paint(bed, p.soilr[1])

        def rows(x, y, bay):
            u, v = F.uv_of(x, y, O, A, B)
            f = (v * 5) % 1.0
            return np.where(f < 0.3, 0, np.where(f > 0.8, 2, -1))
        F.paint_idx(c, bed, p.soilr, rows)
        rng = RNG(17)
        for r in range(5):
            for k in range(9):
                u = (k + 0.5) / 9
                v = (r + 0.55) / 5
                x = O[0] + (A[0] - O[0]) * u + (B[0] - O[0]) * v
                y = O[1] + (B[1] - O[1]) * v
                if s == SPRING:
                    c.paint(m_line(x, y, x - 1, y - 2, 1), p.lfL)
                    c.px(x + 1, y - 2, p.lf)
                elif s == SUMMER:
                    m = m_ellipse(x, y - 2, 2.6, 2.0)
                    c.shade_sphere(m, x, y - 2, 2.6, 2, p.leaf, p.L, amb=0.3)
                    if (k + r) % 3 == 0:
                        c.px(x, y - 3, hx('#e8342a'))
                else:
                    c.paint(m_line(x, y, x + rng.uni(-1, 1), y - 4, 1), hx('#8a6a3a'))
                    if (k + r) % 4 == 0:
                        m = m_ellipse(x, y - 1, 2.4, 1.8)
                        c.paint(m, hx('#e87a28'))
                        c.px(x - 1, y - 2, hx('#ffb060'))
    else:
        c.paint(bed, p.snow[2])

        def humps(x, y, bay):
            u, v = F.uv_of(x, y, O, A, B)
            f = (v * 5) % 1.0
            return np.where(f < 0.25, 3, np.where(f > 0.8, 1, -1))
        F.paint_idx(c, bed, p.snow, humps)
        c.speckle(bed, [hx('#8a7a5e')], 0.03, 7)
    # the frame of boards (the front board)
    c.paint(m_rect(B[0], B[1], A[0] - O[0], 3), p.woodr[2])
    c.paint(m_rect(B[0], B[1], A[0] - O[0], 1), p.woodr[3])
    c.paint(m_line(A[0], A[1], A[0] - 6, B[1], 2), p.woodr[1])
    # the ditch (a thin line of water) along the right side
    if s != WINTER:
        dm = m_polyline([(686, 152), (682, 160), (676, 168), (668, 176)], 3.0)
        c.paint(dm, p.waterD if s != SUMMER else p.mud[1])
        c.paint(m_polyline([(686, 152), (682, 160)], 1.0), p.water if s != SUMMER else p.mud[2])
    scarecrow(c, p, s, 652, 156)


def scarecrow(c, p, s, x, gy):
    c.paint(m_rect(x - 1, gy - 38, 2, 38), p.woodr[1])
    c.paint(m_line(x - 14, gy - 28, x + 14, gy - 29, 2), p.woodr[2])
    # the shirt (blue checks) and the straw
    shirt = m_poly([(x - 9, gy - 30), (x + 9, gy - 31), (x + 7, gy - 16), (x - 7, gy - 15)])
    c.paint(shirt, p.paint_blue[2])

    def checks(xx, yy, bay):
        return np.where(((xx.astype(int) // 2 + yy.astype(int) // 2) % 2) == 0, 1, -1)
    F.paint_idx(c, shirt, p.paint_blue, checks)
    c.paint(m_line(x - 14, gy - 29, x - 9, gy - 29, 2), p.paint_blue[1])
    c.paint(m_line(x + 9, gy - 30, x + 14, gy - 30, 2), p.paint_blue[1])
    for sx, d in ((x - 15, -1), (x + 15, 1)):
        for k in range(3):
            c.paint(m_line(sx, gy - 29, sx + d * 2, gy - 31 + k * 2, 1), p.straw[2])
    c.paint(m_rect(x - 6, gy - 16, 12, 3), p.straw[1])
    for k in range(5):
        c.paint(m_line(x - 5 + k * 2.5, gy - 14, x - 6 + k * 2.8, gy - 10, 1), p.straw[2])
    # the head (a sack), the hat
    hm = m_ellipse(x, gy - 35, 5, 5)
    c.shade_sphere(hm, x, gy - 35, 5, 5, ramp_of(hx('#d8c090'), s, 4, -0.6, 0.4), p.L, amb=0.3)
    c.px(x - 2, gy - 35, hx('#3a2a1a'))
    c.px(x + 2, gy - 35, hx('#3a2a1a'))
    c.paint(m_line(x - 2, gy - 32, x + 2, gy - 32, 1), hx('#3a2a1a'))
    c.paint(m_rect(x - 9, gy - 40, 18, 2), p.straw[1])
    c.paint(m_ellipse(x, gy - 42, 5, 3) & m_rect(x - 6, gy - 46, 12, 5), p.straw[2])
    c.paint(m_rect(x - 5, gy - 41, 10, 1), hx('#c8342a'))
    if s == WINTER:
        c.paint(m_ellipse(x, gy - 45, 5, 1.6), WHITE)
        c.paint(m_rect(x - 9, gy - 41, 18, 1), WHITE)
        c.paint(m_line(x - 14, gy - 30, x + 14, gy - 31, 1), WHITE)
    if s == AUTUMN:
        # a crow on the arm
        c.paint(m_ellipse(x + 12, gy - 33, 3, 2), hx('#1a1a22'))
        c.paint(m_rect(x + 14, gy - 36, 2, 2), hx('#1a1a22'))
        c.px(x + 16, gy - 35, hx('#e8b040'))
        c.paint(m_line(x + 9, gy - 33, x + 7, gy - 31, 1), hx('#1a1a22'))


def yard_life(c, p, s):
    """the hens in the yard (not in winter), a snowman in winter"""
    if s != WINTER:
        rng = RNG(3)
        for (hx_, hy, d) in ((330, 164, 1), (352, 168, -1), (372, 162, 1), (206, 160, -1)):
            col = WHITE if rng() < 0.6 else hx('#b06a3a')
            c.darken(m_ellipse(hx_ + p.slant * 2, hy + 1, 4, 1), 0.72, p.cool)
            c.paint(m_ellipse(hx_, hy - 3, 3.4, 2.4), col)
            c.paint(m_ellipse(hx_ + d * 3, hy - 6, 1.6, 1.6), col)
            c.px(hx_ + d * 3, hy - 8, hx('#e8342a'))
            c.px(hx_ + d * 5, hy - 6, hx('#f2b830'))
            c.px(hx_ - d * 3, hy - 5, mul(col, 0.8))
            c.px(hx_ - 1, hy - 1, hx('#e8a030'))
            c.px(hx_ + 1, hy - 1, hx('#e8a030'))
            c.paint(m_ellipse(hx_ - 0.5, hy - 2.6, 2.0, 1.0), mul(col, 0.86))
    else:
        x, gy = 380, 168
        c.darken(m_ellipse(x - 6, gy + 1, 9, 2), 0.72, p.cool)
        for (r, dy) in ((7, 0), (5, -10), (3.6, -17)):
            m = m_disc(x, gy - r + dy, r)
            c.outline(m, p.snow[0])
        for (r, dy) in ((7, 0), (5, -10), (3.6, -17)):
            m = m_disc(x, gy - r + dy, r)
            c.shade_sphere(m, x, gy - r + dy, r, r, p.snow, p.L, amb=0.35)
        c.px(x - 1, gy - 22, hx('#1a1a22'))
        c.px(x + 1, gy - 22, hx('#1a1a22'))
        c.paint(m_line(x, gy - 20.5, x + 3, gy - 20, 1), hx('#f07a2a'))
        c.paint(m_rect(x - 4, gy - 18, 8, 2), hx('#c8342a'))
        c.paint(m_line(x + 5, gy - 13, x + 10, gy - 17, 1), p.woodr[1])
        c.paint(m_line(x - 5, gy - 13, x - 10, gy - 16, 1), p.woodr[1])


def foreground(c, p, s):
    fm = m_poly([(-40, H + 4)] + [(x, 177 + math.sin(x * 0.05) * 1.6 + math.sin(x * 0.13)) for x in range(-40, W + 41, 4)] + [(W + 40, H + 4)])
    c.paint(fm, p.fg)
    for x in range(-30, W + 30, 3):
        y = 175 + math.sin(x) * 2
        c.paint(m_rect(x, y, 1, 3 + (x % 3)), p.grassL if x % 6 else mul(p.fg, 0.72))
    P.flowers(c, p, m_rect(150, 166, 520, 10) - m_rect(330, 160, 70, 16), 70, 29)
    P.fence(c, p, 400, 184, 572, 181, 7, rail_dy=(-11, -5), post_h=14)
