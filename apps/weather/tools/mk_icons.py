# WEATHER: the icons of the conditions, drawn on the PC at 8 x the size and made smaller (smooth edges), in two
# sizes. The data: the colour of each pixel in the byte order of the frame (RGB565, the two bytes swapped) and a
# 4-bit alpha. Also a sheet to look at (icons_sheet.png, next to this tool). The icons are original: this tool draws
# them with Pillow (no source picture).
#   python apps/weather/tools/mk_icons.py  ->  apps/weather/wx_icons.inc
import math, os
from PIL import Image, ImageDraw, ImageFilter

H = os.path.dirname(os.path.abspath(__file__))
SIZES = [26, 48]
SS = 8
NAMES = ['SUN', 'MOON', 'FEW_D', 'FEW_N', 'PART_D', 'PART_N', 'CLOUD', 'FOG', 'DRIZZLE', 'RAIN', 'HEAVY', 'SHOWER_D',
         'SHOWER_N', 'SNOW', 'SLEET', 'THUNDER']


def radial(size, c0, c1, cx, cy, r):
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    px = im.load()
    for y in range(max(0, int(cy - r)), min(size, int(cy + r) + 1)):
        for x in range(max(0, int(cx - r)), min(size, int(cx + r) + 1)):
            d = math.hypot(x - cx, y - cy) / r
            if d <= 1:
                t = d ** 1.6
                px[x, y] = tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)) + (255,)
    return im


def vgrad(size, box, c0, c1):
    x0, y0, x1, y1 = box
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for y in range(int(y0), int(y1) + 1):
        t = (y - y0) / max(1, y1 - y0)
        d.line([(x0, y), (x1, y)], fill=tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)) + (255,))
    return im


def masked(src, mask):
    out = Image.new('RGBA', src.size, (0, 0, 0, 0))
    out.paste(src, (0, 0), mask)
    return out


def glow(layer, radius, alpha):
    a = layer.split()[3].filter(ImageFilter.GaussianBlur(radius))
    g = Image.new('RGBA', layer.size, (255, 196, 80, 0))
    g.putalpha(a.point(lambda v: int(v * alpha)))
    return g


def sun(S, cx, cy, r, rays=True):
    L = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    if rays:
        d = ImageDraw.Draw(L)
        for k in range(8):
            a = k * math.pi / 4 + math.pi / 8
            x0, y0 = cx + math.cos(a) * r * 1.35, cy + math.sin(a) * r * 1.35
            x1, y1 = cx + math.cos(a) * r * 1.75, cy + math.sin(a) * r * 1.75
            d.line([(x0, y0), (x1, y1)], fill=(255, 190, 60, 255), width=int(r * 0.26))
            for (x, y) in ((x0, y0), (x1, y1)):
                w = r * 0.13
                d.ellipse([x - w, y - w, x + w, y + w], fill=(255, 190, 60, 255))
    disk = radial(S, (255, 236, 120), (255, 160, 40), cx - r * 0.25, cy - r * 0.25, r * 1.3)
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    L.alpha_composite(masked(disk, m))
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.alpha_composite(glow(L, r * 0.5, 0.45))
    out.alpha_composite(L)
    return out


def moon(S, cx, cy, r):
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.ellipse([cx - r * 0.35, cy - r * 1.05, cx + r * 1.45, cy + r * 0.75], fill=0)
    body = radial(S, (255, 250, 225), (215, 210, 185), cx - r * 0.4, cy - r * 0.2, r * 1.4)
    L = masked(body, m)
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    g = glow(L, r * 0.45, 0.35)
    g = Image.merge('RGBA', (Image.new('L', (S, S), 220), Image.new('L', (S, S), 225), Image.new('L', (S, S), 255), g.split()[3]))
    out.alpha_composite(g)
    out.alpha_composite(L)
    return out


