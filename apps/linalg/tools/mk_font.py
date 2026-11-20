"""LINEAR ALGEBRA: the faces of the app as 4-bit alpha atlases (the mfont_t form of tfn/mfont.h).

Usage:
  python apps/linalg/tools/mk_font.py <Inter.ttf> <Rubik.ttf> [--out apps/linalg/la_font.c]

<Inter.ttf> is the variable font of Inter (https://github.com/rsms/inter, SIL Open Font License 1.1,
docs/licenses/Inter-OFL.txt): the Latin and Greek letters, the digits and the signs of the mathematics.
<Rubik.ttf> is the variable font of Rubik (https://github.com/googlefonts/rubik, SIL Open Font License 1.1,
docs/licenses/Rubik-OFL.txt): the Hebrew letters.

Each face has full Unicode code points (UTF-8 in the app). The italic letters of the mathematics are at
U+E000 + the ASCII code (an italic made by a shear of Inter). The tool draws the signs that Inter does not have
(the element sign, the angle brackets, the dots of a matrix). Needs Pillow.
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
TOOL = 'apps/linalg/tools/mk_font.py'
SS = 4  # the supersampling of the italic and the drawn signs

# (C name, the px size, the weight of Inter, the weight of Rubik, the set): M = the full set (text and mathematics),
# B = the titles (ASCII, Greek, Hebrew)
FACES = [
    ('LA_F11', 11, 500, 450, 'M'),
    ('LA_F13', 13, 500, 450, 'M'),
    ('LA_F15', 15, 500, 450, 'M'),
    ('LA_F17', 17, 500, 450, 'M'),
    ('LA_F20', 20, 500, 450, 'M'),
    ('LA_F24', 24, 500, 450, 'M'),
    ('LA_B15', 15, 650, 600, 'B'),
    ('LA_B18', 18, 650, 600, 'B'),
    ('LA_B22', 22, 650, 600, 'B'),
    ('LA_B28', 28, 700, 650, 'B'),
    ('LA_B36', 36, 700, 650, 'B'),
]
ASCII = [chr(c) for c in range(32, 127)]
GREEK = [chr(c) for c in range(0x3B1, 0x3CA) if c != 0x3C2] + list('ΓΔΘΛΞΠΣΦΨΩ')
MATH = list('−×·±≤≥≈≠∞√∑∏⊗⊕‖→←↔⇒∂…°∫′–—’“”•½')
HEBREW = [chr(c) for c in range(0x5D0, 0x5EB)] + ['־', '׳', '״']
ITALIC = [chr(c) for c in range(ord('a'), ord('z') + 1)] + [chr(c) for c in range(ord('A'), ord('Z') + 1)]
DRAWN = ['∈', '⟨', '⟩', '⋯', '⋮', '⋱', '∪', '⊥']
SHEAR = 0.21


def a4_pack(vals):
    b = bytearray()
    for i in range(0, len(vals), 2):
        lo = vals[i] >> 4
        hi = (vals[i + 1] >> 4) if i + 1 < len(vals) else 0
        b.append(lo | (hi << 4))
    return b


def face(ttf, size, weight, opsz=None):
    f = ImageFont.truetype(ttf, size, layout_engine=ImageFont.Layout.BASIC)
    axes = f.get_variation_axes()
    if len(axes) == 2:
        o = size if opsz is None else opsz
        f.set_variation_by_axes([max(axes[0]['minimum'], min(axes[0]['maximum'], o)), weight])
    else:
        f.set_variation_by_axes([max(axes[0]['minimum'], min(axes[0]['maximum'], weight))])
    return f


def plain(f, ch, asc):
    """(adv, ox, oy from the baseline, w, h, alpha values) of a glyph at its size."""
    mask, (ox, oy) = f.getmask2(ch, mode='L')
    w, h = mask.size
    return f.getlength(ch), ox, oy - asc, w, h, (list(mask) if w * h else [])


def from_big(img, ox_big, base_big, adv):
    """Make a glyph from a big (SS x) picture: crop, make it small (box filter), the offsets to the baseline."""
    bb = img.getbbox()
    if not bb:
        return adv, 0, 0, 0, 0, []
    x0, y0, x1, y1 = bb
    x0 -= x0 % SS
    y0 -= (y0 - base_big) % SS
    x1 += (-x1) % SS
    y1 += (-(y1 - base_big)) % SS
    crop = img.crop((x0, y0, x1, y1))
    w, h = (x1 - x0) // SS, (y1 - y0) // SS
    small = crop.resize((w, h), Image.BOX)
    return adv, (x0 - ox_big) // SS, (y0 - base_big) // SS, w, h, list(small.tobytes())


def italic(fbig, ch, size):
    """A letter of Inter with a shear (the italic of the mathematics)."""
    asc, desc = fbig.getmetrics()
    W, H = int(size * SS * 2.2), asc + desc + SS * 4
    pad = int(size * SS * 0.5)
    img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(img).text((pad, 0), ch, font=fbig, fill=255)
    base = asc
    # x' = x + SHEAR * (base - y): the top goes to the right
    img = img.transform(img.size, Image.AFFINE, (1, SHEAR, -SHEAR * base, 0, 1, 0), resample=Image.BICUBIC)
    adv = fbig.getlength(ch) / SS + size * 0.04
    return from_big(img, pad, base, adv)


def drawn(ch, size, weight):
    """The signs that Inter does not have, drawn with lines at SS x."""
    s = size * SS
    W, H = int(s * 1.4), int(s * 1.6)
    base = int(s * 1.15)
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    lw = max(SS, int(round(s * (0.075 if weight < 600 else 0.095))))
    x_h = s * 0.53  # the x height
    mid = base - s * 0.36
    pad = int(s * 0.08)
    if ch == '∈':
        r = s * 0.27
        cx = pad + r + s * 0.06
        d.arc((cx - r, mid - r, cx + r, mid + r), 90, 270, fill=255, width=lw)
        d.line((cx, mid - r + lw / 2, cx + r * 1.4, mid - r + lw / 2), fill=255, width=lw)
        d.line((cx, mid + r - lw / 2, cx + r * 1.4, mid + r - lw / 2), fill=255, width=lw)
        d.line((cx - r + lw / 2, mid, cx + r * 1.2, mid), fill=255, width=lw)
        adv = (cx + r * 1.4 + pad * 2) / SS
    elif ch in '⟨⟩':
        top, bot = base - s * 0.78, base + s * 0.2
        wdt = s * 0.24
        x0 = pad + s * 0.06
        if ch == '⟨':
            pts = [(x0 + wdt, top), (x0, (top + bot) / 2), (x0 + wdt, bot)]
        else:
            pts = [(x0, top), (x0 + wdt, (top + bot) / 2), (x0, bot)]
        d.line(pts, fill=255, width=lw, joint='curve')
        adv = (x0 + wdt + pad + s * 0.08) / SS
    elif ch == '∪':
        w2 = s * 0.5
        x0 = pad + s * 0.08
        top, bot = base - s * 0.62, base
        d.line((x0, top, x0, bot - w2 / 2), fill=255, width=lw)
        d.line((x0 + w2, top, x0 + w2, bot - w2 / 2), fill=255, width=lw)
        d.arc((x0, bot - w2 - lw / 2, x0 + w2, bot - lw / 2), 0, 180, fill=255, width=lw)
        adv = (x0 + w2 + pad + s * 0.08) / SS
    elif ch == '⊥':
        w2 = s * 0.56
        x0 = pad + s * 0.08
        d.line((x0 + w2 / 2, base - s * 0.68, x0 + w2 / 2, base), fill=255, width=lw)
        d.line((x0, base - lw / 2, x0 + w2, base - lw / 2), fill=255, width=lw)
        adv = (x0 + w2 + pad + s * 0.08) / SS
    else:
        r = max(SS * 0.75, s * 0.055)
        if ch == '⋯':
            ps = [(pad + r + i * s * 0.3, mid) for i in range(3)]
            adv = (pad * 2 + 2 * r + s * 0.6) / SS
        elif ch == '⋮':
            ps = [(pad + r, base - i * s * 0.3 - r) for i in range(3)]
            adv = (pad * 2 + 2 * r) / SS
        else:
            ps = [(pad + r + i * s * 0.26, base - s * 0.62 + i * s * 0.26) for i in range(3)]
            adv = (pad * 2 + 2 * r + s * 0.52) / SS
        for (x, y) in ps:
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    return from_big(img, 0, base, adv)


def atlas(inter, rubik, inter_big, size, weight, kind):
    asc, desc = inter.getmetrics()
    rasc, _ = rubik.getmetrics()
    items = []
    chars = ASCII + GREEK + HEBREW + (MATH if kind == 'M' else list('−·’–×±°…'))
    for ch in chars:
        f, a = (rubik, rasc) if '֐' <= ch <= '׿' else (inter, asc)
        items.append((ord(ch), plain(f, ch, a)))
    if kind == 'M':
        for ch in ITALIC:
            items.append((0xE000 + ord(ch), italic(inter_big, ch, size)))
        for ch in DRAWN:
            items.append((ord(ch), drawn(ch, size, weight)))
    items.sort(key=lambda t: t[0])
    glyphs, data = [], bytearray()
    for code, (adv, ox, oy, w, h, vals) in items:
        off = len(data)
        data += a4_pack(vals)
        a4 = int(round(adv * 4))
        assert a4 < 256 and w < 256 and h < 256 and -128 <= oy < 128, (hex(code), a4, w, oy)
        glyphs.append((code, a4, ox, oy, w, h, off))
    capb = inter.getbbox('H')
    capital = asc - capb[1]
    line = int(round((asc + desc) * 1.08))
    return glyphs, data, capital, line


def make(inter_ttf, rubik_ttf):
    out = ['// LINEAR ALGEBRA: the faces of the app (made by %s; do not edit).' % TOOL,
           '// Inter, Copyright 2020 The Inter Project Authors; Rubik, Copyright 2015 The Rubik Project Authors:',
           '// SIL Open Font License 1.1 (docs/licenses/Inter-OFL.txt, docs/licenses/Rubik-OFL.txt).',
           '#include "la_font.h"', '']
    hdr = ['// LINEAR ALGEBRA: the faces of the app (made by %s; do not edit).' % TOOL, '#pragma once',
           '#include "tfn/mfont.h"', '']
    for cname, size, wi, wr, kind in FACES:
        inter = face(inter_ttf, size, wi)
        inter_big = face(inter_ttf, size * SS, wi, opsz=size)
        rubik = face(rubik_ttf, size, wr)
        glyphs, data, cap, line = atlas(inter, rubik, inter_big, size, wi, kind)
        out.append('static const uint8_t %s_A4[%d] = {' % (cname, len(data)))
        for k in range(0, len(data), 40):
            out.append(','.join(str(x) for x in data[k:k + 40]) + ',')
        out.append('};')
        out.append('static const mglyph_t %s_G[%d] = {' % (cname, len(glyphs)))
        for g in glyphs:
            out.append('    {%d, %d, %d, %d, %d, %d, %d},' % g)
        out.append('};')
        out.append('const mfont_t %s = {%d, %d, %d, %s_G, %s_A4};' % (cname, cap, line, len(glyphs), cname, cname))
        hdr.append('extern const mfont_t %s;  // %d px, %s' % (cname, size, 'text and mathematics' if kind == 'M'
                                                                  else 'titles'))
        print('%s: %d glyphs, %d bytes' % (cname, len(glyphs), len(data)))
    return '\n'.join(out) + '\n', '\n'.join(hdr) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('inter')
    p.add_argument('rubik')
    p.add_argument('--out', default=os.path.join(APP, 'la_font.c'))
    a = p.parse_args()
    c, h = make(a.inter, a.rubik)
    with open(a.out, 'wb') as f:
        f.write(c.encode('ascii'))
    with open(os.path.splitext(a.out)[0] + '.h', 'wb') as f:
        f.write(h.encode('ascii'))


if __name__ == '__main__':
    main()
