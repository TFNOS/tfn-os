# FOUR SEASONS: THE VALLEY (a wide view, the view of the look page): the big tree on the hill with the tire swing, the
# vineyard on the left slope, the path, the pond with its small island, the red shed, the way to the farmyard.
import math
import numpy as np
from fsart import (Canvas, Mask, RNG, clamp, lerp, hx, mix, mul, m_rect, m_ellipse, m_disc, m_poly, m_line,
                   m_polyline, m_union, bezier, cast_shadow)
from fspal import pal, SPRING, SUMMER, AUTUMN, WINTER
import fsparts as P

W, H = 720, 184
OX, OY = 40, 6                     # the 640 x 172 frame of the look page in this picture
TX = 205 + OX                      # the trunk of the big tree

VARS = {'kite': [1, 0], 'pole': [0, 1], 'log': [0, 1, 2]}
HOT = {
    'tree': (TX - 30, 30, 60, 64),
    'kite': (TX + 4, 22, 28, 28),
    'swing': (TX + 30, 58, 28, 36),
    'vines': (OX + 10, 92, 150, 60),
    'shed': (OX + 470, 96, 76, 46),
    'pond': (OX + 300, 140, 140, 26),
    'farm': (OX + 580, 112, 60, 44),
    'hill': (TX - 60, 60, 160, 40),
}


def hill_y(x):
    """the top of the main hill (x in this picture)"""
    u = x - OX
    return 141 - 56 * math.exp(-((u - 205) / 135) ** 2) - 4 * math.sin(u * 0.012 + 0.5) + OY


TREE = None


def tree():
    global TREE
    if TREE is None:
        TREE = P.Tree(TX, 86 + OY, seed=404, depth=5, length=20, width=7.5, crown=(58, 31), nclump=300,
                      clump_r=(3.2, 7.4), extra=[(TX + 9, 61 + OY, TX + 51, 53 + OY, 2.4, -0.19, 3)])
    return TREE


def draw(s, st):
    p = pal(s)
    c = Canvas(W, H)
    sun = (p.sun_xy[0] + OX, p.sun_xy[1] + OY)
    P.sky(c, p, 0, 112, sun)
    P.clouds(c, p, 14, 52, 7, 31)
    # far mountains, then a ridge with small trees
    P.mountains(c, p, 110, 36, 0.007, 3)
    P.far_ridge(c, p, 126, 14, 0.011, 9)
    # the main hill
    pts = [(-4, H + 4)] + [(x, hill_y(x)) for x in range(-4, W + 5, 2)] + [(W + 4, H + 4)]
    hill = m_poly(pts)
    c.paint(hill, p.hill)
    # (the hill is darker far from the top, on the side away from the sun)
    sdir = -1 if sun[0] < TX else 1

    def face(x):
        return -(hill_y(x + 2) - hill_y(x - 2)) / 4 * sdir
    P.grass_marks(c, p, hill, 3400, 11, face_fn=face)
    # the path from the shed up to the tree (and on to the farmyard)
    path_pts = bezier((OX + 486, 141 + OY), (OX + 430, 120 + OY), (OX + 330, 122 + OY), (OX + 290, 98 + OY), 40) + \
        bezier((OX + 290, 98 + OY), (OX + 262, 88 + OY), (OX + 240, 86 + OY), (OX + 222, 86 + OY), 12)[1:]
    pw = 3.4
    pm = m_polyline(path_pts, pw, 2.0)
    pm2 = m_polyline([(OX + 486, 141 + OY), (OX + 560, 146 + OY), (OX + 640, 150 + OY), (W + 4, 152 + OY)], 4.5, 5.0)
    if s != WINTER:
        c.paint(pm | pm2, p.pathr[2])
        c.paint((pm | pm2).bottom(1), p.pathr[0])
        c.paint_dither(pm | pm2, p.pathr[3], 0.12)
    else:
        c.paint(pm | pm2, p.snow[1])
        c.paint_dither(pm | pm2, p.snow[0], 0.25)
    # rocks on the slope
    for rx, rr in [(150, 3.2), (266, 2.4), (330, 3.6), (430, 2.6), (612, 3.0)]:
        P.rock(c, p, rx + OX, hill_y(rx + OX) + 6, rr)
    # the vineyard on the left slope
    vineyard(c, p, s)
    # bushes
    for bx, by, br in [(318, 118, 7), (292, 110, 5), (440, 140, 6), (574, 138, 8), (606, 140, 6), (112, 104, 5)]:
        P.bush(c, p, bx + OX, by + OY, br, seed=bx)
    # flowers in the grass
    P.flowers(c, p, hill - m_rect(OX + 290, 140, 160, 40), 160, 21)
    # the shadow of the tree and the shed (they turn with the sun and grow long in winter)
    t = tree()
    elev = clamp((100 - p.sun_xy[1]) / 86, 0.2, 1)
    sl = 10 / elev - 6
    dirx = clamp((TX - sun[0]) / 260, -1.2, 1.2)
    c.darken(m_ellipse(TX + dirx * (14 + sl * 2.4), 87 + OY + abs(dirx) * 1.5, 24 + sl * 2.6, 3.6 + sl * 0.12), 0.74, p.cool)
    tree_draw(c, p, s, st)
    shed(c, p, s)
    pond(c, p, s, st)
    farm_way(c, p, s)
    foreground(c, p, s)
    return c


