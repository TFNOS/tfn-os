# TFN OS SDK: make the package of an app. One app of main/apps.json (or a source out of main/) ->
# for each board kind: sdk/out/<id>/<kind>/ with manifest.txt, app.elf, icon.bin, sig (see main/tfn_app.c).
#
#   python sdk/tools/mk_app.py DIVE                         all 4 kinds that have a DEV build
#   python sdk/tools/mk_app.py DIVE --kinds lcd349 --push <board ip> [--card]   build, then install on a board
#   python sdk/tools/mk_app.py DIVE --nosign                no signature (a board in developer mode takes it)
#   python sdk/tools/mk_app.py path/app_x.c --sym app_x --name X [--group UTIL] [--portrait]   a source out of main/
#                                                           (no host icon yet: the board draws a letter tile)
#
# The steps for one kind:
#   1. app.elf: the sources of the app (and of the apps that it "needs") with the compile options of the DEV build
#      (build_dev/<kind>/compile_commands.json) + -fPIC, sdk/tools/host/elf_entry.c, a shared object, stripped;
#   2. the ABI check: each name that app.elf needs must be in main/tfn_abi.json for this kind (else: stop, and the
#      request file sdk/out/<id>/abi_request.json for main/tfn_abi_extra.json, see sdk/docs/raw.md); the manifest
#      gets the revision of the newest name (rev=) and the permission classes of the names (perms=);
#   3. icon.bin and the app_t flags: sdk/tools/host/iconshot_<kind>.exe (the icon frames of the app's draw_icon on
#      the PC);
#   --dev-key <pem> [--dev-name NAME]: a developer signs with an own P-256 key: publisher=<key ID> in manifest.txt,
#      the file "sigs" (TFNSIG2 lines) and sdk/out/<id>/publisher.txt (the key file for SETUP > TRUSTED PUBLISHERS of a
#      board, main/tfn_pub.h). Only a board that trusts the key as TEST runs the app with no store signature; the other
#      boards show NOT REVIEWED until the store signs the same manifest.txt (sig = the countersign).
#   4. manifest.txt; sig = the ECDSA P-256 signature of the store key over manifest.txt (--sign-key, or the signer
#      of store_conf.py).
#      The description of the store (desc=, desc_he=) comes from store.json in the folder of the app's first source
#      (sdk/docs/store.md); --publish stops when the app has no English description.
# The link, the checks, icon.bin and manifest.txt are the shared steps of tfn_pkg.py (tfn_app.py uses them too: an app
# in its own folder, with the SDK package only).
import argparse
import json
import filecmp
import os
import shutil
import re
import shlex
import struct
import subprocess
import tempfile
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tfn_pkg  # noqa: E402
from tfn_pkg import (DESC_CHARS, DESC_MAX, ICON_FRAMES, ICON_MS, ICON_SIZE, LOADER_OK,  # noqa: E402,F401
                     MANIFEST_MAX, WATCH_DENY, cmd_split, die, icon_palette, policy_need, publish, push, rm_try,
                     run)
import store_conf  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SDK = os.path.join(ROOT, 'sdk')
HOST = os.path.join(SDK, 'tools', 'host')
KINDS = ('lcd349', 'cores3', 'am18', 'watcher', 'tab5', 'am175')
FIRMWARE_BUILDS = {}


def firmware_build(kind):
    return FIRMWARE_BUILDS.get(kind, os.path.join(ROOT, 'build_dev', kind))

# the icon tool of each kind (the Tab5 draws the frame of the CoreS3: the same icon and flags)
ICON_KIND = {'tab5': 'cores3', 'am175': 'watcher'}     # (the AMOLED-1.75C draws the frames of the Watcher)


