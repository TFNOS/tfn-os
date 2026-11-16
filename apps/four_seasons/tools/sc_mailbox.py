# FOUR SEASONS: THE MAILBOX (a close view): the old tin mailbox on its post at the gate of the farm, near. Spring:
# the door hangs open, a wren builds its nest in the box (a twig in its beak); summer: the chicks gape in the nest,
# the wren sings on the roof; autumn: the wren left, a paper packet of sunflower seeds stands in the open box in front
# of the old nest; winter: the door is frozen shut, icicles, snow on the roof. Behind: the big oak of the gate, the
# fence and the field, the farmhouse far to the right.
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER, ramp_of
import fsparts as P
import fsparts_fahm as F

W, H = 672, 180
VARS = {'seeds': [1, 0]}
HOT = {'mailbox': (184, 30, 276, 124)}

WHITE = (255, 255, 255)
# the box: the front arch (centre x, the radius, the top y), the bottom of its wall, the depth to the back end
BX, BR, BTOP = 248, 50, 36
BBOT = 112
BD = (150, -20)
BCY = BTOP + BR
TREE = {}


def ground_y(x):
    return 122 + 2.0 * math.sin(x * 0.011 + 0.4)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    sunx = p.sun_xy[0] * W / 640
    suny = p.sun_xy[1] * 1.15 + 4
    F.close_sky(c, p, 124, sunx, suny, 91, 5)
    P.far_ridge(c, p, 104, 10, 0.009, 21, step=9, tree_h=(5, 10))
    field(c, p, s)
    far_house(c, p, s)
    meadow(c, p, s)
    fence(c, p, s)
    oak(c, p, s)
    path(c, p, s)
    post(c, p, s)
    box(c, p, s, st)
    front_grass(c, p, s)
    P.particles_baked(c, p, (20, 20, 660, 160), {SPRING: 16, SUMMER: 0, AUTUMN: 14, WINTER: 30}[s], 13)
    return c


# ---------------------------------------------------------------- the place
def field(c, p, s):
    top = [(x, 106 + 1.6 * math.sin(x * 0.02)) for x in range(-4, W + 5, 4)]
    fm = m_poly(top + [(W + 4, 132), (-4, 132)])
    cols = {SPRING: [mix(p.soil, p.hill, 0.25), mix(p.lf, p.hill, 0.45)],
            SUMMER: [hx('#c8a040'), hx('#e8c860')],
            AUTUMN: [hx('#9a7a3e'), hx('#c0a060')],
            WINTER: [p.snow[2], p.snow[1]]}[s]
    c.paint(fm, cols[0])

    def fn(x, y, bay):
        k = np.floor((y - 104) / 3.0 + np.sin(x * 0.02) * 0.5).astype(int)
        return np.where(k % 2 == 0, 1, -1)
    F.paint_idx(c, fm, cols, fn)
    if s == SUMMER:
        c.speckle(fm, [hx('#f6e08a'), hx('#b08a30')], 0.08, 3)
    if s == WINTER:
        c.speckle(fm, [hx('#a8987a')], 0.05, 5)


def far_house(c, p, s):
    """the farmhouse far to the right (its gable with the attic window)"""
    x0, x1, gy = 548, 640, 118
    peak = ((x0 + x1) / 2, 50)
    front = m_poly([(x0, 84), peak, (x1, 84), (x1, gy), (x0, gy)])
    c.paint(front, mix(F.face(p, p.plaster, (0, 0, 1), 0.2), p.hz, 0.25))
    c.paint(m_rect(x0, gy - 7, x1 - x0, 7), mix(p.stoneW[1], p.hz, 0.25))
    side = m_poly([(x1, 84), (x1 + 40, 76), (x1 + 40, gy - 6), (x1, gy)])
    c.paint(side, mix(F.face(p, p.plaster, (1, 0, 0), 0.3), p.hz, 0.25))
    roof = m_poly([(peak[0], peak[1] - 3), (peak[0] + 40, peak[1] - 11), (x1 + 46, 78), (x1 + 6, 87)])
    c.paint(roof, mix(p.tile[2], p.hz, 0.2) if s != WINTER else WHITE)
    if s != WINTER:
        for k in range(1, 6):
            f = k / 6
            c.paint(m_line(lerp(peak[0], x1 + 6, f), lerp(peak[1] - 3, 87, f), lerp(peak[0] + 40, x1 + 46, f), lerp(peak[1] - 11, 78, f), 1), mix(p.tile[1], p.hz, 0.2))
    c.paint(m_polyline([(x0 - 5, 86), (peak[0], peak[1] - 3), (x1 + 6, 86)], 2.0), mix(p.woodr[2], p.hz, 0.2))
    aw = m_rect(peak[0] - 7, 62, 14, 12)
    c.paint(aw, p.glass[1] if s != WINTER else mix(p.glass[1], hx('#ffe8b0'), 0.5))
    c.outline(aw, p.trim)
    c.paint(m_line(peak[0], 62, peak[0], 73, 1), p.trim)
    warm = s in (AUTUMN, WINTER)
    for wx in (x0 + 10, x1 - 26):
        wm = m_rect(wx, 92, 16, 14)
        c.paint(wm, mix(p.glass[1], hx('#ffcf6a'), 0.8) if warm else p.glass[1])
        c.outline(wm, p.trim)
        c.paint(m_rect(wx - 5, 91, 4, 16), p.paint_green[2])
        c.paint(m_rect(wx + 17, 91, 4, 16), p.paint_green[2])
    c.paint(m_rect(x1 + 6, 30, 7, 36) & m_poly([(x1, 0), (x1 + 20, 0), (x1 + 20, 74), (x1, 82)]), p.brick[2])
    if s in (AUTUMN, WINTER):
        F.smoke(c, p, x1 + 9, 28, 6, 4, drift=-1.0)


