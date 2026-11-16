# FOUR SEASONS: THE SHORE (a close view): the sluice gate of the pond near. The gate stands in the stone face of the
# dam: two stone pillars hold a wooden gate board; a beam on top carries the nut of the threaded rod, and a shaft goes
# to the iron hand wheel at the right. The crank handle of the wheel is lost (it is in the well). Behind the dam the
# ditch goes out at the right end of the dam to the garden. Summer: the pond is low and dry cracked mud lies in front
# of the gate (you can walk to it); spring: high meltwater; autumn: leaves on the water; winter: ice.
# sluice 1: the handle is on the wheel, the board is up, the water flows into the ditch.
import math
import numpy as np
from fsart import (Canvas, RNG, clamp, lerp, smooth, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fswater as FW

W, H = 672, 180
OX, OY = 16, 4

VARS = {'sluice': [0, 1]}
HOT = {'sluice': (274, 30, 188, 110)}

GX = 346                     # the middle of the gate
PL0, PL1 = 280, 312          # the left pillar
PR0, PR1 = 380, 412          # the right pillar
PTOP, PBOT = 38, 144         # the pillars
SILL = 136                   # the top of the stone sill (the bottom of the shut board)
BH = 96                      # the height of the board (open: its top shows above the beam)
LIFT = 28                    # how far the board goes up when open
BEAM = (272, 20, 148, 12)    # the beam on the pillars
WX, WY, WR = 438, 30, 19     # the hand wheel
LEVEL = {SPRING: 96, SUMMER: 150, AUTUMN: 116, WINTER: 114}
DAM_END = 476                # the dam goes down to the field after this x
WHITE = (255, 255, 255)


def y_dam(x):
    """the crest of the dam (the far bank of the pond); it goes down at its right end"""
    base = 64 + 1.4 * math.sin(x * 0.03 + 0.5) + 0.8 * math.sin(x * 0.09)
    if x <= DAM_END:
        return base
    return lerp(base, 104, smooth((x - DAM_END) / 110))


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    op = st.get('sluice', 0) == 1
    sun = (p.sun_xy[0] + OX, int(p.sun_xy[1] * 0.6) + 4)
    P.sky(c, p, 0, 80, sun)
    P.clouds(c, p, 4, 24, 6, 57)
    P.mountains(c, p, 56, 18, 0.009, 7)
    P.far_ridge(c, p, 62, 8, 0.013, 13, step=6, tree_h=(2, 5))
    far_land(c, p, s, op)
    dam(c, p, s)
    gate(c, p, s, op)
    front(c, p, s, op)
    mechanism(c, p, s, op)
    life(c, p, s, op)
    return c


# ---------------------------------------------------------------- the field behind the dam, the ditch, the garden
def far_land(c, p, s, op):
    pts = [(x, 64 + 1.5 * math.sin(x * 0.011 + 2)) for x in range(-4, W + 6, 4)] + [(W + 4, H + 4), (-4, H + 4)]
    fm = m_poly(pts)
    c.paint(fm, p.hill)
    c.paint_values(fm, [mix(p.hill, p.ridge, 0.35), p.hill], lambda x, y: np.clip((y - 64) / 20, 0, 1), 0.4)
    P.grass_marks(c, p, fm, 1500, 61, hmin=1, hmax=3)
    if s == WINTER:
        c.paint(fm, p.snow[2])
        c.paint_dither(fm, p.snow[1], 0.15)
    # the farmhouse far away, poplars near it
    hx0 = 528
    c.paint(m_rect(hx0, 58, 48, 9), p.plaster[2])
    c.paint(m_rect(hx0, 58, 48, 1), p.plaster[0])
    c.paint(m_poly([(hx0 - 4, 59), (hx0 + 24, 46), (hx0 + 52, 59)]), p.tile[2] if s != WINTER else WHITE)
    c.paint(m_poly([(hx0 - 4, 59), (hx0 + 24, 46), (hx0 + 26, 47), (hx0, 59)]), p.tile[3] if s != WINTER else p.snow[2])
    c.paint(m_rect(hx0 + 6, 60, 5, 4), p.glass[1])
    c.paint(m_rect(hx0 + 36, 60, 5, 4), p.glass[1])
    c.paint(m_rect(hx0 + 20, 60, 6, 7), p.woodr[1])
    c.paint(m_rect(hx0 + 38, 42, 4, 8), p.brick[1])
    if s == WINTER:
        c.paint(m_rect(hx0 + 38, 40, 4, 2), WHITE)
    for k, tx in enumerate([500, 512, 590, 602]):
        th = 18 + (k % 2) * 4
        crown = m_ellipse(tx, 62 - th * 0.5, 3.8, th * 0.55)
        c.paint(m_rect(tx, 58, 1, 8), p.bark[1])
        if s == WINTER:
            c.paint(m_line(tx, 64, tx, 62 - th, 1), p.bark[1])
            c.paint(m_line(tx - 2, 56 - th * 0.3, tx, 60 - th * 0.5, 1), p.bark[1])
            c.paint(m_line(tx + 2, 56 - th * 0.5, tx, 60 - th * 0.7, 1), p.bark[1])
            c.px(tx, 61 - th, WHITE)
        else:
            col = {SPRING: p.lf, SUMMER: p.lfD, AUTUMN: hx('#f2b43a')}[s]
            c.shade_sphere(crown, tx, 62 - th * 0.5, 3.8, th * 0.55, [mul(col, 0.7), col, mix(col, p.warm, 0.4)], p.L, amb=0.25)
    # the garden bed (rows) with its scarecrow, at the end of the ditch
    gb = m_poly([(560, 80), (652, 77), (668, 90), (572, 94)])
    c.paint(gb, p.soilr[1] if s != WINTER else p.snow[1])
    for k in range(5):
        y0 = 80 + k * 2.8
        c.paint(m_line(562 + k * 2.4, y0, 654 + k * 3, y0 - 3 + k * 0.4, 1), p.soilr[0] if s != WINTER else p.snow[0])
        if s == SPRING:
            for j in range(8):
                c.px(570 + k * 2.4 + j * 11, y0 - 1 - j * 0.3, p.lfL)
        elif s == SUMMER:
            for j in range(8):
                c.paint(m_rect(570 + k * 2.4 + j * 11, y0 - 3 - j * 0.3, 2, 3), p.lf)
        elif s == AUTUMN:
            for j in range(6):
                c.paint(m_line(572 + k * 2.4 + j * 14, y0 - j * 0.4, 573 + k * 2.4 + j * 14, y0 - 5 - j * 0.4, 1), hx('#a8873e'))
    if s == WINTER:
        c.paint(gb.top(1), WHITE)
    sx, sy = 640, 86
    c.paint(m_line(sx, sy, sx, sy - 24, 1.6), p.woodr[1])
    c.paint(m_line(sx - 11, sy - 17, sx + 11, sy - 17, 1.6), p.woodr[1])
    shirt = m_poly([(sx - 6, sy - 19), (sx + 6, sy - 19), (sx + 5, sy - 8), (sx - 5, sy - 8)])
    c.paint(shirt, hx('#4a6aa8'))
    c.paint(m_rect(sx - 6, sy - 15, 12, 1), hx('#c8352a'))
    c.paint(m_rect(sx - 11, sy - 18, 5, 3), hx('#4a6aa8'))
    c.paint(m_rect(sx + 6, sy - 18, 5, 3), hx('#4a6aa8'))
    c.paint(m_disc(sx, sy - 23, 3.2), hx('#e8c870'))
    c.px(sx - 1, sy - 24, hx('#2a2a32'))
    c.px(sx + 1, sy - 24, hx('#2a2a32'))
    c.paint(m_rect(sx - 6, sy - 27, 12, 1.4), hx('#8a5a2a'))
    c.paint(m_rect(sx - 3, sy - 30, 7, 3), hx('#8a5a2a'))
    if s == WINTER:
        c.paint(m_rect(sx - 6, sy - 29, 12, 2), WHITE)
        c.paint(m_rect(sx - 11, sy - 19, 22, 1), WHITE)
    # the ditch: it comes out from behind the end of the dam and goes to the bed (a channel with stone edges)
    dpts = bezier((430, 70), (500, 72), (530, 84), (576, 88), 26)
    dm = m_polyline(dpts, 4, 8)
    dry = {SPRING: p.soilr[1], SUMMER: mix(p.mud[3], p.path, 0.5), AUTUMN: p.soilr[1], WINTER: p.snow[1]}[s]
    c.paint(dm.grow(1), p.stone[1] if s != WINTER else p.snow[2])
    c.paint(dm, dry)
    c.paint(dm.top(2), p.soilr[0] if s != WINTER else p.snow[0])
    c.paint(dm.bottom(1), p.stone[3] if s != WINTER else WHITE)
    rng = RNG(65)
    for k in range(16):
        q = dpts[int(rng() * len(dpts))]
        c.px(q[0] + rng.uni(-1, 1), q[1] + 3 + rng.uni(0, 1), p.stone[2] if s != WINTER else WHITE)
    if s == AUTUMN:
        for k in range(18):
            q = dpts[int(rng() * len(dpts))]
            c.paint(m_rect(q[0] + rng.uni(-2, 2), q[1] + rng.uni(-1, 2), 2, 1), rng.pick([hx('#e8682a'), hx('#f2c33a'), hx('#a8541f')]))
    if op:
        if s == WINTER:
            im = m_polyline(dpts[:18], 2, 4)
            c.paint(im, p.ice[2])
            c.paint(im.top(1), p.ice[0])
        elif s == SUMMER:
            c.paint(m_polyline(dpts[:8], 1.5, 2.5), mul(dry, 0.82))
        else:
            wm = m_polyline(dpts, 3, 6.4)
            c.paint(wm, p.water)
            c.paint(wm.top(1), p.waterD)
            rng = RNG(67)
            for k in range(26):
                q = dpts[int(rng() * (len(dpts) - 1))]
                c.paint(m_rect(q[0], q[1] - 0.5, 3, 1) & wm, mix(p.water, WHITE, 0.6))
            # (the water runs into the bed)
            c.paint(m_line(576, 88, 600, 89, 1), mix(p.soilr[0], p.waterD, 0.5))
    # a plank bridge over the ditch
    bx = 516
    c.paint(m_rect(bx, 73, 12, 11), p.woodr[3])
    for k in range(3):
        c.paint(m_rect(bx, 75 + k * 3, 12, 1), p.woodr[1])
    c.paint(m_rect(bx, 84, 12, 1), p.woodr[0])
    if s == WINTER:
        c.paint(m_rect(bx, 72, 12, 2), WHITE)


# ---------------------------------------------------------------- the dam
def dam(c, p, s):
    pts = [(x, y_dam(x)) for x in range(-4, 592, 3)] + [(600, H + 4), (-4, H + 4)]
    dm = m_poly(pts)
    lit = 1 if p.L[0] > 0 else -1
    face = mix(p.hill, p.hillS, 0.55)
    c.paint(dm, face)
    P.grass_marks(c, p, dm, 900, 71, hmin=1, hmax=3, cols=(p.hill, p.hillS, p.hillL))
    # the right end of the dam goes down: its side is in the light or in the shade
    side = m_poly([(DAM_END - 6, y_dam(DAM_END - 6))] + [(x, y_dam(x)) for x in range(DAM_END, 592, 3)] + [(600, H + 4), (DAM_END + 20, H + 4)])
    c.darken(side, 0.85 if lit < 0 else 1.0, p.cool)
    # the crest: a path of earth with grass at its edges
    crest = m_polyline([(x, y_dam(x) + 1.5) for x in range(-4, 592, 4)], 3)
    if s == WINTER:
        c.paint(dm, p.snow[1])
        c.paint_dither(dm, p.snow[2], 0.5)
        c.paint(crest, p.snow[2])
        c.paint(dm.top(2), WHITE)
    else:
        c.paint(crest, p.pathr[2])
        c.paint(crest.bottom(1), p.pathr[1])
        c.paint(dm.top(1), p.hillL)
    # the stone face of the dam near the gate (riprap): rows of stones, wet and dark under the high water line
    rng = RNG(73)
    for row in range(11):
        y = 74 + row * 7.4
        x = 140 + (row % 2) * 7 + rng.uni(-3, 3)
        while x < 540:
            yy = y + rng.uni(-1, 1)
            if yy > y_dam(x) + 7 and yy < 154 and not (PL0 - 5 < x < PR1 + 5):
                f = 1 - min(abs(x - GX) / 230, 1)
                if rng() < 0.45 + 0.6 * f:
                    r = rng.uni(3.4, 5.2)
                    P.rock(c, p, x, yy, r)
                    if s in (SPRING, SUMMER) and yy > 120 and rng() < 0.5:
                        c.paint(m_rect(x - r, yy + r * 0.5, r * 2, 1), mix(p.lfD, p.stone[1], 0.4))
            x += rng.uni(9, 13)
    # the mark of the high water on the face (a dark band)
    if s != SPRING:
        c.darken(m_rect(-4, 96, 600, 3) & dm, 0.84, p.cool)
    P.flowers(c, p, dm - m_rect(130, 70, 420, 110), 60, 75)
    # bushes on the crest at the left, a fence post with a life ring
    for bx, br in [(34, 9), (78, 6), (148, 8), (452, 7)]:
        P.bush(c, p, bx, y_dam(bx) + 3, br, seed=bx + 3)
    px = 230
    c.paint(m_rect(px, y_dam(px) - 16, 3, 18), p.woodr[2])
    c.paint(m_rect(px, y_dam(px) - 16, 1, 18), p.woodr[3])
    ring = m_ellipse(px + 1, y_dam(px) - 7, 6, 6) - m_ellipse(px + 1, y_dam(px) - 7, 3.4, 3.4)
    c.paint(ring, hx('#e8e4da'))
    for a in (0.4, 2.0, 3.6, 5.2):
        c.paint(m_disc(px + 1 + math.cos(a) * 4.7, y_dam(px) - 7 + math.sin(a) * 4.7, 1.3) & ring, hx('#c8352a'))
    c.paint(ring.side(0, 1), mul(hx('#e8e4da'), 0.75))
    if s == WINTER:
        c.paint(m_rect(px - 1, y_dam(px) - 18, 5, 2), WHITE)
        c.paint(m_rect(px - 4, y_dam(px) - 13, 10, 1), WHITE)


# ---------------------------------------------------------------- the gate
def gate(c, p, s, op):
    lit = 1 if p.L[0] > 0 else -1
    # the slot of the gate (dark: the water channel goes through the dam here)
    slot = m_rect(PL1, PTOP, PR0 - PL1, PBOT - PTOP)
    c.paint(slot, hx('#1c2228'))
    c.paint(m_rect(PL1, PTOP, PR0 - PL1, 6), hx('#2a323a'))
    # the board
    by = SILL - BH - (LIFT if op else 0)
    planks = 5
    pw = (PR0 - PL1 - 2) / planks
    wood = [mul(p.woodr[1], 0.85), p.woodr[1], p.woodr[2], p.woodr[3]]
    for k in range(planks):
        x0 = PL1 + 1 + k * pw
        pm = m_rect(x0, by, pw, BH)
        c.shade_dir(pm, wood, lit, -0.3, x0, by, x0 + pw, by + BH, 0.35)
        c.paint(m_rect(x0, by, 1, BH), wood[0])
        rng = RNG(90 + k)
        for j in range(4):
            gx = x0 + 2 + rng.uni(0, pw - 4)
            gy = by + rng.uni(2, BH - 14)
            c.paint(m_line(gx, gy, gx + rng.uni(-1, 1), gy + rng.uni(6, 12), 1), wood[1])
        c.px(x0 + pw * 0.5, by + 20 + k * 7, mul(wood[0], 0.7))
    # the lower part of the board is dark and wet, with green slime at the old water line
    wet = m_rect(PL1 + 1, by + BH - 22, PR0 - PL1 - 2, 22)
    c.darken(wet, 0.72, p.cool)
    if s != WINTER:
        c.paint(m_rect(PL1 + 1, by + BH - 23, PR0 - PL1 - 2, 2), mix(p.lfD, p.woodr[1], 0.5))
        c.paint_dither(m_rect(PL1 + 1, by + BH - 21, PR0 - PL1 - 2, 3), mix(p.lfD, p.woodr[1], 0.5), 0.45)
    # two iron bands with bolts
    for yy in (by + 10, by + 40, by + BH - 30):
        band = m_rect(PL1 + 1, yy, PR0 - PL1 - 2, 4)
        c.paint(band, p.iron[1])
        c.paint(band.top(1), p.iron[3])
        c.paint(band.bottom(1), p.iron[0])
        for k in range(planks):
            c.px(PL1 + 1 + k * pw + pw * 0.5, yy + 1, p.iron[3])
            c.px(PL1 + 1 + k * pw + pw * 0.5, yy + 2, p.iron[0])
        c.paint(m_rect(PL1 + 4, yy + 1, 4, 1), hx('#8a4a24'))
        c.paint(m_rect(PR0 - 12, yy + 2, 5, 1), hx('#8a4a24'))
    # the rod from the board up (it goes behind the beam)
    rod = m_rect(GX - 2, -40, 4, by + 6 + 40)
    c.paint(rod, p.metal[1])
    c.paint(rod.side(-lit, 0), p.metal[3])
    for yy in range(-40, by, 3):
        c.px(GX - 2 + (1 if lit > 0 else 2), yy, p.metal[0])
    c.paint(m_rect(GX - 5, by - 1, 10, 6), p.iron[2])
    c.paint(m_rect(GX - 5, by - 1, 10, 1), p.iron[3])
    if op:
        # the open hole under the board: the dark channel
        hole = m_rect(PL1 + 1, SILL - LIFT, PR0 - PL1 - 2, LIFT)
        c.paint(hole, hx('#141a20'))
        c.paint(m_rect(PL1 + 1, SILL - LIFT, PR0 - PL1 - 2, 2), hx('#2a323a'))
    # the pillars of cut stones, their caps
    for (x0, x1) in ((PL0, PL1), (PR0, PR1)):
        pm = m_rect(x0, PTOP, x1 - x0, PBOT - PTOP)
        c.shade_dir(pm, p.stone[1:], lit, 0, x0, PTOP, x1, PBOT, 0.3)
        rng = RNG(x0)
        for r, yy in enumerate(range(PTOP + 2, PBOT, 11)):
            c.paint(m_rect(x0, yy, x1 - x0, 1), p.stone[0])
            jx = x0 + (9 if r % 2 else 19) + rng.uni(-2, 2)
            c.paint(m_rect(jx, yy, 1, 11), p.stone[0])
            c.paint(m_rect(x0 + 1, yy + 1, x1 - x0 - 2, 1), p.stone[3])
            if rng() < 0.4:
                c.px(x0 + rng.uni(3, x1 - x0 - 3), yy + rng.uni(3, 8), p.stone[0])
        c.paint(pm.side(lit, 0), p.stone[4])
        c.paint(pm.side(-lit, 0), p.stone[0])
        cap = m_rect(x0 - 3, PTOP - 5, x1 - x0 + 6, 6)
        c.paint(cap, p.stone[3])
        c.paint(cap.top(1), p.stone[4])
        c.paint(cap.bottom(1), p.stone[1])
        if s in (SPRING, SUMMER):
            for k in range(10):
                c.paint(m_rect(x0 + rng.uni(0, x1 - x0 - 3), rng.uni(110, 140), rng.uni(2, 4), 1), mix(p.lfD, p.stone[1], 0.35))
    # the grooves of the board in the pillars
    c.paint(m_rect(PL1 - 1, PTOP, 2, PBOT - PTOP), hx('#2a2a30'))
    c.paint(m_rect(PR0 - 1, PTOP, 2, PBOT - PTOP), hx('#2a2a30'))
    # the stone sill
    sm = m_rect(PL1, SILL, PR0 - PL1, 8)
    c.paint(sm, p.stone[2])
    c.paint(sm.top(1), p.stone[4])
    # the water gauge on the left pillar: white with marks
    gm = m_rect(PL0 + 4, 58, 7, 78)
    c.paint(gm, p.paint_white[2])
    c.paint(gm.side(-1, 0), p.paint_white[0])
    for k, yy in enumerate(range(60, 136, 4)):
        c.paint(m_rect(PL0 + 7, yy, 4 if k % 5 == 0 else 2, 1), hx('#c8352a') if k % 5 == 0 else hx('#2a2a30'))
    # a rusty ring in the right pillar
    c.paint(m_ellipse(PR1 - 9, 70, 3, 3.6) - m_ellipse(PR1 - 9, 70, 1.6, 2.2), hx('#8a4a24'))
    c.px(PR1 - 9, 66, p.iron[0])


# ---------------------------------------------------------------- the beam, the nut, the shaft and the wheel
def mechanism(c, p, s, op):
    lit = 1 if p.L[0] > 0 else -1
    bx, by, bw, bh = BEAM
    bm = m_rect(bx, by, bw, bh)
    c.paint(bm.shift(0, 2), mul(p.woodr[0], 0.8))
    c.shade_dir(bm, [p.woodr[1], p.woodr[2], p.woodr[3]], 0, -1, bx, by, bx + bw, by + bh, 0.35)
    c.paint(bm.top(1), p.woodr[4])
    rng = RNG(5)
    for k in range(6):
        gx = bx + rng.uni(4, bw - 20)
        c.paint(m_rect(gx, by + rng.uni(3, bh - 3), rng.uni(8, 18), 1), p.woodr[1])
    for ex in (bx, bx + bw - 5):
        c.paint(m_rect(ex, by, 5, bh), p.woodr[3])
        c.paint(m_rect(ex + 1, by + 3, 3, bh - 6), p.woodr[2])
    for x in (PL0 + 8, PL1 - 8, PR0 + 8, PR1 - 8):
        c.paint(m_rect(x - 1, by + bh - 4, 3, 3), p.iron[1])
        c.px(x - 1, by + bh - 4, p.iron[3])
    # the nut box on the beam (the rod goes through it)
    nm = m_rect(GX - 10, by - 9, 20, 10)
    c.paint(nm, p.iron[1])
    c.paint(nm.top(1), p.iron[3])
    c.paint(nm.side(lit, 0), p.iron[2])
    c.paint(m_rect(GX - 12, by - 1, 24, 2), p.iron[0])
    # the shaft from the nut box to the wheel, and its bearing
    c.paint(m_rect(GX + 10, WY - 2, WX - GX - 10, 4), p.iron[1])
    c.paint(m_rect(GX + 10, WY - 2, WX - GX - 10, 1), p.iron[3])
    c.paint(m_rect(WX - 18, by - 2, 8, bh + 2), p.iron[2])
    c.paint(m_rect(WX - 18, by - 2, 1, bh + 2), p.iron[3])
    # the wheel: a rim, six spokes, a hub with a square hole for the crank
    rim = m_ellipse(WX, WY, WR, WR) - m_ellipse(WX, WY, WR - 3.2, WR - 3.2)
    c.paint(rim.shift(2, 2), mul(p.cool, 0.5))
    for k in range(6):
        a = k * math.pi / 3 + 0.2
        c.paint(m_line(WX, WY, WX + math.cos(a) * (WR - 2), WY + math.sin(a) * (WR - 2), 2.2), p.iron[1])
        c.paint(m_line(WX, WY, WX + math.cos(a) * (WR - 2), WY + math.sin(a) * (WR - 2), 1), p.iron[2])
    c.shade_sphere(rim, WX, WY, WR, WR, p.iron, p.L, amb=0.25)
    c.paint(rim.side(0, 1) & m_rect(WX - WR, WY + 4, WR * 2, WR), p.iron[0])
    hub = m_disc(WX, WY, 5)
    c.paint(hub, p.iron[2])
    c.paint(hub.side(0, -1), p.iron[3])
    c.paint(m_rect(WX - 2, WY - 2, 4, 4), hx('#101014'))
    for a in (0.9, 2.6, 4.4):
        c.paint(m_rect(WX + math.cos(a) * (WR - 1.5), WY + math.sin(a) * (WR - 1.5), 2, 2), hx('#8a4a24'))
    if op:
        FW.crank(c, p, WX, WY, 0.72, 27, sc=1.1)
    if s == WINTER:
        c.paint(m_rect(bx - 1, by - 3, bw + 2, 3), WHITE)
        c.paint(m_rect(bx + 2, by - 4, 30, 1), WHITE)
        c.paint(m_rect(GX - 10, by - 12, 20, 3), WHITE)
        c.paint(m_ellipse(WX, WY - WR + 1, 7, 2), WHITE)
        for (x0, x1) in ((PL0, PL1), (PR0, PR1)):
            c.paint(m_rect(x0 - 3, PTOP - 7, x1 - x0 + 6, 2), WHITE)
        rng = RNG(9)
        for x in range(bx + 4, bx + bw - 4, 5):
            if PL1 < x < PR0 - 2 or rng() < 0.5:
                ln = rng.uni(2, 7)
                c.paint(m_poly([(x, by + bh), (x + 2, by + bh), (x + 1, by + bh + ln)]), p.ice[3])
                c.px(x, by + bh, p.ice[1])


# ---------------------------------------------------------------- the water, the mud and the ice in front
def front(c, p, s, op):
    lv = LEVEL[s]
    if s == SUMMER:
        # the low pond at the left, dry mud from the gate to the front
        mud = m_poly([(150, 136), (PL0 - 20, 132), (PR1 + 40, 132), (W + 4, 140), (W + 4, H + 4), (160, H + 4), (124, 160)])
        mud = mud | m_rect(PL0 - 6, 140, PR1 - PL0 + 12, 8)
        FW.cracked_mud(c, p, mud, 7, 132, 180, cell=(8, 22))
        c.paint(mud.top(1), p.mud[1])
        pool = m_poly([(-4, 128), (120, 126), (176, 132), (164, 146), (118, 168), (60, 184), (-4, 184)])
        FW.water(c, p, pool, 126, 180, 13, n=40, glint=False)
        c.paint(pool.top(1), mix(p.mud[1], p.waterD, 0.4))
        c.paint(pool.grow(2) - pool - FW.as_mask(~FW.full(c, mud)), p.mud[1])
        # footprints across the mud to the gate (it is dry: you can walk to it)
        for k in range(9):
            fx = 520 - k * 22 + (k % 2) * 6
            fy = 172 - k * 4.2
            if fy < 140:
                break
            c.paint(m_ellipse(fx, fy, 2.6, 1.3), p.mud[1])
            c.px(fx - 1, fy - 1, p.mud[0])
        FW.reeds(c, p, 150, 150, 6, 26, 81, spread=1.0)
        # a stranded old bucket
        c.paint(m_poly([(452, 160), (466, 160), (464, 170), (454, 170)]), p.metal[1])
        c.paint(m_rect(452, 160, 14, 1), p.metal[3])
        c.paint(m_ellipse(459, 160, 7, 1.6), p.metal[0])
        c.paint(m_rect(456, 166, 3, 2), hx('#8a4a24'))
        if op:
            c.paint(m_rect(PL1 + 6, SILL + 1, PR0 - PL1 - 12, 2), p.mud[0])
        return
    wm = m_rect(-4, lv, W + 8, H - lv + 4)
    if s == WINTER:
        FW.ice(c, p, wm, lv, H, 31, n_crack=10, n_snow=12)
        c.paint(m_rect(-4, lv, W + 8, 1), WHITE)
        rng = RNG(33)
        by = SILL - BH - (LIFT if op else 0)
        for x in range(PL1 + 3, PR0 - 2, 4):
            ln = rng.uni(3, 9)
            yy = (by + BH) if (by + BH) < lv else lv - 10
            c.paint(m_poly([(x, yy), (x + 2.4, yy), (x + 1.2, yy + ln)]), p.ice[3])
            c.px(x, yy, p.ice[1])
        for (x0, x1) in ((PL0, PL1), (PR0, PR1)):
            c.paint(m_ellipse((x0 + x1) / 2, lv, (x1 - x0) * 0.75, 4), WHITE)
            c.paint(m_ellipse((x0 + x1) / 2, lv + 2, (x1 - x0) * 0.7, 2), p.snow[2])
        if op:
            c.paint(m_rect(PL1 + 1, SILL - LIFT, PR0 - PL1 - 2, lv - (SILL - LIFT)), p.ice[1])
            c.paint(m_rect(PL1 + 1, SILL - LIFT, PR0 - PL1 - 2, 1), p.ice[3])
        return
    FW.water(c, p, wm, lv, H, 17, sun_x=p.sun_xy[0] + OX, n=160)
    c.paint(m_rect(-4, lv, W + 8, 1), mix(p.water, WHITE, 0.55))
    for (x0, x1) in ((PL0, PL1), (PR0, PR1)):
        c.paint(m_rect(x0, lv + 1, x1 - x0, 3), mix(p.stone[1], p.waterD, 0.5))
        c.paint(m_rect(x0 + 2, lv + 5, x1 - x0 - 4, 2), mix(p.stone[1], p.waterD, 0.6))
    by = SILL - BH - (LIFT if op else 0)
    if by + BH > lv:
        c.paint(m_rect(PL1 + 1, lv + 1, PR0 - PL1 - 2, 3), mix(p.woodr[1], p.waterD, 0.5))
    rng = RNG(35)
    if s == SPRING:
        # meltwater: floes of the last ice, petals
        for k in range(9):
            fx, fy = rng.uni(20, 640), rng.uni(lv + 6, 170)
            if PL0 - 20 < fx < PR1 + 20 and fy < lv + 30:
                continue
            f = (fy - lv) / (H - lv)
            fm = m_ellipse(fx, fy, 4 + 9 * f, 1.2 + 2 * f)
            c.paint(fm.shift(0, 1), p.waterD)
            c.paint(fm, p.ice[2])
            c.paint(fm.top(1), WHITE)
        FW.floaters(c, p, wm, 90, 36)
    elif s == AUTUMN:
        FW.floaters(c, p, wm, 170, 37, front=1.4)
        # a raft of leaves against the gate
        for k in range(70):
            fx = rng.uni(PL0 - 30, PR1 + 30)
            fy = lv + 1 + rng.uni(0, 1) ** 2 * 9
            if op and PL1 < fx < PR0:
                continue
            c.paint(m_rect(fx, fy, 3, 1.4), rng.pick([hx('#f2c33a'), hx('#e8682a'), hx('#c9351f'), hx('#a8541f'), hx('#ffb23d')]))
    if op:
        # the water goes into the open hole: a dip in front of the gate with white rings of foam
        cx = GX
        if SILL - LIFT < lv:
            ry0 = lv + 2
        else:
            ry0 = lv + 1
            for k in range(14):
                x = PL1 + 3 + k * 4.6
                c.paint(m_line(x, lv - 1, x + rng.uni(-1, 1), lv - 4 - rng.uni(0, 3), 1), mix(p.water, WHITE, 0.7))
        big = 1.0 if s == SPRING else 0.7
        for k in range(5):
            rx = (18 + k * 13) * big
            ry = (3 + k * 2.4) * big
            arc = m_ellipse(cx, ry0, rx, ry) - m_ellipse(cx, ry0 - 1, rx - 1.5, ry - 1)
            arc = arc - m_rect(cx - 200, 0, 400, ry0)
            col = WHITE if k < 2 else mix(p.water, WHITE, 0.55)
            c.paint(arc & m_rect(0, lv, W, H), col)
        c.paint(m_ellipse(cx, ry0 + 1, 14 * big, 2.4 * big), mul(p.waterD, 0.8))
        for k in range(10):
            a = rng.uni(0, math.pi)
            r = rng.uni(10, 60) * big
            c.paint(m_rect(cx + math.cos(a) * r * 1.4, ry0 + math.sin(a) * r * 0.25, 3, 1), WHITE)


# ---------------------------------------------------------------- the life of each season
def life(c, p, s, op):
    lv = LEVEL[s]
    if s != SUMMER:
        FW.reeds(c, p, 60, lv + 4, 8, 34, 43, spread=1.2)
        FW.reeds(c, p, 200, lv + 2, 6, 26, 45, spread=1.0)
        FW.reeds(c, p, 530, lv + 2, 7, 28, 49, spread=1.0)
    else:
        FW.reeds(c, p, 560, 136, 7, 30, 49, spread=1.0)
    FW.reeds(c, p, 620, 178, 9, 40, 47, spread=1.3, lean=-0.06)
    if s == SPRING:
        # a pair of ducks on the high water
        for (dx, dy, sc) in ((150, 120, 1.4), (176, 124, 1.3)):
            c.paint(m_rect(dx - 8 * sc, dy + 3 * sc, 16 * sc, 1), mul(p.waterD, 0.8))
            bm = m_ellipse(dx, dy, 7 * sc, 3 * sc)
            c.paint(bm, hx('#8a6a4a'))
            c.paint(bm.top(1), hx('#b08a64'))
            c.paint(m_disc(dx + 5 * sc, dy - 4 * sc, 2.2 * sc), hx('#2a7a4a') if dx < 160 else hx('#8a6a4a'))
            c.paint(m_rect(dx + 7 * sc, dy - 4 * sc, 3, 1.4), hx('#f0a020'))
            c.px(dx + 5 * sc, dy - 5 * sc, WHITE)
    elif s == SUMMER:
        # a frog on the mud at the water, dragonflies, butterflies on the dam
        fx, fy = 176, 140
        c.paint(m_ellipse(fx, fy, 4, 2.4), hx('#4a8a2a'))
        c.paint(m_rect(fx - 4, fy + 1, 2, 2), hx('#3a6a20'))
        c.paint(m_rect(fx + 3, fy + 1, 2, 2), hx('#3a6a20'))
        c.px(fx - 2, fy - 2, hx('#e8e060'))
        c.px(fx + 1, fy - 2, hx('#e8e060'))
        for (dx, dy) in ((228, 118), (500, 112)):
            c.paint(m_rect(dx, dy, 7, 1), hx('#2a6ad8'))
            c.paint(m_rect(dx + 1, dy - 1, 2, 1), hx('#cfe8ff'))
            c.paint(m_rect(dx + 4, dy - 1, 2, 1), hx('#cfe8ff'))
        for (bx, by) in ((182, 58), (470, 54)):
            c.paint(m_rect(bx, by, 2, 2), hx('#ffe14d'))
            c.paint(m_rect(bx + 3, by, 2, 2), hx('#ffe14d'))
            c.px(bx + 2, by + 1, hx('#2a2a32'))
    elif s == AUTUMN:
        # a heron on the crest of the dam, leaves in the air
        hx0, hy0 = 196, 66
        c.paint(m_line(hx0, hy0, hx0, hy0 - 8, 1), hx('#c8a040'))
        bm = m_ellipse(hx0, hy0 - 11, 6, 3.6, rot=-0.35)
        c.paint(bm, hx('#9aa2b0'))
        c.paint(bm.bottom(1), hx('#6a7280'))
        c.paint(m_polyline([(hx0 + 4, hy0 - 13), (hx0 + 5, hy0 - 18), (hx0 + 4, hy0 - 21)], 1.6), hx('#e8eaf0'))
        c.paint(m_disc(hx0 + 5, hy0 - 22, 1.8), hx('#e8eaf0'))
        c.paint(m_line(hx0 + 4, hy0 - 23, hx0 + 1, hy0 - 24, 1), hx('#2a2a32'))
        c.paint(m_line(hx0 + 6, hy0 - 22, hx0 + 12, hy0 - 21, 1), hx('#e8b030'))
        P.particles_baked(c, p, (40, 30, 640, 100), 24, 43)
    else:
        # bird tracks on the snow of the ice, a robin on the beam
        for k in range(10):
            tx, ty = 470 + k * 9, 150 + (k % 2) * 3
            c.paint(m_line(tx, ty, tx - 2, ty - 2, 1), p.snow[0])
            c.paint(m_line(tx, ty, tx + 2, ty - 2, 1), p.snow[0])
            c.paint(m_line(tx, ty, tx, ty - 3, 1), p.snow[0])
        rx, ry = 300, 15
        c.paint(m_ellipse(rx, ry, 4, 3), hx('#6a5040'))
        c.paint(m_ellipse(rx + 1, ry + 1, 2.6, 2), hx('#e8582a'))
        c.paint(m_disc(rx + 3, ry - 3, 2), hx('#6a5040'))
        c.px(rx + 5, ry - 3, hx('#e8b030'))
        c.paint(m_line(rx - 4, ry, rx - 7, ry - 1, 1), hx('#6a5040'))
        P.particles_baked(c, p, (20, 10, 650, 110), 50, 44)
