# FOUR SEASONS: THE SHED (a close view): the red shed of the valley, near: its gable front with the door (a white X,
# a padlock on the hasp) and a big workshop window; the dark side wall and the roof go back to the right; a wood pile
# and a rain barrel; up the hill at the left, far, the big tree.
#   door 0: locked; 1: open (the door swings out to the left, the doorway shows the inside: shelves, tools, a bucket).
#   pole 1: the long hooked pole leans in the shed next to the workbench, behind the window; 0: taken.
#   The workbench with the glue pot and the pole show through the window (the layers of the two things must not touch
#   the same pixels: so the pole cannot hide behind the closed door). Summer: the sun on the bench, the glue is soft
#   and shiny. Autumn and winter: a lantern burns inside (as the valley shows a lit window). Winter: snow on the
#   roof, icicles, frost on the glass.
import math
import numpy as np
from fsart import Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline, m_union
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fsparts_c as C

W, H = 672, 180
VARS = {'door': [0, 1], 'pole': [1, 0]}
HOT = {
    'door': (190, 70, 92, 98),
    'bench': (322, 98, 72, 44),
    'pole': (398, 72, 54, 70),
}

FX0, FX1, EAVE, GROUND = 168, 472, 64, 168
APEX = (320, 4)
DX0, DX1, DY0 = 196, 276, 72                # the door
WX0, WX1, WY0, WY1 = 316, 456, 74, 140      # the window (outer frame)
IX0, IX1, IY0, IY1 = 322, 450, 79, 135      # the glass
BX0, BX1, BY0, BY1 = 384, 388, 98, 101      # the bars of the window
SIDE = [(FX1, EAVE), (566, 50), (566, 156), (FX1, GROUND)]
WHITE = (255, 255, 255)


def hill_y(x):
    return 122 - 50 * math.exp(-((x - 64) / 120) ** 2) - 2 * math.sin(x * 0.03)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    C.sky(c, p, 130, nclouds=5, seed=57)
    P.mountains(c, p, 116, 32, 0.007, 19)
    P.far_ridge(c, p, 126, 9, 0.012, 24, step=8, tree_h=(3, 6))
    hm = m_poly([(-4, H + 4)] + [(x, hill_y(x)) for x in range(-4, W + 6, 3)] + [(W + 4, H + 4)])
    c.paint(hm, p.hill)
    P.grass_marks(c, p, hm, 1400, 58, face_fn=lambda x: -(hill_y(x + 2) - hill_y(x - 2)) / 4 * (1 if p.L[0] < 0 else -1))
    C.far_tree(c, p, 70, hill_y(70) + 2)
    ground(c, p, s)
    P.bush(c, p, 122, 168, 13, seed=5)
    block(c, p, s)
    woodpile(c, p, s)
    shed_body(c, p, s)
    window(c, p, s, st)
    door(c, p, s, st)
    barrel(c, p, s)
    P.particles_baked(c, p, (0, 0, W, H), 30, 59)
    return c


def ground(c, p, s):
    gm = m_poly([(-4, H + 4)] + [(x, 160 + 2 * math.sin(x * 0.02)) for x in range(-4, W + 6, 4)] + [(W + 4, H + 4)])
    path = m_poly([(DX0 + 4, GROUND), (DX1 - 4, GROUND), (DX1 + 30, H + 4), (DX0 - 50, H + 4)])
    if s == WINTER:
        C.snowfield(c, p, gm, 63, 160, H, stalks=40)
        c.paint_values(path, p.snow, lambda x, y: np.full_like(x, 0.45), 0.3)
        for k in range(6):
            x = lerp(DX0 + 30, DX0 - 6, k / 5) + (k % 2) * 10
            y = lerp(GROUND + 2, H - 2, k / 5)
            c.paint(m_ellipse(x, y, 3 + k * 0.3, 1.5), p.snow[0])
        return
    C.turf(c, p, gm, 63, 160, H, n=900, hmax=7)
    c.paint_values(path, p.pathr, lambda x, y: np.clip(0.55 + (y - GROUND) * 0.012, 0, 1), 0.4)
    c.paint(path.edge(), p.pathr[0])
    rng = RNG(64)
    for k in range(40):
        x = rng.uni(DX0 - 30, DX1 + 10)
        y = rng.uni(GROUND + 1, H)
        c.paint(m_ellipse(x, y, rng.uni(1, 2.4), rng.uni(0.6, 1.2)) & path, p.pathr[rng.int(0, 3)])
    C.litter(c, p, gm, 60, 65)
    P.flowers(c, p, gm - path, 60, 66)