def meadow(c, p, s):
    gm = m_poly([(-4, H + 4)] + [(x, ground_y(x)) for x in range(-4, W + 5, 4)] + [(W + 4, H + 4)])
    c.paint(gm, p.hill)
    P.grass_marks(c, p, gm, 2400, 7, hmin=2, hmax=5)
    if s == WINTER:
        c.paint_values(gm, p.snow[1:], lambda x, y: np.clip((y - 118) / 60.0, 0, 1) * 0.0 + 0.6 + 0.3 * np.sin(x * 0.03) * 0.2, 0.3)
        P.grass_marks(c, p, gm, 900, 8, hmin=2, hmax=4, cols=(p.snow[3], p.snow[1], hx('#a8987a')))


def fence(c, p, s):
    """the fence of the yard behind the mailbox and the open gate"""
    P.fence(c, p, 330, 124, 560, 120, 6, rail_dy=(-20, -10), post_h=26)
    P.fence(c, p, -10, 128, 120, 126, 3, rail_dy=(-20, -10), post_h=26)


def oak(c, p, s):
    """the big oak at the gate: its trunk on the left, its low crown over the top left"""
    if 'oak' not in TREE:
        TREE['oak'] = P.Tree(64, 196, seed=812, depth=4, length=58, width=30, crown=(120, 46), nclump=170,
                             clump_r=(8.0, 17.0), spread=0.8)
    t = TREE['oak']
    c.darken(m_ellipse(64 + p.slant * 22, 172, 60, 8), 0.74, p.cool)
    t.draw_wood(c, p)
    # (the bark of the trunk: dark cracks along it)
    tm = m_poly([(44, 182), (50, 120), (58, 60), (72, 60), (80, 120), (86, 182)])
    rng = RNG(4)
    for k in range(9):
        x = rng.uni(50, 80)
        c.paint(m_polyline([(x, 182), (x + rng.uni(-3, 3), 150), (x + rng.uni(-4, 4), 120)], 1) & tm, p.bark[0])
    t.draw_leaves(c, p, seed=12)
    if s == WINTER:
        c.paint(m_polyline([(42, 132), (50, 100), (56, 70)], 2) & tm.grow(1), WHITE)


def path(c, p, s):
    pts = bezier((120, 186), (220, 160), (420, 150), (680, 138), 40)
    pm = m_polyline(pts, 34, 12)
    if s != WINTER:
        c.paint(pm, p.pathr[2])
        c.paint(pm.top(2), p.pathr[1])
        c.paint_dither(pm, p.pathr[3], 0.1)
        c.speckle(pm, [p.pathr[1], p.pathr[0], p.pathr[3]], 0.06, 9)
        rng = RNG(8)
        for k in range(10):
            x = rng.uni(150, 650)
            y = 158 - (x - 150) * 0.04 + rng.uni(-6, 8)
            P.rock(c, p, x, y, rng.uni(1.0, 2.2))
    else:
        c.paint(pm, p.snow[1])
        c.paint_dither(pm, p.snow[0], 0.2)
        # the boots of grandpa in the snow
        for k in range(9):
            f = k / 8
            x, y = pts[int(f * 28) + 6]
            x += (k % 2) * 6
            c.paint(m_ellipse(x, y + (k % 2) * 4 - 2, 3, 1.5), p.snow[0])


def front_grass(c, p, s):
    F.tall_grass(c, p, 0, W, lambda x: H + 2, 140, 8, 20, 31, lean=0.1)
    rng = RNG(2)
    pts = [(rng.uni(140, 660), rng.uni(166, 178)) for _ in range(26)]
    F.close_flowers(c, p, pts, 5, 1.2)


