# FOUR SEASONS: the baker. It draws each scene in the four seasons and in each state of its things (the VARS of a scene
# module), and writes the files of the data pack and the geometry for the C code:
#   <out>/sNN.fsn       a scene: per season a palette (256 colours) and the base picture (1 byte a pixel, zlib), and the
#                       layers (a part of the picture for one value of one thing: its pixels that change, 0 = no change)
#   apps/four_seasons/fs_geo.inc   the scenes, their sizes, the hot spots, the anchors and the things (generated)
#   python bake.py [scene ...] [--out DIR] [--check]
# The default out folder is tools/out/seasons (the copy of /sdcard/seasons on the PC).
import importlib, os, struct, sys, time, zlib
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.normpath(os.path.join(HERE, '..'))
OUT = os.path.normpath(os.path.join(HERE, 'out', 'seasons'))
sys.path.insert(0, HERE)

# the scenes in their order (the scene id = the index); the wide views first
SCENES = ['valley', 'farm', 'pond', 'tree', 'swing', 'vines', 'shed', 'shore', 'island', 'attic', 'well', 'hive',
          'mailbox', 'garden']
PAL_MAX = 248                # indices 1..247 are colours; 0 = no pixel (a layer); 248..255 = the animated colours
FILE_VER = 1


def quantize(pixels, nmax):
    """pixels (N, 3) -> a palette (K, 3) of K <= nmax colours, exact when the picture has few colours"""
    u, inv, cnt = np.unique(pixels, axis=0, return_inverse=True, return_counts=True)
    if len(u) <= nmax:
        return u.astype(np.uint8)
    im = Image.fromarray(pixels.reshape(1, -1, 3).astype(np.uint8), 'RGB')
    q = im.quantize(colors=nmax, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette()[:nmax * 3], np.uint8).reshape(-1, 3)
    used = np.unique(np.array(q).reshape(-1))
    return pal[used]


def nearest(rgb, pal):
    """the index (in pal) of the nearest colour of each pixel of rgb (H, W, 3)"""
    flat = rgb.reshape(-1, 3).astype(np.int32)
    u, inv = np.unique(flat, axis=0, return_inverse=True)
    pp = pal.astype(np.int32)
    d = ((u[:, None, :] - pp[None, :, :]) ** 2).sum(axis=2)
    best = d.argmin(axis=1)
    return best[inv.reshape(-1)].reshape(rgb.shape[:2])


def bake_scene(sid, name, out, check):
    mod = importlib.import_module('sc_' + name)
    W, H = mod.W, mod.H
    vars_ = getattr(mod, 'VARS', {})
    base_st = {k: v[0] for k, v in vars_.items()}
    seasons = []
    t0 = time.time()
    worst = 0.0
    for s in range(4):
        base = mod.draw(s, dict(base_st)).rgb
        layers = []
        for vi, (k, vals) in enumerate(vars_.items()):
            for val in vals[1:]:
                st = dict(base_st)
                st[k] = val
                img = mod.draw(s, st).rgb
                diff = np.any(img != base, axis=2)
                if not diff.any():
                    layers.append((vi, val, None))
                    continue
                ys, xs = np.nonzero(diff)
                x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
                layers.append((vi, val, (x0, y0, img[y0:y1, x0:x1].copy(), diff[y0:y1, x0:x1].copy())))
        # the palette of the season: the base and all its layers
        pix = [base.reshape(-1, 3)]
        for (vi, val, L) in layers:
            if L is not None:
                pix.append(L[2][L[3]].reshape(-1, 3))
        allp = np.concatenate(pix)
        palc = quantize(allp, PAL_MAX - 1)
        idx = nearest(base, palc) + 1
        # the error of the palette (when the season has too many colours)
        recon = palc[idx - 1]
        err = np.abs(recon.astype(int) - base.astype(int)).max()
        worst = max(worst, err)
        lay_out = []
        for (vi, val, L) in layers:
            if L is None:
                lay_out.append((vi, val, None))
                continue
            x0, y0, img, dm = L
            li = nearest(img, palc) + 1
            li[~dm] = 0
            lay_out.append((vi, val, (x0, y0, li.astype(np.uint8))))
        seasons.append((palc, idx.astype(np.uint8), lay_out))
        # the check: each pair of things with values that are not the base, composed, is the same as the drawing
        if check and len(vars_) > 1:
            keys = list(vars_.keys())
            for a in range(len(keys)):
                for b in range(a + 1, len(keys)):
                    for va in vars_[keys[a]][1:]:
                        for vb in vars_[keys[b]][1:]:
                            st = dict(base_st)
                            st[keys[a]] = va
                            st[keys[b]] = vb
                            ref = nearest(mod.draw(s, st).rgb, palc) + 1
                            comp = idx.copy()
                            for (vi, val, L) in lay_out:
                                if L is None:
                                    continue
                                if (keys[vi] == keys[a] and val == va) or (keys[vi] == keys[b] and val == vb):
                                    x0, y0, li = L
                                    sub = comp[y0:y0 + li.shape[0], x0:x0 + li.shape[1]]
                                    sub[li > 0] = li[li > 0]
                            bad = int((ref != comp).sum())
                            if bad:
                                print('  CHECK %s season %d: %s=%d with %s=%d: %d pixels differ' % (name, s, keys[a], va, keys[b], vb, bad))
    # the file
    blobs = []
    hdr = bytearray()
    nlayer = sum(len([l for l in sz[2]]) for sz in seasons)
    head_size = 20 + 4 * 24 + nlayer * 24
    off = head_size
    season_recs = []
    layer_recs = []
    for s, (palc, idx, lay) in enumerate(seasons):
        pal = np.zeros((256, 3), np.uint8)
        pal[1:1 + len(palc)] = palc
        pb = pal.tobytes()
        zb = zlib.compress(idx.tobytes(), 9)
        season_recs.append((off, off + len(pb), len(zb), len(palc)))
        blobs.append(pb)
        blobs.append(zb)
        off += len(pb) + len(zb)
        for (vi, val, L) in lay:
            if L is None:
                layer_recs.append((s, vi, val, 0, 0, 0, 0, 0, 0, 0))
                continue
            x0, y0, li = L
            z = zlib.compress(li.tobytes(), 9)
            layer_recs.append((s, vi, val, 1, x0, y0, li.shape[1], li.shape[0], off, len(z)))
            blobs.append(z)
            off += len(z)
    hdr += b'FSN1' + struct.pack('<HHHHHHI', FILE_VER, sid, W, H, len(vars_), len(layer_recs), 0)
    for (po, bo, bz, npal) in season_recs:
        hdr += struct.pack('<IIIIII', po, bo, bz, npal, 0, 0)
    for r in layer_recs:
        hdr += struct.pack('<BBBBHHHHIII', r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], 0)
    assert len(hdr) == head_size, (len(hdr), head_size)
    data = bytes(hdr) + b''.join(blobs)
    fn = os.path.join(out, 's%02d.fsn' % sid)
    with open(fn, 'wb') as f:
        f.write(data)
    print('%-8s %3dx%3d  %2d layers  %7d bytes  palette %s  err %d  %.1f s' % (
        name, W, H, len(layer_recs), len(data), '/'.join(str(len(sz[0])) for sz in seasons), worst, time.time() - t0))
    return mod