def block(c, p, s):
    """a chopping block with an axe, at the left"""
    x, y = 58, 170
    top = m_ellipse(x, y - 16, 15, 4)
    body = m_rect(x - 15, y - 16, 30, 16) | m_ellipse(x, y, 15, 4)
    c.darken(m_ellipse(x + p.slant * 12, y + 2, 20, 3), 0.75, p.cool)
    c.outline(body | top, mul(p.woodD, 0.6))
    c.shade_dir(body, p.bark, -1 if p.L[0] < 0 else 1, 0, x - 15, 0, x + 15, 1)
    for k in range(5):
        c.paint(m_line(x - 12 + k * 6, y - 14, x - 13 + k * 6, y + 2, 1) & body, p.bark[0])
    c.paint(top, p.woodr[3])
    c.paint(m_ellipse(x, y - 16, 10, 2.6) - m_ellipse(x, y - 16, 9, 2), p.woodr[2])
    c.paint(m_ellipse(x, y - 16, 5, 1.4) - m_ellipse(x, y - 16, 4, 0.9), p.woodr[2])
    # the axe in the block
    c.paint(m_line(x + 2, y - 17, x + 22, y - 40, 2.4), p.woodr[3])
    c.paint(m_line(x + 2.6, y - 17, x + 22.6, y - 40, 1), p.woodr[1])
    c.paint(m_poly([(x - 3, y - 15), (x + 6, y - 22), (x + 9, y - 17), (x + 3, y - 13)]), p.metal[1])
    c.paint(m_line(x - 3, y - 15, x + 3, y - 13, 1), p.metal[3])
    if s == WINTER:
        c.paint(m_ellipse(x - 2, y - 18, 14, 3.4), WHITE)
        c.paint(m_line(x + 8, y - 26, x + 20, y - 41, 1), WHITE)


def woodpile(c, p, s):
    """the firewood stacked against the right of the shed: the ends of the logs with their rings"""
    rng = RNG(71)
    x0, x1 = 596, 672
    rows = 5 if s in (AUTUMN, WINTER) else 3
    cols = [hx('#7a5032'), hx('#b07a4a'), hx('#d8a46a'), hx('#f0c890')]
    tops = []
    for r in range(rows):
        y = GROUND - 6 - r * 11
        x = x0 + (r % 2) * 6 + r * 3
        while x < x1 + 6:
            rr = rng.uni(5, 6.4)
            m = m_ellipse(x, y, rr, rr * 0.92)
            c.outline(m, hx('#3a2414'))
            c.paint(m, cols[1])
            c.paint(m_ellipse(x - 1, y - 1, rr * 0.7, rr * 0.64), cols[2])
            c.paint(m_ellipse(x - 1, y - 1, rr * 0.36, rr * 0.33) - m_ellipse(x - 1, y - 1, rr * 0.2, rr * 0.18), cols[1])
            c.paint(m.side(1 if p.L[0] < 0 else -1, 1, 1), cols[0])
            c.paint(m_line(x, y, x + rr * 0.8, y - rr * 0.4, 1) & m, cols[0])
            if r == rows - 1:
                tops.append(m)
            x += rr * 2 + 0.5
    if s == WINTER and tops:
        allm = m_union(tops)
        c.snow_cap(allm, 3, p.snow, rough=1)


def barrel(c, p, s):
    """the rain barrel under the corner of the roof"""
    x, y, rx, h = 584, GROUND - 1, 13, 30
    body = m_rect(x - rx, y - h, rx * 2, h) | m_ellipse(x, y, rx, 3)
    c.darken(m_ellipse(x + p.slant * 10, y + 2, 18, 3), 0.75, p.cool)
    c.outline(body, hx('#2a1a10'))
    c.shade_dir(body, p.woodr[:4], -1 if p.L[0] < 0 else 1, 0, x - rx, 0, x + rx, 1)
    for k in range(5):
        c.paint(m_line(x - rx + 3 + k * 5, y - h, x - rx + 3 + k * 5, y + 2, 1) & body, p.woodr[1])
    for hy in (y - h + 5, y - 6):
        c.paint(m_rect(x - rx, hy, rx * 2, 2.4), p.iron[1])
        c.paint(m_rect(x - rx, hy, rx * 2, 1), p.iron[2])
    top = m_ellipse(x, y - h, rx, 3.4)
    c.paint(top, p.woodr[1])
    inner = m_ellipse(x, y - h + 0.6, rx - 2, 2.4)
    if s == WINTER:
        c.paint(inner, p.ice[2])
        c.paint(m_ellipse(x, y - h - 1, rx + 1, 2.4), WHITE)
    else:
        c.paint(inner, mix(p.waterD, p.skyT, 0.3))
        c.paint(m_rect(x - 6, y - h, 7, 1) & inner, mix(p.water, WHITE, 0.4))
    # the down pipe from the gutter
    c.paint(m_rect(x - 2, 54, 4, y - h - 54 + 2), p.metal[1])
    c.paint(m_rect(x - 2, 54, 1, y - h - 54 + 2), p.metal[3])