# ---------------------------------------------------------------- the post and the box
def post(c, p, s):
    x, w = BX - 11, 22
    c.darken(m_poly([(x, 172), (x + w, 172), (x + w + p.slant * 30, 178), (x + p.slant * 30, 178)]), 0.72, p.cool)
    pm = m_rect(x, BBOT + 4, w, H - BBOT)
    F.boards(c, p, pm, (x, BBOT + 4), (x, H), (x + w, BBOT + 4), 1, F.wood_ramp(p, hx('#7a6a58')), seed=3, knots=1, nails=False, gap=False)
    c.paint(m_rect(x, BBOT + 4, 2, H), mul(F.wood_ramp(p, hx('#7a6a58'))[1], 0.9))
    sm = m_poly([(x + w, BBOT + 4), (x + w + 8, BBOT), (x + w + 8, H), (x + w, H)])
    c.paint(sm, F.face(p, F.wood_ramp(p, hx('#7a6a58')), (1, 0, 0)))
    # a small notice of the farm nailed on the post: a horseshoe for luck
    hx0, hy0 = x + w / 2, 158
    c.paint(m_ellipse(hx0, hy0, 6, 7) - m_ellipse(hx0, hy0 + 1, 3, 5) - m_rect(hx0 - 7, hy0 + 3, 14, 6), p.iron[2])
    c.paint(m_ellipse(hx0, hy0, 6, 7).top(1) - m_rect(hx0 - 2, hy0 - 9, 4, 3), p.iron[3])
    c.px(hx0 - 4, hy0 - 2, p.iron[0])
    c.px(hx0 + 4, hy0 - 2, p.iron[0])
    if s == WINTER:
        c.paint(m_ellipse(x + w / 2 + 2, H - 4, 30, 9), p.snow[2])
        c.paint(m_ellipse(x + w / 2 + 2, H - 6, 28, 6).top(1), WHITE)


def board_under(c, p, s):
    """the board that holds the box on the post"""
    wr = F.wood_ramp(p, hx('#7a6a58'))
    x0, x1 = BX - BR - 4, BX + BR + 4
    fm = m_rect(x0, BBOT, x1 - x0, 6)
    c.paint(fm, wr[2])
    c.paint(m_rect(x0, BBOT, x1 - x0, 1), wr[3])
    c.paint(m_rect(x0, BBOT + 5, x1 - x0, 1), wr[0])
    sm = m_poly([(x1, BBOT), (x1 + BD[0] * 0.95, BBOT + BD[1] * 0.95), (x1 + BD[0] * 0.95, BBOT + 6 + BD[1] * 0.95), (x1, BBOT + 6)])
    c.paint(sm, F.face(p, wr, (1, 0, 0)))
    c.paint(m_line(x1, BBOT + 5, x1 + BD[0] * 0.95, BBOT + 5 + BD[1] * 0.95, 1), wr[0])
    if s == WINTER:
        c.paint(m_rect(x0 - 2, BBOT - 1, x1 - x0 + 4, 2), WHITE)


def box(c, p, s, st):
    board_under(c, p, s)
    tin = ramp_of(hx('#a4aeb4'), s, 5, -0.75, 0.5)          # old galvanised tin
    cols = tin + [mix(tin[4], WHITE, 0.5)]
    # the shadow of the box on the ground
    c.darken(m_poly([(BX - 30, 168), (BX + 160, 150), (BX + 160 + p.slant * 30, 158), (BX - 30 + p.slant * 30, 176)]), 0.78, p.cool)
    # the body: the round roof and the right wall, swept from the back end to the front (each slice lit by its normal)
    V = np.full((H, W), np.nan, np.float32)
    steps = int(math.hypot(*BD) * 1.4)
    na = int(math.pi * BR * 2.2)
    ang = np.linspace(0, math.pi, na)
    Lx, Ly = p.L[0], p.L[1]
    d_arch = np.cos(ang) * Lx - np.sin(ang) * Ly
    # (the tin also shows the light sky: a light band on the upper part of the roof)
    sky_ref = np.exp(-((ang - 1.75) / 0.28) ** 2) * 0.22
    v_arch = np.clip(0.36 + 0.66 * np.clip(d_arch, -0.3, 1) + sky_ref, 0, 1)
    wall_y = np.arange(BCY, BBOT + 1, 0.5)
    v_wall = np.clip(0.36 + 0.66 * Lx, 0.12, 1)
    ribs = (0.18, 0.5, 0.82)
    for i in range(steps, -1, -1):
        t = i / steps
        ox, oy = BD[0] * t, BD[1] * t
        rib = 0.0
        for rb in ribs:
            if abs(t - rb) < 0.6 / steps * 1.5:
                rib = -0.22
            elif 0 < t - rb < 2.2 / steps * 1.5:
                rib = 0.18
        xs = (BX + np.cos(ang) * BR + ox).astype(int)
        ys = (BCY - np.sin(ang) * BR + oy).astype(int)
        ok = (xs >= 0) & (xs < W) & (ys >= 0) & (ys < H)
        V[ys[ok], xs[ok]] = np.clip(v_arch[ok] + rib, 0, 1)
        wx = int(BX + BR + ox)
        wy = (wall_y + oy).astype(int)
        if 0 <= wx < W:
            V[np.clip(wy, 0, H - 1), wx] = clamp(v_wall + rib, 0, 1)
    body = Mask(0, 0, ~np.isnan(V))
    c.paint_values(body, cols, np.nan_to_num(V), 0.3)
    # rust and dents on the side
    side = body - m_rect(0, 0, BX + BR + 1, H)
    F.patches(c, side & m_rect(0, BCY - 10, W, 60), mix(p.metal[1], hx('#a0522d'), 0.55), 6, 0.42, 21, 0.06)
    c.paint(m_line(BX + BR + 60, BCY + 4, BX + BR + 74, BCY + 1, 1) & side, cols[1])
    c.paint(m_line(BX + BR + 60, BCY + 5, BX + BR + 74, BCY + 2, 1) & side, cols[4])
    # rivets along the bottom of the side
    for k in range(1, 9):
        f = k / 9
        c.px(BX + BR + BD[0] * f, BBOT - 3 + BD[1] * f, cols[0])
        c.px(BX + BR + BD[0] * f - 1, BBOT - 4 + BD[1] * f, cols[5])
    flag(c, p, s)
    # the front end
    front = m_disc(BX, BCY, BR) & m_rect(BX - BR - 1, BTOP - 1, 2 * BR + 2, BR + 1) | m_rect(BX - BR, BCY, 2 * BR + 1, BBOT - BCY)
    fcol = cols[:]
    nf = (-0.35, 0.0, 0.94)
    if s == WINTER:
        door_shut(c, p, s, front, fcol, nf)
    else:
        door_open(c, p, s, st, front, fcol, nf)


