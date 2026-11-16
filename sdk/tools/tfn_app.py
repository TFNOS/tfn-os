# TFN OS SDK: build an app in its own folder, with the SDK package only (no file of the TFN OS repo).
#
#   python tfn_app.py build <app folder> [--kinds lcd349,cores3] [--sdk <package folder>] [--out <folder>]
#                     [--idf <ESP-IDF folder>] [--icon <png>] [--ver 1.0.0] [--sign-key <pem>]
#                     [--dev-key <pem> [--dev-name NAME]]
#                     [--push <board ip> [--card] [--open] [--password ...]] [--list-includes]
#
# The app folder has app.json (sdk/docs/app_json.md) and the sources. For each kind the tool:
#   1. compiles the sources with the options of kinds/<kind>.flags of the package (the ESP-IDF headers come from
#      $IDF_PATH, all other headers from the package), and host/elf_entry.c of the package;
#   2. links app.elf with the link script of the package, strips it and checks it (the same steps as mk_app.py);
#   3. checks each name that app.elf needs against tfn_abi.json of the package;
#   4. reads the app_t flags (landscape, round ...) from app.elf, makes icon.bin from a PNG file (--icon, or "icon"
#      of app.json) or a plain tile with the first letter of the name, and writes manifest.txt.
# The package goes to <out>/<id>/<kind>/ (manifest.txt, app.elf, icon.bin, sig, watch.elf, sigs): the format of
# mk_app.py.
# --dev-key <pem> [--dev-name NAME]: a developer signs with an own P-256 key, as mk_app.py: publisher=<key ID> in
#   manifest.txt, the file "sigs" (TFNSIG2 lines) and <out>/<id>/publisher.txt (the key file for SETUP > TRUSTED
#   PUBLISHERS of a board, main/tfn_pub.h). Only a board that trusts the key as TEST runs the app with no store
#   signature; the other boards show NOT REVIEWED until the store signs the same manifest.txt (sig = the countersign).
# The tool finds the compiler of the kind in $IDF_TOOLS_PATH (or ~/.espressif), else on PATH, at the version of the
# package. ESP-IDF: --idf, else $IDF_PATH; its version must be the version of the package.
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tfn_pkg  # noqa: E402
from tfn_pkg import die  # noqa: E402

GROUPS = ('GAMES', 'UTIL')
APP_KEYS = {'tfn_app', 'id', 'name', 'sym', 'group', 'emoji', 'version', 'srcs', 'defines', 'link', 'watch', 'new',
            'form', 'form_help', 'oauth', 'class', 'kinds', 'idf', 'store', 'icon', 'abi_rev', 'about'}


def norm(p):
    return os.path.abspath(p).replace('\\', '/')


def default_sdk():
    if os.environ.get('TFN_SDK'):
        return os.environ['TFN_SDK']
    up = os.path.dirname(HERE)
    return up if os.path.exists(os.path.join(up, 'sdk.json')) else ''


def read_flags(path):
    meta, sec, cur = {}, {}, None
    for line in open(path, encoding='utf-8').read().splitlines():
        if not line or line.startswith('#'):
            continue
        m = re.match(r'\[(\w+)\]$', line)
        if m:
            cur = sec.setdefault(m.group(1), [])
        elif cur is None:
            k, v = line.split('=', 1)
            meta[k] = v
        else:
            cur.append(line)
    return meta, sec


def idf_version(idf):
    p = os.path.join(idf, 'tools', 'cmake', 'version.cmake')
    if not os.path.exists(p):
        return ''
    t = open(p, encoding='utf-8').read()
    v = [re.search(r'IDF_VERSION_%s\s+(\d+)' % k, t) for k in ('MAJOR', 'MINOR')]
    return 'v%s.%s' % (v[0].group(1), v[1].group(1)) if all(v) else ''


