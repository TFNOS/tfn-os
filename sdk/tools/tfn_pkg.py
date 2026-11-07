# TFN OS SDK: the shared steps of an app package. mk_app.py (an app of the repo) and tfn_app.py (an app in its own
# folder, with the SDK package only) use this module. It reads no file of the repo: each path comes from the caller.
#
#   link_app()      link the objects into app.elf, strip it, check its sections, list the names that it needs
#   abi_disabled()  the ABI names that the configuration of the kind turns off
#   abi_check()     stop when a name is not in the ABI of the kind (and write the request file)
#   desc_check()    check the store texts (the board fonts, the length)
#   icon_write()    icon.bin from the icon frames (a palette of 255 colours, a run-length code)
#   manifest()      the bytes of manifest.txt
#   sign_local()    the signature of manifest.txt with a local P-256 key
#   dev_key()       the public key and the key ID of a developer key (main/tfn_pub.h); dev_sigs(), pub_file()
#   package_files() the files of a complete package (manifest.txt and each file that it names, with its SHA-256)
#   push()          install the package on a board (the board web page API); a package that is not complete stops
#   publish()       send the package to the store of a manager; a package that is not complete stops
import base64
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ICON_SIZE, ICON_FRAMES, ICON_MS = 128, 16, 125

# the store text: the app page of STORE shows about 330 characters on the 3.49 and 290 on the 320 x 240 frame, in the
# fonts of the board (ASCII, the Hebrew letters and a few marks)
DESC_MAX = 300
DESC_CHARS = set(range(0x20, 0x7f)) | set(range(0x5d0, 0x5eb)) | {0x5f3, 0x5f4, 0x2013, 0x2014, 0x2019, 0x201c,
                                                                  0x201d, 0x2026, 0x266a}
MANIFEST_MAX = 4096         # (the loader of the board does not read a longer manifest.txt)

# the sections that the loader places
LOADER_OK = {'.hash', '.gnu.hash', '.dynsym', '.dynstr', '.rela.dyn', '.rela.plt', '.text', '.rodata', '.got.loc',
             '.data.rel.ro', '.dynamic', '.got', '.got.plt', '.data', '.bss'}

# the names that a watcher cannot have (it runs while another app draws and plays)
WATCH_DENY = ('gfx_', 'ui_', 'mf_', 'kf_', 'g_fb', 'sfx_', 'audio_', 'input_', 'app_open', 'board_lcd', 'board_backlight',
              'board_speaker', 'menu_', 'sysui_', 'kbd_', 'midi_bg_', 'imu_', 'tfd_')

# the compile options that each source of an app file gets after the options of the kind
APP_CFLAGS = ['-fPIC', '-fvisibility=hidden', '-Wno-error']


def die(msg):
    print('STOP: ' + msg)
    sys.exit(1)


def cmd_split(cmd):
    # a compile command of compile_commands.json: the Windows paths get /, but a \" stays (an escaped quote of a
    # define, -DIDF_VER=\"v6.1\")
    return shlex.split(re.sub(r'\\(?!")', '/', cmd))


def run(cmd, cwd=None, input=None, stderr_to=None):
    # Windows limits CreateProcess to 32767 characters. The full IDF include
    # list can exceed that; GCC response files preserve each argument.
    response = None
    try:
        if os.name == 'nt' and len(subprocess.list2cmdline(cmd)) > 28000 and re.search(r'(gcc|g\+\+)(\.exe)?$', str(cmd[0])):
            fd, response = tempfile.mkstemp(suffix='.rsp')
            with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as out:
                out.write('\n'.join('"' + str(a).replace('\\', '\\\\').replace('"', '\\"') + '"' for a in cmd[1:]))
            r = subprocess.run([cmd[0], '@' + response], cwd=cwd, capture_output=True, input=input)
        else:
            r = subprocess.run(cmd, cwd=cwd, capture_output=True, input=input)
    finally:
        if response:
            os.remove(response)
    if r.returncode:
        print(r.stdout.decode(errors='replace')[-3000:], r.stderr.decode(errors='replace')[-3000:])
        die('the command failed: %s' % ' '.join(str(c) for c in cmd[:4]))
    if stderr_to is not None:
        stderr_to.append(r.stderr.decode(errors='replace').replace('\r', ''))
    return r.stdout


