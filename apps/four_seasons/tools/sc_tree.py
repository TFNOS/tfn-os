# FOUR SEASONS: THE TREE (a close view): the crown of the big tree of the valley, near. The trunk comes up from the
# bottom and splits into three limbs; the long right limb is the limb of the tire swing (its rope goes down at the
# right). The lost kite of grandpa is caught in the branches at the right: spring blossom and summer leaves cover it,
# in autumn a little of it shows through the red leaves, in winter the bare branches show it clearly.
import math
import numpy as np
from fsart import Canvas, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line, m_polyline, m_union
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P
import fsparts_c as C

W, H = 672, 180
VARS = {'kite': [1, 0]}
HOT = {
    'kite': (426, 26, 88, 96),
    'trunk': (150, 112, 100, 56),
}

KX, KY = 468, 52                    # the cross of the sticks of the kite
ROPE_X = 604                        # the rope of the swing
PEEK = (442, 42)                  # the corner of the kite that shows in autumn


def xl(y):
    y = np.asarray(y, dtype=np.float64)
    return 170 - np.maximum(0, y - 110) * 0.28 - np.maximum(0, y - 158) ** 2 * 0.035


def xr(y):
    y = np.asarray(y, dtype=np.float64)
    return 232 + np.maximum(0, y - 110) * 0.22 + np.maximum(0, y - 158) ** 2 * 0.035


LIMBS = [
    ([(186, 116), (152, 82), (104, 50), (56, 22), (8, -6)], 30, 10),           # A: up to the left
    ([(206, 112), (212, 72), (222, 36), (236, -8)], 30, 13),                    # B: up
    ([(226, 120), (300, 106), (380, 98), (470, 93), (560, 90), (690, 85)], 30, 15),  # C: the swing limb
    ([(318, 104), (360, 76), (410, 48), (452, 22), (486, -8)], 14, 6),          # D: up from C
    ([(124, 62), (84, 66), (36, 60), (-10, 66)], 11, 6),                        # E: from A to the left
]

_GROWN = None


def grown():
    global _GROWN
    if _GROWN is None:
        br, tips = C.grow(LIMBS, 77, per=7, sub_len=(26, 54), sub_w=0.42, depth=2, up=0.32)
        rng = RNG(901)
        clumps = []

        def add(cx, cy, r):
            if 132 < cx < 270 and cy > 96:
                return                                       # the trunk stays clear
            if cy > 146 or (ROPE_X - 22 < cx < ROPE_X + 22 and cy > 92):
                return                                       # the low view and the rope stay clear
            th = rng()
            if math.hypot(cx - PEEK[0], cy - PEEK[1]) < 30:
                th = max(th, 0.6)                            # (in autumn these fall: the kite peeks out)
            clumps.append((cx, cy, r, th, rng(), rng() < 0.55))
        for (x, y, a) in tips:
            for k in range(2):
                add(x + rng.uni(-8, 8) + math.cos(a) * 5, y + rng.uni(-7, 5) + math.sin(a) * 5, rng.uni(11, 18))
        for (pts, w0, w1, lvl) in br:
            if lvl >= 1:
                for t in (0.45, 0.95):
                    x, y, a, w = C.along(pts, w0, w1, t)
                    add(x + rng.uni(-9, 9), y + rng.uni(-9, 4), rng.uni(10, 15))
        # the clumps that cover the kite and its tail (they show in spring and summer; in autumn some fall and a
        # little of the kite shows)
        cover = [(446, 30, 15, 0.30), (474, 24, 16, 0.30), (500, 40, 15, 0.30), (440, 54, 15, 0.70),
                 (464, 52, 13, 0.70), (494, 62, 15, 0.30), (452, 78, 16, 0.30), (480, 80, 15, 0.30),
                 (430, 76, 13, 0.70), (508, 84, 12, 0.30), (456, 108, 14, 0.70), (476, 116, 14, 0.30),
                 (452, 128, 13, 0.70), (470, 136, 12, 0.30), (488, 104, 13, 0.30), (436, 110, 12, 0.30),
                 (484, 44, 12, 0.30), (446, 40, 11, 0.70), (476, 56, 9, 0.70), (472, 40, 8, 0.70), (475, 66, 8, 0.70)]
        for i, (cx, cy, r, th) in enumerate(cover):
            if cy < 90:
                cy -= 8
            clumps.append((cx, cy, r, th, 0.3 + 0.4 * rng(), True))
        _GROWN = (br, clumps)
    return _GROWN


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    C.sky(c, p, H, nclouds=6, seed=41)
    # the far mountains low behind the crown (the view from the hill)
    P.mountains(c, p, 170, 40, 0.006, 5)
    P.far_ridge(c, p, 172, 10, 0.01, 12, step=9, tree_h=(4, 8))
    # the top of the hill under the tree
    gm = m_poly([(-4, H + 4)] + [(x, 170 + 5 * math.sin(x * 0.011 + 1) + 2 * math.sin(x * 0.05)) for x in range(-4, W + 6, 4)]
                + [(W + 4, H + 4)])
    if s == WINTER:
        C.snowfield(c, p, gm, 61, 166, 180, stalks=30)
    else:
        C.turf(c, p, gm, 61, 166, 180, n=500, hmax=5)
        P.flowers(c, p, gm, 60, 62)
    br, clumps = grown()
    C.leaves_list(c, p, clumps, front=False, seed=500)
    # the branches that grow from the limbs (thin last, so that they lie on the thick ones)
    for i, (pts, w0, w1, lvl) in enumerate(sorted(br, key=lambda b: -b[1])):
        C.limb(c, p, pts, w0, w1, seed=i * 3)
    # the kite (behind the main limbs: the swing limb crosses its lower tip)
    if st.get('kite', 1):
        kite(c, p)
    for i, (pts, w0, w1) in enumerate(LIMBS):
        C.limb(c, p, pts, w0, w1, body=False)
    C.trunk(c, p, xl, xr, 108, H + 4, seed=13, cracks=6)
    for i, (pts, w0, w1) in enumerate(LIMBS):
        C.limb(c, p, pts, w0, w1, outline=False, seed=100 + i)
    swing_rope(c, p)
    if s == WINTER:
        C.robin(c, p, 268, 103 - 4, face=-1)
    elif s == SPRING:
        nest(c, p)
    C.leaves_list(c, p, clumps, front=True, seed=700)
    P.particles_baked(c, p, (0, 0, W, H), 40, 55)
    return c