def find_compiler(meta, toolbin):
    exe = meta['compiler'] + ('.exe' if os.name == 'nt' else '')
    tried = []
    roots = [os.environ.get('IDF_TOOLS_PATH', ''), os.path.join(os.path.expanduser('~'), '.espressif')]
    if os.name == 'nt':
        roots.append('C:/Espressif')
    cands = [os.path.join(toolbin, exe)] if toolbin else []
    cands += [os.path.join(r, 'tools', meta['toolchain'], meta['toolchain_version'], meta['toolchain'], 'bin', exe)
              for r in roots if r]
    w = shutil.which(meta['compiler'])
    if w:
        cands.append(w)
    for c in cands:
        if not os.path.exists(c):
            continue
        tried.append(c)
        r = subprocess.run([c, '--version'], capture_output=True, text=True)
        if meta['toolchain_version'] in r.stdout:
            return c.replace('\\', '/')
    die('no %s of the version %s (tried: %s): install the ESP-IDF tools of %s, or give --toolchain <bin folder>'
        % (meta['compiler'], meta['toolchain_version'], ', '.join(tried) or 'none found', meta.get('idf', '?')))


def load_app(folder, sdk_kinds):
    p = os.path.join(folder, 'app.json')
    if not os.path.exists(p):
        die('no app.json in %s (sdk/docs/app_json.md)' % folder)
    j = json.load(open(p, encoding='utf-8'))
    bad = sorted(set(j) - APP_KEYS)
    if bad:
        die('app.json: unknown keys: %s (sdk/docs/app_json.md)' % ', '.join(bad))
    for k in ('name', 'sym', 'srcs'):
        if not j.get(k):
            die('app.json: no "%s"' % k)
    if not re.match(r'app_[a-z0-9_]+$', j['sym']):
        die('app.json: "sym" must be app_<id> (small letters, digits, _): %s' % j['sym'])
    pid = j['sym'][4:]
    if j.get('id', pid) != pid:
        die('app.json: "id" %s is not the "sym" without app_ (%s)' % (j['id'], pid))
    if j.get('group', 'GAMES') not in GROUPS:
        die('app.json: "group" must be one of %s' % ', '.join(GROUPS))
    if j.get('idf', 'min') not in ('min', 'all'):
        die('app.json: "idf" must be "min" or "all"')
    kinds = j.get('kinds') or list(sdk_kinds)
    for k in kinds:
        if k not in sdk_kinds:
            die('app.json: the kind %s is not in the SDK package (it has %s)' % (k, ', '.join(sdk_kinds)))

    def srcs(lst, what):
        out = []
        for s in lst:
            q = os.path.normpath(os.path.join(folder, s))
            if not os.path.exists(q):
                die('app.json: %s %s does not exist' % (what, s))
            if os.path.relpath(q, folder).startswith('..'):
                print('NOTE: %s %s is out of the app folder' % (what, s))
            out.append(q)
        return out
    a = {k: j[k] for k in ('name', 'sym', 'group', 'emoji', 'new', 'form', 'form_help', 'oauth') if k in j}
    a['srcs'] = srcs(j['srcs'], 'the source')
    a['link'] = srcs(j.get('link', []), 'the library')
    if j.get('watch'):
        w = j['watch']
        if not (w.get('srcs') and w.get('sym')):
            die('app.json: "watch" needs "srcs" and "sym"')
        a['watch'] = {'srcs': srcs(w['srcs'], 'the watch source'), 'sym': w['sym'], 'every': int(w.get('every', 15))}
    return j, a, pid, kinds


def elf_app_t(path, sym, layout):
    """The app_t flags from the copy of app.elf with its names: the bytes of the symbol sym (32-bit little-endian
    ELF, the layout of sdk.json "app_t")."""
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', d, 32)
    shnum, = struct.unpack_from('<H', d, 48)
    sh = [struct.unpack_from('<10I', d, shoff + 40 * i) for i in range(shnum)]
    for s in sh:
        if s[1] != 2:                               # SHT_SYMTAB
            continue
        strtab = sh[s[6]]
        for i in range(s[5] // 16):
            nm, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, s[4] + 16 * i)
            e = d.index(b'\0', strtab[4] + nm)
            if d[strtab[4] + nm:e].decode() != sym:
                continue
            sec = sh[shndx]
            if sec[1] == 8:                         # SHT_NOBITS
                die('%s: the app_t %s is not const data' % (path, sym))
            b = d[sec[4] + val - sec[3]:sec[4] + val - sec[3] + size]
            return {k: str(b[o]) for k, o in layout.items()}
    die('%s: no app_t %s in app.elf (is "sym" of app.json the name of the app_t?)' % (path, sym))


