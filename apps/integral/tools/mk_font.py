"""INTEGRALS: the math faces as 4-bit alpha atlases (mfont_t of tfn/mfont.h) and the tall glyphs.

The tool writes apps/integral/ig_font.c and apps/integral/ig_font.h:
  IGF_R[5]   the upright face (digits, signs, function names, the capital Greek letters, the math signs)
  IGF_I[5]   the italic face (the variables: a-z, A-Z, the small Greek letters)
  IG_TALL    the tall glyphs: ( ) [ ] { } | at many heights (the brackets that grow) and the integral sign and the
             sum sign at the sizes of the display style

The 5 levels have the em sizes of IGF_EMS (px). Level 0 is the display size of the 3.49; a script goes 2 levels down.

The font is STIX (the STIX fonts of matplotlib: STIXGeneral, STIXGeneralItalic, STIXSizOneSymReg ...; SIL Open Font
License 1.1: docs/licenses/STIX-OFL.txt). Usage:
  python apps/integral/tools/mk_font.py [<the folder of the STIX .ttf files>]
With no folder, the tool takes the folder of the matplotlib package. Needs Pillow and matplotlib (for the path only).
"""
import hashlib
import os
import sys

from PIL import Image, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
TOOL = 'apps/integral/tools/mk_font.py'
IGF_EMS = [30, 24, 19, 15, 12]

R_CHARS = ([chr(c) for c in range(32, 127)] + [chr(c) for c in (0x393, 0x394, 0x398, 0x39B, 0x39E, 0x3A0, 0x3A3,
                                                                  0x3A5, 0x3A6, 0x3A8, 0x3A9)] +
           list('∞∑∏√≤≥≠≈≡→⇒±⋅×′″∂−…⋯∘∈∫←↦'))
I_CHARS = ([chr(c) for c in range(ord('a'), ord('z') + 1)] + [chr(c) for c in range(ord('A'), ord('Z') + 1)] +
           [chr(c) for c in range(0x3B1, 0x3CA)] + ['ϑ', 'ϕ', 'ϵ'])

# the tall glyphs: kind -> the character; the brackets come from the size variants of STIX, the signs from STIXGeneral
TALL = ['(', ')', '[', ']', '{', '}', '|', '∫', '∑']
BRACKET_H = list(range(20, 161, 6))
INT_H = list(range(26, 85, 4))
SUM_H = list(range(26, 61, 6))
VARIANTS = ['STIXGeneral.ttf', 'STIXSizOneSymReg.ttf', 'STIXSizTwoSymReg.ttf', 'STIXSizThreeSymReg.ttf',
            'STIXSizFourSymReg.ttf']


def a4_pack(vals):
    b = bytearray()
    for i in range(0, len(vals), 2):
        lo = vals[i] >> 4
        hi = (vals[i + 1] >> 4) if i + 1 < len(vals) else 0
        b.append(lo | (hi << 4))
    return b


def atlas(f, chars):
    asc, _ = f.getmetrics()
    glyphs, data = [], bytearray()
    for ch in sorted(set(chars), key=ord):
        mask, (ox, oy) = f.getmask2(ch, mode='L')
        w, h = mask.size
        adv = f.getlength(ch)
        vals = list(mask) if w * h else []
        off = len(data)
        data += a4_pack(vals)
        a4 = int(round(adv * 4))
        assert a4 < 256 and w < 256 and h < 256 and -128 <= oy - asc < 128, (ch, a4, w, oy - asc)
        glyphs.append((ord(ch), a4, ox, oy - asc, w, h, off))
    return glyphs, data


def ink(f, ch):
    """The ink of one glyph: (the mask as an L picture, the top of the ink over the baseline)."""
    asc, _ = f.getmetrics()
    mask, (ox, oy) = f.getmask2(ch, mode='L')
    im = Image.frombytes('L', mask.size, bytes(mask)) if mask.size[0] * mask.size[1] else None
    bb = im.getbbox() if im else None
    if not bb:
        return None, 0
    return im.crop(bb), asc - (oy + bb[1])


def tall_glyphs(ttf_dir):
    """[(kind, h, w, the ink top over the axis, data)]: each glyph is cut to its ink and made exactly h px high."""
    big = 220
    out = []
    for kind, ch in enumerate(TALL):
        if ch in '∫∑':
            src = 'STIXGeneral.ttf' if ch == '∫' else 'STIXSizOneSymReg.ttf'
            im, _ = ink(ImageFont.truetype(os.path.join(ttf_dir, src), big), ch)
            for h in (INT_H if ch == '∫' else SUM_H):
                w = max(3, int(round(im.size[0] * h / im.size[1])))
                out.append((kind, h, w, im.resize((w, h), Image.LANCZOS)))
            continue
        cands = []
        for v in VARIANTS:
            f = ImageFont.truetype(os.path.join(ttf_dir, v), 24)
            im24, _ = ink(f, ch)
            if im24 is not None:
                cands.append((im24.size[1], v))
        for h in BRACKET_H:
            # the variant whose height at the em 24 is nearest to h: the scale stays near 1, the stroke stays right
            hv, v = min(cands, key=lambda c: abs(c[0] - h))
            k = big / 24.0
            im, _ = ink(ImageFont.truetype(os.path.join(ttf_dir, v), int(24 * k)), ch)
            w0 = im.size[0] * h / im.size[1]
            w = max(2, int(round(im.size[0] / k * (h / hv) ** 0.25 if hv else w0)))
            out.append((kind, h, w, im.resize((w, h), Image.LANCZOS)))
    return out