def cloud(S, cx, cy, w, dark=0.0):
    # three puffs and a flat base
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    h = w * 0.58
    base_y = cy + h * 0.32
    d.rounded_rectangle([cx - w * 0.5, cy - h * 0.05, cx + w * 0.5, base_y], radius=h * 0.37, fill=255)
    for (px_, py_, pr) in ((-0.22, 0.02, 0.24), (0.06, -0.12, 0.31), (0.3, 0.06, 0.2)):
        x, y, r = cx + px_ * w, cy + py_ * w, pr * w
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)
    top = tuple(int(255 - (255 - 120) * dark) for _ in range(3))
    top = (top[0], top[1], min(255, top[2] + 6))
    bot = (int(206 - 90 * dark), int(216 - 92 * dark), int(232 - 86 * dark))
    body = vgrad(S, (cx - w * 0.6, cy - w * 0.45, cx + w * 0.6, base_y), top, bot)
    L = masked(body, m)
    sh = Image.new('RGBA', (S, S), (20, 30, 50, 0))
    sh.putalpha(m.filter(ImageFilter.GaussianBlur(w * 0.05)).point(lambda v: int(v * 0.35)))
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.alpha_composite(sh, (0, int(w * 0.04)))
    out.alpha_composite(L)
    return out


def drops(S, cx, y0, w, n, kind):
    L = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(L)
    for k in range(n):
        x = cx + (k - (n - 1) / 2) * w * (0.8 / max(1, n - 1) if n > 1 else 0)
        y = y0 + (k % 2) * w * 0.1
        if kind == 'drop':
            L2 = w * 0.2
            d.line([(x + L2 * 0.25, y), (x - L2 * 0.25, y + L2)], fill=(90, 170, 255, 255), width=int(w * 0.07))
        elif kind == 'dot':
            r = w * 0.045
            d.ellipse([x - r, y + w * 0.05 - r, x + r, y + w * 0.05 + r], fill=(110, 185, 255, 255))
        elif kind == 'snow':
            r = w * 0.075
            yy = y + w * 0.08
            for a in range(3):
                ang = a * math.pi / 3
                d.line([(x - math.cos(ang) * r, yy - math.sin(ang) * r), (x + math.cos(ang) * r, yy + math.sin(ang) * r)],
                       fill=(235, 245, 255, 255), width=max(2, int(w * 0.035)))
        elif kind == 'sleet':
            if k % 2:
                r = w * 0.05
                d.ellipse([x - r, y + w * 0.06 - r, x + r, y + w * 0.06 + r], fill=(225, 240, 255, 255))
            else:
                L2 = w * 0.18
                d.line([(x + L2 * 0.25, y), (x - L2 * 0.25, y + L2)], fill=(90, 170, 255, 255), width=int(w * 0.07))
    return L


def bolt(S, cx, cy, w):
    L = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(L)
    pts = [(0.08, -0.22), (-0.12, 0.06), (0.0, 0.06), (-0.08, 0.3), (0.16, -0.02), (0.03, -0.02), (0.14, -0.22)]
    d.polygon([(cx + x * w, cy + y * w) for x, y in pts], fill=(255, 214, 60, 255))
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.alpha_composite(glow(L, w * 0.04, 0.6))
    out.alpha_composite(L)
    return out


def fog(S, cx, cy, w):
    L = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(L)
    for k, (dx, ww) in enumerate(((0.0, 0.8), (0.08, 0.66), (-0.05, 0.72))):
        y = cy - w * 0.2 + k * w * 0.2
        x0, x1 = cx - w * ww / 2 + dx * w, cx + w * ww / 2 + dx * w
        d.rounded_rectangle([x0, y - w * 0.045, x1, y + w * 0.045], radius=w * 0.045, fill=(205, 212, 224, 235))
    return L