def vineyard(c, p, s):
    for r in range(5, -1, -1):
        off = 7 + r * 7.5
        sc = 0.7 + r * 0.1

        def yat(px):
            return hill_y(px) + off + (px - 80 - OX) * 0.02
        soil = [(x, yat(x) + 1) for x in range(-10, OX + 172, 4)] + [(x, yat(x) + 3.4 * sc) for x in range(OX + 172, -11, -4)]
        sm = m_poly(soil)
        if s == WINTER:
            c.paint(sm, p.snow[1])
        else:
            c.paint(sm, mix(p.soil, p.hill, 0.35))
            c.paint(sm.bottom(1), mul(p.soil, 0.8))
        dens = {SPRING: 0.5, SUMMER: 1.0, AUTUMN: 0.85, WINTER: 0.0}[s]
        px = -6
        k = 0
        while px <= OX + 166:
            py = yat(px)
            ph = 8 * sc
            c.paint(m_rect(px, py - ph, 1.4, ph), p.woodr[2])
            c.px(px, py - ph, p.woodr[3])
            if s == WINTER:
                c.paint(m_rect(px - 1, py - ph - 1, 3, 1), (255, 255, 255))
            if dens > 0.05:
                n2 = px + 4.5 * sc
                py2 = yat(n2)
                leaf = p.vine
                if s == AUTUMN:
                    leaf = mix(leaf, hx('#e2602c') if (k % 3) else hx('#f2b43a'), 0.6)
                rx = 5 * sc * dens + 0.5
                ry = 2.6 * sc * min(1, dens + 0.3)
                lm = m_ellipse(n2, py2 - 5 * sc, rx, ry)
                c.paint(lm, mul(leaf, 0.72))
                c.paint(m_ellipse(n2 - 0.5, py2 - 6 * sc, rx * 0.84, ry * 0.73), leaf)
                c.paint(m_rect(n2 - 2 * sc, py2 - 7.4 * sc, 1.6, 1), mix(leaf, (255, 255, 255), 0.3))
                if s == SUMMER:
                    c.paint(m_rect(n2 - 1.4, py2 - 3.4 * sc, 1.6, 2.4), hx('#5d2f86'))
                    c.paint(m_rect(n2 + 1.2, py2 - 3.6 * sc, 1.4, 2), hx('#7a3fa8'))
                if s == SPRING and k % 2 == 0:
                    c.px(n2, py2 - 7 * sc, hx('#e8f8c0'))
            elif s == WINTER:
                # pruned vines: a bare stump and a curl
                c.paint(m_line(px + 1, py - ph * 0.5, px + 4 * sc, py - ph * 0.85, 1), p.woodr[1])
            px += 9 * sc
            k += 1
        # the wire
        c.paint(m_polyline([(x, yat(x) - 6 * sc) for x in range(-6, OX + 167, 6)], 1), mix(p.woodD, p.hill, 0.45))
        if s == AUTUMN and r % 2 == 0:
            # a harvest basket at the end of the row
            bx = OX + 150
            c.paint(m_rect(bx, yat(bx) - 4, 7, 4), hx('#9a6a3a'))
            c.paint(m_rect(bx + 0.5, yat(bx) - 5, 6, 1.4), hx('#5b2a6e'))