def shed_body(c, p, s):
    lx, ly = C.ldir(p)
    front = m_poly([(FX0, GROUND), (FX0, EAVE), APEX, (FX1, EAVE), (FX1, GROUND)])
    side = m_poly(SIDE)
    # the shadow of the shed on the ground
    c.darken(m_poly([(FX0, GROUND), (566, 156), (566 + p.slant * 30, 160), (FX0 + p.slant * 30, GROUND + 6)]), 0.72, p.cool)
    # the side wall (away from the light in spring and summer)
    sidec = [mul(p.shedD, 0.7), p.shedD, mix(p.shedD, p.shed, 0.5), p.shed]
    if p.L[0] > 0:
        sidec = [p.shedD, mix(p.shedD, p.shed, 0.5), p.shed, mix(p.shed, p.warm, 0.25)]
    c.paint(side, sidec[1])
    for k in range(7):
        f = (k + 0.5) / 7
        xa = lerp(FX1, 566, f)
        c.paint(m_line(xa, lerp(EAVE, 50, f), xa, lerp(GROUND, 156, f), 1), sidec[0])
        c.paint(m_line(xa + 1, lerp(EAVE, 50, f), xa + 1, lerp(GROUND, 156, f), 1), sidec[2])
    c.paint(m_poly([(FX1, EAVE), (566, 50), (566, 60), (FX1, EAVE + 12)]), mul(sidec[0], 0.85))
    # the front wall: red boards, some paint peeled off
    c.outline(front, mul(p.shedD, 0.5))
    redc = [mul(p.shedD, 0.8), p.shedD, p.shed, mix(p.shed, p.warm, 0.2), mix(p.shed, p.warm, 0.4)]
    C.boards(c, p, FX0, 0, FX1, GROUND, 16, redc, 801, clip=front, nails=True, knots=False)
    rng = RNG(802)
    for k in range(9):
        x = rng.uni(FX0 + 8, FX1 - 8)
        y = rng.uni(EAVE + 6, GROUND - 10)
        if DX0 - 4 < x < DX1 + 4 or (WX0 - 6 < x < WX1 + 6 and WY0 - 6 < y < WY1 + 18):
            continue
        pm = m_poly([(x + rng.uni(-5, -2), y + rng.uni(-3, 0)), (x + rng.uni(1, 5), y + rng.uni(-4, -1)),
                     (x + rng.uni(2, 6), y + rng.uni(1, 4)), (x + rng.uni(-4, 0), y + rng.uni(2, 5))]) & front
        c.paint(pm, p.woodr[3])
        c.paint(pm.side(-lx or 1, 1, 1), p.woodr[2])
        c.outline(pm, redc[1])
    # the base board, dark and damp
    c.paint(m_rect(FX0, GROUND - 6, FX1 - FX0, 6), mul(p.shedD, 0.7))
    c.paint(m_rect(FX0, GROUND - 6, FX1 - FX0, 1), p.shed)
    # the vent in the gable
    vm = m_poly([(APEX[0] - 18, 42), (APEX[0], 26), (APEX[0] + 18, 42)])
    c.outline(vm, p.trim)
    c.paint(vm, hx('#1c1414'))
    for k in range(4):
        c.paint(m_rect(APEX[0] - 16 + k * 2, 31 + k * 3, 32 - k * 4, 1) & vm, p.redr[1])
    # the roof: over the side, going back
    roof = m_poly([(APEX[0] - 2, APEX[1] - 3), (414, -14), (574, 46), (FX1 + 4, EAVE + 2)])
    rc = p.roofr
    c.paint(roof, rc[2])
    for k in range(1, 12):
        f = k / 12
        c.paint(m_line(lerp(APEX[0] - 2, FX1 + 4, f), lerp(APEX[1] - 3, EAVE + 2, f),
                       lerp(414, 574, f), lerp(-14, 46, f), 1), rc[1])
        c.paint(m_line(lerp(APEX[0] - 2, FX1 + 4, f) + 1, lerp(APEX[1] - 3, EAVE + 2, f),
                       lerp(414, 574, f) + 1, lerp(-14, 46, f), 1) & roof, rc[3] if p.L[0] > 0 else rc[2])
    c.paint(m_line(FX1 + 4, EAVE + 2, 574, 46, 2), rc[0])
    # the gutter
    c.paint(m_line(FX1 + 2, EAVE + 4, 576, 48, 3), p.metal[1])
    c.paint(m_line(FX1 + 2, EAVE + 3, 576, 47, 1), p.metal[3])
    if s == WINTER:
        snow = m_poly([(APEX[0] - 4, APEX[1] - 7), (416, -18), (578, 42), (FX1 + 6, EAVE - 1)])
        c.paint_values(snow, p.snow, lambda x, y: np.clip(0.85 - (x - 320) / 600, 0, 1), 0.35)
        c.paint(m_line(FX1 + 6, EAVE - 1, 578, 42, 2), WHITE)
        c.paint(m_polyline([(FX1 + 4, EAVE + 1), (526, 56), (578, 45)], 1), p.snow[0])
        rng2 = RNG(803)
        for k in range(16):
            f = rng2()
            x = lerp(FX1 + 6, 576, f)
            y = lerp(EAVE + 6, 50, f)
            L = rng2.uni(3, 11)
            c.paint(m_poly([(x - 1.4, y), (x + 1.4, y), (x, y + L)]), hx('#dff0ff'))
            c.px(x, y + 1, WHITE)
    # the trim of the gable (white boards) and the overhang of the left roof
    tr = m_polyline([(FX0 - 8, EAVE + 4), APEX, (FX1 + 8, EAVE + 4)], 5)
    c.outline(tr, mul(p.roof, 0.5))
    c.paint(tr, p.trim)
    c.paint(m_polyline([(FX0 - 8, EAVE + 6), (APEX[0], APEX[1] + 2), (FX1 + 8, EAVE + 6)], 1), mul(p.trim, 0.8))
    if s == WINTER:
        c.paint(m_polyline([(FX0 - 9, EAVE + 1), (APEX[0], APEX[1] - 3), (FX1 + 9, EAVE + 1)], 3), WHITE)
        c.paint(m_polyline([(FX0 - 9, EAVE + 2), (APEX[0], APEX[1] - 2), (FX1 + 9, EAVE + 2)], 1), p.snow[1])
    # the corner boards
    for x in (FX0, FX1 - 5):
        c.paint(m_rect(x, EAVE + 2, 5, GROUND - EAVE - 2), p.trim)
        c.paint(m_rect(x + (4 if lx < 0 else 0), EAVE + 2, 1, GROUND - EAVE - 2), mul(p.trim, 0.8))
    # the horseshoe over the door (for luck)
    hm = m_ellipse(236, 60, 6, 6.6) - m_ellipse(236, 60, 3.6, 4.2) - m_rect(230, 62, 12, 6)
    c.outline(hm, hx('#1a1a1e'))
    c.paint(hm, p.iron[2])
    c.paint(hm.side(lx, ly, 1), p.iron[3])