def rm_try(p):
    # a remove that tries again (a sync tool or a virus scanner can hold a new file for a moment)
    for k in range(20):
        try:
            os.remove(p)
            return
        except FileNotFoundError:
            return
        except PermissionError:
            time.sleep(0.25)
    print('   (cannot remove %s: another program holds it)' % p)


def link_app(gcc, pre, objs, eo, elf, entry, ld, libs, app_name, cwd, keep=''):
    """Link the objects into elf, strip it and check it. Returns (elf, the undefined names, the RAM of the LOAD
    segments). keep: a path for a copy of the file before the strip (its names: a backtrace, the app_t flags)."""
    xt = [] if 'riscv' in os.path.basename(pre) else ['-mlongcalls']      # (RISC-V: the Tab5)
    run([gcc] + xt + ['-shared', '-nostartfiles', '-nostdlib', '-e', entry, '-Wl,--gc-sections',
         '-Wl,-T,' + ld, '-o', elf] + objs + [eo] + libs + (['-Wl,--exclude-libs,ALL'] if libs else []), cwd)
    if keep:
        shutil.copyfile(elf, keep)
    run([pre + 'strip', '--strip-unneeded', '--remove-section=.comment', '--remove-section=.xt.lit',
         '--remove-section=.xt.prop', '--remove-section=.xtensa.info', elf])
    for o in objs + [eo]:
        rm_try(o)
    # no address one past the end of a loaded section (elf_end_check.py: the loader of older firmware maps such an
    # address to 0; the link script leaves 16 free bytes after each section, so this stops only a new kind of fault)
    import elf_end_check
    ends = elf_end_check.check(elf)
    if ends:
        die('%s: the app file holds an address at the end of a section (the loader of older firmware maps it to 0): %s'
            % (app_name, '; '.join(ends[:3])))
    # each loaded section must be one that the loader places
    bad = []
    for l in run([pre + 'readelf', '-SW', elf]).decode().splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+(\S+)\s+\S+\s+(\S*)', l)
        # (RISC-V, the Tab5: the loader places the whole LOAD segments, .plt too: no list of sections there)
        if m and 'A' in m.group(3) and int(m.group(2), 16) and m.group(1) not in LOADER_OK and not xt == []:
            bad.append(m.group(1))
    if bad:
        die('%s: sections that the loader cannot place: %s' % (app_name, ' '.join(bad)))
    # the loader places .bss only as NOBITS (a PROGBITS .bss is not placed: the app writes into memory that it does
    # not own)
    if any(re.search(r'\]\s+\.bss\s+PROGBITS', l) for l in run([pre + 'readelf', '-SW', elf]).decode().splitlines()):
        die('%s: its .bss is PROGBITS: the loader would not place it (check the link script)' % app_name)
    und = sorted(set(l.split()[-1] for l in run([pre + 'nm', '-D', '-u', elf]).decode().splitlines() if l.strip()))
    # the memory: the sum of the LOAD segments (the loader copies them to PSRAM)
    ram = 0
    for l in run([pre + 'readelf', '-lW', elf]).decode().splitlines():
        p = l.split()
        if p and p[0] == 'LOAD':
            ram += int(p[5], 16)
    return elf, und, ram


def abi_disabled(abi, config_text):
    # the names of the ABI with a condition (a Kconfig option) that the configuration of the kind turns off
    if not abi.get('conditions'):
        return set()
    return {name for name, option in abi['conditions'].items()
            if not re.search(r'^#define\s+' + re.escape(option) + r'\s+1\s*$', config_text, re.M)}


def abi_check(abi, und, kind, disabled, a, pid, req, req_shown, noabi=False):
    # each name that the app file needs must be in the ABI of the kind; else stop, and write the request for the
    # names (sdk/docs/raw.md)
    bad = [n for n in und if kind not in abi['symbols'].get(n, []) or n in disabled]
    if bad and noabi:
        print('   (no ABI check: %s)' % ' '.join(bad))
    elif bad:
        json.dump({'app': a['name'], 'id': pid, 'kind': kind,
                   'how': 'Fill in perm (a class of main/tfn_abi_extra.json, or a new class, or "" for none), why '
                          'and by. Then add the entries to "names" of main/tfn_abi_extra.json, run '
                          'sdk/tools/abi_gen.py and build a DEV firmware to test, and send the file with a pull '
                          'request (sdk/docs/raw.md).',
                   'names': [{'name': n, 'perm': '?', 'why': '?', 'by': '?'} for n in bad]},
                  open(req, 'w', newline='\n'), indent=1)
        die('%s %s needs names that are not in the ABI %d rev %d of %s: %s\n'
            '   The request for them is in %s (sdk/docs/raw.md: how to ask for more names).'
            % (a['name'], kind, abi['version'], abi.get('rev', 0), kind, ' '.join(bad), req_shown))
    # the revision that the app needs (its newest name)
    return max([abi.get('revs', {}).get(n, 0) for n in und] + [0])