def flag(c, p, s):
    """the red flag on the right wall (down)"""
    px, py = BX + BR + BD[0] * 0.78, BCY + 6 + BD[1] * 0.78
    arm = m_poly([(px, py), (px - 70, py + 9), (px - 70, py + 13), (px, py + 4)])
    red = ramp_of(hx('#d23a2c'), p.s, 4, -0.6, 0.4)
    c.paint(arm, red[1])
    c.paint(m_line(px, py, px - 70, py + 9, 1), red[3])
    fm = m_poly([(px - 70, py + 4), (px - 50, py + 1), (px - 50, py + 15), (px - 70, py + 18)])
    c.paint(fm, red[2])
    c.paint(m_line(px - 70, py + 4, px - 50, py + 1, 1), red[3])
    c.paint(m_line(px - 70, py + 18, px - 50, py + 15, 1), red[0])
    c.paint(m_disc(px, py + 2, 2.4), p.iron[1])
    c.px(px - 1, py + 1, p.iron[3])
    if s == WINTER:
        c.paint(m_line(px - 70, py + 3, px - 50, py, 2), WHITE)


def door_open(c, p, s, st, front, cols, nf):
    # the rim of the front (a rolled edge) and the dark inside
    c.paint(front, F.face(p, cols, nf, 0.05))
    inner = m_disc(BX, BCY, BR - 5) & m_rect(BX - BR, BTOP, 2 * BR, BR) | m_rect(BX - BR + 5, BCY, 2 * BR - 10, BBOT - BCY - 3)
    c.paint(front.edge(), cols[1])
    c.paint(front.top(1), cols[5])
    dark = [hx('#0e1014'), hx('#1a1d24'), hx('#272b34'), hx('#3a3f4a')]
    c.paint_values(inner, dark, lambda x, y: np.clip(0.75 - (y - BTOP) / 120.0 + (x - BX) / 300.0, 0, 1), 0.3)
    # the floor of the box inside (it goes to the back) and the inner roof that catches the light
    c.paint(m_poly([(BX - BR + 5, BBOT - 3), (BX + BR - 5, BBOT - 3), (BX + BR - 22, BBOT - 12), (BX - BR + 14, BBOT - 12)]), dark[3])
    c.paint(inner.top(1), dark[3])
    # the hinge
    c.paint(m_rect(BX - 30, BBOT - 2, 60, 3), p.iron[1])
    c.paint(m_rect(BX - 30, BBOT - 2, 60, 1), p.iron[3])
    # the door that hangs down (we see its inside: the arch upside down and short, in the shadow of the box)
    dm = m_ellipse(BX, BBOT + 1, BR, 30) & m_rect(BX - BR, BBOT + 1, 2 * BR + 1, 31)
    dm = dm | m_rect(BX - BR, BBOT + 1, 2 * BR + 1, 6)
    dcol = [cols[0], cols[1], cols[2], cols[3]]
    c.paint_values(dm, dcol, lambda x, y: np.clip(0.2 + (y - BBOT) / 50.0 + (x - BX) / 400.0 * (1 if p.L[0] > 0 else -1), 0, 1), 0.3)
    c.paint(dm.edge(), cols[0])
    din = m_ellipse(BX, BBOT + 1, BR - 6, 24) & m_rect(BX - BR, BBOT + 7, 2 * BR + 1, 30)
    c.paint(din.edge(), cols[1])
    c.paint(din.top(1), cols[3])
    c.paint(m_rect(BX - 3, BBOT + 22, 7, 6), p.iron[1])            # the latch tab
    c.paint(m_rect(BX - 3, BBOT + 22, 7, 1), p.iron[3])
    F.patches(c, din & m_rect(0, BBOT + 10, BX, 40), mix(cols[1], hx('#a0522d'), 0.5), 5, 0.4, 44, 0.05)
    # what is in the box
    if s in (SPRING, SUMMER, AUTUMN):
        nest(c, p, s, inner)
    if s == AUTUMN and st.get('seeds', 1):
        packet(c, p, s)
    if s == SPRING:
        wren(c, p, BX + 30, BBOT - 3, 1.25, 'twig')
    if s == SUMMER:
        chicks(c, p)
        wren(c, p, BX + 104, BTOP - 6, 1.25, 'sing')
    if s == AUTUMN:
        # a feather the wren left on the door
        c.paint(m_polyline([(BX + 14, BBOT + 14), (BX + 24, BBOT + 11), (BX + 34, BBOT + 12)], 2.4), hx('#b08a60'))
        c.paint(m_line(BX + 14, BBOT + 14, BX + 34, BBOT + 12, 1), hx('#e8d8b8'))
        for k in range(4):
            c.px(BX + 18 + k * 4, BBOT + 11, hx('#6a4a2a'))


