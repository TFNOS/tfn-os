# FOUR SEASONS: THE WELL (a close view): the old stone well of the farmyard, seen from above and the side. A winch
# (two posts and a roller with its rope and bucket) stands on the rim; the square end of the roller has no crank: the
# crank handle fell into the well. We look down the shaft: the far wall of stones and, deep down, the water. The light
# of the season goes into the shaft: the high summer sun lights the bottom (the handle lies there); in spring the
# meltwater is high and the handle floats near the top (its wooden grip holds it up); in autumn and winter the shaft
# is dark (winter: ice) and the handle does not show. handle 1: in the well, 0: taken.
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier, fbm2)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fswater as FW

W, H = 672, 180
OX, OY = 16, 4
WHITE = (255, 255, 255)

CX, CY = 300, 84                 # the middle of the rim
RXO, RYO = 116, 60               # the outer edge of the rim
RXI, RYI = 96, 46                # the inner edge (the shaft)
WALL_H = 24                      # the outer wall under the rim (the front)
KV = 0.8                         # a world depth of 1 px goes down this many px on the screen
DEPTH = {SPRING: 10, SUMMER: 62, AUTUMN: 54, WINTER: 58}      # the water (screen px under the rim)
ELEV = {SPRING: 52, SUMMER: 80, AUTUMN: 26, WINTER: 14}       # the sun over the horizon (degrees)
PX0, PX1 = CX - RXO + 14, CX + RXO - 14                       # the two posts of the winch
ROLL_Y = 32

VARS = {'handle': [1, 0]}
HOT = {
    'handle': (234, 64, 132, 66),     # the shaft, where the handle lies (summer) or floats (spring)
    'well': (382, 30, 92, 100),       # the winch post with the empty crank end, the bucket
}


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    ground(c, p, s)
    yard(c, p, s)
    shadow(c, p, s)
    outer_wall(c, p, s)
    rim(c, p, s)
    shaft(c, p, s, st.get('handle', 1))
    winch(c, p, s)
    on_rim(c, p, s)
    corner_branch(c, p, s)
    return c


# ---------------------------------------------------------------- the ground of the farmyard
def ground(c, p, s):
    allm = m_rect(0, 0, W, H)
    nz = np.clip(0.5 + fbm2(W, H, 46, seed=17) * 0.62, 0, 1)
    if s == WINTER:
        c.paint_values(allm, [p.snow[2], WHITE], np.clip(nz * 1.2 - 0.1, 0, 1), 0.35)
    else:
        c.paint_values(allm, [p.hillS, p.hill, p.hillL], nz, 0.35)
        P.grass_marks(c, p, allm, 5200, 121, hmin=1, hmax=3)
    # the cobbles round the well (a ring of round stones)
    ring = m_ellipse(CX, CY + 18, RXO + 52, RYO + 40) - m_ellipse(CX, CY + 10, RXO - 4, RYO - 4)
    rng = RNG(123)
    if s != WINTER:
        inner_ring = m_ellipse(CX, CY + 18, RXO + 42, RYO + 32) - m_ellipse(CX, CY + 10, RXO - 4, RYO - 4)
        c.paint(inner_ring, mix(mix(p.soilr[2], p.path, 0.4), p.hill, 0.3))
        c.paint_dither(ring - inner_ring, mix(mix(p.soilr[2], p.path, 0.4), p.hill, 0.3), 0.5)
    for k in range(380):
        a = rng.uni(0, 2 * math.pi)
        r = math.sqrt(rng.uni(0.55, 1.0))
        x = CX + math.cos(a) * (RXO + 50) * r
        y = CY + 18 + math.sin(a) * (RYO + 38) * r
        rr = rng.uni(2.6, 4.2) * (0.8 + 0.3 * (y / H))
        cm = m_ellipse(x, y, rr * 1.3, rr * 0.8)
        if s == WINTER:
            if rng() < 0.18:
                c.paint(cm.top(1), p.snow[0])
            continue
        c.paint(cm, p.stone[2] if k % 3 else p.stone[1])
        c.paint(cm.top(1), p.stone[3])
        c.paint(cm.bottom(1), p.stone[0])
    if s == WINTER:
        # a trodden path in the snow to the well, with footprints
        for k in range(14):
            fx = 560 - k * 18 + (k % 2) * 5
            fy = 176 - k * 3.2
            c.paint(m_ellipse(fx, fy, 2.8, 1.5), p.snow[0])
            c.paint(m_ellipse(fx, fy - 0.6, 2.2, 0.9), p.snow[1])