def nest(c, p):
    """a small nest of a bird in the fork of limb E (spring)"""
    x, y = 96, 60
    c.paint(m_ellipse(x, y, 9, 4.5), hx('#7a5a3a'))
    for k in range(7):
        c.paint(m_line(x - 9 + k * 2.6, y - 3 + (k % 2), x - 6 + k * 2.6, y + 3, 1), hx('#a8845a'))
    c.paint(m_ellipse(x, y - 3, 6, 1.6), hx('#4a3424'))
    for k, ex in enumerate((x - 3, x, x + 3)):
        c.paint(m_ellipse(ex, y - 4, 1.6, 1.3), hx('#8fd0e0'))


def swing_rope(c, p):
    """the rope of the tire swing: a knot round the limb C, then down out of the picture"""
    y0 = 88
    C.rope(c, p, [(ROPE_X - 6, y0 - 8), (ROPE_X - 7, y0 + 2), (ROPE_X - 4, y0 + 9)], 3)
    C.rope(c, p, [(ROPE_X + 6, y0 - 9), (ROPE_X + 5, y0 + 2), (ROPE_X + 2, y0 + 9)], 3)
    C.rope(c, p, [(ROPE_X - 1, y0 + 6), (ROPE_X, y0 + 40), (ROPE_X + 1, H + 4)], 4)
    C.rope(c, p, [(ROPE_X - 7, y0 - 10), (ROPE_X + 7, y0 - 11)], 3)
    if p.s == WINTER:
        c.paint(m_ellipse(ROPE_X, y0 - 13, 8, 2.2), (255, 255, 255))