NEST = (BX - 2, BBOT - 3)            # the bottom middle of the nest
NDOOR = (BX - 10, BBOT - 30)         # the round door of the nest


def nest(c, p, s, inner):
    """the domed nest of the wren in the box: a ragged ball of dry grass, moss and twigs with a round door at its
    top left; strands hang over the rim. In autumn the old nest is grey and low (the wren left it)."""
    old = s == AUTUMN
    nx, ny = NEST
    hgt = 44 if not old else 34
    rng = RNG(33)
    pts = []
    for i in range(29):
        a = math.pi * i / 28
        r = 1.0 + 0.12 * math.sin(a * 7 + 1) + rng.uni(-0.07, 0.07)
        pts.append((nx - math.cos(a) * (BR - 6) * r, ny - math.sin(a) * hgt * r))
    nm = m_poly(pts) & inner
    straw = p.straw if not old else ramp_of(hx('#a49478'), s, 4, -0.65, 0.3)
    c.shade_sphere(nm, nx - 6, ny - 8, BR - 4, hgt + 4, straw, p.L, amb=0.3)
    moss = [mix(p.lfD, p.lf, 0.4), p.lf] if not old else [hx('#6a6a4e'), hx('#86866a')]
    F.patches(c, nm, moss[0], 6, 0.3, 7 if not old else 9, 0.06)
    # strands of grass all over (short curved lines), twigs, a few feathers
    for k in range(150):
        a = rng.uni(0.05, math.pi - 0.05)
        r = math.sqrt(rng()) * 1.05
        x = nx - math.cos(a) * (BR - 8) * r
        y = ny - math.sin(a) * hgt * r
        L = rng.uni(4, 11)
        b = rng.uni(-0.9, 0.9) + (0.6 if x > nx else -0.6) * 0
        col = rng.pick([straw[0], straw[1], straw[3], straw[3], moss[1]])
        q = bezier((x, y), (x + L * 0.5 * math.cos(b), y + L * 0.5 * math.sin(b) - 2), (x + L * math.cos(b), y + L * math.sin(b) * 0.6), None, 4)
        c.paint(m_polyline(q, 1.0) & inner.grow(2), col)
    for k in range(6):
        x = rng.uni(nx - 30, nx + 30)
        y = rng.uni(ny - hgt * 0.7, ny - 4)
        c.paint(m_line(x, y, x + rng.uni(10, 18), y + rng.uni(-4, 4), 1) & inner, mul(p.woodr[1], 1.1))
    # strands that hang over the rim of the box
    for k in range(9):
        x = rng.uni(BX - BR + 4, BX + BR - 4)
        L = rng.uni(5, 13)
        c.paint(m_polyline(bezier((x, BBOT - 4), (x + 2, BBOT + 1), (x - 1, BBOT - 3 + L), None, 5), 1.0), rng.pick([straw[1], straw[3]]))
    # the round door of the nest
    dx, dy = NDOOR
    if old:
        dy += 8
    c.paint(m_ellipse(dx, dy, 13, 10), straw[0])
    c.paint(m_ellipse(dx + 1, dy + 1, 10, 7.5), hx('#140e08'))
    c.paint(m_ellipse(dx + 3, dy + 3, 6, 4), hx('#24180e'))
    c.paint(m_ellipse(dx, dy, 13, 10).top(1), straw[3])
    if old:
        # a broken edge and a white feather
        c.paint(m_line(dx - 12, dy - 2, dx + 12, dy + 5, 1), straw[1])
        c.paint(m_polyline([(nx + 20, ny - 18), (nx + 27, ny - 22), (nx + 32, ny - 21)], 2.0), hx('#ece6da'))
    else:
        # fresh green moss at the door (spring and summer)
        for k in range(8):
            a = rng.uni(0, 2 * math.pi)
            c.paint(m_disc(dx + math.cos(a) * 13, dy + math.sin(a) * 10, 1.6), p.lfL)