def yard(c, p, s):
    """the things of the yard round the well: a stone trough, hens, flowers, the season"""
    lit = 1 if p.L[0] > 0 else -1
    rng = RNG(131)
    # the stone trough at the upper right
    tx, ty = 540, 22
    body = m_rect(tx, ty, 96, 22)
    c.darken(m_rect(tx - p.L[0] * 10, ty + 20, 100, 8), 0.75, p.cool)
    c.paint(body, p.stone[2])
    c.paint(m_rect(tx, ty + 14, 96, 8), p.stone[1])
    c.paint(m_rect(tx, ty + 14, 96, 1), p.stone[3])
    inner = m_rect(tx + 4, ty + 3, 88, 10)
    if s == WINTER:
        c.paint(inner, p.ice[1])
        c.paint(m_rect(tx + 6, ty + 5, 30, 1), WHITE)
        c.paint(m_rect(tx - 1, ty - 2, 98, 3), WHITE)
    else:
        c.paint(inner, p.water if s != AUTUMN else mix(p.water, p.waterD, 0.4))
        c.paint(m_rect(tx + 4, ty + 3, 88, 2), mul(p.stone[0], 0.9))
        c.paint(m_rect(tx + 10, ty + 8, 18, 1), mix(p.water, WHITE, 0.5))
        if s == AUTUMN:
            for k in range(10):
                c.paint(m_rect(tx + 8 + k * 8, ty + 6 + (k % 3) * 2, 3, 1), rng.pick([hx('#e8682a'), hx('#f2c33a'), hx('#c9351f')]))
    for xx in range(tx, tx + 96, 24):
        c.paint(m_rect(xx, ty + 14, 1, 8), p.stone[0])
    # the season of the yard
    if s == SPRING:
        hen(c, p, 70, 100, False)
        for k in range(4):
            chick(c, p, 96 + k * 9, 112 + (k % 2) * 3)
        P.flowers(c, p, m_rect(20, 130, 150, 46) | m_rect(470, 70, 190, 100), 120, 133)
    elif s == SUMMER:
        hen(c, p, 80, 96, False)
        hen(c, p, 500, 96, True)
        P.flowers(c, p, m_rect(20, 130, 150, 46) | m_rect(470, 70, 190, 100), 140, 135)
    elif s == AUTUMN:
        for k in range(320):
            x, y = rng.uni(0, W), rng.uni(0, H)
            c.paint(m_rect(x, y, 3, 2), rng.pick([hx('#e8682a'), hx('#f2c33a'), hx('#c9351f'), hx('#a8541f'), hx('#ffb23d')]))
        pumpkin(c, p, 468, 150, 11)
        pumpkin(c, p, 492, 162, 8)
    else:
        P.particles_baked(c, p, (0, 0, W, H), 90, 137)
    woodpile(c, p, s, 586, 150)


def woodpile(c, p, s, x, y):
    """a pile of split logs, their cut ends to us, on two rails"""
    c.darken(m_ellipse(x - p.L[0] * 16, y + 14, 58, 7), 0.72, p.cool)
    c.paint(m_rect(x - 52, y + 10, 104, 3), p.woodr[1])
    rng = RNG(191)
    rows = [(5, 0), (4, -12), (3, -24)]
    for (n, dy) in rows:
        for k in range(n):
            lx = x + (k - (n - 1) / 2) * 21 + rng.uni(-1.5, 1.5)
            ly = y + dy
            em = m_ellipse(lx, ly, 10.5, 6.6)
            c.paint(em, p.bark[1])
            c.paint(em.side(0, -1), p.bark[3])
            fm = m_ellipse(lx, ly, 8.6, 5.0)
            c.paint(fm, hx('#d8b07a') if s != WINTER else hx('#c8b49a'))
            c.paint(m_ellipse(lx, ly, 5.4, 3.0) - m_ellipse(lx, ly, 4.2, 2.1), hx('#b08050'))
            c.paint(m_ellipse(lx, ly, 2.2, 1.2) - m_ellipse(lx, ly, 1.2, 0.5), hx('#b08050'))
            c.paint(m_line(lx - 1, ly - 4, lx + 3, ly + 4, 1), hx('#9a6a3a'))
    if s == WINTER:
        for (n, dy) in rows:
            c.paint(m_ellipse(x, y + dy - 6, n * 10.5, 2.6), WHITE)
        c.paint(m_ellipse(x, y - 31, 30, 3.6), WHITE)
        c.paint(m_ellipse(x, y - 30, 26, 2), p.snow[2])


