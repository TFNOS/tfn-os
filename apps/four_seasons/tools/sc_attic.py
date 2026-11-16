# FOUR SEASONS: THE ATTIC (a close view): inside the attic of the farmhouse, under the roof. We look at the inner side
# of the gable with its small window; the rafters and the roof boards go down to the left and to the right; old things
# stand on the floor boards. The old trunk stands in the dark corner on the left. The light of the season comes in by
# the window: winter: the low sun sends a long beam through the window onto the trunk (the trunk is clear); autumn: the
# beam falls on the floor before the trunk; summer: the high sun makes a small patch under the window; spring: the sun
# is on the other side (only the soft light of the window). In all the seasons but winter the corner is dark and the
# trunk hardly shows. trunk 1: the lid stands open and the trunk is empty (the paper is taken).
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER, ramp_of
import fsparts as P
import fsparts_fahm as F

W, H = 672, 180
VARS = {'trunk': [0, 1]}
HOT = {'trunk': (138, 84, 168, 84), 'window': (364, 28, 88, 78)}

WHITE = (255, 255, 255)
VP = (404, 84)                        # the vanishing point (the eye)
GL, GR = 254, 554                     # the gable wall: left and right at the floor
GF, GK = 140, 118                     # the gable wall: the floor line and the top of its knee walls
PEAK = (404, -40)
WIN = (380, 34, 48, 50)               # the window in the gable (x, y, w, h)
TRK = (150, 166, 126, 44, 0.9)        # the trunk: front left x, the floor y, width, height of the box, depth ratio


def persp(pt, s):
    """the point pt of the far wall brought nearer by the scale s (s > 1: nearer to the eye)"""
    return (VP[0] + (pt[0] - VP[0]) * s, VP[1] + (pt[1] - VP[1]) * s)


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    roof(c, p, s)
    gable(c, p, s)
    floor(c, p, s)
    window(c, p, s)
    beams(c, p, s)
    things(c, p, s)
    cobwebs(c, p, s)
    trunk(c, p, s, st)
    light(c, p, s)
    season_things(c, p, s)
    return c


# ---------------------------------------------------------------- the room
def wood(p, base):
    return F.wood_ramp(p, hx(base))


def roof(c, p, s):
    """the two slopes of the roof (the lower side of the roof boards between the rafters) and the knee walls"""
    bw = wood(p, '#8a6a4c')
    for side in (-1, 1):
        g = GL if side < 0 else GR
        far_lo, far_hi = (g, GK), PEAK
        # the slope: from the far edge out to the edge of the picture
        n0 = persp(far_lo, 4.0)
        n1 = persp(far_hi, 4.0)
        sm = m_poly([far_lo, far_hi, n1, n0])
        c.paint(sm, bw[2])
        # the boards of the roof go along the slope (lines from the low edge to the ridge, toward the eye)
        for k in range(1, 26):
            f = k / 26
            a = (lerp(far_lo[0], far_hi[0], f), lerp(far_lo[1], far_hi[1], f))
            b = persp(a, 4.0)
            c.paint(m_line(a[0], a[1], b[0], b[1], 1) & sm, bw[1])
            if k % 3 == 0:
                c.paint(m_line(a[0] + side, a[1] + 1, b[0] + side * 4, b[1] + 4, 1) & sm, bw[3])
        # small holes of light between the tiles
        rng = RNG(17 + side)
        for k in range(14):
            f = rng.uni(0.05, 0.9)
            ss = rng.uni(1.05, 2.4)
            pt = persp((lerp(far_lo[0], far_hi[0], f), lerp(far_lo[1], far_hi[1], f)), ss)
            c.px(pt[0], pt[1], mix(p.win, WHITE, 0.4))
        # the knee wall (short boards)
        kw = m_poly([(g, GF), (g, GK), persp((g, GK), 4.0), persp((g, GF), 4.0)])
        c.paint(kw, mul(bw[2], 0.92))
        for k in range(1, 30):
            ss = 1 + k * 0.1
            a = persp((g, GK), ss)
            b = persp((g, GF), ss)
            c.paint(m_line(a[0], a[1], b[0], b[1], 1) & kw, bw[1])


def gable(c, p, s):
    """the inner side of the gable: vertical boards, the knee walls, the window frame"""
    gw = m_poly([(GL, GF), (GL, GK), PEAK, (GR, GK), (GR, GF)])
    cols = wood(p, '#8a6a4c')
    F.boards(c, p, gw, (GL, GF), (GL, PEAK[1]), (GR, GF), 16, cols, seed=5, knots=2, nails=True, tones=False)