def chicks(c, p):
    """three chicks gape in the round door of the nest (wide yellow mouths)"""
    dx, dy = NDOOR
    for k, (ox, oy, a) in enumerate(((-6, 1, -0.35), (2, -2, 0.05), (8, 2, 0.45))):
        x, y = dx + ox, dy + oy
        c.paint(m_disc(x, y + 3, 4.6), hx('#7a7060'))
        c.paint(m_disc(x - 1, y + 2, 2.2), hx('#a89c88'))
        c.px(x + 2, y + 1, hx('#141010'))
        tip = (x + 7 * math.sin(a), y - 10)
        c.paint(m_poly([(x - 4.5, y + 1), (x + 4.5, y + 1), tip]), hx('#ffd23a'))
        c.paint(m_poly([(x - 2.4, y), (x + 2.4, y), (x + 4.4 * math.sin(a), y - 6.5)]), hx('#f0602a'))


def packet(c, p, s):
    """a paper packet of sunflower seeds (a picture of a sunflower on it) and seeds that fell out"""
    x, y, w, h = BX + 4, BBOT - 58, 42, 54
    pm = m_poly([(x, y + 2), (x + w, y), (x + w + 2, y + h), (x + 2, y + h + 1)])
    c.darken(m_poly([(x + 3, y + 4), (x + w + 9, y + 2), (x + w + 11, y + h), (x + 4, y + h + 1)]), 0.6, p.cool)
    paper = ramp_of(hx('#efe2c0'), s, 4, -0.5, 0.35)
    c.paint(pm, paper[2])
    c.paint(pm.side(1, 0, 2), paper[1])
    c.paint(pm.edge(), paper[0])
    # the fold at the top
    c.paint(m_poly([(x, y + 2), (x + w, y), (x + w, y + 7), (x, y + 9)]), paper[3])
    c.paint(m_line(x, y + 9, x + w, y + 7, 1), paper[1])
    # the green band at the bottom
    c.paint(m_poly([(x + 2, y + h - 9), (x + w + 2, y + h - 10), (x + w + 2, y + h), (x + 2, y + h + 1)]), hx('#4f8a3a'))
    c.paint(m_line(x + 2, y + h - 9, x + w + 2, y + h - 10, 1), hx('#7fb85a'))
    # the sunflower on the packet
    fx, fy = x + w / 2 + 1, y + 25
    for k in range(12):
        a = k * math.pi / 6
        c.paint(m_ellipse(fx + math.cos(a) * 8.5, fy + math.sin(a) * 8.5, 3.6, 1.9, rot=a), hx('#ffc020'))
        c.px(fx + math.cos(a) * 10, fy + math.sin(a) * 10, hx('#ffe070'))
    c.paint(m_disc(fx, fy, 6), hx('#5a3a1a'))
    c.speckle(m_disc(fx, fy, 5), [hx('#3a2410'), hx('#8a5a2a')], 0.4, 3)
    c.paint(m_line(fx, fy + 7, fx, y + h - 10, 1), hx('#4f8a3a'))
    c.paint(m_ellipse(fx + 4, fy + 13, 3.4, 1.5, rot=-0.5), hx('#4f8a3a'))
    # a torn corner: the seeds come out
    c.paint(m_poly([(x + w - 6, y + h - 10), (x + w + 2, y + h - 12), (x + w + 2, y + h - 4)]), hx('#2a1e14'))
    for (sx, sy, a) in ((x - 8, BBOT - 5, 0.3), (x - 2, BBOT - 3, -0.6), (x + w + 7, BBOT - 4, 1.1),
                        (x + w + 12, BBOT - 6, -0.2), (BX - 18, BBOT + 10, 0.2), (BX + 6, BBOT + 16, 1.4)):
        c.paint(m_ellipse(sx, sy, 2.8, 1.4, rot=a), hx('#2a2420'))
        c.paint(m_line(sx - 1.5, sy, sx + 1.5, sy - 0.5, 1), hx('#e8e0d0'))