def hen(c, p, x, y, flip):
    d = -1 if flip else 1
    c.darken(m_ellipse(x + 3, y + 6, 9, 2.4), 0.75, p.cool)
    bm = m_ellipse(x, y, 8, 5.4)
    c.paint(bm, hx('#c87a3a'))
    c.paint(bm.top(2), hx('#e8a060'))
    c.paint(m_ellipse(x - d * 3, y + 1, 4, 2.4), hx('#9a5a2a'))
    c.paint(m_poly([(x - d * 7, y - 2), (x - d * 12, y - 7), (x - d * 10, y + 1)]), hx('#6a3a1a'))
    c.paint(m_disc(x + d * 7, y - 5, 3.2), hx('#d88a4a'))
    c.paint(m_rect(x + d * 6 - 1, y - 10, 3, 2), hx('#d8242a'))
    c.px(x + d * 8, y - 6, hx('#1a1a1a'))
    c.paint(m_rect(x + d * 10 - (1 if d < 0 else 0), y - 5, 2, 1), hx('#f0b020'))
    c.px(x + d * 7, y - 2, hx('#d8242a'))
    c.paint(m_line(x - 1, y + 5, x - 1, y + 8, 1), hx('#e0a030'))
    c.paint(m_line(x + 2, y + 5, x + 2, y + 8, 1), hx('#e0a030'))


def chick(c, p, x, y):
    c.paint(m_ellipse(x, y, 3, 2.4), hx('#f4d850'))
    c.paint(m_disc(x + 2, y - 2, 1.6), hx('#f4d850'))
    c.px(x + 4, y - 2, hx('#f0a020'))
    c.px(x + 2, y - 3, hx('#1a1a1a'))


def pumpkin(c, p, x, y, r):
    m = m_ellipse(x, y, r * 1.3, r)
    c.shade_sphere(m, x, y, r * 1.3, r, [hx('#8a3a10'), hx('#c85a1a'), hx('#f08a2a'), hx('#ffc070')], p.L, amb=0.25)
    for k in (-0.5, 0, 0.5):
        c.paint(m_line(x + k * r, y - r + 1, x + k * r * 1.3, y + r - 1, 1) & m, hx('#a8481a'))
    c.paint(m_rect(x - 1, y - r - 3, 3, 4), hx('#5a6a2a'))


# ---------------------------------------------------------------- the well
def well_mask():
    return m_ellipse(CX, CY, RXO, RYO) | m_ellipse(CX, CY + 4 + WALL_H, RXO, RYO) | m_rect(CX - RXO, CY, RXO * 2, 4 + WALL_H)


def shadow(c, p, s):
    ln = {SPRING: 26, SUMMER: 8, AUTUMN: 46, WINTER: 80}[s]
    dx = -p.L[0] * ln
    sm = m_ellipse(CX + dx, CY + 4 + WALL_H + 3, RXO + 4, RYO) | m_poly([
        (CX - RXO, CY + 4 + WALL_H), (CX + RXO, CY + 4 + WALL_H), (CX + RXO + dx, CY + 4 + WALL_H + 3), (CX - RXO + dx, CY + 4 + WALL_H + 3)])
    c.darken(sm - well_mask(), 0.7, p.cool)
    # the shadows of the posts
    for px in (PX0, PX1):
        c.darken(m_line(px, CY + 4 + WALL_H - 2, px + dx * 1.3, CY + 4 + WALL_H + 30 - abs(dx) * 0.1, 4) - well_mask(), 0.72, p.cool)