def tree_draw(c, p, s, st):
    t = tree()
    dirx = 1 if p.L[0] < 0 else -1
    # the roots
    c.paint(m_poly([(TX - 9, 88 + OY), (TX - 3, 81 + OY), (TX + 3, 81 + OY), (TX + 9, 88 + OY), (TX + 4, 87 + OY), (TX, 86 + OY), (TX - 4, 87 + OY)]), p.bark[1])
    t.draw_wood(c, p)
    # the lost kite: the leaves hide it; in winter it shows
    if st.get('kite', 1):
        kite_in_tree(c, p, TX + 19, 31 + OY)
    t.draw_leaves(c, p, seed=3)
    # the tire swing on the right branch
    rx, ry, L = TX + 42, 54.6 + OY, 22
    c.paint(m_line(rx, ry, rx, ry + L - 4.5, 1), hx('#e2cea0'))
    tx, ty = rx, ry + L
    c.darken(m_ellipse(tx + p.slant * 6, hill_y(tx) + 1.2, 6, 1.4), 0.72, p.cool)
    tm = m_ellipse(tx, ty + 1, 4.6, 5.6)
    c.paint(tm, hx('#26262b'))
    c.paint(m_ellipse(tx + 0.3, ty + 1, 1.8, 3), mix(p.hill, p.skyB, 0.35))
    c.paint(tm.side(1 if p.L[0] > 0 else -1, 0, 1), hx('#55555e'))
    if s == WINTER:
        c.paint(m_ellipse(tx, ty - 4, 3.6, 1.3), (255, 255, 255))
    if st.get('pole', 0):
        c.paint(m_line(TX + 6, 88 + OY, TX + 24, 40 + OY, 1.4), hx('#b08850'))


def kite_in_tree(c, p, kx, ky):
    c.paint(m_poly([(kx, ky - 6), (kx + 4.6, ky), (kx, ky + 7), (kx - 4.6, ky)]), hx('#e8342a'))
    c.paint(m_poly([(kx, ky - 6), (kx + 4.6, ky), (kx, ky)]), hx('#ffd23f'))
    c.paint(m_poly([(kx, ky + 7), (kx - 4.6, ky), (kx, ky)]), hx('#ffd23f'))
    c.paint(m_line(kx, ky - 6, kx, ky + 7, 1), hx('#5a3a22'))
    c.paint(m_line(kx - 4.6, ky, kx + 4.6, ky, 1), hx('#5a3a22'))
    for k in range(1, 9):
        x = kx + math.sin(k * 0.9) * 2 + k * 0.5
        y = ky + 7 + k * 2.6
        c.px(x, y, (240, 240, 240))
        if k % 2 == 0:
            c.paint(m_rect(x - 1, y, 3, 1.4), hx('#3fa0e8') if k % 4 else hx('#ffd23f'))
    if p.s == WINTER:
        c.px(kx + 5, ky - 9, (255, 255, 255))
        c.paint(m_rect(kx - 3, ky - 6, 3, 1), (255, 255, 255))