def wren(c, p, x, y, sc, pose):
    """the wren: a round brown bird, its barred tail up, a pale stripe over the eye, a thin beak (sc = its scale)"""
    def q(dx, dy):
        return (x + dx * sc, y + dy * sc)

    def Q(pts):
        return [q(a, b) for a, b in pts]
    body = ramp_of(hx('#8a5a32'), p.s, 4, -0.65, 0.45)
    buff = ramp_of(hx('#d8b88a'), p.s, 3, -0.4, 0.35)
    leg = hx('#c89a7a')
    beak = hx('#5a4030')
    # the tail (up and back) with its dark bars
    t0, t1, t2, t3 = q(-10, -10), q(-20, -30), q(-15, -32), q(-4, -13)
    c.paint(m_poly([t0, t1, t2, t3]), body[1])
    for k in range(4):
        f = 0.25 + k * 0.18
        c.paint(m_line(lerp(t0[0], t1[0], f), lerp(t0[1], t1[1], f), lerp(t3[0], t2[0], f), lerp(t3[1], t2[1], f), 1), body[0])
    # the legs
    c.paint(m_line(*q(-2, -2), *q(-4, 4), 1), leg)
    c.paint(m_line(*q(3, -2), *q(4, 4), 1), leg)
    c.paint(m_line(*q(-7, 4), *q(7, 4), 1), leg)
    # the body
    bx, by = q(0, -9)
    bm = m_ellipse(bx, by, 13 * sc, 9 * sc)
    c.outline(bm, body[0])
    c.shade_sphere(bm, bx, by, 13 * sc, 9 * sc, body, p.L, amb=0.3)
    c.paint(m_ellipse(*q(4, -5), 8 * sc, 4.5 * sc), buff[1])
    c.paint(m_ellipse(*q(5, -6), 5 * sc, 2.5 * sc), buff[2])
    # the wing with its bars
    wm = m_ellipse(*q(-2, -10), 8 * sc, 5 * sc, rot=-0.2)
    c.paint(wm, body[1])
    for k in range(4):
        c.paint(m_line(*q(-8 + k * 3, -13 + k), *q(-6 + k * 3, -6 + k * 0.5), 1) & wm, body[0])
    # the head
    hy = -18 if pose != 'sing' else -20
    hc = q(9, hy)
    hm = m_disc(hc[0], hc[1], 6.5 * sc)
    c.outline(hm, body[0])
    c.shade_sphere(hm, hc[0], hc[1], 6.5 * sc, 6.5 * sc, body, p.L, amb=0.35)
    c.paint(m_line(*q(5, hy - 2), *q(15, hy - 3), 1), buff[2])          # the stripe over the eye
    c.paint(m_disc(*q(11, hy), 1.4 * sc), hx('#141010'))
    c.px(*q(11, hy - 1), WHITE)
    c.paint(m_ellipse(*q(11, hy + 3), 3 * sc, 1.5 * sc), buff[1])
    if pose == 'sing':
        c.paint(m_poly(Q([(14, hy - 1), (22, hy - 6), (15, hy)])), beak)
        c.paint(m_poly(Q([(14, hy + 1), (22, hy + 1), (15, hy + 2)])), beak)
        # the song (small notes in the air)
        for k, (nx, ny) in enumerate(((28, hy - 12), (36, hy - 20), (30, hy - 27))):
            col = mix(body[1], WHITE, 0.25)
            c.paint(m_ellipse(*q(nx, ny), 2 * sc, 1.5 * sc), col)
            c.paint(m_line(*q(nx + 1.5, ny), *q(nx + 1.5, ny - 6), 1), col)
    else:
        c.paint(m_poly(Q([(14, hy), (22, hy + 1), (14, hy + 2)])), beak)
        if pose == 'twig':
            twig = hx('#8a6a3a')
            c.paint(m_polyline(Q([(8, hy + 6), (18, hy + 1), (30, hy - 4), (38, hy - 3)]), 1.4), twig)
            c.paint(m_line(*q(26, hy - 3), *q(29, hy - 9), 1), twig)
            c.paint(m_ellipse(*q(30, hy - 10), 2.4, 1.4), p.lfL)
            c.paint(m_ellipse(*q(36, hy - 6), 2.0, 1.2), p.lf)