def c_name(s):
    return ''.join(ch if ch.isalnum() else '_' for ch in s).upper()


def write_geo(mods):
    L = ['// FOUR SEASONS: the geometry of the scenes (MADE by tools/bake.py from the sc_*.py files; do not edit).', '']
    L.append('enum {')
    for i, (name, mod) in enumerate(mods):
        L.append('    SC_%s,' % c_name(name))
    L.append('    SC_N')
    L.append('};')
    # the hot spots: one enum over all scenes
    L.append('enum {')
    for name, mod in mods:
        for k in getattr(mod, 'HOT', {}):
            L.append('    HS_%s_%s,' % (c_name(name), c_name(k)))
    L.append('    HS_N')
    L.append('};')
    L.append('typedef struct { uint8_t scene; int16_t x, y, w, h; } hot_t;')
    L.append('static const hot_t HOT[HS_N] = {')
    for name, mod in mods:
        for k, (x, y, w, h) in getattr(mod, 'HOT', {}).items():
            L.append('    {SC_%s, %d, %d, %d, %d},   // %s' % (c_name(name), x, y, w, h, k))
    L.append('};')
    # the anchors (points of the live things)
    L.append('enum {')
    for name, mod in mods:
        for k in getattr(mod, 'ANCH', {}):
            L.append('    AN_%s_%s,' % (c_name(name), c_name(k)))
    L.append('    AN_N')
    L.append('};')
    L.append('static const int16_t ANCH[AN_N + 1][2] = {')
    for name, mod in mods:
        for k, (x, y) in getattr(mod, 'ANCH', {}).items():
            L.append('    {%d, %d},   // %s %s' % (x, y, name, k))
    L.append('    {0, 0}')
    L.append('};')
    # the things (vars) of each scene: their index in the file
    for name, mod in mods:
        for vi, k in enumerate(getattr(mod, 'VARS', {})):
            L.append('#define V_%s_%s %d' % (c_name(name), c_name(k), vi))
    L.append('typedef struct { const char *name; int16_t w, h; uint8_t nvar; } scene_t;')
    L.append('static const scene_t SCENE[SC_N] = {')
    for name, mod in mods:
        L.append('    {"%s", %d, %d, %d},' % (name, mod.W, mod.H, len(getattr(mod, 'VARS', {}))))
    L.append('};')
    path = os.path.join(APP, 'fs_geo.inc')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('wrote', os.path.relpath(path, APP))


def main():
    args = sys.argv[1:]
    out = OUT
    check = '--check' in args
    if '--out' in args:
        out = args[args.index('--out') + 1]
    names = [a for a in args if not a.startswith('--') and a != out]
    os.makedirs(out, exist_ok=True)
    have = [n for n in SCENES if os.path.exists(os.path.join(HERE, 'sc_%s.py' % n))]
    todo = [n for n in have if not names or n in names]
    mods = []
    for n in have:
        if n in todo:
            mods.append((n, bake_scene(SCENES.index(n), n, out, check)))
        else:
            mods.append((n, importlib.import_module('sc_' + n)))
    # the geometry needs all the scenes in their order (the ids are the indices of SCENES)
    if have == SCENES[:len(have)]:
        write_geo(mods)
    else:
        print('(no fs_geo.inc: the scenes are not complete: %s)' % ', '.join(n for n in SCENES if n not in have))


if __name__ == '__main__':
    main()