def window(c, p, s, st):
    """the workshop window: the inside (the shelf, the saw, the lantern, the bench, the glue pot, the pole), the glass,
    the frame, the flower box"""
    glass = m_rect(IX0, IY0, IX1 - IX0, IY1 - IY0)
    dim = {SPRING: 0.8, SUMMER: 0.9, AUTUMN: 0.72, WINTER: 0.66}[s]
    wall = [mul(p.woodD, 0.5 * dim), mul(p.woodD, 0.8 * dim), mul(p.wood, 0.78 * dim), mul(p.wood, 0.98 * dim)]
    C.boards(c, p, IX0 - 4, IY0, IX1 + 4, IY1, 13, wall, 811, clip=glass, nails=False, knots=True)
    lamp = s in (AUTUMN, WINTER)
    # the shelf with jars (upper left pane)
    c.paint(m_rect(IX0, 92, BX0 - IX0, 3), wall[3])
    c.paint(m_rect(IX0, 95, BX0 - IX0, 1), wall[0])
    for (x, h, col) in [(328, 9, hx('#7fa0bc')), (337, 7, hx('#c8a040')), (346, 10, hx('#6a8a5a')), (356, 6, hx('#a04030')),
                        (366, 8, hx('#d8d0c0'))]:
        jm = m_rect(x, 92 - h, 7, h)
        c.outline(jm, wall[0])
        c.paint(jm, mul(col, dim))
        c.paint(m_rect(x + 1, 92 - h + 1, 1, h - 2), mix(mul(col, dim), WHITE, 0.4))
        c.paint(m_rect(x, 92 - h - 1, 7, 1.4), mul(p.metal[2], dim))
    # the saw on the wall (upper right pane)
    saw = m_poly([(394, 83), (430, 84), (430, 90), (398, 93)])
    c.paint(saw, mul(p.metal[2], dim + 0.1))
    c.paint(m_line(395, 92, 430, 89, 1), mul(p.metal[0], dim + 0.1))
    c.paint(m_line(396, 84, 429, 85, 1), mul(p.metal[3], dim + 0.1))
    c.paint(m_rect(430, 82, 9, 10), mul(p.woodr[2], dim + 0.15))
    c.paint(m_rect(432, 85, 5, 3), wall[0])
    # the lantern on its hook (it burns in autumn and winter)
    c.paint(m_line(370, BY1, 370, 104, 1), wall[0])
    lm = m_rect(365, 104, 11, 13)
    c.outline(lm, wall[0])
    c.paint(lm, mul(p.iron[1], 0.9))
    c.paint(m_rect(367, 106, 7, 8), hx('#ffcf6a') if lamp else mul(p.glass[1], dim + 0.2))
    c.paint(m_rect(369, 108, 3, 5), hx('#fff4c0') if lamp else mul(p.glass[2], dim + 0.2))
    if lamp:
        # the warm light of the lantern on the wall (a flat dither: no new colours)
        c.paint_dither(m_disc(370.5, 110, 24) & glass, mul(p.woodr[3], 1.0), 0.35)
        c.paint_dither(m_disc(370.5, 110, 13) & glass, hx('#e0a868'), 0.45)
    # the workbench
    c.paint(m_rect(IX0, 121, IX1 - IX0, 4), mul(p.woodr[3], dim + 0.1))
    c.paint(m_rect(IX0, 121, IX1 - IX0, 1), mul(p.woodr[4], dim + 0.15))
    c.paint(m_rect(IX0, 125, IX1 - IX0, 8), mul(p.woodr[2], dim))
    c.paint(m_rect(IX0, 132, IX1 - IX0, 3), mul(p.woodr[0], dim))
    c.paint(m_rect(IX0 + 30, 127, 14, 3), mul(p.woodr[1], dim))                    # a drawer
    c.paint(m_rect(IX0 + 35, 128, 4, 1), mul(p.metal[3], dim))
    if s == SUMMER:
        # the summer sun lies on the bench
        c.paint(m_poly([(330, 121), (404, 121), (408, 125), (334, 125)]), hx('#fff0c0'))
        c.paint(m_poly([(334, 125), (408, 125), (410, 132), (336, 132)]), hx('#e8c080'))
    rng = RNG(812)
    for k in range(10):
        x = rng.uni(362, 404)
        c.paint(m_ellipse(x, 120.5, 2.2, 1.3) - m_ellipse(x + 0.6, 120, 1.3, 0.6), mul(hx('#f0d8a8'), dim + 0.2))
    c.paint(m_rect(376, 118, 13, 2), mul(p.metal[2], dim + 0.2))                     # a chisel
    c.paint(m_rect(389, 118, 7, 2), mul(p.woodr[3], dim + 0.2))
    glue_pot(c, p, s, dim)
    if st.get('pole', 1):
        pole(c, p, s, dim, glass)
    # the glass: two thin light streaks (and frost in the corners in winter), then the frame and the bars
    gl = mix(p.skyB, WHITE, 0.55)
    for (a, w) in ((IX0 + 18, 1), (IX0 + 23, 2), (IX0 + 96, 1)):
        c.paint(m_line(a, IY0, a - 18, IY0 + 26, w) & glass, gl)
    if s == WINTER:
        for (x0, y0, x1, y1) in [(IX0, IY0, BX0, BY0), (BX1, IY0, IX1, BY0), (IX0, BY1, BX0, IY1), (BX1, BY1, IX1, IY1)]:
            for (cx, cy) in [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]:
                fm = m_disc(cx, cy, 6) & m_rect(x0, y0, x1 - x0, y1 - y0)
                c.paint_dither(fm, hx('#e8f4ff'), 0.5)
                c.paint(m_disc(cx, cy, 3) & fm, WHITE)
    frame = m_rect(WX0, WY0, WX1 - WX0, WY1 - WY0) - glass
    c.outline(m_rect(WX0, WY0, WX1 - WX0, WY1 - WY0), mul(p.shedD, 0.5))
    c.paint(frame, p.trim)
    c.paint(m_rect(BX0, IY0, BX1 - BX0, IY1 - IY0), p.trim)
    c.paint(m_rect(IX0, BY0, IX1 - IX0, BY1 - BY0), p.trim)
    c.paint(m_rect(IX0, BY1, IX1 - IX0, 1), mul(p.trim, 0.7))
    c.paint(m_rect(IX0, IY0, IX1 - IX0, 1), mul(p.trim, 0.6))
    # the sill and the flower box
    c.paint(m_rect(WX0 - 4, WY1, WX1 - WX0 + 8, 3), p.trim)
    box = m_rect(WX0 + 2, WY1 + 3, WX1 - WX0 - 4, 11)
    c.outline(box, mul(p.woodD, 0.6))
    c.paint(box, p.paint_green[2])
    c.paint(m_rect(WX0 + 2, WY1 + 3, WX1 - WX0 - 4, 1), p.paint_green[3])
    c.paint(m_rect(WX0 + 2, WY1 + 12, WX1 - WX0 - 4, 2), p.paint_green[0])
    rng = RNG(813)
    if s == WINTER:
        c.paint(m_ellipse((WX0 + WX1) / 2, WY1 + 2, (WX1 - WX0) / 2 + 2, 3), WHITE)
        for k in range(7):
            c.paint(m_line(WX0 + 10 + k * 19, WY1 + 1, WX0 + 10 + k * 19 + rng.uni(-2, 2), WY1 - 3, 1), hx('#7a6a50'))
        return
    for k in range(24):
        x = WX0 + 6 + k * 5.6 + rng.uni(-1, 1)
        hgt = rng.uni(3, 7)
        stem = hx('#4a7a3a') if s != AUTUMN else hx('#8a7a4a')
        c.paint(m_line(x, WY1 + 5, x + rng.uni(-1.5, 1.5), WY1 + 5 - hgt, 1), stem)
        if s == AUTUMN:
            c.px(x, WY1 + 4 - hgt, hx('#6a4a2a'))
            continue
        col = rng.pick(p.flower) if s == SUMMER else rng.pick([hx('#ff5a7a'), hx('#ffd23f'), WHITE, hx('#c7a4ff')])
        if rng() < 0.75:
            C.sprite(c, int(x) - 1, int(WY1 + 3 - hgt), ['.P.', 'PCP', '.P.'], {'P': col, 'C': hx('#ffe36b')})
        c.paint(m_ellipse(x + 2, WY1 + 4, 2, 1.2), p.leaf[2])