def app_of(name, kinds=None):
    j = json.load(open(os.path.join(ROOT, 'main', 'apps.json'), encoding='utf-8'))
    by = {a['name']: a for a in j['apps']}
    a = by.get(name) or next((x for x in j['apps'] if x['sym'] in (name, 'app_' + name.lower())), None)
    if not a:
        die('no app %s in main/apps.json' % name)
    # (a core app is in the firmware; "store_on" names the slim kinds where it is an app file)
    if a.get('core') and not (kinds and set(kinds) <= set(a.get('store_on', []))):
        die('%s is a core app (%s): it stays in the firmware' % (a['name'], a.get('why_core', '')))
    # an app whose app_t is in the object of another app ("with": SUNSET in CRUISE): its own package from the sources
    # of that app (for example SUNSET as an app file of the slim firmware). The entry gives its app_t; the code of the
    # other app comes along (about 290 KB for SUNSET).
    host = by[a['with']] if a.get('with') else a
    srcs = list(host.get('srcs', []))
    for n in host.get('needs', []) + a.get('needs', []):
        srcs += [s for s in by[n].get('srcs', []) if s not in srcs]
    if not srcs:
        die('%s has no sources' % a['name'])
    return a, srcs


def store_desc(pid, folder, args):
    # store.json in the folder of the app: {"<id>": {"en": "...", "he": "..."}}; --desc / --desc-he win. Stops when a
    # text is too long or has a character that the board fonts do not have (a TAB and a line break too).
    d = {}
    p = os.path.join(folder, 'store.json')
    if os.path.exists(p):
        d = json.load(open(p, encoding='utf-8')).get(pid, {})
    en, he = args.desc or d.get('en', ''), args.desc_he or d.get('he', '')
    return tfn_pkg.desc_check(pid, en, he)


# a source of main/ (a core app that is an app file on some kinds) compiles from its copy in
# build_dev/work/main: the options of the DEV build find the headers there (the copy in main/ found main/tfn/ first:
# two copies of one header, "conflicting types")
def work_src(p):
    w = os.path.join(ROOT, 'build_dev', 'work', 'main')
    if os.path.dirname(os.path.abspath(p)) != os.path.join(ROOT, 'main') or not os.path.isdir(w):
        return p
    c = os.path.join(w, os.path.basename(p))
    if not os.path.exists(c) or not filecmp.cmp(p, c, shallow=False):
        shutil.copy2(p, c)
    return c


def src_path(s):
    # (a name of apps.json is in main/, libs/<module>/, third_party/<name>/ or apps/<app>/)
    if os.path.isabs(s):
        return s
    sys.path.insert(0, os.path.join(ROOT, 'main'))
    import apps_gen
    return apps_gen.source_path(s)


def flags_of(kind, src0):
    if os.path.isabs(src0):
        src0 = 'app_stack.c'                    # (a source out of main/: the options of a plain app)
    cc = json.load(open(os.path.join(firmware_build(kind), 'compile_commands.json')))
    def find(name):
        return next((x for x in cc if x['file'].replace('\\', '/').endswith('/' + name)), None)
    e = find(src0)
    if not e:       # (an app that is not in this firmware build: the options of a plain app are the same)
        e = find('app_stack.c') or find('main.c')
        src0 = os.path.basename(e['file'].replace('\\', '/'))
    args = e.get('arguments') or cmd_split(e['command'])
    out, skip = [], False
    for x in args[1:]:
        if skip:
            skip = False
            continue
        if x in ('-o', '-c'):
            skip = x == '-o'
            continue
        if x.replace('\\', '/').endswith('/' + src0):     # (the template source: main/, or apps/<app>/ in a full build)
            continue
        out.append(x)
    # (libs/ and third_party/ themselves: an app includes a lib in the SDK form "tfdraw/tfdraw.h"; a build made before
    # main/CMakeLists.txt had them does not give them). They go FIRST: the options of the DEV build name the copies in
    # build_dev/work, which are as old as the last DEV build (VOICE 1.0.137 took an old tinyvoca file from there).
    for top in ('third_party', 'libs'):
        libs = '-I' + os.path.join(ROOT, top).replace('\\', '/')
        if libs in out:
            out.remove(libs)
        out.insert(0, libs)
    return args[0], out + ['-fPIC', '-fvisibility=hidden', '-Wno-error', '@' + all_includes(kind, cc, out)], e['directory']