def floor(c, p, s):
    """the floor boards: they go to the eye (lines from the vanishing point), the joints across"""
    fl = m_poly([persp((GL, GF), 5.0), (GL, GF), (GR, GF), persp((GR, GF), 5.0)])
    cols = wood(p, '#7a5a3e')
    c.paint(fl, cols[2])
    n = 22
    for k in range(n + 1):
        x = lerp(GL, GR, k / n)
        b = persp((x, GF), 5.0)
        c.paint(m_line(x, GF, b[0], b[1], 1) & fl, cols[1])
        c.paint(m_line(x + 1, GF, b[0] + 5, b[1], 1) & fl, cols[3] if k % 2 else cols[2])
    # the joints of the boards (across, at random depths) and nail heads
    rng = RNG(3)
    for k in range(n):
        x0, x1 = lerp(GL, GR, k / n), lerp(GL, GR, (k + 1) / n)
        ss = rng.uni(1.1, 1.6)
        for j in range(2):
            a, b = persp((x0, GF), ss), persp((x1, GF), ss)
            c.paint(m_line(a[0], a[1], b[0], b[1], 1), cols[0])
            ss *= rng.uni(1.25, 1.5)
    c.paint(m_line(GL, GF, GR, GF, 1), cols[0])
    # the grain of the floor (a few long dark lines)
    for k in range(40):
        x = rng.uni(GL, GR)
        s0, s1 = rng.uni(1.05, 2.0), rng.uni(0.15, 0.6)
        a, b = persp((x, GF), s0), persp((x, GF), s0 + s1)
        c.paint(m_line(a[0], a[1], b[0], b[1], 1), cols[1])


def beams(c, p, s):
    """the rafters (thick beams on the slopes), the collar beam across, the king post, a purlin"""
    bw = wood(p, '#5e4430')
    for side in (-1, 1):
        g = GL if side < 0 else GR
        for ss in (1.0, 1.32, 1.8, 2.6):
            a = persp((g, GK), ss)
            b = persp(PEAK, ss)
            w = 4.5 * ss
            m = m_line(a[0], a[1], b[0], b[1], w)
            c.paint(m, bw[2])
            c.paint(m.side(-side, 0, max(1, int(w * 0.3))), bw[1])
            c.paint(m.side(side, 0, 1), bw[3])
            # its foot on the knee wall (a short post)
            f0 = persp((g, GF), ss)
            pm = m_rect(a[0] - w * 0.4, a[1], w * 0.8, f0[1] - a[1])
            c.paint(pm, bw[2])
            c.paint(m_rect(a[0] - w * 0.4, a[1], 1, f0[1] - a[1]), bw[3] if side > 0 else bw[1])
    # the collar beam across the room (near) and the king post up to the ridge
    ss = 1.32
    ya = 22
    la = persp((lerp(GL, PEAK[0], 0.62), lerp(GK, PEAK[1], 0.62)), ss)
    ra = persp((lerp(GR, PEAK[0], 0.62), lerp(GK, PEAK[1], 0.62)), ss)
    cm = m_rect(la[0], la[1] - 3, ra[0] - la[0], 9)
    c.paint(cm, bw[2])
    c.paint(m_rect(la[0], la[1] - 3, ra[0] - la[0], 1), bw[4])
    c.paint(m_rect(la[0], la[1] + 5, ra[0] - la[0], 1), bw[0])
    for k in range(6):
        x = lerp(la[0], ra[0], (k + 0.5) / 6)
        c.paint(m_line(x, la[1] - 1, x + 12, la[1], 1), bw[1])
    kp = m_rect(VP[0] - 4, -2, 8, la[1] - 1)
    c.paint(kp, bw[2])
    c.paint(m_rect(VP[0] - 4, -2, 2, la[1] - 1), bw[3])
    c.paint(m_rect(VP[0] + 3, -2, 1, la[1] - 1), bw[0])
    # (pegs in the joints)
    for x in (la[0] + 6, ra[0] - 8, VP[0]):
        c.paint(m_disc(x, la[1] + 1, 1.6), bw[4])