def glue_pot(c, p, s, dim):
    """the glue pot on the bench: summer: the glue is soft, shiny, it runs down the side; else hard and dull"""
    x, y = 344, 121
    body = m_rect(x - 9, y - 15, 18, 15)
    c.outline(body, hx('#141418'))
    c.shade_dir(body, [mul(cc, dim + 0.15) for cc in p.metal], -1 if p.L[0] < 0 else 1, 0, x - 9, 0, x + 9, 1)
    c.paint(m_rect(x - 9, y - 11, 18, 6), mul(hx('#c8a040'), dim + 0.15))         # the label
    c.paint(m_rect(x - 5, y - 9, 10, 1), mul(hx('#5a3a10'), dim + 0.15))
    c.paint(m_rect(x - 3, y - 7, 6, 1), mul(hx('#5a3a10'), dim + 0.15))
    rim = m_ellipse(x, y - 15, 9, 2.4)
    if s == SUMMER:
        c.paint(rim, hx('#d8902a'))
        c.paint(m_ellipse(x - 3, y - 15.5, 4, 1), hx('#ffe8a0'))
        c.px(x + 2, y - 15, WHITE)
        c.px(x - 4, y - 16, WHITE)
        c.paint(m_polyline([(x + 7, y - 15), (x + 8, y - 10), (x + 7.5, y - 6)], 2), hx('#e8a03a'))
        c.paint(m_ellipse(x + 7.5, y - 5.5, 1.8, 1.6), hx('#ffc860'))
        c.px(x + 7, y - 6, WHITE)
    else:
        c.paint(rim, mul(hx('#7a5020'), dim + 0.1))
        c.paint(m_line(x - 5, y - 15, x + 4, y - 15, 1), mul(hx('#4a2a10'), dim + 0.1))
        c.paint(m_line(x - 2, y - 16, x + 1, y - 14, 1), mul(hx('#4a2a10'), dim + 0.1))
        c.paint(m_rect(x + 6, y - 14, 2, 3), mul(hx('#6a4418'), dim + 0.1))
    # the brush stuck in it
    c.paint(m_line(x + 1, y - 15, x + 8, y - 26, 2.4), mul(p.woodr[3], dim + 0.2))
    c.paint(m_rect(x - 1, y - 19, 4, 4), mul(p.metal[3], dim + 0.15))