def policy_need(abi, src, cache={}):
    # the class of each name: the default of the ABI, with the changes of the published policy (the manager tab
    # PERMISSIONS: need=<name>|<class>, free=<name>). The manager writes perms= again when it signs; for a file with
    # no signature (the developer mode) the board checks it against its policy, so give the policy then.
    need = dict(abi.get('need', {}))
    if not src:
        return need
    if src not in cache:
        if src.startswith('http'):
            b = urllib.request.urlopen(src.rstrip('/') + '/api/store/policy?have=0', timeout=20).read().decode()
            cache[src] = b.split('\n', 2)[2] if b.startswith('TFNP1\n') else ''
        else:
            cache[src] = open(src, encoding='utf-8').read()
    for line in cache[src].splitlines():
        if line.startswith('need=') and '|' in line:
            n, c = line[5:].split('|', 1)
            need[n] = c
        elif line.startswith('free='):
            need.pop(line[5:], None)
    return need


def desc_check(pid, en, he):
    # stop when a text is too long or has a character that the board fonts do not have (a TAB and a line break too)
    for lang, t in (('en', en), ('he', he)):
        bad = sorted({c for c in t if ord(c) not in DESC_CHARS})
        if bad:
            die('%s: the description (%s) has characters that the board fonts do not have: %s'
                % (pid, lang, ' '.join('U+%04X' % ord(c) for c in bad)))
        if len(t) > DESC_MAX:
            die('%s: the description (%s) has %d characters (%d at most)' % (pid, lang, len(t), DESC_MAX))
    return ' '.join(en.split()), ' '.join(he.split())


def swap16(v):
    # a pixel of the frame (RGB565, the bytes in the order of the panel) <-> RGB565
    return ((v >> 8) | (v << 8)) & 0xFFFF