# ---------------------------------------------------------------- the icon
def png_read(path):
    """RGBA rows of a PNG file (8 bits a channel; gray, RGB, palette, with or without alpha; no interlace)."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        die('%s is not a PNG file' % path)
    i, idat, pal, trns = 8, b'', None, None
    while i < len(d):
        n, t = struct.unpack_from('>I4s', d, i)
        c = d[i + 8:i + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype, _, _, lace = struct.unpack('>IIBBBBB', c)
        elif t == b'PLTE':
            pal = [tuple(c[k:k + 3]) for k in range(0, len(c), 3)]
        elif t == b'tRNS':
            trns = c
        elif t == b'IDAT':
            idat += c
        i += 12 + n
    if depth != 8 or lace:
        die('%s: only 8-bit PNG files with no interlace' % path)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    rows, prev, k = [], bytearray(w * ch), 0
    for y in range(h):
        f = raw[k]
        line = bytearray(raw[k + 1:k + 1 + w * ch])
        k += 1 + w * ch
        for x in range(len(line)):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        px = []
        for x in range(w):
            v = line[x * ch:(x + 1) * ch]
            if ctype == 0:
                px.append((v[0], v[0], v[0], 255))
            elif ctype == 4:
                px.append((v[0], v[0], v[0], v[1]))
            elif ctype == 2:
                px.append((v[0], v[1], v[2], 255))
            elif ctype == 6:
                px.append(tuple(v))
            else:
                r, g, bb = pal[v[0]]
                px.append((r, g, bb, trns[v[0]] if trns and v[0] < len(trns) else 255))
        rows.append(px)
    return w, h, rows


def to_px(r, g, b):
    return tfn_pkg.swap16(((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255))


def icon_png(path):
    """The frames of the icon from a PNG file: one picture = a still icon; a column of square pictures (the height = 2
    to ICON_FRAMES times the width, the first frame at the top) = an animated icon. Each frame is scaled to
    ICON_SIZE x ICON_SIZE; a pixel with alpha < 128 is a hole."""
    w, h, rows = png_read(path)
    n = tfn_pkg.ICON_SIZE
    count = h // w if h % w == 0 and 2 <= h // w <= tfn_pkg.ICON_FRAMES else 1
    fh = h // count
    frames = []
    for f in range(count):
        fr = []
        for y in range(n):
            for x in range(n):
                r, g, b, a = rows[f * fh + y * fh // n][x * w // n]
                fr.append(to_px(r, g, b) if a >= 128 else None)
        frames.append(fr)
    return frames


# a 5 x 7 font for the plain icon (one row = 5 bits, the high bit at the left)
FONT = {
    'A': '0E 11 11 1F 11 11 11', 'B': '1E 11 11 1E 11 11 1E', 'C': '0E 11 10 10 10 11 0E', 'D': '1E 11 11 11 11 11 1E',
    'E': '1F 10 10 1E 10 10 1F', 'F': '1F 10 10 1E 10 10 10', 'G': '0E 11 10 17 11 11 0F', 'H': '11 11 11 1F 11 11 11',
    'I': '0E 04 04 04 04 04 0E', 'J': '07 02 02 02 02 12 0C', 'K': '11 12 14 18 14 12 11', 'L': '10 10 10 10 10 10 1F',
    'M': '11 1B 15 15 11 11 11', 'N': '11 11 19 15 13 11 11', 'O': '0E 11 11 11 11 11 0E', 'P': '1E 11 11 1E 10 10 10',
    'Q': '0E 11 11 11 15 12 0D', 'R': '1E 11 11 1E 14 12 11', 'S': '0F 10 10 0E 01 01 1E', 'T': '1F 04 04 04 04 04 04',
    'U': '11 11 11 11 11 11 0E', 'V': '11 11 11 11 11 0A 04', 'W': '11 11 11 15 15 15 0A', 'X': '11 11 0A 04 0A 11 11',
    'Y': '11 11 0A 04 04 04 04', 'Z': '1F 01 02 04 08 10 1F', '0': '0E 11 13 15 19 11 0E', '1': '04 0C 04 04 04 04 0E',
    '2': '0E 11 01 02 04 08 1F', '3': '1F 02 04 02 01 11 0E', '4': '02 06 0A 12 1F 02 02', '5': '1F 10 1E 01 01 11 0E',
    '6': '06 08 10 1E 11 11 0E', '7': '1F 01 02 04 08 08 08', '8': '0E 11 11 0E 11 11 0E', '9': '0E 11 11 0F 01 02 0C',
}


def icon_plain(name, pid):
    """A plain tile: a rounded square with a soft colour from the id, the first letter of the name in white."""
    n = tfn_pkg.ICON_SIZE
    hsh = sum((i + 1) * ord(c) for i, c in enumerate(pid))
    hue = (hsh % 360) / 60.0
    x6 = 1 - abs(hue % 2 - 1)
    base = [(1, x6, 0), (x6, 1, 0), (0, 1, x6), (0, x6, 1), (x6, 0, 1), (1, 0, x6)][int(hue) % 6]
    ch = next((c for c in name.upper() if c in FONT), '')
    glyph = [int(v, 16) for v in FONT[ch].split()] if ch else []
    s, r = 12, 22
    gx, gy = (n - 5 * s) // 2, (n - 7 * s) // 2
    fr = []
    for y in range(n):
        k = 0.95 - 0.45 * y / n
        col = to_px(*(int(255 * (0.25 + 0.75 * c) * k) for c in base))
        for x in range(n):
            cx, cy = min(max(x, r), n - 1 - r), min(max(y, r), n - 1 - r)
            if (x - cx) ** 2 + (y - cy) ** 2 > r * r:
                fr.append(None)
                continue
            gxx, gyy = (x - gx) // s, (y - gy) // s
            on = glyph and 0 <= gxx < 5 and 0 <= gyy < 7 and x >= gx and y >= gy and glyph[gyy] >> (4 - gxx) & 1
            fr.append(to_px(255, 255, 255) if on else col)
    return [fr]


# ---------------------------------------------------------------- the build
class Build:
    def __init__(self, args):
        self.args = args
        self.sdk = os.path.abspath(args.sdk or default_sdk() or die('no SDK package: give --sdk <folder> (or the env TFN_SDK)'))
        if not os.path.exists(os.path.join(self.sdk, 'sdk.json')):
            die('%s is not an SDK package (no sdk.json; make one with sdk/tools/sdk_pack.py)' % self.sdk)
        self.info = json.load(open(os.path.join(self.sdk, 'sdk.json')))
        self.abi = json.load(open(os.path.join(self.sdk, 'tfn_abi.json')))
        self.folder = os.path.abspath(args.folder)
        self.j, self.a, self.pid, kinds = load_app(self.folder, list(self.info['kinds']))
        self.kinds = args.kinds.split(',') if args.kinds else kinds
        for k in self.kinds:
            if k not in self.info['kinds']:
                die('the kind %s is not in the SDK package' % k)
        self.idf = os.path.abspath(args.idf or os.environ.get('IDF_PATH', '') or die('no ESP-IDF: give --idf <folder> (or the env IDF_PATH)'))
        self.outdir = os.path.abspath(args.out or os.path.join(self.folder, 'out'))
        self.includes = []
        self.kid = ''

    def subst(self, lst):
        return [x.replace('$SDK', norm(self.sdk)).replace('$IDF_PATH', norm(self.idf)) for x in lst]

    def compile(self, gcc, cflags, src, obj, cwd):
        cmd = [gcc] + cflags + (['-H'] if self.args.list_includes else []) + ['-c', src, '-o', obj]
        err = []
        tfn_pkg.run(cmd, cwd, stderr_to=err)
        if self.args.list_includes:
            self.includes += [norm(m.group(1)) for m in re.finditer(r'^\.+ (.+)$', err[0], re.M)]

    def elf(self, kind, gcc, cflags, out, srcs, entry, define, sym, entry_src, name, libs):
        pre = gcc[:-len('gcc.exe')] if gcc.endswith('gcc.exe') else gcc[:-len('gcc')]
        objs = []
        for s in srcs:
            o = os.path.join(out, os.path.splitext(os.path.basename(s))[0] + '.o')
            cc = gcc if s.endswith('.c') else pre + 'g++'
            self.compile(cc, cflags, norm(s), o, out)    # (/ in the path: -fmacro-prefix-map applies to __FILE__)
            objs.append(o)
        eo = os.path.join(out, '_entry.o')
        self.compile(gcc, cflags + ['-D%s=%s' % (define, sym)], os.path.join(self.sdk, 'host', entry_src), eo, out)
        riscv = 'riscv' in os.path.basename(pre)
        if riscv:
            ro = os.path.join(out, '_riscv_rt.o')
            self.compile(gcc, cflags, os.path.join(self.sdk, 'host', 'riscv_rt.c'), ro, out)
            objs.append(ro)
        ld = os.path.join(self.sdk, 'host', 'app_riscv.ld' if riscv else 'app.ld')
        elf = os.path.join(out, name)
        return tfn_pkg.link_app(gcc, pre, objs, eo, elf, entry, ld, libs, self.a['name'], out, elf + '.syms')

    def kind(self, kind, code):
        a, pid, args = self.a, self.pid, self.args
        meta, sec = read_flags(os.path.join(self.sdk, 'kinds', kind + '.flags'))
        have = idf_version(self.idf)
        if have != meta['idf']:
            die('the ESP-IDF in %s is %s: the SDK package needs %s (give --idf <folder>)' % (self.idf, have or 'not found', meta['idf']))
        gcc = find_compiler(meta, args.toolchain)
        inc = sec['include_all'] if self.j.get('idf') == 'all' else sec['include']
        cflags = (self.subst(sec['cflags']) + self.subst(inc) + tfn_pkg.APP_CFLAGS +
                  ['-D' + d for d in self.j.get('defines', [])] + ['-fmacro-prefix-map=%s=.' % norm(self.folder)])
        out = os.path.join(self.outdir, pid, kind)
        os.makedirs(out, exist_ok=True)
        # the new package goes into its own folder: its files replace the package of out only after the last step
        work = tfn_pkg.pkg_stage(out)
        elf, und, ram = self.elf(kind, gcc, cflags, work, a['srcs'], 'app_main', 'APP_SYM', a['sym'], 'elf_entry.c',
                                 'app.elf', a['link'])
        fl = elf_app_t(elf + '.syms', a['sym'], self.info['app_t'])
        wa, wb = a.get('watch'), b''
        if wa:
            welf, wund, _ = self.elf(kind, gcc, cflags, work, wa['srcs'], 'watch_main', 'WATCH_SYM', wa['sym'],
                                     'watch_entry.c', 'watch.elf', [])
            tfn_pkg.rm_try(welf + '.syms')
            deny = [n for n in wund if n.startswith(tfn_pkg.WATCH_DENY)]
            if deny:
                die('%s: the watcher calls names that a watcher cannot have (the screen, the sound, the touch): %s'
                    % (a['name'], ' '.join(deny)))
            und = sorted(set(und) | set(wund))
            wb = open(welf, 'rb').read()
        if not args.keep_syms:
            tfn_pkg.rm_try(elf + '.syms')
        abi = self.abi
        disabled = tfn_pkg.abi_disabled(abi, open(os.path.join(self.sdk, 'kinds', kind, 'config', 'sdkconfig.h'),
                                                  encoding='utf-8').read())
        req = os.path.join(self.outdir, pid, 'abi_request.json')
        rev = tfn_pkg.abi_check(abi, und, kind, disabled, a, pid, req, req, args.noabi)
        if 'abi_rev' in self.j and rev > int(self.j['abi_rev']):
            die('%s needs ABI rev %d on %s, but app.json says "abi_rev": %s' % (a['name'], rev, kind, self.j['abi_rev']))
        need = tfn_pkg.policy_need(abi, args.policy)
        perms = sorted(set(need[n] for n in und if n in need) | ({'background'} if wa else set()))
        icon = args.icon or (os.path.join(self.folder, self.j['icon']) if self.j.get('icon') else '')
        frames = icon_png(icon) if icon else icon_plain(a['name'], pid)
        nfr, ncol = tfn_pkg.icon_write(frames, os.path.join(work, 'icon.bin'))
        ib = open(os.path.join(work, 'icon.bin'), 'rb').read()
        eb = open(elf, 'rb').read()
        st = self.j.get('store', {})
        desc_en, desc_he = tfn_pkg.desc_check(pid, st.get('en', ''), st.get('he', ''))
        ver = args.ver or self.j.get('version', '1.0.0')
        mb = tfn_pkg.manifest(pid, a, ver, code, abi, rev, perms, kind, fl, eb, ram, args.notes, wb, ib, nfr, desc_en,
                              desc_he, self.kid)
        open(os.path.join(work, 'manifest.txt'), 'wb').write(mb)
        if args.dev_key:
            open(os.path.join(work, 'sigs'), 'wb').write(tfn_pkg.dev_sigs(args.dev_key, mb))
        if args.sign_key:
            open(os.path.join(work, 'sig'), 'wb').write(tfn_pkg.sign_local(args.sign_key, mb))
        tfn_pkg.pkg_commit(work, out)
        tfn_pkg.summary(pid, kind, eb, ram, und, nfr, ncol, ib, bool(args.sign_key), rev, perms)
        if args.push:
            tfn_pkg.push(args.push, out, a, 'card' if args.card else 'board', args.open, args.password)

    def run(self):
        code = int(time.strftime('%y%j%H%M'))     # (yy, the day of the year, hhmm: it fits in 32 bits)
        if self.args.dev_key:
            if not re.fullmatch(r'[A-Za-z0-9 ._-]{1,15}', self.args.dev_name):
                die('--dev-name: 1-15 letters A-Z a-z 0-9 space - _ .')
            _, self.kid = tfn_pkg.dev_key(self.args.dev_key)
            os.makedirs(os.path.join(self.outdir, self.pid), exist_ok=True)
            pf = os.path.join(self.outdir, self.pid, 'publisher.txt')
            open(pf, 'wb').write(tfn_pkg.pub_file(self.args.dev_key, self.args.dev_name))
            print('%s: developer key %s (%s): %s' % (self.pid, self.kid.upper(), self.args.dev_name, pf))
        if not (self.args.icon or self.j.get('icon')):
            print('NOTE: no icon (app.json "icon" or --icon <png>): a plain tile with the first letter of the name')
        for kind in self.kinds:
            self.kind(kind, code)
        if self.args.list_includes:
            p = os.path.join(self.outdir, self.pid, 'includes.txt')
            open(p, 'w', newline='\n').write('\n'.join(sorted(set(self.includes))) + '\n')
            print('the opened headers: %s (%d files)' % (p, len(set(self.includes))))


def main():
    ap = argparse.ArgumentParser(description='Build an app in its own folder with the TFN OS SDK package.')
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build', help='build the package of the app for each kind')
    b.add_argument('folder', help='the app folder (it has app.json)')
    b.add_argument('--kinds', default='', help='the kinds (default: "kinds" of app.json, else all kinds of the SDK)')
    b.add_argument('--sdk', default='', help='the SDK package folder (default: the env TFN_SDK, else the package of '
                   'this tool)')
    b.add_argument('--out', default='', help='the output folder (default: <app folder>/out)')
    b.add_argument('--idf', default='', help='the ESP-IDF folder (default: the env IDF_PATH)')
    b.add_argument('--toolchain', default='', help='the bin folder of the compiler (default: the ESP-IDF tools)')
    b.add_argument('--icon', default='', help='a PNG file for the menu icon (it replaces "icon" of app.json)')
    b.add_argument('--ver', default='', help='the version (default: "version" of app.json, else 1.0.0)')
    b.add_argument('--notes', default='', help='what is new in this version (one line)')
    b.add_argument('--sign-key', default='', help='a local P-256 PEM key: the package gets a signature (else none: a '
                   'board in the developer mode runs it)')
    b.add_argument('--dev-key', default='', help='a local P-256 PEM file of a developer key: publisher= in the '
                   'manifest and the file "sigs" (the store signature "sig" stays as --sign-key gives it)')
    b.add_argument('--dev-name', default='DEV', help='with --dev-key: the name of the key in publisher.txt (1-15 '
                   'letters A-Z a-z 0-9 space - _ .)')
    b.add_argument('--policy', default='', help='the published permission policy: a manager URL or a policy file')
    b.add_argument('--noabi', action='store_true', help='no ABI check (only for a test of the loader)')
    b.add_argument('--keep-syms', action='store_true', help='keep app.elf.syms (app.elf with its names: a backtrace)')
    b.add_argument('--list-includes', action='store_true', help='write <out>/<id>/includes.txt: each header that the '
                   'compiler opened')
    b.add_argument('--push', default='', help='install on this board (its IP address) after the build')
    b.add_argument('--card', action='store_true', help='with --push: install on the SD card')
    b.add_argument('--open', action='store_true', help='with --push: open the app on the board')
    b.add_argument('--password', default=os.environ.get('TFN_BOARD_PASSWORD', ''),
                   help='with --push: the web password of the board (default: the env TFN_BOARD_PASSWORD)')
    args = ap.parse_args()
    if args.cmd == 'build':
        Build(args).run()


if __name__ == '__main__':
    main()