def pole(c, p, s, dim, glass):
    """the long pole with an iron hook at its top, leant in the corner next to the bench"""
    a, b = (448, IY1 + 14), (424, IY0 + 6)
    pm = m_line(a[0], a[1], b[0], b[1], 4.4) & glass
    c.outline(pm, hx('#1a120a'))
    c.paint(pm, mul(p.woodr[3], dim + 0.2))
    c.paint(m_line(a[0] - 1, a[1], b[0] - 1, b[1], 1) & pm, mul(p.woodr[4], dim + 0.3))
    c.paint(m_line(a[0] + 1.6, a[1], b[0] + 1.6, b[1], 1) & pm, mul(p.woodr[1], dim + 0.1))
    for k in range(3):
        y = IY0 + 20 + k * 13
        x = lerp(b[0], a[0], (y - b[1]) / (a[1] - b[1]))
        c.paint(m_rect(x - 3, y, 6, 2) & glass, mul(hx('#a8844a'), dim + 0.2))       # the twine wraps
    hook = m_polyline([(424, IY0 + 7), (420, IY0 + 4), (421, IY0 + 1), (426, IY0), (430, IY0 + 2), (430, IY0 + 6),
                       (427, IY0 + 8)], 2.2) & glass
    c.outline(hook & glass, hx('#141418'))
    c.paint(hook, mul(p.iron[3], dim + 0.3))
    c.paint(m_rect(422, IY0 + 7, 4, 3) & glass, mul(p.iron[2], dim + 0.3))
    if s == SUMMER:
        c.paint(m_line(429, 122, 437, 122, 1), hx('#fff0c0'))