def icon_palette(count, sw, size=255):
    # count: {pixel value (RGB565 as the frame has it): the number of pixels}. Returns {pixel value: palette value}.
    if len(count) <= size:
        return {v: v for v in count}

    def rgb(v):
        c = sw(v)
        return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31

    def pack(r, g, b):
        return sw(((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255))

    items = [(rgb(v), n, v) for v, n in count.items()]
    boxes = [items]
    while len(boxes) < size:
        # the box with the largest spread (weighted by its pixels) goes in two at the weighted middle of its widest axis
        best, bk, bax = -1, -1, 0
        for k, b in enumerate(boxes):
            if len(b) < 2:
                continue
            spans = [max(c[0][a] for c in b) - min(c[0][a] for c in b) for a in range(3)]
            ax = max(range(3), key=lambda a: spans[a] * (2 if a == 1 else 1))
            score = spans[ax] * sum(c[1] for c in b)
            if score > best:
                best, bk, bax = score, k, ax
        if bk < 0:
            break
        b = sorted(boxes[bk], key=lambda c: c[0][bax])
        half, acc, cut = sum(c[1] for c in b) / 2.0, 0, 1
        for i, c in enumerate(b):
            acc += c[1]
            if acc >= half:
                cut = min(max(i + 1, 1), len(b) - 1)
                break
        boxes[bk:bk + 1] = [b[:cut], b[cut:]]
    pal = []
    for b in boxes:
        w = sum(c[1] for c in b)
        pal.append(tuple(int(round(sum(c[0][a] * c[1] for c in b) / w)) for a in range(3)))
    out = {}
    for (r, g, b), _, v in items:
        p = min(pal, key=lambda q: (q[0] - r) ** 2 + 2 * (q[1] - g) ** 2 + (q[2] - b) ** 2)
        out[v] = pack(*p)
    return out


def icon_write(frames, path):
    """icon.bin (TFI1) from the frames: each frame is a list of ICON_SIZE x ICON_SIZE pixels (a pixel value as the
    frame has it, or None for a hole). The same frame again and again (a still icon) gives one frame. Returns (the
    number of frames, the number of colours)."""
    if all(fr == frames[0] for fr in frames[1:]):
        frames = frames[:1]
    # the palette (index 0 = a hole): the exact colours when they are 255 or fewer; else a palette of 255 colours made
    # by a median cut that counts the pixels of each colour, and each colour takes the nearest one
    count = {}
    for fr in frames:
        for v in fr:
            if v is not None:
                count[v] = count.get(v, 0) + 1
    cmap = icon_palette(count, swap16)
    cols = sorted(set(cmap.values()))
    idx = {c: i + 1 for i, c in enumerate(cols)}

    def rle(seq):
        o = bytearray()
        i = 0
        while i < len(seq):
            j = i
            while j < len(seq) and j - i < 129 and seq[j] == seq[i]:
                j += 1
            if j - i >= 3:
                o += bytes([257 - (j - i), seq[i]])
                i = j
                continue
            k = i
            lit = []
            while k < len(seq) and len(lit) < 128:
                if k + 2 < len(seq) and seq[k] == seq[k + 1] == seq[k + 2]:
                    break
                lit.append(seq[k])
                k += 1
            o += bytes([len(lit) - 1]) + bytes(lit)
            i = k
        return bytes(o)
    body = bytearray(b'TFI1' + struct.pack('<HHHH', ICON_SIZE, len(frames), ICON_MS, len(cols)))
    body += struct.pack('<%dH' % len(cols), *cols)
    for fr in frames:
        seq = [0 if v is None else idx[cmap[v]] for v in fr]
        r = rle(seq)
        body += struct.pack('<I', len(r)) + r
    open(path, 'wb').write(body)
    return len(frames), len(cols)


# the app_t flags of the manifest, in the order of the manifest
APP_FLAGS = ('landscape', 'native', 'round', 'round_fit', 'wheel', 'key', 'run', 'needs')


def manifest(pid, a, ver, code, abi, rev, perms, kind, fl, eb, ram, notes='', wb=b'', ib=b'', nfr=0, desc_en='',
             desc_he='', publisher=''):
    """The bytes of manifest.txt. a: the app entry (name, group, emoji, new, form, form_help, oauth, watch); fl: the
    app_t flags (APP_FLAGS, as text); eb, wb, ib: the bytes of app.elf, watch.elf, icon.bin; publisher: the key ID of
    the developer key (16 hex digits, dev_key()), or ''."""
    wa = a.get('watch')
    lines = ['id=' + pid, 'name=' + a['name'], 'ver=' + ver, 'code=%d' % code, 'abi=%d' % abi['version'],
             'rev=%d' % rev, 'perms=' + ','.join(perms), 'kind=' + kind, 'group=' + a.get('group', 'GAMES'), 'emoji=' + a.get('emoji', ''),
             'saves=' + pid + '_']
    lines += ['%s=%s' % (k, fl[k]) for k in APP_FLAGS]
    if a.get('new'):
        lines.append('new=1')                       # (the menu puts a new app first, with its NEW pill)
    lines += ['bytes=%d' % len(eb), 'ram=%d' % ram, 'elf_sha256=' + hashlib.sha256(eb).hexdigest()]
    if notes:
        lines.append('notes=' + ' '.join(notes.split())[:200])
    for k in ('form', 'form_help', 'oauth'):           # (the account form on the board page)
        if a.get(k):
            lines.append('%s=%s' % (k, ' '.join(str(a[k]).split())))
    if wa:
        lines += ['watch=%d' % wa.get('every', 15), 'watch_bytes=%d' % len(wb), 'watch_sha256=' + hashlib.sha256(wb).hexdigest()]
    if publisher:
        lines.append('publisher=' + publisher)        # (the store signature over it = the countersign, tfn_pub.h)
    if ib:
        lines += ['icon_sha256=' + hashlib.sha256(ib).hexdigest(), 'icon=%d,%d,%d' % (ICON_SIZE, nfr, ICON_MS)]
    if desc_en:
        lines.append('desc=' + desc_en)
    if desc_he:
        lines.append('desc_he=' + desc_he)
    mb = ('\n'.join(lines) + '\n').encode()
    if len(mb) > MANIFEST_MAX:
        die('%s: manifest.txt has %d bytes: a board reads %d at most (make the description shorter)'
            % (pid, len(mb), MANIFEST_MAX))
    return mb


def sign_local(key_path, mb):
    # the signature of manifest.txt with a local P-256 key: r || s (32 + 32 bytes)
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    key = serialization.load_pem_private_key(open(key_path, 'rb').read(), None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        die('--sign-key must be a P-256 private key')
    r, s = utils.decode_dss_signature(key.sign(mb, ec.ECDSA(hashes.SHA256())))
    return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')


def dev_key(key_path):
    """The 65-byte public key (an uncompressed P-256 point) of a local P-256 private key, and its key ID: the first 8
    bytes of the SHA-256 of the 65 bytes, as 16 hex digits (the same ID as tfn_pub_key_id() of the board)."""
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    key = serialization.load_pem_private_key(open(key_path, 'rb').read(), None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        die('--dev-key must be a P-256 private key')
    pk = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    return pk, hashlib.sha256(pk).hexdigest()[:16]


def dev_sigs(key_path, mb):
    """The file "sigs": one line "TFNSIG2 <key ID> <r || s>" (hex) of the developer key over manifest.txt."""
    _, kid = dev_key(key_path)
    return ('TFNSIG2 %s %s\n' % (kid, sign_local(key_path, mb).hex())).encode()


def pub_file(key_path, name, flags='dev'):
    """The key file of a developer key for a board (SETUP > TRUSTED PUBLISHERS > ADD from /sdcard/pub/, or the body of
    POST /api/pub?op=add): the board owner of the board taps ALLOW on its screen."""
    pk, _ = dev_key(key_path)
    return ('TFNPUB1\nname=%s\nflags=%s\nkey=%s\n' % (name, flags, pk.hex())).encode()


def summary(pid, kind, eb, ram, und, nfr, ncol, ib, signed, rev, perms):
    print('%s %-7s app.elf %6d B, RAM %6d B, %2d names, icon %2d frames %3d colours %6d B%s%s' %
          (pid, kind, len(eb), ram, len(und), nfr, ncol, len(ib), ', signed' if signed else '',
           ', rev %d, perms %s' % (rev, ','.join(perms)) if rev or perms else ''))


PKG_FILES = ('manifest.txt', 'app.elf', 'icon.bin', 'sig', 'watch.elf', 'sigs')
OPTIONAL_NEW = ('sigs',)  # (an older board or manager does not take this file: a refusal is only a note)
# the line of manifest.txt with the SHA-256 of each file of the package that it names
PKG_HASH = (('app.elf', 'elf_sha256'), ('icon.bin', 'icon_sha256'), ('watch.elf', 'watch_sha256'))
BODY = {'Content-Type': 'application/octet-stream'}  # (the body of a request is a file, not a form)


STAGE = '_new'  # the folder in the package folder where a tool makes a new package (pkg_stage, pkg_commit)


def mv_try(src, dst):
    # a move over a file that tries again (a sync tool or a virus scanner can hold a new file for a moment)
    for k in range(20):
        try:
            os.replace(src, dst)
            return
        except PermissionError:
            time.sleep(0.25)
    die('cannot move %s to %s: another program holds it' % (src, dst))


def pkg_stage(out):
    """The empty folder out/_new for a new package of the folder out. The tool makes all files there, and pkg_commit()
    moves them into out after the last step: a step that stops keeps the package of out as it is."""
    work = os.path.join(out, STAGE)
    os.makedirs(work, exist_ok=True)
    for f in os.listdir(work):
        if os.path.isfile(os.path.join(work, f)):
            rm_try(os.path.join(work, f))
    return work


def pkg_commit(work, out):
    """The new package of work takes the place of the package of out. manifest.txt moves last: until then the old
    manifest.txt does not agree with the new files, and package_files() stops a send."""
    for f in [x for x in PKG_FILES if x != 'manifest.txt'] + ['app.elf.syms']:
        if os.path.exists(os.path.join(work, f)):
            mv_try(os.path.join(work, f), os.path.join(out, f))
        else:
            rm_try(os.path.join(out, f))
    mv_try(os.path.join(work, 'manifest.txt'), os.path.join(out, 'manifest.txt'))
    try:
        os.rmdir(work)
    except OSError:
        pass


def package_files(out):
    """The files of the package in the folder out, in the order of a send (manifest.txt first). It stops before the
    first request when the package is not complete: no manifest.txt, or a file that manifest.txt names is not there or
    does not agree with its SHA-256 in manifest.txt."""
    have = [f for f in PKG_FILES if os.path.isfile(os.path.join(out, f))]
    if 'manifest.txt' not in have:
        die('the package in %s is not complete: no manifest.txt (it has: %s). Did its build stop? Make it again with '
            'mk_app.py' % (out, ', '.join(have) or 'no file'))
    with open(os.path.join(out, 'manifest.txt'), 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    vals = dict(ln.split('=', 1) for ln in text.splitlines() if '=' in ln)
    for name, key in PKG_HASH:
        want = vals.get(key, '')
        if not want and name != 'app.elf':
            continue
        if name not in have:
            die('the package in %s is not complete: no %s (manifest.txt names it). Make it again with mk_app.py' %
                (out, name))
        with open(os.path.join(out, name), 'rb') as f:
            got = hashlib.sha256(f.read()).hexdigest()
        if got != want:
            die('the package in %s is not complete: %s does not match manifest.txt (a file of another build). Make it '
                'again with mk_app.py' % (out, name))
    return have


def put_count(name, n, msg):
    # the answer of a put names the bytes that the board kept ("app.elf: 505764 bytes in 5300 ms"); an answer of an
    # older firmware can have another text: then there is no check
    m = re.match(r'%s: (\d+) bytes' % re.escape(name), msg or '')
    if m and int(m.group(1)) != n:
        die('put %s: the board kept %s of %d bytes (%s)' % (name, m.group(1), n, msg))


def push(ip, out, a, place, open_after=False, password=''):
    files = package_files(out)          # (before any request: a package that is not complete never goes)
    # the web password of the board (SETUP > WEB PASSWORD): HTTP Basic with an empty user name
    auth = {'Authorization': 'Basic ' + base64.b64encode((':' + password).encode()).decode()} if password else {}

    def ctl(q):
        try:
            urllib.request.urlopen(urllib.request.Request('http://%s/api/ctl?%s' % (ip, q), headers=auth),
                                   timeout=10).read()
        except Exception as e:
            print('   /api/ctl?%s: %s' % (q, e))
    if open_after:
        ctl('menu=1')                   # (the hot reload: an open app cannot be replaced)
        time.sleep(1.2)

    def post(q, body):
        req = urllib.request.Request('http://%s/api/apps?%s' % (ip, q), data=body, method='POST',
                                     headers=dict(auth, **BODY))
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code == 401:
                die('the board has a web password: give --password (or the env TFN_BOARD_PASSWORD)'
                    if not password else 'the board refused the web password')
            if e.code == 429:
                die('too many wrong passwords: the board waits %s s' % e.headers.get('Retry-After', '?'))
            raise
    pid = a['sym'][4:]
    t0 = time.time()
    for f in files:
        with open(os.path.join(out, f), 'rb') as fh:
            body = fh.read()
        # (the place also with each file: the board puts the files of a card install on the card)
        r = post('op=put&id=%s&file=%s&place=%s' % (pid, f, place), body)
        if not r['ok'] and f in OPTIONAL_NEW:
            print('   NOTE: the board did not take %s (an older firmware): %s' % (f, r['msg']))
        elif not r['ok']:
            die('put %s: %s' % (f, r['msg']))
        else:
            put_count(f, len(body), r.get('msg', ''))
    r = post('op=commit&id=%s&place=%s' % (pid, place), b'')
    print('   push to %s: %s (%.1f s)' % (ip, r['msg'], time.time() - t0))
    if not r['ok']:
        sys.exit(1)
    if open_after:
        time.sleep(1.0)                 # (the board reads its app list again on its next frames)
        ctl('open=' + urllib.parse.quote(a['name']))
        print('   opened %s on the board (%.1f s from the start of the push)' % (a['name'], time.time() - t0))


def publish(url, out, pid, kind):
    # the files of one kind to the store of the manager (api/store/upload); a package that is not complete never goes
    for f in package_files(out):
        with open(os.path.join(out, f), 'rb') as fh:
            body = fh.read()
        req = urllib.request.Request('%s/api/store/upload?id=%s&kind=%s&file=%s' % (url.rstrip('/'), pid, kind, f),
                                     data=body, method='POST', headers=BODY)
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                j = json.load(r)
        except urllib.error.HTTPError as e:
            if f not in OPTIONAL_NEW:
                raise
            j = {'ok': False, 'msg': 'HTTP %d' % e.code}
        if not j.get('ok') and f in OPTIONAL_NEW:
            print('   NOTE: the store did not take %s (an older manager): %s' % (f, j.get('msg')))
        elif not j.get('ok'):
            die('publish %s: %s' % (f, j.get('msg')))
    print('   published to the store of %s' % url)
