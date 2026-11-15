"""INTERFONT: the Inter faces of the info apps as 4-bit alpha atlases (mfont_t of tfn/mfont.h).

The tool writes the two made files of this lib:
  ck_fonts.inc     the faces of CLOCK (CK_*): CLOCK and CHESS CLOCK use them
  stocks_font.inc  the faces of STOCKS (SF_*): STOCKS and AI LIMITS use them
  sb_fonts.inc     the faces of STANDBY (SB_*): the standby screen of the firmware (main/standby_draw.c)

Usage:
  python libs/interfont/tools/export_inter.py <Inter.ttf> [--out DIR]

<Inter.ttf> is the variable font of Inter (https://github.com/rsms/inter, SIL Open Font License 1.1:
docs/licenses/Inter-OFL.txt), with the two axes optical size and weight. The made files of the repo come from the
file with the SHA-256 29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031.
The digits have one width (tabular): a number that changes does not move. Needs Pillow.
"""
import argparse
import os

from PIL import ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(HERE)
TOOL = 'libs/interfont/tools/export_inter.py'
LICENCE = '// Inter, Copyright 2020 The Inter Project Authors: SIL Open Font License 1.1 (docs/licenses/Inter-OFL.txt).'
WEIGHT = {'Regular': 400, 'Medium': 500, 'SemiBold': 600, 'Bold': 700}
DIGITS = list('0123456789')
ASCII = [chr(c) for c in range(32, 127)]

# CLOCK: (C name, size, weight, the characters; None = ASCII + CK_EXTRA). The optical size is the size.
CK_FONTS = [
    ('CK_XL', 92, 'SemiBold', '0123456789:-'),  # the time on the 3.49
    ('CK_L', 66, 'SemiBold', '0123456789:-'),  # the time on the 320 x 240 frame, the time of EDIT and RING
    ('CK_M', 28, 'SemiBold', None),  # the seconds, the times of the lists
    ('CK_S', 15, 'Medium', None),  # AM / PM, the small times, UTC+2
]
# the bytes over 127: 0x96 the minus sign (U+2212), 0xB7 the middle dot, 0xC4 the capital delta (a delta marker)
CK_EXTRA = {0x96: '−', 0xB7: '·', 0xC4: 'Δ'}

# STOCKS: (C name, size, weight, the optical size, the characters)
PRICE = DIGITS + list('.,-+ ')
UPPER = [chr(c) for c in range(32, 96)]
SF_FONTS = [
    ('SF_P80', 80, 'SemiBold', 32, PRICE),  # the price on the 3.49
    ('SF_P60', 60, 'SemiBold', 32, PRICE),  # the price on the CoreS3 frame (and a long price on the 3.49)
    ('SF_N34', 34, 'SemiBold', 28, UPPER),  # the countdown, the values of the alert editor
    ('SF_N26', 26, 'Bold', 24, UPPER),  # the symbol, the headings
    ('SF_T19', 19, 'SemiBold', 18, ASCII),  # the change, the buttons
    ('SF_T16', 16, 'Medium', 16, ASCII),  # the name of the company
    ('SF_T13', 13, 'Medium', 14, ASCII),  # the labels, the line at the bottom
]

# STANDBY: (C name, size, weight, the characters). The byte 0xB0 is the degree sign (as in Latin-1).
SB_VALUE = '0123456789:-+% \xb0'
SB_FONTS = [
    ('SB_T', 96, 'SemiBold', '0123456789:-'),  # the big time of each face
    ('SB_V', 44, 'SemiBold', SB_VALUE),  # the values of the slots, the temperature of LIVE SKY
    ('SB_S', 26, 'SemiBold', SB_VALUE),  # the small values (the hours of the weather)
]


def a4_pack(vals):
    b = bytearray()
    for i in range(0, len(vals), 2):
        lo = vals[i] >> 4
        hi = (vals[i + 1] >> 4) if i + 1 < len(vals) else 0
        b.append(lo | (hi << 4))
    return b


def face(ttf, size, weight, opsz):
    f = ImageFont.truetype(ttf, size, layout_engine=ImageFont.Layout.BASIC)
    axes = f.get_variation_axes()
    if len(axes) == 2:  # (the optical size and the weight)
        f.set_variation_by_axes([max(axes[0]['minimum'], min(axes[0]['maximum'], opsz)), WEIGHT[weight]])
    else:
        f.set_variation_by_axes([max(axes[0]['minimum'], WEIGHT[weight])])
    return f