def door(c, p, s, st):
    """the door: locked (boards, the white X, the hasp with a padlock) or open (out to the left: the inside shows)"""
    lx, ly = C.ldir(p)
    dm = m_rect(DX0, DY0, DX1 - DX0, GROUND - DY0)
    frame = m_rect(DX0 - 5, DY0 - 5, DX1 - DX0 + 10, GROUND - DY0 + 5) - dm
    c.outline(m_rect(DX0 - 5, DY0 - 5, DX1 - DX0 + 10, GROUND - DY0 + 5), mul(p.shedD, 0.5))
    c.paint(frame, p.trim)
    c.paint(m_rect(DX0 - 5, DY0 - 5, DX1 - DX0 + 10, 1), mul(p.trim, 0.8))
    if not st.get('door', 0):
        dc = [mul(p.shedD, 0.55), mul(p.shedD, 0.75), p.shedD, mix(p.shedD, p.shed, 0.6), p.shed]
        C.boards(c, p, DX0, DY0, DX1, GROUND, 13, dc, 821, nails=False, knots=False)
        # the X and the rails (white), as on the valley shed
        for (a, b) in [((DX0 + 4, DY0 + 6), (DX1 - 4, GROUND - 6)), ((DX1 - 4, DY0 + 6), (DX0 + 4, GROUND - 6))]:
            xm = m_line(a[0], a[1], b[0], b[1], 6)
            c.outline(xm & dm, dc[0])
            c.paint(xm & dm, p.trim)
            c.paint(m_line(a[0] + 1, a[1] - 2, b[0] + 1, b[1] - 2, 1) & xm & dm, WHITE)
        for y in (DY0 + 2, GROUND - 7):
            c.paint(m_rect(DX0, y, DX1 - DX0, 5), p.trim)
            c.paint(m_rect(DX0, y + 4, DX1 - DX0, 1), mul(p.trim, 0.75))
        # the hinges, the hasp and the padlock
        for y in (DY0 + 12, GROUND - 18):
            c.paint(m_rect(DX0 - 3, y, 20, 4), p.iron[1])
            c.paint(m_rect(DX0 - 3, y, 20, 1), p.iron[3])
        c.paint(m_rect(DX1 - 14, 114, 20, 6), p.iron[1])
        c.paint(m_rect(DX1 - 14, 114, 20, 1), p.iron[3])
        c.paint(m_ellipse(DX1 + 3, 117, 2, 3), p.iron[0])
        shack = m_ellipse(DX1 + 3, 122, 5, 6) - m_ellipse(DX1 + 3, 122, 3, 4.4) - m_rect(DX1 - 4, 123, 14, 10)
        c.paint(shack, p.metal[3])
        lock = m_rect(DX1 - 4, 123, 14, 13)
        c.outline(lock, hx('#141418'))
        c.shade_dir(lock, [hx('#6a4a10'), hx('#a8781c'), hx('#d8a838'), hx('#ffe080')], lx or 1, 0, DX1 - 4, 0, DX1 + 10, 1)
        c.paint(m_disc(DX1 + 3, 128, 1.6), hx('#141418'))
        c.paint(m_rect(DX1 + 2.4, 129, 1.4, 3), hx('#141418'))
        if s == WINTER:
            c.paint(m_rect(DX1 - 4, 122, 14, 1.4), WHITE)
            drift = m_poly([(DX0 - 5, GROUND + 1)] + [(x, GROUND - 7 + 3 * math.sin(x * 0.09)) for x in range(DX0 - 5, DX1 + 7, 4)]
                           + [(DX1 + 6, GROUND + 1)])
            c.paint_values(drift, p.snow, lambda x, y: np.clip(0.9 - (y - GROUND + 9) * 0.05, 0, 1), 0.35)
            c.paint(drift.top(1), WHITE)
        return
    # open: the inside of the shed through the doorway
    c.vgrad(dm, [hx('#120c08'), hx('#1e140c'), hx('#2c2014')], DY0, GROUND, 0.3)
    inner = [hx('#1a120a'), hx('#2a1e12'), hx('#3e2c1a'), hx('#56402a')]
    C.boards(c, p, DX0, DY0, DX1, 148, 12, inner, 831, clip=dm, nails=False, knots=False)
    # the floor in depth
    fl = m_poly([(DX0, GROUND), (DX0 + 10, 148), (DX1 - 10, 148), (DX1, GROUND)])
    c.paint(fl, hx('#3a2a1a'))
    for k in range(6):
        f = k / 5
        c.paint(m_line(lerp(DX0 + 10, DX1 - 10, f), 148, lerp(DX0, DX1, f), GROUND, 1), hx('#22180e'))
    # the light from the door on the floor
    c.paint(m_poly([(DX0 + 6, GROUND), (DX0 + 14, 156), (DX1 - 20, 156), (DX1 - 6, GROUND)]), mix(hx('#3a2a1a'), p.warm, 0.3))
    # a shelf with paint tins, a rake and a spade, a coil of rope, a bucket
    c.paint(m_rect(DX0 + 4, 96, 46, 3), inner[3])
    for k, col in enumerate([p.paint_blue[1], p.paint_yel[1], p.paint_white[1]]):
        c.paint(m_rect(DX0 + 8 + k * 13, 87, 10, 9), mul(col, 0.7))
        c.paint(m_rect(DX0 + 8 + k * 13, 87, 10, 1), mul(p.metal[3], 0.6))
    c.paint(m_line(DX1 - 14, 82, DX1 - 20, 150, 2), hx('#6a4a2a'))
    for k in range(6):
        c.paint(m_line(DX1 - 20 + k * 2.4, 78 + k * 0.2, DX1 - 18 + k * 2.4, 86, 1), hx('#5a5a62'))
    c.paint(m_rect(DX1 - 24, 78, 14, 2), hx('#5a5a62'))
    c.paint(m_line(DX1 - 30, 98, DX1 - 34, 152, 2), hx('#6a4a2a'))
    c.paint(m_poly([(DX1 - 38, 146), (DX1 - 30, 146), (DX1 - 31, 158), (DX1 - 36, 160)]), hx('#6a6a72'))
    rp = m_ellipse(DX0 + 30, 116, 9, 8) - m_ellipse(DX0 + 30, 116, 5, 4)
    c.paint(rp, hx('#8a7a4a'))
    c.paint(m_ellipse(DX0 + 30, 116, 9, 8).side(lx or 1, -1, 1) & rp, hx('#b8a46a'))
    bk = m_poly([(DX0 + 14, 140), (DX0 + 32, 140), (DX0 + 30, 158), (DX0 + 16, 158)])
    c.paint(bk, hx('#5a6a72'))
    c.paint(m_rect(DX0 + 14, 140, 18, 1), hx('#8a9aa2'))
    if s in (AUTUMN, WINTER):
        c.paint_dither(m_rect(DX1 - 30, DY0, 30, GROUND - DY0) & dm, hx('#5a3a1e'), 0.25)
    # the door panel, open toward us and to the left: its inner side (plain boards, a Z brace)
    panel = m_poly([(DX0 - 3, DY0), (DX0 - 48, DY0 - 8), (DX0 - 48, GROUND + 8), (DX0 - 3, GROUND)])
    c.darken(m_poly([(DX0 - 3, GROUND), (DX0 - 48, GROUND + 8), (DX0 - 48 + p.slant * 10, GROUND + 11), (DX0, GROUND + 2)]),
             0.7, p.cool)
    c.outline(panel, hx('#2a1a10'))
    pc = [mul(p.woodD, 0.7), p.woodD, p.wood, mix(p.wood, p.warm, 0.2)]
    c.paint(panel, pc[2])
    for k in range(1, 4):
        f = k / 4
        c.paint(m_line(lerp(DX0 - 3, DX0 - 48, f), lerp(DY0, DY0 - 8, f), lerp(DX0 - 3, DX0 - 48, f), lerp(GROUND, GROUND + 8, f), 1),
                pc[0])
    for (ya, yb) in [(DY0 + 6, DY0 - 2), (GROUND - 10, GROUND - 3)]:
        c.paint(m_line(DX0 - 5, ya, DX0 - 46, yb, 4) & panel, pc[3])
    c.paint(m_line(DX0 - 6, GROUND - 12, DX0 - 45, DY0 + 2, 4) & panel, pc[3])
    c.paint(panel.side(-1, 0, 2), pc[1])
    # the open padlock hangs on the staple of the frame
    c.paint(m_rect(DX1 + 1, 114, 6, 6), p.iron[1])
    shack = m_ellipse(DX1 + 4, 124, 5, 6) - m_ellipse(DX1 + 4, 124, 3, 4.4) - m_rect(DX1 - 2, 125, 6, 10)
    c.paint(shack, p.metal[3])
    lock = m_rect(DX1 + 2, 128, 14, 13)
    c.outline(lock, hx('#141418'))
    c.shade_dir(lock, [hx('#6a4a10'), hx('#a8781c'), hx('#d8a838'), hx('#ffe080')], lx or 1, 0, DX1 + 2, 0, DX1 + 16, 1)
    c.paint(m_disc(DX1 + 9, 133, 1.6), hx('#141418'))