def window(c, p, s):
    """the small window of the gable: what we see out of it (the sky and a branch of the season), the frame"""
    x, y, w, h = WIN
    gm = m_rect(x, y, w, h)
    sky = [p.skyT, mix(p.skyT, p.skyB, 0.5), p.skyB, p.hz]
    c.vgrad(gm, sky, y - 10, y + h + 20, 0.3)
    if s == WINTER:
        # the low sun of winter shines in by the window
        c.rings(x + w * 0.66, y + h * 0.62, 8, p.sunG, levels=2, k=0.25, mask=gm)
        c.paint(m_disc(x + w * 0.66, y + h * 0.62, 6) & gm, p.sun)
        c.paint(m_disc(x + w * 0.66 - 1, y + h * 0.62 - 1, 2.6) & gm, WHITE)
        c.paint(m_rect(x, y + h - 9, w, 9), mix(p.snow[2], p.hz, 0.3))
    else:
        c.paint(m_poly([(x, y + h - 8), (x + 16, y + h - 13), (x + 30, y + h - 10), (x + w, y + h - 14), (x + w, y + h), (x, y + h)]), mix(p.ridge, p.hz, 0.3))
    # a branch of the apple tree outside: blossom, leaves, red leaves, bare with snow
    br = m_polyline([(x + w + 4, y + 12), (x + w - 14, y + 18), (x + 8, y + 28), (x - 4, y + 26)], 2.0, 1.2) & gm
    c.paint(br, mix(p.bark[1], p.hz, 0.2))
    c.paint(m_line(x + w - 14, y + 18, x + w - 22, y + 8, 1) & gm, mix(p.bark[1], p.hz, 0.2))
    rng = RNG(9)
    if s != WINTER:
        cols = {SPRING: [p.blossom[1], p.blossom[2], WHITE, p.lf], SUMMER: [p.lfD, p.lf, p.lfL, p.lf],
                AUTUMN: [hx('#c9351f'), hx('#e8582a'), hx('#ffb23d'), hx('#9a2a18')]}[s]
        for k in range(26):
            t = rng()
            bx = lerp(x + w + 2, x, t) + rng.uni(-3, 3)
            by = lerp(y + 12, y + 28, t) + rng.uni(-6, 5)
            c.paint(m_ellipse(bx, by, 3.0, 2.0, rot=rng.uni(-1, 1)) & gm, rng.pick(cols))
        if s == SUMMER:
            c.paint(m_disc(x + 20, y + 32, 2.6) & gm, hx('#9acb3a'))
    else:
        c.paint(m_polyline([(x + w + 4, y + 10), (x + w - 14, y + 16), (x + 8, y + 26)], 1.0) & gm, WHITE)
    # the frame (old paint), the cross bars, the sill
    fr = m_rect(x - 5, y - 5, w + 10, h + 10) - gm
    paintc = p.paint_white
    c.paint(fr, paintc[2])
    c.paint(m_rect(x - 5, y - 5, w + 10, 1), paintc[3])
    c.paint(m_rect(x - 5, y + h + 4, w + 10, 1), paintc[0])
    F.patches(c, fr, wood(p, '#8a6a4c')[2], 3, 0.42, 7, 0.0)
    c.paint(m_rect(x + w // 2 - 1, y, 3, h), paintc[2])
    c.paint(m_rect(x, y + h // 2 - 1, w, 3), paintc[2])
    c.paint(m_rect(x + w // 2 - 1, y, 1, h), paintc[3])
    c.paint(m_rect(x - 9, y + h + 5, w + 18, 4), wood(p, '#8a6a4c')[3])
    c.paint(m_rect(x - 9, y + h + 9, w + 18, 1), wood(p, '#8a6a4c')[0])
    # the glass: a light streak
    for (gx, gy) in ((x + 3, y + 3), (x + w // 2 + 3, y + 3), (x + 3, y + h // 2 + 3), (x + w // 2 + 3, y + h // 2 + 3)):
        c.paint(m_line(gx, gy + 8, gx + 8, gy, 1), mix(p.hz, WHITE, 0.5))
    if s == WINTER:
        # frost in the corners of the panes
        for (cx, cy, dx, dy) in ((x, y, 1, 1), (x + w, y, -1, 1), (x, y + h, 1, -1), (x + w, y + h, -1, -1)):
            c.paint(m_poly([(cx, cy), (cx + dx * 12, cy), (cx, cy + dy * 10)]) & gm, mix(WHITE, p.ice[2], 0.4))
            c.paint(m_poly([(cx, cy), (cx + dx * 6, cy), (cx, cy + dy * 5)]) & gm, WHITE)


def things(c, p, s):
    """the old things of the attic (the right and the middle): boxes, books, a rocking chair, a sled, a lamp, a
    bird cage on the beam, a dress form, herbs that dry on the beam"""
    rng = RNG(21)
    bw = wood(p, '#7a5a3e')
    card = ramp_of(hx('#b89a6a'), s, 4, -0.6, 0.35)
    # an old painting of the valley in a gilt frame leans on the gable wall (right)
    fx0, fy0, fx1, fy1 = 488, 84, 540, 140
    gold = ramp_of(hx('#b8902e'), s, 4, -0.6, 0.55)
    fr = m_poly([(fx0 + 6, fy0), (fx1 + 6, fy0 + 2), (fx1, fy1 + 2), (fx0, fy1)])
    c.paint(fr, gold[1])
    inner = m_poly([(fx0 + 12, fy0 + 6), (fx1, fy0 + 8), (fx1 - 6, fy1 - 4), (fx0 + 6, fy1 - 6)])
    c.paint(fr.top(1) | fr.side(-1, 0, 1), gold[3])
    c.paint(inner.grow(1) - inner, gold[0])
    c.vgrad(inner, [hx('#5a8ac0'), hx('#9ac8e0'), hx('#e8e0c0')], fy0 + 6, fy0 + 34, 0.3)
    hill = m_poly([(fx0 + 4, fy1 - 4), (fx0 + 10, fy0 + 30), (fx0 + 24, fy0 + 24), (fx1 - 4, fy0 + 36), (fx1, fy1)]) & inner
    c.paint(hill, hx('#5a9a4a'))
    c.paint(m_line(fx0 + 22, fy0 + 26, fx0 + 22, fy0 + 18, 1) & inner, hx('#4a3020'))
    c.paint(m_disc(fx0 + 22, fy0 + 16, 4) & inner, hx('#3a7a3a'))
    c.paint(m_rect(fx0 + 36, fy0 + 36, 6, 5) & inner, hx('#b8433a'))
    c.paint(m_disc(fx1 - 12, fy0 + 14, 2.4) & inner, hx('#fff0c0'))
    # a stack of card boxes and books on the right
    bx0 = 566
    for k, (w, h, d) in enumerate(((70, 30, 0), (58, 24, 6), (46, 20, 2))):
        yb = 168 - sum(hh for _, hh, _ in ((70, 30, 0), (58, 24, 6), (46, 20, 2))[:k])
        fm = m_rect(bx0 + d, yb - h, w, h)
        c.paint(fm, card[2] if k != 1 else card[1])
        c.paint(m_rect(bx0 + d, yb - h, w, 2), card[3])
        c.paint(m_rect(bx0 + d + w * 0.4, yb - h, 6, h), mix(card[1], hx('#d8c8a8'), 0.4))   # the tape
        top = m_poly([(bx0 + d, yb - h), (bx0 + d + w, yb - h), persp((bx0 + d + w, yb - h), 0.92), persp((bx0 + d, yb - h), 0.92)])
        c.paint(top, card[3])
    # books on top of the boxes
    for k in range(4):
        c.paint(m_rect(bx0 + 6, 168 - 74 - (k + 1) * 5, 34 - k * 3, 5), [hx('#7a2a24'), hx('#2a4a6a'), hx('#4a5a2a'), hx('#8a6a2a')][k])
        c.paint(m_rect(bx0 + 6, 168 - 74 - (k + 1) * 5, 34 - k * 3, 1), hx('#e8dcc0'))
    # the rocking chair (in the middle, in front of the gable)
    cx, cy = 446, 150
    ch = wood(p, '#5e4430')
    c.paint(m_polyline(bezier((cx - 30, cy + 2), (cx - 4, cy + 8), (cx + 30, cy + 2), None, 10), 2.0), ch[1])   # the rocker
    for lx in (cx - 18, cx + 16):
        c.paint(m_line(lx, cy + 4, lx, cy - 20, 2), ch[2])
    c.paint(m_rect(cx - 22, cy - 22, 42, 4), ch[3])                                          # the seat
    for k in range(5):
        c.paint(m_line(cx - 16 + k * 7, cy - 22, cx - 12 + k * 7, cy - 66, 2), ch[2])         # the spokes
    c.paint(m_polyline(bezier((cx - 20, cy - 64), (cx + 4, cy - 74), (cx + 24, cy - 64), None, 8), 4.0), ch[2])
    c.paint(m_line(cx - 26, cy - 40, cx - 18, cy - 22, 2), ch[1])
    c.paint(m_line(cx + 26, cy - 40, cx + 18, cy - 22, 2), ch[1])
    c.paint(m_line(cx - 30, cy - 42, cx - 14, cy - 42, 2), ch[3])
    c.paint(m_line(cx + 14, cy - 42, cx + 30, cy - 42, 2), ch[3])
    # the dress form (middle left, before the gable)
    dx, dy = 330, 146
    c.paint(m_line(dx, dy, dx, dy - 30, 2), p.iron[1])
    c.paint(m_line(dx - 10, dy, dx + 10, dy, 2), p.iron[1])
    torso = m_poly([(dx - 12, dy - 30), (dx - 15, dy - 50), (dx - 10, dy - 66), (dx - 14, dy - 74), (dx + 14, dy - 74),
                    (dx + 10, dy - 66), (dx + 15, dy - 50), (dx + 12, dy - 30)])
    linen = ramp_of(hx('#c8b8a0'), s, 4, -0.6, 0.35)
    c.shade_sphere(torso, dx, dy - 52, 16, 26, linen, p.L, amb=0.35)
    c.paint(m_rect(dx - 3, dy - 80, 6, 6), p.iron[2])
    c.paint(m_line(dx - 12, dy - 56, dx + 12, dy - 56, 1), linen[0])
    # the bird cage that hangs from the collar beam (left of the king post)
    gx, gy = 352, 22
    c.paint(m_line(gx, gy, gx, gy + 10, 1), p.iron[1])
    cage = m_ellipse(gx, gy + 24, 10, 14) & m_rect(gx - 11, gy + 10, 22, 26)
    for k in range(-8, 9, 4):
        c.paint(m_line(gx + k, gy + 14 - (10 - abs(k)) * 0.4, gx + k, gy + 36, 1) & cage.grow(1), p.metal[2])
    c.paint(m_rect(gx - 11, gy + 34, 22, 3), p.metal[1])
    c.paint(m_polyline(bezier((gx - 9, gy + 18), (gx, gy + 8), (gx + 9, gy + 18), None, 6), 1), p.metal[3])
    # the herbs and the onions that dry on the collar beam (right of the king post)
    for k, hxp in enumerate((430, 446, 462, 478)):
        L = 14 + (k % 2) * 6
        c.paint(m_line(hxp, 26, hxp, 26 + L, 1), card[1])
        if k % 2 == 0:
            for j in range(5):
                c.paint(m_ellipse(hxp + rng.uni(-3, 3), 26 + L + rng.uni(-2, 4), 3.0, 2.0, rot=rng.uni(-1, 1)), rng.pick([hx('#6a7a4a'), hx('#8a9a5a'), hx('#5a6a3e')]))
        else:
            for j in range(3):
                m = m_ellipse(hxp + (j - 1) * 4, 26 + L + 2 + (j % 2) * 3, 3.4, 3.8)
                c.shade_sphere(m, hxp + (j - 1) * 4, 26 + L + 2, 3.4, 3.8, ramp_of(hx('#c88a4a'), s, 4, -0.5, 0.4), p.L, amb=0.4)
    # the old oil lamp on a nail of the gable (left of the window)
    lx, ly = 344, 90
    c.paint(m_rect(lx - 1, ly - 10, 2, 4), p.iron[1])
    c.paint(m_ellipse(lx, ly + 6, 6, 5), p.metal[1])
    c.paint(m_ellipse(lx, ly - 2, 4, 6), mix(p.glass[2], WHITE, 0.3))
    c.paint(m_rect(lx - 3, ly - 9, 6, 2), p.metal[2])


def trunk(c, p, s, st):
    """the old trunk: boards covered with leather, brass corners, two straps, a lock; trunk 1: the lid open"""
    x0, fy, w, h, dr = TRK
    x1 = x0 + w
    ty = fy - h
    leather = ramp_of(hx('#6a3a22'), s, 5, -0.7, 0.45)
    brass = ramp_of(hx('#c8962e'), s, 4, -0.6, 0.6)
    slat = wood(p, '#8a6a4c')
    # the shadow on the floor
    c.darken(m_poly([(x0 - 4, fy), (x1 + 4, fy), (x1 + 24, fy + 4), (x0 + 10, fy + 6)]), 0.6, p.cool)
    # the right side of the box (it goes back to the eye's vanishing point)
    s_back = dr
    rt = (VP[0] + (x1 - VP[0]) * s_back, VP[1] + (ty - VP[1]) * s_back)
    rb = (VP[0] + (x1 - VP[0]) * s_back, VP[1] + (fy - VP[1]) * s_back)
    side = m_poly([(x1, ty), rt, rb, (x1, fy)])
    c.paint(side, leather[1])
    c.paint(m_line(x1, ty + 10, rt[0], rt[1] + 8, 2), slat[2])
    c.paint(m_line(x1, fy - 8, rb[0], rb[1] - 6, 2), slat[2])
    # the handle on the side
    hmx, hmy = (x1 + rt[0]) / 2, (ty + rt[1]) / 2 + 18
    c.paint(m_rect(hmx - 5, hmy, 10, 3), p.iron[1])
    c.paint(m_rect(hmx - 4, hmy + 3, 8, 1), p.iron[0])
    # the front of the box
    fm = m_rect(x0, ty, w, h)
    c.paint_values(fm, leather, lambda x, y: np.clip(0.55 - (y - ty) / 140.0 + (x - x0) / 600.0, 0, 1), 0.3)
    F.patches(c, fm, leather[3], 4, 0.42, 9, 0.05)        # (worn leather)
    # the wooden slats across (top and bottom) and the corner irons
    for yy in (ty + 4, fy - 8):
        sm = m_rect(x0, yy, w, 5)
        c.paint(sm, slat[2])
        c.paint(m_rect(x0, yy, w, 1), slat[4])
        c.paint(m_rect(x0, yy + 4, w, 1), slat[0])
    for (cx, cy) in ((x0, ty), (x1 - 9, ty), (x0, fy - 9), (x1 - 9, fy - 9)):
        cm = m_rect(cx, cy, 9, 9)
        c.paint(cm, brass[2])
        c.paint(m_rect(cx, cy, 9, 1), brass[3])
        c.px(cx + 4, cy + 4, brass[0])
    # the two straps
    for sx in (x0 + 26, x1 - 34):
        c.paint(m_rect(sx, ty, 8, h), leather[0])
        c.paint(m_rect(sx + 1, ty, 1, h), leather[2])
        c.paint(m_rect(sx - 1, ty + h * 0.55, 10, 7), brass[1])
        c.paint(m_rect(sx + 1, ty + h * 0.55 + 2, 6, 3), leather[0])
    if st.get('trunk', 0) == 0:
        lid_shut(c, p, s, x0, x1, ty, leather, brass, slat, rt)
    else:
        lid_open(c, p, s, x0, x1, ty, leather, brass, slat, rt)


def lid_shut(c, p, s, x0, x1, ty, leather, brass, slat, rt):
    w = x1 - x0
    # the domed lid: the front curve and its side end
    lid = m_ellipse((x0 + x1) / 2, ty + 2, w / 2 + 2, 18) & m_rect(x0 - 3, ty - 20, w + 6, 22)
    c.shade_sphere(lid, (x0 + x1) / 2, ty + 8, w / 2 + 2, 24, leather, p.L, amb=0.3)
    c.paint(lid.top(1), leather[4])
    lside = m_poly([(x1, ty - 6), (rt[0], rt[1] - 10), (rt[0], rt[1]), (x1 + 2, ty + 1)])
    c.paint(lside, leather[2])
    # the slats over the lid and the lock plate
    for k in (0.22, 0.78):
        xx = lerp(x0, x1, k)
        c.paint(m_rect(xx - 3, ty - 17, 6, 19), slat[2])
        c.paint(m_rect(xx - 3, ty - 17, 1, 19), slat[4])
    c.paint(m_rect(x0 - 2, ty - 1, w + 4, 3), brass[1])
    lp = m_rect((x0 + x1) / 2 - 7, ty - 4, 14, 14)
    c.paint(lp, brass[2])
    c.paint(m_rect((x0 + x1) / 2 - 7, ty - 4, 14, 1), brass[3])
    c.paint(m_disc((x0 + x1) / 2, ty + 2, 1.6), hx('#141010'))
    c.paint(m_rect((x0 + x1) / 2 - 0.5, ty + 3, 1, 4), hx('#141010'))
    # the corner of a sheet of paper (red) that comes out under the lid
    c.paint(m_poly([(x0 + 70, ty), (x0 + 84, ty - 1), (x0 + 80, ty + 6)]), hx('#d8342a'))
    c.paint(m_line(x0 + 70, ty, x0 + 84, ty - 1, 1), hx('#ff7a5a'))


def lid_open(c, p, s, x0, x1, ty, leather, brass, slat, rt):
    w = x1 - x0
    # the inside of the box (we look down into it): the back wall, the lining, empty
    back_y = VP[1] + (ty - VP[1]) * TRK[4]
    bl = (VP[0] + (x0 - VP[0]) * TRK[4], back_y)
    br = (rt[0], back_y)
    inner = m_poly([(x0 + 2, ty + 1), bl, br, (x1 - 2, ty + 1)])
    lining = ramp_of(hx('#8a7a5a'), s, 4, -0.6, 0.3)
    c.paint(inner, lining[0])
    c.paint(m_poly([bl, br, (br[0], back_y + 4), (bl[0], back_y + 4)]), lining[1])
    for k in range(6):
        xx = lerp(bl[0], br[0], (k + 0.5) / 6)
        c.paint(m_line(xx, back_y + 1, xx, back_y + 3, 1), lining[2])
    # the lid stands open behind: its inside with old flowered paper
    lt = ty - 48
    lidm = m_poly([(bl[0] - 4, back_y + 1), (bl[0] + 2, lt), (br[0] + 4, lt - 3), (br[0] + 2, back_y + 1)])
    c.paint(lidm, leather[1])
    paper = ramp_of(hx('#c8c4a0'), s, 4, -0.55, 0.3)
    pin = m_poly([(bl[0], back_y - 2), (bl[0] + 5, lt + 4), (br[0], lt + 1), (br[0] - 1, back_y - 2)])
    c.paint(pin, paper[2])
    c.paint(pin.top(1), paper[3])
    # (old wallpaper: rows of small roses with green leaves, a brown stain of water)
    for j in range(5):
        for k in range(9):
            fx = lerp(bl[0] + 8, br[0] - 6, (k + 0.5 * (j % 2)) / 9)
            fy = lerp(lt + 8, back_y - 6, j / 4)
            c.paint(m_rect(fx, fy, 2, 2) & pin, hx('#b0605e'))
            c.px(fx + 2, fy + 2, hx('#6a8a4a'))
    F.patches(c, pin, paper[1], 7, 0.45, 12, 0.05)
    c.paint(pin.side(1, 0, 2) | pin.side(-1, 0, 1), paper[0])
    # the two straps hang from the lid over the front of the box
    for k in (0.24, 0.72):
        sx = lerp(bl[0], br[0], k)
        c.paint(m_polyline([(sx, lt + 2), (sx - 2, back_y), (sx - 8, ty + 10)], 5.0), leather[0])
        c.paint(m_polyline([(sx + 1, lt + 2), (sx - 1, back_y), (sx - 7, ty + 10)], 1.0), leather[2])
        c.paint(m_rect(sx - 11, ty + 9, 7, 5), brass[2])
    # the lock plate on the front, open
    c.paint(m_rect(x0 - 2, ty - 1, w + 4, 3), brass[1])
    c.paint(m_rect((x0 + x1) / 2 - 6, ty + 1, 12, 8), brass[2])
    c.paint(m_rect((x0 + x1) / 2 - 1, ty + 3, 2, 3), hx('#141010'))


def light(c, p, s):
    """the light of the room: dark far from the window, a beam of the sun by the season; the corner of the trunk is
    dark (but in winter the beam lies on it)"""
    wx, wy = WIN[0] + WIN[2] / 2, WIN[1] + WIN[3] / 2
    amb = {SPRING: 0.42, SUMMER: 0.5, AUTUMN: 0.36, WINTER: 0.26}[s]
    glow = {SPRING: 0.62, SUMMER: 0.62, AUTUMN: 0.55, WINTER: 0.5}[s]
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    d2 = ((xs - wx) / 190.0) ** 2 + ((ys - wy) / 150.0) ** 2
    v = amb + glow * np.exp(-d2 * 1.4)
    # (the left corner is darker: the slope of the roof hides it)
    v -= 0.34 * np.clip((310 - xs) / 190.0, 0, 1)
    # (the corner of the trunk: the roof hides it from the window)
    v -= 0.82 * np.exp(-((xs - 214) / 130.0) ** 2 - ((ys - 128) / 70.0) ** 2)
    # the patch of the sun and the beam (a band from the window to the patch)
    beam = None
    patch = None
    x, y, w, h = WIN
    if s == WINTER:
        patch = m_poly([(160, 64), (300, 56), (302, 166), (148, 172)])
        beam = m_poly([(x + w, y), (x + w, y + h), (282, 168), (150, 172), (150, 110)])
        beam = m_poly([(x + 2, y + 2), (x + w - 2, y + 4), (282, 150), (230, 172), (150, 116)])
    elif s == AUTUMN:
        patch = m_poly([(300, 150), (360, 148), (352, 168), (284, 170)])
        beam = m_poly([(x + 2, y + 2), (x + w - 2, y + 4), (360, 148), (352, 168), (284, 170)])
    elif s == SUMMER:
        patch = m_poly([(372, GF + 2), (420, GF + 2), (426, GF + 12), (366, GF + 12)])
        beam = m_poly([(x, y + h), (x + w, y + h), (426, GF + 12), (366, GF + 12)])
    if patch is not None:
        r = c._clip(patch)
        pm = np.zeros((H, W), bool)
        sl, mm, _ = r
        pm[sl] = mm
        v = np.where(pm, np.maximum(v, 1.0), v)
        # (the air in the beam is one step lighter: the same colours, no new ones)
        r = c._clip(beam)
        bm = np.zeros((H, W), bool)
        sl, mm, _ = r
        bm[sl] = mm
        v = np.where(bm & ~pm, v + 0.26, v)
    v = np.clip(v, 0, 1)
    ks = [0.15, 0.3, 0.52, 0.78, 1.0]
    f = v * (len(ks) - 1)
    i0 = np.floor(f).astype(int)
    fr = f - i0
    t = np.clip((fr - 0.7) / 0.3, 0, 1)
    idx = np.where(c.bayer < t, np.minimum(i0 + 1, len(ks) - 1), i0)
    # (the window and the sky in it are not dark)
    win = np.zeros((H, W), bool)
    win[y:y + h, x:x + w] = True
    for j, k in enumerate(ks):
        if k >= 1.0:
            continue
        sel = (idx == j) & ~win
        if sel.any():
            c.darken(Mask(0, 0, sel), k, p.cool)
    if beam is not None:
        warm = mix(p.win, WHITE, 0.3)
        # dust in the beam
        rng = RNG(5)
        bx0, by0, bx1, by1 = beam.bbox()
        for k in range(60):
            px, py = rng.uni(bx0, bx1), rng.uni(by0, by1)
            if 0 <= px < W and 0 <= py < H and beam.m[int(py) - by0, int(px) - bx0] if (bx0 <= px < bx1 and by0 <= py < by1) else False:
                c.px(px, py, mix(warm, WHITE, 0.6))
    if patch is not None:
        # the shadow of the cross bars of the window in the patch of the sun
        x0p, y0p, x1p, y1p = patch.bbox()
        bar = m_line((x0p + x1p) / 2 + 4, y0p, (x0p + x1p) / 2 - 2, y1p, 3) | \
            m_line(x0p, (y0p + y1p) / 2 + 4, x1p, (y0p + y1p) / 2 - 3, 3)
        c.darken(bar & patch, 0.62, p.cool)


def season_things(c, p, s):
    """the small signs of the season in the attic (after the light: they are in the light of the window)"""
    x, y, w, h = WIN
    if s == SPRING:
        # petals of the blossom on the sill (the wind blew them in)
        rng = RNG(2)
        for k in range(9):
            c.paint(m_rect(x - 6 + rng.uni(0, w + 10), y + h + 4 + rng.uni(0, 2), 2, 1), p.blossom[rng.int(1, 2)])
    elif s == SUMMER:
        # a nest of wasps (grey paper) under the collar beam on the left, and a wasp
        nx, ny = 300, 46
        m = m_ellipse(nx, ny, 9, 12)
        c.shade_sphere(m, nx, ny, 9, 12, ramp_of(hx('#9a948a'), s, 4, -0.6, 0.3), p.L, amb=0.35)
        for k in range(4):
            c.paint(m_line(nx - 8, ny - 8 + k * 5, nx + 8, ny - 6 + k * 5, 1) & m, hx('#6a645a'))
        c.paint(m_disc(nx, ny + 10, 2), hx('#2a2420'))
        c.paint(m_rect(nx + 14, ny + 4, 4, 2), hx('#e8b830'))
        c.px(nx + 15, ny + 4, hx('#1a1410'))
    elif s == AUTUMN:
        # apples that dry on a paper on the floor (in the light), and onions
        rng = RNG(4)
        c.paint(m_poly([(380, 152), (440, 151), (446, 164), (372, 166)]), ramp_of(hx('#d8ccb0'), s, 4, -0.6, 0.3)[1])
        for k in range(9):
            ax, ay = 384 + (k % 5) * 12 + (k // 5) * 6, 156 + (k // 5) * 6
            m = m_disc(ax, ay, 4)
            c.shade_sphere(m, ax, ay, 4, 4, p.apple, p.L, amb=0.3)
    else:
        # snow on the outer sill, a woollen blanket on the chair, a drift of snow under a gap of the roof
        c.paint(m_rect(x - 2, y + h - 3, w + 4, 3), WHITE)
        cx, cy = 446, 150
        bl = m_poly([(cx - 22, cy - 66), (cx + 24, cy - 68), (cx + 22, cy - 44), (cx + 8, cy - 38), (cx - 6, cy - 46), (cx - 20, cy - 40)])
        c.paint(bl, mul(hx('#7a3030'), 0.62))
        for k in range(3):
            c.paint(m_line(cx - 22, cy - 62 + k * 8, cx + 24, cy - 64 + k * 8, 1) & bl, mul(hx('#b0604a'), 0.62))
            c.paint(m_line(cx - 14 + k * 14, cy - 68, cx - 12 + k * 14, cy - 38, 1) & bl, mul(hx('#b0604a'), 0.62))
        c.paint(m_poly([(600, 168), (640, 164), (668, 168)]), mix(p.snow[1], p.cool, 0.2))
        c.paint(m_line(620, 165, 650, 165, 1), p.snow[2])


def cobwebs(c, p, s):
    """cobwebs in the corners of the beams"""
    col = mix(wood(p, '#8a6a4c')[3], WHITE, 0.3)
    for d in (-1, 1):
        # the corner where the slope of the gable meets the rafter: threads go down into the corner
        cy = 64
        cx = VP[0] + d * (150 * (GK - cy) / (GK - PEAK[1]) - 150) * -1
        a0 = math.atan2(GK - PEAK[1], -d * 150 * 1.0)
        a0 = math.atan2(-(GK - PEAK[1]), d * 150)
        angs = [math.pi / 2 + d * (-0.15 + k * 0.32) for k in range(5)]
        ends = []
        for a in angs:
            ends.append((cx + math.cos(a) * 26, cy + math.sin(a) * 26))
            c.paint(m_line(cx, cy, ends[-1][0], ends[-1][1], 1), col)
        for f in (0.35, 0.65, 0.95):
            pts = [(lerp(cx, ex, f), lerp(cy, ey, f) + 1.5 * f) for ex, ey in ends]
            c.paint(m_polyline(pts, 1), col)