def door_shut(c, p, s, front, cols, nf):
    """winter: the door shut and frozen: a glaze of ice on the seam, frost flowers, icicles, snow on the roof"""
    c.paint(front, F.face(p, cols, nf, 0.05))
    door = m_disc(BX, BCY, BR - 2) & m_rect(BX - BR, BTOP, 2 * BR, BR) | m_rect(BX - BR + 2, BCY, 2 * BR - 3, BBOT - BCY - 1)
    c.paint_values(door, cols[1:5], lambda x, y: np.clip(0.8 - (x - (BX - BR)) / 300.0 - (y - BTOP) / 500.0, 0, 1), 0.3)
    c.paint(door.edge(), cols[1])
    c.paint(front.edge(), cols[0])
    # the bead round the door and the latch
    bead = m_disc(BX, BCY, BR - 9) & m_rect(BX - BR, BTOP, 2 * BR, BR) | m_rect(BX - BR + 9, BCY, 2 * BR - 17, BBOT - BCY - 8)
    c.paint(bead.edge() & bead.top(1), cols[5])
    c.paint(bead.side(1, 0, 1) | bead.bottom(1), cols[1])
    c.paint(m_rect(BX - 4, BTOP - 4, 8, 6), cols[2])
    c.paint(m_rect(BX - 4, BTOP - 4, 8, 1), cols[5])
    # frost flowers at the lower corners of the door (white branches of ice)
    rng = RNG(12)
    for (fx, fy, a0) in ((BX - BR + 6, BBOT - 6, -0.6), (BX + BR - 8, BBOT - 6, -2.5), (BX - 10, BBOT - 4, -1.4)):
        for k in range(5):
            a = a0 + rng.uni(-0.7, 0.7)
            L = rng.uni(10, 22)
            x1, y1 = fx + math.cos(a) * L, fy + math.sin(a) * L
            c.paint(m_line(fx, fy, x1, y1, 1) & door, WHITE)
            for j in range(1, 4):
                f = j / 4
                mx, my = lerp(fx, x1, f), lerp(fy, y1, f)
                for sgn in (-1, 1):
                    b = a + sgn * 0.8
                    c.paint(m_line(mx, my, mx + math.cos(b) * L * 0.22, my + math.sin(b) * L * 0.22, 1) & door, hx('#dceefa'))
    # the glaze of ice on the seam (it holds the door shut)
    seam = front.edge() | door.edge()
    c.paint(seam & m_rect(0, BCY - 20, W, 80), hx('#bfe0f2'))
    c.paint(m_rect(BX - BR - 1, BBOT - 3, 2 * BR + 2, 4), hx('#cfe6f6'))
    c.paint(m_rect(BX - BR - 1, BBOT - 3, 2 * BR + 2, 1), WHITE)
    # snow on the roof (a thick slab along the box) and its end that we see at the front
    caps = []
    for k in range(0, 41):
        t = k / 40
        ox, oy = BD[0] * t, BD[1] * t
        pts = []
        for a in np.linspace(0.32, math.pi - 0.32, 14):
            bump = 7 + 2.0 * math.sin(a * 5 + t * 9)
            pts.append((BX + math.cos(a) * (BR + bump * math.sin(a)) + ox, BCY - math.sin(a) * (BR + bump * math.sin(a)) + oy))
        for a in np.linspace(math.pi - 0.32, 0.32, 10):
            pts.append((BX + math.cos(a) * (BR - 1) + ox, BCY - math.sin(a) * (BR - 1) + oy))
        caps.append(m_poly(pts))
    allc = m_union(caps)
    c.paint(allc, p.snow[2])
    c.paint(allc.top(2), WHITE)
    c.paint(caps[0], p.snow[1])
    c.paint(caps[0].top(1), p.snow[2])
    c.paint((allc.shift(0, 1) - allc) & front, p.snow[0])
    # icicles on the front edge and along the side
    F.icicles(c, p, BX - BR - 2, BX + BR + 2, BBOT + 2, 7, lmax=20, step=4.0, dens=0.85)
    for k in range(9):
        f = 0.08 + k * 0.1
        x = BX + BR + 4 + BD[0] * f
        y = BBOT + 6 + BD[1] * f
        L = 4 + (k * 7) % 11
        c.paint(m_poly([(x - 1.5, y), (x + 1.5, y), (x, y + L)]), hx('#cfe6f6'))
        c.paint(m_line(x, y, x, y + L * 0.6, 1), WHITE)
    # a robin on the snow of the roof (the only bird in winter)
    rx, ry = BX + BR + 70, BTOP - 10 + BD[1] * 0.45
    c.paint(m_ellipse(rx, ry, 8, 6), hx('#6a5a4a'))
    c.paint(m_ellipse(rx + 3, ry + 1, 5, 4), hx('#e8642a'))
    c.paint(m_disc(rx + 7, ry - 5, 4), hx('#6a5a4a'))
    c.paint(m_disc(rx + 8, ry - 4, 2.4), hx('#e8642a'))
    c.px(rx + 8, ry - 6, hx('#141010'))
    c.paint(m_line(rx + 11, ry - 5, rx + 13, ry - 5, 1), hx('#3a2a1a'))
    c.paint(m_poly([(rx - 7, ry - 2), (rx - 15, ry - 6), (rx - 14, ry - 2)]), hx('#5a4a3a'))