def c_bytes(name, data):
    lines = ['static const uint8_t %s[%d] = {' % (name, len(data))]
    for i in range(0, len(data), 40):
        lines.append(','.join(str(b) for b in data[i:i + 40]) + ',')
    lines.append('};')
    return lines


def main():
    if len(sys.argv) > 1:
        ttf_dir = sys.argv[1]
    else:
        import matplotlib
        ttf_dir = os.path.join(os.path.dirname(matplotlib.__file__), 'mpl-data', 'fonts', 'ttf')
    reg = os.path.join(ttf_dir, 'STIXGeneral.ttf')
    ita = os.path.join(ttf_dir, 'STIXGeneralItalic.ttf')
    sha = hashlib.sha256(open(reg, 'rb').read()).hexdigest()[:16]
    c = ['// The math faces of INTEGRALS (made by %s; do not edit).' % TOOL,
         '// STIX fonts (STIXGeneral %s...): SIL Open Font License 1.1 (docs/licenses/STIX-OFL.txt).' % sha,
         '#include "ig_font.h"', '']
    metrics = []
    for face, path, chars in (('R', reg, R_CHARS), ('I', ita, I_CHARS)):
        for lv, em in enumerate(IGF_EMS):
            f = ImageFont.truetype(path, em)
            asc, desc = f.getmetrics()
            glyphs, data = atlas(f, chars)
            nm = 'IGF_%s%d' % (face, lv)
            c += c_bytes(nm + '_A4', data)
            c.append('static const mglyph_t %s_G[%d] = {' % (nm, len(glyphs)))
            c += ['    {%d, %d, %d, %d, %d, %d, %d},' % g for g in glyphs]
            c.append('};')
            if face == 'R':
                metrics.append((asc, asc + desc))
    c.append('const mfont_t IGF_R[5] = {')
    c += ['    {%d, %d, %d, IGF_R%d_G, IGF_R%d_A4},' % (metrics[lv][0], metrics[lv][1], len(set(R_CHARS)), lv, lv)
          for lv in range(5)]
    c.append('};')
    c.append('const mfont_t IGF_I[5] = {')
    c += ['    {%d, %d, %d, IGF_I%d_G, IGF_I%d_A4},' % (metrics[lv][0], metrics[lv][1], len(set(I_CHARS)), lv, lv)
          for lv in range(5)]
    c.append('};')
    tall = tall_glyphs(ttf_dir)
    data = bytearray()
    rows = []
    for kind, h, w, im in tall:
        off = len(data)
        data += a4_pack(list(im.getdata()))
        rows.append('    {%d, %d, %d, %d},' % (kind, h, w, off))
    c += c_bytes('IG_TALL_A4', data)
    c.append('const ig_tall_t IG_TALL[%d] = {' % len(tall))
    c += rows
    c.append('};')
    c.append('const uint8_t *const IG_TALL_DATA = IG_TALL_A4;')
    h = ['// The math faces of INTEGRALS (made by %s; do not edit).' % TOOL, '#pragma once', '#include <stdint.h>',
         '#include "tfn/mfont.h"', '',
         '// the em of each level (px): level 0 = the display size of the 3.49, a script goes 2 levels down',
         '#define IGF_EM_LIST {%s}' % ', '.join(str(e) for e in IGF_EMS), '#define IGF_LEVELS 5',
         '// the kinds of the tall glyphs', 'enum { IGT_LP, IGT_RP, IGT_LB, IGT_RB, IGT_LC, IGT_RC, IGT_BAR, IGT_INT, '
         'IGT_SUM, IGT_KINDS };',
         'typedef struct {', '    uint8_t kind, h, w;', '    uint32_t off;', '} ig_tall_t;',
         '#define IG_TALL_N %d' % len(tall),
         'extern const mfont_t IGF_R[5], IGF_I[5];', 'extern const ig_tall_t IG_TALL[IG_TALL_N];',
         'extern const uint8_t *const IG_TALL_DATA;', '']
    for name, text in (('ig_font.c', '\n'.join(c) + '\n'), ('ig_font.h', '\n'.join(h))):
        with open(os.path.join(APP, name), 'wb') as fh:
            fh.write(text.encode('utf-8'))
        print('%s: %d bytes' % (name, len(text)))
    print('tall glyphs: %d, %d bytes' % (len(tall), len(data)))


if __name__ == '__main__':
    main()