def shed(c, p, s):
    x0, x1, gy, top, apex = OX + 478, OX + 524, 139 + OY, 115 + OY, 102 + OY
    mx = (x0 + x1) / 2
    # the shadow on the ground
    c.darken(m_poly([(x0, gy), (x1, gy), (x1 + p.slant * 18, gy + 3), (x0 + p.slant * 18, gy + 3)]), 0.72, p.cool)
    c.paint(m_poly([(x1, gy), (x1 + 12, gy - 5), (x1 + 12, top - 5), (x1, top)]), p.shedD)
    c.paint(m_rect(x0, top, x1 - x0, gy - top), p.shed)
    for px in range(x0 + 4, x1, 4):
        c.paint(m_rect(px, top + 1, 1, gy - top - 1), mul(p.shed, 0.82))
    c.paint(m_poly([(x0 - 1, top), (x1 + 1, top), (mx, apex)]), p.shed)
    roof = m_poly([(mx, apex - 1.5), (mx + 12, apex - 6.5), (x1 + 14, top - 6.5), (x1 + 2, top + 1)])
    c.paint(roof, p.roof)
    for k in range(1, 5):
        f = k / 5
        c.paint(m_line(lerp(mx, x1 + 2, f), lerp(apex - 1.5, top + 1, f), lerp(mx + 12, x1 + 14, f), lerp(apex - 6.5, top - 6.5, f), 1), mul(p.roof, 0.8))
    c.paint(m_polyline([(x0 - 2, top + 1), (mx, apex - 1.5), (x1 + 2, top + 1)], 1.4), p.trim)
    c.paint(m_rect(x0 + 6, top + 7, 11, 17), mul(p.shedD, 0.75))
    dm = m_rect(x0 + 6, top + 7, 11, 17)
    c.outline(dm, p.trim)
    c.paint(m_line(x0 + 6, top + 7, x0 + 17, top + 24, 1), p.trim)
    c.paint(m_line(x0 + 17, top + 7, x0 + 6, top + 24, 1), p.trim)
    warm = {SPRING: 0.0, SUMMER: 0.0, AUTUMN: 0.7, WINTER: 1.0}[s]
    wm = m_rect(x0 + 25, top + 6, 14, 9)
    c.paint(wm, mix(hx('#2c3a4a'), hx('#ffcf6a'), warm))
    if warm > 0.1:
        c.glow(x0 + 32, top + 10, 14, hx('#ffc35a'), 0.35 * warm)
    c.outline(wm, p.trim)
    c.paint(m_line(x0 + 32, top + 6, x0 + 32, top + 14, 1), p.trim)
    c.paint(m_line(x0 + 25, top + 10, x0 + 38, top + 10, 1), p.trim)
    c.paint(m_rect(x0 + 24, top + 15, 16, 2.4), p.wood)
    if s in (SPRING, SUMMER):
        for k in range(6):
            c.paint(m_rect(x0 + 25 + k * 2.5, top + 13.6, 1.8, 1.6), [hx('#ff5a7a'), hx('#ffd23f'), (255, 255, 255)][k % 3])
    c.paint(m_rect(OX + 531, 100 + OY, 3, 8), hx('#3b3b40'))
    if s == WINTER:
        c.paint(m_poly([(mx, apex - 3.5), (mx + 12, apex - 8.5), (x1 + 15, top - 8), (x1 + 3, top - 0.6), (x1 + 4, top + 1.5)]), (255, 255, 255))
        c.paint(m_poly([(x0 - 3, top + 1.5), (mx, apex - 3.5), (mx + 1, apex - 1)]), (255, 255, 255))
        for k in range(5):
            c.paint(m_rect(x1 + 3 + k * 2.6, top - 3 - k, 1, 2 + (k % 2) * 2), (235, 245, 255))
    if s in (AUTUMN, WINTER):
        for k in range(6):
            lx = OX + 541 + (k % 3) * 4.2
            ly = 137 + OY - (k // 3) * 3.6
            c.paint(m_ellipse(lx, ly, 2.1, 1.8), hx('#8a5a32') if k % 2 else hx('#a87447'))
            c.px(lx - 0.4, ly - 0.6, hx('#e8c79a'))
        if s == WINTER:
            c.paint(m_rect(OX + 539, 130 + OY, 13, 2), (255, 255, 255))


def pond(c, p, s, st):
    cx, cy, rx, ry = OX + 368, 153 + OY, 64, 9.5
    c.paint(m_ellipse(cx, cy + 0.8, rx + 3, ry + 2), mul(p.grass, 0.85) if s != WINTER else p.snow[1])
    pm = m_ellipse(cx, cy, rx, ry)
    if s == WINTER:
        c.paint_values(pm, p.ice, lambda x, y: np.clip(1 - (y - (cy - ry)) / (2 * ry), 0, 1), 0.3)
        c.paint(m_polyline(bezier((cx - 40, cy + 1), (cx - 6, cy + 5), (cx + 20, cy - 3)), 1), hx('#9cc6dc'))
        c.paint(m_line(cx - 50, cy - 3, cx - 30, cy - 5, 1), (255, 255, 255))
    else:
        c.vgrad(pm, [mix(p.skyB, p.water, 0.35), p.water, p.waterD], cy - ry, cy + ry, 0.3)
        c.paint(m_rect(cx - rx, cy - ry, rx * 2, 2) & pm, mix(mul(p.hill, 0.8), p.water, 0.4))
        for k in range(10):
            ry2 = cy - ry + 3 + k * 1.8
            c.paint(m_rect(cx - 44 + (k * 23) % 56, ry2, 9 + (k % 3) * 6, 1) & pm, mix(p.water, (255, 255, 255), 0.45))
    # the island with its willow
    ix, iy = cx + 18, cy - 3
    c.paint(m_ellipse(ix, iy + 1, 9, 2.6), p.stone[1] if s != WINTER else p.snow[2])
    c.paint(m_ellipse(ix, iy, 8, 2.2), p.hill if s != WINTER else (255, 255, 255))
    wc = {SPRING: p.lfL, SUMMER: p.lf, AUTUMN: hx('#e8b040'), WINTER: hx('#8a7a6a')}[s]
    c.paint(m_line(ix, iy, ix, iy - 7, 1.4), p.bark[1])
    for k in range(7):
        a = -2.6 + k * 0.4
        c.paint(m_polyline([(ix, iy - 7), (ix + math.cos(a) * 6, iy - 9 + math.sin(a) * 2), (ix + math.cos(a) * 8, iy - 2)], 1), wc)
    lg = st.get('log', 0)
    if lg == 0:
        c.paint(m_rect(cx - rx - 6, cy - 2, 12, 3), p.bark[2])
    elif lg == 2:
        c.paint(m_line(ix - 12, iy + 1, ix - 7, iy + 1, 2), p.bark[2])
    if s != WINTER:
        pad = {SPRING: 0.75, SUMMER: 1.0, AUTUMN: 0.4}[s]
        for (px, py) in [(330, 151), (346, 155), (394, 149), (406, 155), (374, 158), (318, 155)]:
            r = 3.4 * pad
            c.paint(m_ellipse(px + OX, py + OY, r * 1.4, r * 0.5), mix(p.lf, p.lfD, 0.3) if s != AUTUMN else hx('#8a7a3a'))
        # the reeds on the right bank
    for k in range(9):
        rxp = cx + rx - 10 + k * 2.2
        h = 7 + (k % 3) * 2.5
        col = hx('#b9a98a') if s == WINTER else mix(p.grass, hx('#7a6a3a'), 0.6 if s == AUTUMN else 0.0)
        c.paint(m_line(rxp, cy + 2, rxp, cy + 2 - h, 1), col)
        if k % 2 == 0:
            c.paint(m_rect(rxp, cy + 2 - h - 0.5, 1.6, 3), (255, 255, 255) if s == WINTER else hx('#6a4228'))


def farm_way(c, p, s):
    """the far right: a sign post where the path goes on to the farmyard, the roof of the house behind the hill"""
    x, y = OX + 606, 146 + OY
    c.paint(m_rect(x, y - 18, 2, 18), p.woodr[2])
    c.paint(m_poly([(x - 8, y - 18), (x + 12, y - 18), (x + 16, y - 15), (x + 12, y - 12), (x - 8, y - 12)]), p.woodr[3])
    c.paint(m_line(x - 6, y - 15, x + 10, y - 15, 1), p.woodr[1])
    # the roof and the chimney of the farmhouse far behind
    rx = OX + 640
    c.paint(m_poly([(rx - 6, 128 + OY), (rx + 20, 118 + OY), (rx + 50, 128 + OY)]), p.tile[2] if s != WINTER else (255, 255, 255))
    c.paint(m_rect(rx + 26, 116 + OY, 3, 6), p.brick[1])


def foreground(c, p, s):
    fm = m_poly([(-40, H + 4)] + [(x, 172 + OY - 6 + math.sin(x * 0.05) * 2 + math.sin(x * 0.13) * 1.2) for x in range(-40, W + 41, 4)] + [(W + 40, H + 4)])
    c.paint(fm, p.fg)
    for x in range(-30, W + 30, 3):
        y = 164 + OY + math.sin(x) * 2
        c.paint(m_rect(x, y, 1, 3 + (x % 3)), p.grassL if x % 6 else mul(p.fg, 0.72))
    P.fence(c, p, OX + 540, 167 + OY, OX + 659, 157 + OY, 8)