def atlas(f, chars, space_half=False):
    """(glyphs, data): the glyphs in the order of the code (mf_glyph does a binary search)."""
    asc, _ = f.getmetrics()
    tab = max(f.getlength(d) for d in DIGITS)
    glyphs, data = [], bytearray()
    for code, ch in sorted(chars):
        mask, (ox, oy) = f.getmask2(ch, mode='L')
        w, h = mask.size
        adv = f.getlength(ch)
        if ch in DIGITS:
            ox += int(round((tab - adv) / 2))
            adv = tab
        if ch == ' ' and space_half:
            adv = tab / 2
        vals = list(mask) if w * h else []
        off = len(data)
        data += a4_pack(vals)
        a4 = int(round(adv * 4))
        assert a4 < 256 and w < 256 and h < 256 and -128 <= oy - asc < 128, (ch, a4, w, oy - asc)
        glyphs.append((code, a4, ox, oy - asc, w, h, off))
    return glyphs, data


def clock_file(ttf):
    out = ['// The faces of CLOCK (made by %s; do not edit).' % TOOL, LICENCE,
           '// The bytes over 127: 0x96 the minus sign, 0xB7 the middle dot, 0xC4 the capital delta.']
    for cname, size, weight, only in CK_FONTS:
        f = face(ttf, size, weight, size)
        asc, desc = f.getmetrics()
        chars = [(ord(c), c) for c in (only or ASCII)] + ([] if only else sorted(CK_EXTRA.items()))
        glyphs, data = atlas(f, chars)
        capb = f.getbbox('H')
        capital = asc - capb[1] if capb else asc
        line = int(round((asc + desc) * 1.12))
        out.append('static const uint8_t %s_A4[%d] = {' % (cname, len(data)))
        for i in range(0, len(data), 32):
            out.append('    ' + ','.join(str(b) for b in data[i:i + 32]) + ',')
        out.append('};')
        out.append('static const mglyph_t %s_G[%d] = {' % (cname, len(glyphs)))
        for g in glyphs:
            out.append('    {%d, %d, %d, %d, %d, %d, %d},' % g)
        out.append('};')
        out.append('static const mfont_t %s = {%d, %d, %d, %s_G, %s_A4};' % (cname, min(255, capital), min(255, line),
                                                                         len(glyphs), cname, cname))
    return '\n'.join(out) + '\n'


def stocks_file(ttf):
    out = ['// The fonts of STOCKS (made by %s; do not edit).' % TOOL, LICENCE]
    for cname, size, weight, opsz, chars in SF_FONTS:
        f = face(ttf, size, weight, opsz)
        asc, desc = f.getmetrics()
        glyphs, data = atlas(f, [(ord(c), c) for c in chars], space_half=chars is PRICE)
        out.append('static const uint8_t %s_A4[%d] = {' % (cname, len(data)))
        for k in range(0, len(data), 40):
            out.append(','.join(str(x) for x in data[k:k + 40]) + ',')
        out.append('};')
        out.append('static const mglyph_t %s_G[%d] = {' % (cname, len(glyphs)))
        for g in glyphs:
            out.append('    {%d, %d, %d, %d, %d, %d, %d},' % g)
        out.append('};')
        out.append('static const mfont_t %s = {%d, %d, %d, %s_G, %s_A4};' % (cname, asc, asc + desc, len(glyphs),
                                                                         cname, cname))
    return '\n'.join(out) + '\n'


def standby_file(ttf):
    out = ['// The faces of STANDBY (made by %s; do not edit).' % TOOL, LICENCE,
           '// The byte 0xB0 is the degree sign.']
    for cname, size, weight, chars in SB_FONTS:
        f = face(ttf, size, weight, size)
        asc, desc = f.getmetrics()
        glyphs, data = atlas(f, [(ord(c), c) for c in chars], space_half=True)
        capb = f.getbbox('0')
        capital = asc - capb[1] if capb else asc
        out.append('static const uint8_t %s_A4[%d] = {' % (cname, len(data)))
        for k in range(0, len(data), 40):
            out.append(','.join(str(x) for x in data[k:k + 40]) + ',')
        out.append('};')
        out.append('static const mglyph_t %s_G[%d] = {' % (cname, len(glyphs)))
        for g in glyphs:
            out.append('    {%d, %d, %d, %d, %d, %d, %d},' % g)
        out.append('};')
        out.append('static const mfont_t %s = {%d, %d, %d, %s_G, %s_A4};' % (cname, min(255, capital),
                                                                         int(round((asc + desc) * 1.1)),
                                                                         len(glyphs), cname, cname))
    return '\n'.join(out) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('ttf', help='the variable font file of Inter')
    p.add_argument('--out', default=LIB, help='the folder of the made files (default: libs/interfont/)')
    a = p.parse_args()
    for name, text in (('ck_fonts.inc', clock_file(a.ttf)), ('stocks_font.inc', stocks_file(a.ttf)),
                       ('sb_fonts.inc', standby_file(a.ttf))):
        with open(os.path.join(a.out, name), 'wb') as h:
            h.write(text.encode('ascii'))
        print('%s: %d bytes' % (name, len(text)))


if __name__ == '__main__':
    main()