def all_includes(kind, cc, have):
    # the include folders of EACH component of the build (the raw ESP-IDF names of the ABI: an app can
    # include "driver/gpio.h", "esp_http_client.h", "host/ble_gap.h" ... and not only what main/ needs). About 380
    # folders: too long for a Windows command line, so they go into a gcc response file.
    path = os.path.join(SDK, 'out', '_includes_%s.rsp' % kind)
    seen = set(x[2:] for x in have if x.startswith('-I'))
    lines = []
    for e in cc:
        args = e.get('arguments') or cmd_split(e['command'])
        for i, a in enumerate(args):
            p = a[2:] or (args[i + 1] if i + 1 < len(args) else '') if a.startswith('-I') else ''
            if p and p not in seen:
                seen.add(p)
                lines.append('-I' + p.replace('\\', '/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    txt = '\n'.join(lines) + '\n'
    if not os.path.exists(path) or open(path).read() != txt:
        open(path, 'w', newline='\n').write(txt)
    return path.replace('\\', '/')


# the link script of an app (made once from the default script of the linker for shared objects): .ctors and
# .init_array go inside .data.rel.ro with start and end names (sdk/tools/host/elf_entry.c runs them), because the
# loader places only .text, .rodata, .data, .data.rel.ro and .bss; .dtors and .fini_array go (an app never ends
# by exit: the runtime frees what it holds; tfn_pkg.LOADER_OK: the sections that the loader places)


def app_ld(pre):
    if 'riscv' in os.path.basename(pre):
        return app_ld_riscv(pre)
    path = os.path.join(HOST, 'app.ld')
    if os.path.exists(path) and 'TFN app.ld v4' in open(path).read():
        return path
    v = run([pre + 'ld', '--verbose', '-shared']).decode()
    body = v.split('==================================================')[1]
    body = re.sub(r'\n  \.preinit_array.*?\n  \.data\.rel\.ro', '\n  .data.rel.ro', body, flags=re.S)
    ro = '.data.rel.ro : {'
    assert body.count(ro) == 1, 'the default script changed: check sdk/tools/mk_app.py app_ld'
    body = body.replace(ro, ro + ' PROVIDE_HIDDEN(__tfn_ctors_start = .); KEEP (*(SORT(.ctors.*))) KEEP (*(.ctors)) '
                        'PROVIDE_HIDDEN(__tfn_ctors_end = .); PROVIDE_HIDDEN(__tfn_init_start = .); KEEP (*(.preinit_array)) '
                        'KEEP (*(SORT(.init_array.*))) KEEP (*(.init_array)) PROVIDE_HIDDEN(__tfn_init_end = .); ')
    body = body.replace('/DISCARD/ : {', '/DISCARD/ : { *(.dtors .dtors.* .fini_array .fini_array.*) ', 1)
    # (v2: the firmware attributes of the apps: IRAM_ATTR code, DRAM_ATTR data and EXT_RAM_BSS_ATTR
    # statics. In an app file the loader places all of them in PSRAM, with the other code and data)
    for a, b in (('*(.literal .text .stub', '*(.iram1 .iram1.*) *(.literal .text .stub'),
                 ('*(.data .data.* .gnu.linkonce.d.*)', '*(.data .data.* .gnu.linkonce.d.*) *(.dram1 .dram1.* .ext_ram.data .ext_ram.data.*)'),
                 ('*(.bss .bss.* .gnu.linkonce.b.*)', '*(.bss .bss.* .gnu.linkonce.b.*) *(.ext_ram.bss .ext_ram.bss.*)'),
                 # (v3: the .ext_ram.bss input is PROGBITS; the loader places .bss only as NOBITS: NOLOAD keeps it so)
                 ('  .bss            :', '  .bss (NOLOAD)   :'),
                 # (v4: 16 free bytes after each data section. The loader maps an address only when it is inside a
                 # section; a loop end such as `s_blob + N` at the end of .bss (the symbol _end) becomes 0, and the
                 # loop runs through all the memory. A newer firmware maps the end too: this is for an older firmware)
                 ('.rodata         : { *(.rodata .rodata.* .gnu.linkonce.r.*) }',
                  '.rodata         : { *(.rodata .rodata.* .gnu.linkonce.r.*) . += 16; }'),
                 ('*(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) }',
                  '*(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) . += 16; }'),
                 ('*(.dram1 .dram1.* .ext_ram.data .ext_ram.data.*)', '*(.dram1 .dram1.* .ext_ram.data .ext_ram.data.*) . += 16;'),
                 # (the end of .bss: the line of .bss is unique after its change above; GCC puts no COMMON symbols)
                 ('*(.bss .bss.* .gnu.linkonce.b.*) *(.ext_ram.bss .ext_ram.bss.*)',
                  '*(.bss .bss.* .gnu.linkonce.b.*) *(.ext_ram.bss .ext_ram.bss.*) *(COMMON) . += 16;')):
        assert body.count(a) == 1, 'the default script changed: check sdk/tools/mk_app.py app_ld (%s)' % a
        body = body.replace(a, b)
    open(path, 'w', newline='\n').write('/* TFN OS SDK: the link script of app files (made by sdk/tools/mk_app.py from '
                                         'the linker default; TFN app.ld v4) */\n' + body)
    return path


# the same script for RISC-V (the Tab5): the default script of its linker for shared objects, with the
# changes of app.ld v3, and the small data (.sdata, .srodata, .sbss) in .data and .bss (the loader places only the
# sections of LOADER_OK). NOT TESTED on a board yet.
def app_ld_riscv(pre):
    path = os.path.join(HOST, 'app_riscv.ld')
    if os.path.exists(path) and 'TFN app_riscv.ld v1' in open(path).read():
        return path
    v = run([pre + 'ld', '--verbose', '-shared']).decode()
    body = v.split('==================================================')[1]
    body = re.sub(r'\n  \.preinit_array.*?\n  \.data\.rel\.ro', '\n  .data.rel.ro', body, flags=re.S)
    ro = '.data.rel.ro : {'
    assert body.count(ro) == 1, 'the default RISC-V script changed: check sdk/tools/mk_app.py app_ld_riscv'
    body = body.replace(ro, ro + ' PROVIDE_HIDDEN(__tfn_ctors_start = .); KEEP (*(SORT(.ctors.*))) KEEP (*(.ctors)) '
                        'PROVIDE_HIDDEN(__tfn_ctors_end = .); PROVIDE_HIDDEN(__tfn_init_start = .); KEEP (*(.preinit_array)) '
                        'KEEP (*(SORT(.init_array.*))) KEEP (*(.init_array)) PROVIDE_HIDDEN(__tfn_init_end = .); ')
    body = body.replace('/DISCARD/ : {', '/DISCARD/ : { *(.dtors .dtors.* .fini_array .fini_array.*) ', 1)
    body = re.sub(r'\n  \.sdata +:\n  \{.*?\n  \}', '', body, flags=re.S)
    body = re.sub(r'\n  \.sbss +:\n  \{.*?\n  \}', '', body, flags=re.S)
    for a, b in (('*(.text .stub .text.* .gnu.linkonce.t.*)', '*(.iram1 .iram1.*) *(.text .stub .text.* .gnu.linkonce.t.*)'),
                 ('*(.data .data.* .gnu.linkonce.d.*)', '*(.data .data.* .gnu.linkonce.d.*) *(.dram1 .dram1.* .ext_ram.data .ext_ram.data.*) '
                  '*(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2) *(.srodata .srodata.*) '
                  '*(.sdata2 .sdata2.* .gnu.linkonce.s2.*) *(.sdata .sdata.* .gnu.linkonce.s.*)'),
                 ('*(.bss .bss.* .gnu.linkonce.b.*)', '*(.sbss2 .sbss2.* .gnu.linkonce.sb2.*) *(.dynsbss) *(.sbss .sbss.* .gnu.linkonce.sb.*) '
                  '*(.scommon) *(.bss .bss.* .gnu.linkonce.b.*) *(.ext_ram.bss .ext_ram.bss.*)'),
                 ('  .bss            :', '  .bss (NOLOAD)   :')):
        assert body.count(a) == 1, 'the default RISC-V script changed: check sdk/tools/mk_app.py app_ld_riscv (%s)' % a
        body = body.replace(a, b)
    open(path, 'w', newline='\n').write('/* TFN OS SDK: the link script of app files for RISC-V (made by sdk/tools/mk_app.py '
                                         'from the linker default; TFN app_riscv.ld v1) */\n' + body)
    return path


def ext_iconshot(kind, a, src):
    # the icon tool of an app out of main/: iconshot.c in its ICON_EXT mode + the source + the library of build.sh
    kind = ICON_KIND.get(kind, kind)
    lib = os.path.join(HOST, 'iconlib_%s.a' % kind)
    flf = os.path.join(HOST, 'iconflags_%s.txt' % kind)
    if not (os.path.exists(lib) and os.path.exists(flf)):
        die('no %s: run sh sdk/tools/host/build.sh %s' % (lib, kind))
    flags = shlex.split(open(flf).read().strip())
    exe = os.path.join(HOST, 'iconext_%s.exe' % kind)
    rm_try(exe)
    # (raw/: the PC stand-ins of the raw ESP-IDF headers and names of the extra ABI names, sdk/docs/raw.md)
    run(['clang'] + flags + ['-I', 'raw', '-DICON_EXT=' + a['sym'], 'iconshot.c', os.path.abspath(src), 'raw/raw_stubs.c',
                             'iconlib_%s.a' % kind, '-o', exe, '-lm'], HOST)
    return exe


def build_elf(kind, a, srcs, out, entry='app_main', define='APP_SYM', sym=None, entry_src='elf_entry.c', name='app.elf'):
    gcc, flags, cwd = flags_of(kind, srcs[0])
    pre = gcc[:-len('gcc.exe')] if gcc.endswith('gcc.exe') else gcc[:-len('gcc')]
    objs = []
    for s in srcs:
        _, fl, _ = flags_of(kind, s)
        o = os.path.join(out, os.path.splitext(os.path.basename(s))[0] + '.o')
        cc = gcc if s.endswith('.c') else pre + 'g++'
        run([cc] + fl + ['-c', work_src(src_path(s)), '-o', o], cwd)
        objs.append(o)
    eo = os.path.join(out, '_entry.o')
    run([gcc] + flags + ['-D%s=%s' % (define, sym or a['sym']), '-I', os.path.join(ROOT, 'main'), '-c',
                         os.path.join(HOST, entry_src), '-o', eo], cwd)
    elf = os.path.join(out, name)
    if 'riscv' in os.path.basename(pre):
        # (RISC-V: the helpers that its compiler calls and the firmware does not give: sdk/tools/host/riscv_rt.c)
        ro = os.path.join(out, '_riscv_rt.o')
        run([gcc] + flags + ['-c', os.path.join(HOST, 'riscv_rt.c'), '-o', ro], cwd)
        objs.append(ro)
    # a prebuilt library of the app (main/apps.json "link", for example libtfjpeg_s3.a of DEMO): only the app, not
    # its watcher; the linker takes only the objects that the app calls
    # (--exclude-libs: the names of the library stay in the app file, so its direct calls between its objects link)
    libs = [src_path(f) for f in a.get('link', [])] if entry == 'app_main' else []
    # MK_APP_KEEP=1: a copy with the names (app.elf.syms) to read a backtrace of the board: the address in the
    # backtrace minus (the entry in the log "elf->entry=" minus e_entry of app.elf) = the address in app.elf.syms
    return tfn_pkg.link_app(gcc, pre, objs, eo, elf, entry, app_ld(pre), libs, a['name'], cwd,
                            elf + '.syms' if os.environ.get('MK_APP_KEEP') else '')


def src_stamp():
    """The state of the source (main/, boards/, apps/, libs/, third_party/): the git trees of HEAD and a hash of the changes that are not
    committed. build.sh writes it into host/src_stamp_<kind>.txt; the content counts, not the file times (a git switch
    gives new times to the same content)."""
    import hashlib
    trees = subprocess.run(['git', '-C', ROOT, 'rev-parse', 'HEAD:main', 'HEAD:boards', 'HEAD:apps', 'HEAD:libs',
                            'HEAD:third_party'],
                           capture_output=True,
                           text=True).stdout.split()
    diff = subprocess.run(['git', '-C', ROOT, 'diff', 'HEAD', '--', 'main', 'boards', 'apps', 'libs', 'third_party'],
                          capture_output=True).stdout
    return ' '.join(trees + [hashlib.sha256(diff).hexdigest()[:16]])


def icon_tool(kind):
    # the icon tool of an app of main/apps.json. The app_t flags of the manifest (round, round_fit, native ...) come
    # from this host tool, and the board uses the manifest flags: a tool older than the source can give wrong flags
    # (for example round=0 to a round app). main() calls it before the build starts.
    k = ICON_KIND.get(kind, kind)
    exe = os.path.join(HOST, 'iconshot_%s.exe' % k)
    if not os.path.exists(exe):
        die('no %s: run sh sdk/tools/host/build.sh %s' % (exe, k))
    stamp = os.path.join(HOST, 'src_stamp_%s.txt' % k)
    if not os.path.exists(stamp) or open(stamp).read().strip() != src_stamp():
        die('%s was built from another source: run sh sdk/tools/host/build.sh %s (else the flags of the manifest are old)'
            % (os.path.basename(exe), k))
    return exe


def icon_and_flags(kind, name, out, exe):
    raw = os.path.join(out, '_icon.raw')
    txt = run([exe, name, str(ICON_SIZE), str(ICON_FRAMES), str(ICON_MS), raw]).decode()
    fl = dict(l.split('=', 1) for l in txt.splitlines() if '=' in l)
    data = open(raw, 'rb').read()
    rm_try(raw)
    n = ICON_SIZE * ICON_SIZE
    frames = []
    for f in range(ICON_FRAMES):
        blk = data[f * 4 * n:(f + 1) * 4 * n]
        a = struct.unpack('<%dH' % n, blk[:2 * n])
        b = struct.unpack('<%dH' % n, blk[2 * n:])
        frames.append([x if x == y else None for x, y in zip(a, b)])
    nfr, ncol = tfn_pkg.icon_write(frames, os.path.join(out, 'icon.bin'))
    return fl, nfr, ncol


def sign(manifest_bytes):
    # the signature of the store key with no --sign-key (store_conf.py: the signer of the store setup)
    return store_conf.sign(manifest_bytes)


def default_manager():
    # the manager of the store setup (store_conf.py: the environment variable TFN_MANAGER or the site module)
    return store_conf.manager_url()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('app')
    ap.add_argument('--kinds', default='')
    ap.add_argument('--firmware-build', default='', help='Use this existing build/config directory; requires one --kinds value')
    signing = ap.add_mutually_exclusive_group()
    signing.add_argument('--nosign', action='store_true')
    signing.add_argument('--sign-key', default='', help='a local P-256 PEM file of the store key (else the signer of store_conf.py)')
    ap.add_argument('--dev-key', default='', help='a local P-256 PEM file of a developer key: publisher= in the '
                    'manifest and the file "sigs" (the store signature "sig" stays as the other options give it)')
    ap.add_argument('--dev-name', default='DEV', help='with --dev-key: the name of the key in publisher.txt (1-15 '
                    'letters A-Z a-z 0-9 space - _ .)')
    ap.add_argument('--push', default='')
    ap.add_argument('--card', action='store_true')
    ap.add_argument('--open', action='store_true', help='with --push: open the app on the board after the install')
    ap.add_argument('--password', default=os.environ.get('TFN_BOARD_PASSWORD', ''),
                    help='with --push: the web password of the board (default: the env TFN_BOARD_PASSWORD)')
    ap.add_argument('--publish', nargs='?', const=default_manager(), default='',
                    help='send the packages to the store of the manager (default: the manager of the store setup, '
                    'store_conf.py: the environment variable TFN_MANAGER)')
    ap.add_argument('--ver', default='')
    ap.add_argument('--watch', default='', help='an app out of main/: the source of its watcher (main/watch.h)')
    ap.add_argument('--watch-sym', default='', help='the watch_t of that watcher')
    ap.add_argument('--watch-every', type=int, default=15, help='its minutes between two checks')
    ap.add_argument('--notes', default='', help='what is new in this version (one line): the manager sends it with the '
                    'notice of the update to the boards that have the app')
    ap.add_argument('--desc', default='', help='the description of the store in English (else store.json of the app)')
    ap.add_argument('--desc-he', default='', help='the description of the store in Hebrew (else store.json of the app)')
    ap.add_argument('--sym', default='')
    ap.add_argument('--name', default='')
    ap.add_argument('--group', default='GAMES')
    ap.add_argument('--portrait', action='store_true')
    ap.add_argument('--noabi', action='store_true', help='no ABI check (only for a test of the loader)')
    ap.add_argument('--policy', default='', help='the published permission policy: the manager URL or a policy file '
                    '(perms= then follows it; with no --policy: the default classes of main/tfn_abi.json)')
    args = ap.parse_args()
    if '--publish' in sys.argv and not args.publish:
        ap.error('no manager: give --publish http://<manager>:<port> (or set TFN_MANAGER)')
    if args.firmware_build:
        if args.kinds not in KINDS:
            ap.error('--firmware-build requires exactly one --kinds value')
        FIRMWARE_BUILDS[args.kinds] = os.path.abspath(args.firmware_build)
    ext = args.app.endswith(('.c', '.cc'))
    if ext:
        if not (args.sym and args.name):
            die('a source out of main/ needs --sym and --name')
        a, srcs = {'name': args.name, 'sym': args.sym, 'group': args.group}, [os.path.abspath(args.app)]
        if args.watch:
            a['watch'] = {'srcs': [os.path.abspath(args.watch)], 'sym': args.watch_sym or 'watch_' + args.sym[4:],
                          'every': args.watch_every}
    else:
        a, srcs = app_of(args.app, args.kinds.split(',') if args.kinds else None)
    pid = a['sym'][4:]
    folder = os.path.dirname(src_path(srcs[0]))
    desc_en, desc_he = store_desc(pid, folder, args)
    if args.publish and not desc_en:
        die('%s: the store needs a description: add "%s": {"en": "...", "he": "..."} to %s (sdk/docs/store.md), or give '
            '--desc' % (pid, pid, os.path.relpath(os.path.join(folder, 'store.json'), ROOT).replace(os.sep, '/')))
    if args.publish and not desc_he:
        print('NOTE: %s has no Hebrew description (a board in Hebrew shows the English one)' % pid)
    abi = json.load(open(os.path.join(ROOT, 'main', 'tfn_abi.json')))
    kinds = [k for k in (args.kinds.split(',') if args.kinds else KINDS)
             if os.path.exists(os.path.join(firmware_build(k), 'compile_commands.json'))]
    if args.firmware_build and not kinds:
        die('no compiled firmware configuration for the requested kind')
    fw_ver = open(os.path.join(ROOT, 'version.txt')).read().strip() if os.path.exists(os.path.join(ROOT, 'version.txt')) else '1.0.0'
    ver = args.ver or fw_ver
    code = int(time.strftime('%y%j%H%M'))     # (yy, the day of the year, hhmm: it fits in 32 bits)
    kid = ''
    if args.dev_key:
        if not re.fullmatch(r'[A-Za-z0-9 ._-]{1,15}', args.dev_name):
            die('--dev-name: 1-15 letters A-Z a-z 0-9 space - _ .')
        _, kid = tfn_pkg.dev_key(args.dev_key)
        os.makedirs(os.path.join(SDK, 'out', pid), exist_ok=True)
        open(os.path.join(SDK, 'out', pid, 'publisher.txt'), 'wb').write(tfn_pkg.pub_file(args.dev_key, args.dev_name))
        print('%s: developer key %s (%s): sdk/out/%s/publisher.txt' % (pid, kid.upper(), args.dev_name, pid))
    for kind in kinds:
        out = os.path.join(SDK, 'out', pid, kind)
        os.makedirs(out, exist_ok=True)
        tool = None if ext else icon_tool(kind)
        # the new package goes into its own folder: its files replace the package of out only after the last step
        work = tfn_pkg.pkg_stage(out)
        elf, und, ram = build_elf(kind, a, srcs, work)
        wa, wb = a.get('watch'), b''
        if wa:
            # the watcher (main/watch.h): watch.elf, its own entry; its names count for the revision and the classes
            welf, wund, _ = build_elf(kind, a, wa['srcs'], work, 'watch_main', 'WATCH_SYM', wa['sym'], 'watch_entry.c', 'watch.elf')
            deny = [n for n in wund if n.startswith(WATCH_DENY)]
            if deny:
                die('%s: the watcher calls names that a watcher cannot have (the screen, the sound, the touch): %s'
                    % (a['name'], ' '.join(deny)))
            und = sorted(set(und) | set(wund))
            wb = open(welf, 'rb').read()
        disabled = set()
        if abi.get('conditions'):
            config_path = os.path.join(firmware_build(kind), 'config', 'sdkconfig.h')
            disabled = tfn_pkg.abi_disabled(abi, open(config_path, encoding='utf-8').read())
        # the request for the names that are not in the ABI (sdk/docs/raw.md): the entries for main/tfn_abi_extra.json
        req = os.path.join(SDK, 'out', pid, 'abi_request.json')
        rev = tfn_pkg.abi_check(abi, und, kind, disabled, a, pid, req, os.path.relpath(req, ROOT), args.noabi)
        # the permission classes of its names
        need = policy_need(abi, args.policy)
        perms = sorted(set(need[n] for n in und if n in need) | ({'background'} if wa else set()))
        # (an app out of main/: its own icon tool, linked with the library of sdk/tools/host/build.sh)
        fl, nfr, ncol = icon_and_flags(kind, a['name'], work, ext_iconshot(kind, a, srcs[0]) if ext else tool)
        ib = open(os.path.join(work, 'icon.bin'), 'rb').read()
        eb = open(elf, 'rb').read()
        mb = tfn_pkg.manifest(pid, a, ver, code, abi, rev, perms, kind, fl, eb, ram, args.notes, wb, ib, nfr, desc_en,
                              desc_he, kid)
        open(os.path.join(work, 'manifest.txt'), 'wb').write(mb)
        if args.dev_key:
            open(os.path.join(work, 'sigs'), 'wb').write(tfn_pkg.dev_sigs(args.dev_key, mb))
        if args.sign_key:
            open(os.path.join(work, 'sig'), 'wb').write(tfn_pkg.sign_local(args.sign_key, mb))
        elif not args.nosign:
            open(os.path.join(work, 'sig'), 'wb').write(sign(mb))
        tfn_pkg.pkg_commit(work, out)
        tfn_pkg.summary(pid, kind, eb, ram, und, nfr, ncol, ib, not args.nosign, rev, perms)
        if args.publish:
            publish(args.publish, out, pid, kind)
        if args.push:
            push(args.push, out, a, 'card' if args.card else 'board', args.open, args.password)


if __name__ == '__main__':
    if sys.argv[1:] == ['--src-stamp']:           # (sdk/tools/host/build.sh)
        print(src_stamp())
        sys.exit(0)
    main()