def outer_wall(c, p, s):
    lit = 1 if p.L[0] > 0 else -1
    top = m_ellipse(CX, CY + 4, RXO, RYO)
    wm = (m_ellipse(CX, CY + 4 + WALL_H, RXO, RYO) | m_rect(CX - RXO, CY + 4, RXO * 2, WALL_H)) - top
    c.shade_dir(wm, [p.stone[0], p.stone[1], p.stone[2], p.stone[3]], lit * 0.9, 0.25, CX - RXO, CY, CX + RXO, CY + RYO + WALL_H + 4, 0.3)
    # the courses of stones: arcs, and joints that change place in each course
    rng = RNG(141)
    for j in range(0, WALL_H + 1, 8):
        arc = m_ellipse(CX, CY + 4 + j, RXO, RYO).bottom(1) & wm
        c.paint(arc, p.stone[0])
        c.paint(arc.shift(0, 1) & wm, p.stone[3] if j < WALL_H else p.stone[0])
    for j in range(0, WALL_H, 8):
        n = 13
        for k in range(n):
            ph = (k + (0.5 if (j // 8) % 2 else 0.0) + 0.5) / n * math.pi
            x = CX + math.cos(ph) * RXO * 0.99
            yb = CY + 4 + j + RYO * math.sin(ph)
            c.paint(m_line(x, yb + 1, x, yb + 8, 1) & wm, p.stone[0])
    # moss at the foot (spring, summer), snow drifts (winter)
    if s in (SPRING, SUMMER):
        foot = m_ellipse(CX, CY + 4 + WALL_H, RXO, RYO).bottom(3) & wm
        c.paint_dither(foot, mix(p.lfD, p.stone[1], 0.3), 0.6)
    if s == WINTER:
        drift = m_ellipse(CX, CY + 4 + WALL_H + 2, RXO + 6, RYO + 2).bottom(6) - m_ellipse(CX, CY + 4 + WALL_H - 6, RXO, RYO)
        c.paint(drift, WHITE)
        c.paint(drift.top(1), p.snow[1])
    for k in range(10):
        x = CX + rng.uni(-0.9, 0.9) * RXO
        y = CY + 10 + rng.uni(0, WALL_H) + RYO * math.sqrt(max(0, 1 - ((x - CX) / RXO) ** 2))
        c.px(x, y, p.stone[4])


def rim(c, p, s):
    """the cap stones of the rim: a ring of blocks, and the front face of the ring"""
    outer = m_ellipse(CX, CY, RXO, RYO)
    inner = m_ellipse(CX, CY, RXI, RYI)
    ring = outer - inner
    face = m_ellipse(CX, CY + 4, RXO, RYO) - outer
    c.paint(face, p.stone[1])
    c.paint(face.bottom(1), p.stone[0])
    nb = 24
    x0, y0, x1, y1 = ring.bbox()
    ys, xs = np.mgrid[y0:y1, x0:x1]
    X = xs + 0.5 - CX
    Y = ys + 0.5 - CY
    phi = np.arctan2(Y / RYO, X / RXO)
    t = (phi + math.pi) / (2 * math.pi) * nb
    blk = np.floor(t).astype(int)
    fr = t - blk
    rnd = ((blk * 7919) % 13) / 13.0
    # the light: the blocks are flat; each has its own tone; the outer edge on the side of the sun is lighter
    rn = np.sqrt((X / RXO) ** 2 + (Y / RYO) ** 2)
    lit = 1 if p.L[0] > 0 else -1
    val = 0.5 + 0.18 * (rnd - 0.5) + 0.12 * np.clip(X / RXO * lit, -1, 1) + 0.1 * (rn > 0.96)
    val = np.where((fr < 0.07) | (fr > 0.97), 0.12, val)
    c.paint_values(ring, p.stone, val, 0.3)
    # the joints on the front face, under the joints of the ring
    for k in range(nb):
        ph = -math.pi + (k / nb) * 2 * math.pi
        if math.sin(ph) <= 0.05:
            continue
        x = CX + math.cos(ph) * RXO
        y = CY + math.sin(ph) * RYO
        c.paint(m_line(x, y + 1, x, y + 4, 1) & face, p.stone[0])
    # cracks and lichen on the stones
    rng = RNG(151)
    for k in range(18):
        a = rng.uni(0, 2 * math.pi)
        r = rng.uni(0.86, 0.98)
        x = CX + math.cos(a) * RXO * r
        y = CY + math.sin(a) * RYO * r
        if s == WINTER:
            continue
        col = hx('#c8c070') if k % 3 == 0 else p.stone[0]
        c.paint(m_rect(x, y, 2 + (k % 3), 1) & ring, col)
    if s == WINTER:
        snow = ring.top(5) | (ring - m_ellipse(CX, CY - 1, RXO - 6, RYO - 4))
        c.paint(ring, p.snow[1])
        c.paint(m_ellipse(CX, CY - 1, RXO - 1, RYO - 1) - inner, p.snow[2])
        c.paint(outer.top(1), WHITE)
        c.paint(face, p.snow[1])
        c.paint(face.bottom(1), p.snow[0])
        for k in range(nb):
            ph = -math.pi + (k / nb) * 2 * math.pi
            x = CX + math.cos(ph) * (RXO + RXI) * 0.5
            y = CY + math.sin(ph) * (RYO + RYI) * 0.5
            c.px(x, y, p.snow[0])
        # icicles from the front of the rim
        for k in range(26):
            ph = 0.15 + k / 25 * (math.pi - 0.3)
            x = CX + math.cos(ph) * RXO
            y = CY + 4 + math.sin(ph) * RYO
            ln = 2 + (k * 7 % 5)
            c.paint(m_poly([(x - 1, y), (x + 1.4, y), (x, y + ln)]), p.ice[3])
    # the lip into the shaft: dark on the near side (it hangs over the shaft), light on the far side
    lipn = (inner.grow(1) - inner) & m_rect(CX - RXI - 2, CY, RXI * 2 + 4, RYI + 4)
    c.paint(lipn, p.stone[0] if s != WINTER else p.snow[0])
    lipf = (inner.grow(1) - inner) & m_rect(CX - RXI - 2, CY - RYI - 4, RXI * 2 + 4, RYI + 4)
    c.paint(lipf, p.stone[4] if s != WINTER else WHITE)


def shaft(c, p, s, handle):
    """the inside of the well: the far wall of stones and the water, with the light of the season"""
    D = DEPTH[s]
    x0, y0, x1, y1 = CX - RXI - 1, CY - RYI - 1, CX + RXI + 2, CY + RYI + 2
    ys, xs = np.mgrid[y0:y1, x0:x1]
    X = xs + 0.5
    Y = ys + 0.5
    inm = ((X - CX) / (RXI - 0.5)) ** 2 + ((Y - CY) / (RYI - 0.5)) ** 2 <= 1.0
    u = np.clip((X - CX) / RXI, -1, 1)
    vfar = -np.sqrt(1 - u * u)
    dz = Y - (CY + RYI * vfar)                  # the screen depth of the far wall seen in this pixel
    wall = inm & (dz < D)
    bot = inm & (dz >= D)
    t = 1 / math.tan(math.radians(ELEV[s]))
    ax = 0.85 if p.L[0] > 0 else -0.85
    az = 0.5
    # the far wall
    dn = dz / (KV * RXI)
    qx = u + dn * t * ax
    qz = vfar + dn * t * az
    lit = (qx * qx + qz * qz) <= 1.0
    course = dz / 7.0
    row = np.floor(course)
    fr = course - row
    ph = np.arccos(u) * 15 / math.pi + row * 0.5
    fv = ph - np.floor(ph)
    joint = (fr < 0.17) | (fv < 0.09)
    rnd = (((row.astype(int) * 31 + np.floor(ph).astype(int) * 17) % 11) / 11.0 - 0.5) * 0.1
    v_lit = 0.66 + 0.24 * (1 - np.clip(dz / 80, 0, 1)) + rnd
    v_sh = 0.42 * (1 - np.clip(dz / (D + 26), 0, 1)) ** 1.3 + rnd * 0.5
    val = np.where(lit, v_lit, v_sh)
    val = np.where(joint, val - 0.16, val)
    # (moss and wet near the water)
    wall_cols = [hx('#0a0d10'), mix(hx('#0a0d10'), p.stone[0], 0.5), p.stone[0], p.stone[1], p.stone[2], p.stone[3]]
    c.paint_values(Mask(x0, y0, wall), wall_cols, val, 0.3)
    # the lit edge of the shadow on the wall (a thin light line)
    edge = lit & ~np.roll(lit, -1, axis=0) & wall
    # the water at the bottom
    ub = (X - CX) / RXI
    vb = (Y - CY - D) / RYI
    dnb = D / (KV * RXI)
    litb = ((ub + dnb * t * ax) ** 2 + (vb + dnb * t * az) ** 2 <= 1.0) & (s in (SPRING, SUMMER))
    bm = Mask(x0, y0, bot)
    if s == SPRING:
        cols = [mul(p.waterD, 0.5), mul(p.waterD, 0.75), p.waterD, p.water, mix(p.water, WHITE, 0.4)]
        bv = np.where(litb, 0.62 + 0.25 * np.clip(-vb, 0, 1), 0.2)
    elif s == SUMMER:
        cols = [hx('#10161a'), mix(p.mud[0], p.waterD, 0.4), mix(p.mud[1], p.water, 0.4), mix(p.mud[2], p.water, 0.45), mix(p.mud[3], p.water, 0.35)]
        bv = np.where(litb, 0.55 + 0.35 * np.clip(0.5 - vb, 0, 1), 0.05)
    elif s == AUTUMN:
        cols = [hx('#06080a'), hx('#0e1418'), hx('#18222a')]
        bv = 0.15 + 0.4 * np.clip(-vb - 0.3, 0, 1)
    else:
        cols = [hx('#0a0e14'), hx('#16202c'), hx('#24324a')]
        bv = 0.2 + 0.5 * np.clip(-vb - 0.3, 0, 1)
    c.paint_values(bm, cols, bv, 0.3)
    full_bot = np.zeros((c.h, c.w), bool)
    full_bot[y0:y1, x0:x1] = bot
    botm = FW.as_mask(full_bot)
    # the line where the water meets the wall
    wl = Mask(x0, y0, bot & ~np.roll(bot, 1, axis=0))
    c.paint(wl, {SPRING: mix(p.water, WHITE, 0.5), SUMMER: mix(p.mud[3], p.water, 0.2), AUTUMN: hx('#202a32'), WINTER: hx('#2e3e56')}[s])
    rng = RNG(161)
    if s == SUMMER:
        # stones on the bottom, ripples of light (the sun in the shallow water)
        for k in range(16):
            sx = CX + rng.uni(-70, 70)
            sy = CY + D - RYI + rng.uni(8, 60)
            sm = m_ellipse(sx, sy, rng.uni(2.4, 4.6), rng.uni(1.4, 2.4)) & botm
            c.paint(sm, mix(p.stone[1], p.water, 0.35))
            c.paint(sm.top(1) & botm, mix(p.stone[3], p.water, 0.3))
        for k in range(26):
            sx = CX + rng.uni(-80, 80)
            sy = CY + D - RYI + rng.uni(6, 60)
            c.paint(m_rect(sx, sy, rng.uni(3, 8), 1) & botm, mix(p.water, WHITE, 0.55))
        if handle:
            # (it lies on the bottom; only the part in the water shows)
            keep = c.rgb.copy()
            FW.crank(c, p, CX - 24, CY + 34, -0.15, 40, sc=1.0, under=0.22)
            c.rgb[~full_bot] = keep[~full_bot]
    elif s == SPRING:
        # the high water: petals, a dead leaf, the sky in it
        for k in range(30):
            sx = CX + rng.uni(-90, 90)
            sy = CY + D - RYI + rng.uni(0, 92)
            c.paint(m_rect(sx, sy, 2, 1) & botm, p.blossom[2] if k % 3 else WHITE)
        c.paint(m_rect(CX - 60, CY - 14, 40, 1) & botm, mix(p.water, WHITE, 0.6))
        c.paint(m_rect(CX + 20, CY - 8, 30, 1) & botm, mix(p.water, WHITE, 0.6))
        if handle:
            hx0, hy0 = CX - 8, CY + 4
            # (rings on the water round the grip that floats)
            for r in (12, 18):
                c.paint((m_ellipse(hx0 + 34, hy0 - 4, r, r * 0.4) - m_ellipse(hx0 + 34, hy0 - 4, r - 1.3, r * 0.4 - 1)) & botm,
                        mix(p.water, WHITE, 0.5))
            FW.crank(c, p, hx0, hy0, -0.12, 36, sc=1.0, under=0.55, grip_under=0.0)
    elif s == AUTUMN:
        for k in range(14):
            sx = CX + rng.uni(-70, 70)
            sy = CY + D - RYI + rng.uni(2, 40)
            c.paint(m_rect(sx, sy, 3, 1) & botm, rng.pick([hx('#3a1a10'), hx('#4a3010'), hx('#2a1a10')]))
    else:
        for k in range(5):
            sx = CX + rng.uni(-60, 60)
            sy = CY + D - RYI + rng.uni(4, 30)
            c.paint(m_line(sx, sy, sx + rng.uni(8, 20), sy + rng.uni(-2, 2), 1) & botm, hx('#3a4e6a'))


def winch(c, p, s):
    """two posts on the rim, the roller with the rope, the square end of the roller (no crank: it is in the well)"""
    lit = 1 if p.L[0] > 0 else -1
    for px in (PX0, PX1):
        pm = m_rect(px - 5, ROLL_Y - 6, 10, CY - ROLL_Y + 8)
        c.shade_dir(pm, [p.woodr[0], p.woodr[1], p.woodr[2], p.woodr[3]], lit, 0, px - 5, 0, px + 5, 1, 0.3)
        c.paint(m_rect(px - 5, ROLL_Y - 6, 10, 2), p.woodr[4])
        for k in range(4):
            c.paint(m_rect(px - 3 + k * 2, ROLL_Y + 4 + k * 9, 1, 6), p.woodr[1])
        # the foot of the post on the rim, iron straps
        c.paint(m_rect(px - 7, CY + 2, 14, 4), p.woodr[1])
        c.paint(m_rect(px - 5, CY - 10, 10, 2), p.iron[1])
        c.paint(m_rect(px - 5, ROLL_Y + 6, 10, 2), p.iron[1])
    # the roller (a round log) with rope wound on its middle
    rm = m_rect(PX0 - 2, ROLL_Y - 6, PX1 - PX0 + 4, 12)
    c.paint(rm.shift(0, 3), mul(p.cool, 0.45))
    c.shade_dir(rm, [p.woodr[1], p.woodr[2], p.woodr[3], p.woodr[4]], 0, -1, 0, ROLL_Y - 6, 1, ROLL_Y + 6, 0.35)
    c.paint(rm.bottom(1), p.woodr[0])
    rope = m_rect(CX - 30, ROLL_Y - 7, 54, 14)
    c.paint(rope, p.rope[1])
    for x in range(CX - 30, CX + 24, 3):
        c.paint(m_line(x, ROLL_Y - 7, x + 2, ROLL_Y + 6, 1), p.rope[0])
        c.px(x + 1, ROLL_Y - 6, p.rope[2])
    # the axle end beyond the right post: square, with no crank on it
    ax = PX1 + 6
    c.paint(m_rect(PX1 + 5, ROLL_Y - 2, 8, 4), p.iron[1])
    sq = m_rect(ax + 6, ROLL_Y - 4, 7, 8)
    c.paint(sq, p.iron[2])
    c.paint(sq.side(0, -1), p.iron[3])
    c.paint(sq.side(1, 0), p.iron[0])
    c.paint(m_rect(ax + 8, ROLL_Y - 2, 3, 3), hx('#8a4a24'))
    # the rope from the roller down to the bucket on the rim
    bx, by = BUCKET
    c.paint(m_polyline(bezier((CX + 20, ROLL_Y + 5), (CX + 40, ROLL_Y + 40), (bx - 6, by - 30), (bx, by - 14), 12), 1.6), p.rope[1])
    c.paint(m_polyline(bezier((CX + 21, ROLL_Y + 5), (CX + 41, ROLL_Y + 40), (bx - 5, by - 30), (bx + 1, by - 14), 12), 1), p.rope[2])
    if s == WINTER:
        c.paint(m_rect(PX0 - 2, ROLL_Y - 8, PX1 - PX0 + 4, 3), WHITE)
        for px in (PX0, PX1):
            c.paint(m_rect(px - 6, ROLL_Y - 9, 12, 3), WHITE)
        c.paint(m_rect(ax + 6, ROLL_Y - 6, 7, 2), WHITE)
        for x in range(PX0 + 4, PX1 - 4, 9):
            c.paint(m_poly([(x, ROLL_Y + 6), (x + 2, ROLL_Y + 6), (x + 1, ROLL_Y + 9 + (x % 4))]), p.ice[3])


BUCKET = (CX + 96, CY + 46)


def on_rim(c, p, s):
    """the bucket on the rim, the cat (summer), petals, leaves, snow"""
    bx, by = BUCKET
    lit = 1 if p.L[0] > 0 else -1
    body = m_poly([(bx - 13, by - 12), (bx + 13, by - 12), (bx + 11, by + 10), (bx - 11, by + 10)]) | m_ellipse(bx, by + 10, 11, 3.2)
    c.darken(m_ellipse(bx - p.L[0] * 10, by + 11, 14, 4), 0.7, p.cool)
    c.shade_dir(body, [p.woodr[0], p.woodr[1], p.woodr[2], p.woodr[3]], lit, 0, bx - 13, 0, bx + 13, 1, 0.3)
    for k in range(-10, 12, 4):
        c.paint(m_line(bx + k, by - 11, bx + k * 0.85, by + 11, 1) & body, p.woodr[0])
    for yy in (by - 7, by + 5):
        c.paint(m_rect(bx - 13, yy, 26, 2) & body, p.iron[1])
    top = m_ellipse(bx, by - 12, 13, 4)
    c.paint(top, p.woodr[3])
    ins = m_ellipse(bx, by - 12, 11, 3)
    if s == WINTER:
        c.paint(ins, WHITE)
    elif s == AUTUMN:
        c.paint(ins, mul(p.woodr[0], 0.6))
        c.paint(m_rect(bx - 4, by - 13, 3, 1), hx('#e8682a'))
        c.paint(m_rect(bx + 2, by - 12, 3, 1), hx('#f2c33a'))
    else:
        c.paint(ins, p.water)
        c.paint(m_rect(bx - 6, by - 13, 6, 1), mix(p.water, WHITE, 0.5))
    # the bail (the iron handle of the bucket)
    c.paint(m_polyline(bezier((bx - 13, by - 10), (bx - 10, by - 26), (bx + 10, by - 26), (bx + 13, by - 10), 10), 1), p.iron[1])
    rng = RNG(171)
    if s == SUMMER:
        cat(c, p, CX - 96, CY + 34)
    elif s == SPRING:
        for k in range(26):
            a = rng.uni(0, 2 * math.pi)
            r = rng.uni(0.82, 1.0)
            c.paint(m_rect(CX + math.cos(a) * RXO * r, CY + math.sin(a) * RYO * r, 2, 1), p.blossom[2] if k % 2 else WHITE)
    elif s == AUTUMN:
        for k in range(30):
            a = rng.uni(0, 2 * math.pi)
            r = rng.uni(0.84, 1.0)
            c.paint(m_rect(CX + math.cos(a) * RXO * r, CY + math.sin(a) * RYO * r, 3, 2),
                    rng.pick([hx('#e8682a'), hx('#f2c33a'), hx('#c9351f'), hx('#a8541f')]))


def cat(c, p, x, y):
    """a ginger cat that sleeps on the warm rim (summer)"""
    c.darken(m_ellipse(x + 3, y + 5, 15, 3), 0.7, p.cool)
    bm = m_ellipse(x, y, 13, 6.5)
    c.shade_sphere(bm, x, y, 13, 6.5, [hx('#8a4a1a'), hx('#c8702a'), hx('#e8984a'), hx('#ffc888')], p.L, amb=0.3)
    for k in range(4):
        c.paint(m_line(x - 8 + k * 5, y - 5, x - 6 + k * 5, y + 2, 1) & bm, hx('#a85a20'))
    hm = m_disc(x + 11, y - 2, 5)
    c.shade_sphere(hm, x + 11, y - 2, 5, 5, [hx('#8a4a1a'), hx('#c8702a'), hx('#e8984a'), hx('#ffc888')], p.L, amb=0.3)
    c.paint(m_poly([(x + 7, y - 5), (x + 8, y - 10), (x + 11, y - 6)]), hx('#c8702a'))
    c.paint(m_poly([(x + 12, y - 6), (x + 15, y - 10), (x + 15, y - 4)]), hx('#c8702a'))
    c.paint(m_line(x + 9, y - 2, x + 11, y - 2, 1), hx('#3a2010'))
    c.paint(m_line(x + 13, y - 2, x + 15, y - 2, 1), hx('#3a2010'))
    c.px(x + 12, y, hx('#e86a7a'))
    c.paint(m_polyline(bezier((x - 12, y + 2), (x - 18, y + 6), (x - 8, y + 8), (x + 2, y + 6), 8), 2.2), hx('#c8702a'))


def corner_branch(c, p, s):
    """a branch of the old apple tree of the yard in the upper left corner: blossom, leaves and apples, red leaves,
    snow"""
    pts = bezier((-6, 18), (30, 10), (70, 22), (110, 8), 16)
    c.paint(m_polyline(pts, 6, 2.4), p.bark[1])
    c.paint(m_polyline([(x, y - 1.5) for (x, y) in pts], 2, 1), p.bark[2])
    twigs = [((40, 13), (52, -2)), ((70, 18), (80, 32)), ((88, 14), (100, -1)), ((20, 16), (24, 32)), ((104, 10), (126, 18))]
    for (a, b) in twigs:
        c.paint(m_line(a[0], a[1], b[0], b[1], 1.4), p.bark[1])
    rng = RNG(181)
    if s == WINTER:
        c.paint(m_polyline([(x, y - 3) for (x, y) in pts], 2.4, 1.2), WHITE)
        for (a, b) in twigs:
            c.paint(m_line(a[0], a[1] - 1, b[0], b[1] - 1, 1), WHITE)
        return
    spots = [b for (a, b) in twigs] + [pts[3], pts[7], pts[11], pts[14]]
    for (x, y) in spots:
        for k in range(5):
            lx, ly = x + rng.uni(-8, 8), y + rng.uni(-6, 6)
            if s == SPRING:
                m = m_disc(lx, ly, rng.uni(2.4, 3.6))
                c.paint(m, p.blossom[1] if k % 2 else p.blossom[2])
                c.paint(m.top(1), WHITE)
                c.px(lx, ly, p.blossom[0])
            elif s == SUMMER:
                m = m_ellipse(lx, ly, 4, 2.2, rot=rng.uni(-1, 1))
                c.paint(m, p.lf if k % 2 else p.lfD)
                c.paint(m.top(1), p.lfL)
            else:
                m = m_ellipse(lx, ly, 3.6, 2, rot=rng.uni(-1, 1))
                c.paint(m, rng.pick([hx('#e8682a'), hx('#f2c33a'), hx('#c9351f')]))
        if s == SUMMER and rng() < 0.5:
            am = m_disc(x + 2, y + 6, 3.4)
            c.shade_sphere(am, x + 2, y + 6, 3.4, 3.4, p.apple, p.L, amb=0.3)