def icon(name, S):
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    c = S / 2
    W = S * 0.86
    def add(l):
        im.alpha_composite(l)
    if name == 'SUN':
        add(sun(S, c, c, S * 0.2))
    elif name == 'MOON':
        add(moon(S, c, c, S * 0.3))
    elif name in ('FEW_D', 'PART_D', 'SHOWER_D'):
        add(sun(S, c - S * 0.12, c - S * 0.14, S * 0.15 if name != 'FEW_D' else S * 0.17))
        cw = W * (0.6 if name == 'FEW_D' else 0.78)
        add(cloud(S, c + S * 0.08, c + S * (0.14 if name != 'SHOWER_D' else 0.06), cw, 0.0 if name != 'SHOWER_D' else 0.25))
        if name == 'SHOWER_D':
            add(drops(S, c + S * 0.06, c + S * 0.3, W, 3, 'drop'))
    elif name in ('FEW_N', 'PART_N', 'SHOWER_N'):
        add(moon(S, c - S * 0.12, c - S * 0.14, S * 0.2))
        cw = W * (0.6 if name == 'FEW_N' else 0.78)
        add(cloud(S, c + S * 0.08, c + S * (0.14 if name != 'SHOWER_N' else 0.06), cw, 0.15 if name != 'SHOWER_N' else 0.35))
        if name == 'SHOWER_N':
            add(drops(S, c + S * 0.06, c + S * 0.3, W, 3, 'drop'))
    elif name == 'CLOUD':
        add(cloud(S, c + S * 0.12, c - S * 0.1, W * 0.62, 0.3))
        add(cloud(S, c - S * 0.04, c + S * 0.06, W * 0.86, 0.0))
    elif name == 'FOG':
        add(cloud(S, c, c - S * 0.12, W * 0.74, 0.2))
        add(fog(S, c, c + S * 0.2, W))
    else:
        dark = {'DRIZZLE': 0.2, 'RAIN': 0.4, 'HEAVY': 0.65, 'SNOW': 0.15, 'SLEET': 0.3, 'THUNDER': 0.75}[name]
        add(cloud(S, c, c - S * 0.1, W * 0.9, dark))
        y0 = c + S * 0.16
        if name == 'DRIZZLE':
            add(drops(S, c, y0, W, 4, 'dot'))
        elif name == 'RAIN':
            add(drops(S, c, y0, W, 3, 'drop'))
        elif name == 'HEAVY':
            add(drops(S, c, y0, W, 5, 'drop'))
        elif name == 'SNOW':
            add(drops(S, c, y0, W, 3, 'snow'))
        elif name == 'SLEET':
            add(drops(S, c, y0, W, 4, 'sleet'))
        elif name == 'THUNDER':
            add(bolt(S, c, c + S * 0.2, W))
            add(drops(S, c, y0, W, 2, 'drop'))
    return im


def a4_pack(vals):
    b = bytearray()
    for i in range(0, len(vals), 2):
        lo = vals[i] >> 4
        hi = (vals[i + 1] >> 4) if i + 1 < len(vals) else 0
        b.append(lo | (hi << 4))
    return b


out = ['// WEATHER: the icons of the conditions (made by an icon script; do not edit).',
       '// For each size: the colours (RGB565 in the byte order of the frame) and a 4-bit alpha, the icons one after',
       '// the other.']
sheet = Image.new('RGBA', (len(NAMES) * 56, 2 * 56 + 4), (40, 60, 100, 255))
for si, sz in enumerate(SIZES):
    cols, alph = [], []
    for k, name in enumerate(NAMES):
        big = icon(name, sz * SS)
        im = big.resize((sz, sz), Image.LANCZOS)
        sheet.alpha_composite(im, (k * 56 + (56 - sz) // 2, si * 56 + 4 + (56 - sz) // 2))
        for (r, g, b, a) in im.getdata():
            if a:
                r, g, b = min(255, r * 255 // a) if False else r, g, b
            c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            cols.append(((c >> 8) | (c << 8)) & 0xFFFF)
            alph.append(a)
    out.append('#define WI_%d_N %d' % (sz, sz * sz))
    out.append('static const uint16_t WI_%d_C[%d] = {' % (sz, len(cols)))
    for i in range(0, len(cols), 24):
        out.append('    ' + ','.join('0x%04X' % v for v in cols[i:i + 24]) + ',')
    out.append('};')
    pk = a4_pack(alph)
    out.append('static const uint8_t WI_%d_A[%d] = {' % (sz, len(pk)))
    for i in range(0, len(pk), 40):
        out.append('    ' + ','.join(str(v) for v in pk[i:i + 40]) + ',')
    out.append('};')
open(os.path.join(H, '..', 'wx_icons.inc'), 'w', newline='\n').write('\n'.join(out) + '\n')
sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(os.path.join(H, 'icons_sheet.png'))
print('icons', len(NAMES), 'sizes', SIZES)