def kite(c, p):
    """grandpa's kite: a diamond of red and yellow paper on a cross of sticks, torn, its tail in the branches"""
    T, R, B, L = (KX + 6, KY - 38), (KX + 32, KY - 4), (KX - 4, KY + 42), (KX - 30, KY - 8)
    X = (KX, KY - 6)
    red = [hx('#7a1612'), hx('#b8261e'), hx('#e8342a'), hx('#ff7a5a')]
    yel = [hx('#a07010'), hx('#d8a020'), hx('#ffd23f'), hx('#fff08a')]
    lit_k = 1 if p.L[0] > 0 else 0
    quads = [(T, R, red if lit_k else yel), (R, B, red), (B, L, yel if lit_k else red), (L, T, red)]
    quads = [(T, R, yel), (R, B, red), (B, L, yel), (L, T, red)]
    hole = m_poly([(KX + 8, KY + 2), (KX + 18, KY + 4), (KX + 20, KY + 10), (KX + 12, KY + 18), (KX + 6, KY + 12),
                   (KX + 9, KY + 8)])
    paper = m_poly([T, R, B, L])
    c.outline(paper - hole, hx('#4a1410'))
    for i, (a, b, ramp) in enumerate(quads):
        q = m_poly([a, b, X]) - hole
        # a little light on the paper: the side of the sun is lighter
        mx = (a[0] + b[0] + X[0]) / 3
        k = 2 if (mx - KX) * p.L[0] > 0 else 1
        c.paint(q, ramp[k + 1] if p.s != WINTER or k == 2 else ramp[2])
        c.paint_dither(q, ramp[k], 0.3)
        # a crease in the paper
        c.paint(m_line(lerp(a[0], X[0], 0.5), lerp(a[1], X[1], 0.5), lerp(b[0], X[0], 0.3), lerp(b[1], X[1], 0.3), 1) & q,
                ramp[k - 1 if k > 0 else 0])
    # the torn flap: it hangs down from the hole
    flap = m_poly([(KX + 12, KY + 18), (KX + 20, KY + 10), (KX + 22, KY + 26), (KX + 16, KY + 30)])
    c.paint(flap, hx('#f0b0a0'))
    c.paint(flap.side(1, 0, 1), hx('#c07060'))
    c.outline(flap, hx('#4a1410'))
    # the sticks: the spine, the cross spar (broken at the right, at the hole)
    stick = [hx('#5a3a1e'), hx('#9a6a3a'), hx('#d8a868')]
    c.paint(m_line(T[0], T[1], B[0], B[1], 2.4), stick[0])
    c.paint(m_line(T[0] + 0.6, T[1], B[0] + 0.6, B[1], 1), stick[2])
    c.paint(m_line(L[0], L[1], KX + 8, KY - 3, 2.4), stick[0])
    c.paint(m_line(L[0], L[1] - 0.6, KX + 8, KY - 3.6, 1), stick[2])
    c.paint(m_line(KX + 16, KY - 2, R[0], R[1], 2.4), stick[0])
    c.paint(m_line(KX + 16, KY - 2.6, R[0], R[1] - 0.6, 1), stick[2])
    c.paint(m_line(KX + 8, KY - 3, KX + 11, KY + 1, 1), stick[1])          # the splinters
    c.paint(m_line(KX + 16, KY - 2, KX + 13, KY + 2, 1), stick[1])
    c.paint(m_disc(X[0], X[1], 2.2), hx('#e8e0d0'))                       # the binding of the cross
    # the string: from the cross, round a twig, out of the picture up
    sc = (232, 232, 224)
    c.paint(m_polyline([X, (KX - 12, KY + 6), (KX - 22, KY + 1), (KX - 19, KY - 9), (KX - 25, KY - 14)], 1), sc)
    # the tail: from the bottom tip down, with bows
    tail = [(B[0], B[1]), (B[0] - 4, B[1] + 9), (B[0] + 2, B[1] + 18), (B[0] - 5, B[1] + 27), (B[0] - 1, B[1] + 34)]
    c.paint(m_polyline(tail, 1), sc)
    for k in range(1, len(tail)):
        bx, by = tail[k]
        col = hx('#3fa0e8') if k % 2 else hx('#ffd23f')
        c.paint(m_poly([(bx - 5, by - 3), (bx, by), (bx - 5, by + 3)]), col)
        c.paint(m_poly([(bx + 5, by - 3), (bx, by), (bx + 5, by + 3)]), col)
        c.px(bx, by, mul(col, 0.6))
    if p.s == WINTER:
        c.paint(m_polyline([(L[0] + 1, L[1] - 2), (T[0], T[1] - 2)], 2), (255, 255, 255))
        c.paint(m_polyline([(T[0] + 1, T[1] - 1), (R[0] - 2, R[1] - 3)], 1.4), (240, 246, 255))
        c.paint(m_line(L[0] + 2, L[1] - 2, KX + 6, KY - 5, 1.4), (255, 255, 255))
