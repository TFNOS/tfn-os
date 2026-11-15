# TFN OS SDK: make the SDK package (a folder and a zip). With the package, an app builds in its own folder, out of the
# repo (sdk/tools/tfn_app.py; sdk/docs/app_repo.md).
#
#   python sdk/tools/sdk_pack.py [--build-root build_dev] [--src .] [--kinds lcd349,cores3] [--out sdk/out/tfn-sdk]
#
# --build-root: the folder with a DEV build of each kind (<kind>/compile_commands.json, <kind>/config/sdkconfig.h).
# --src: the source tree of that build (main/tfn, boards, libs, third_party, main/tfn_abi.json); default: this repo. The headers
#        must fit the build: the tool stops when board.h of --src does not select the board of the kind.
#
# The package:
#   sdk.json                      the ABI, the ESP-IDF version, the kinds, the source state
#   include/tfn/*.h               the public API headers
#   boards/<kind>/board_caps.h    the capabilities of each board (tfn/board.h includes the one of the kind)
#   libs/<lib>/                   the header libs and the source libs that a public app can include, and the headers
#                                 of the module libs (their code is in the firmware: ABI names); the libs of other
#                                 projects (third_party/<lib>/ of the repo) are here too, with their licence
#   host/                         the entry of an app file and of a watcher, the RISC-V helpers, the link scripts
#   kinds/<kind>.flags            the compiler and the compile options of the kind: ESP-IDF paths as $IDF_PATH/...,
#                                 the package paths as $SDK/...; no path of the build PC
#   kinds/<kind>/config/sdkconfig.h, kinds/<kind>/picolibc.specs
#   tfn_abi.json                  the names that an app file can call on each kind
#   tools/                        tfn_app.py (the build front end), tfn_pkg.py, elf_end_check.py; the tools of the
#                                 static store and of a release check (store_index.py, release_pack.py,
#                                 release_check.py, fw_sign.py)
#   docs/, examples/              the docs of an app in its own folder, the examples with their app.json
#
# The include folders of ESP-IDF: [include] in the flags file is the smallest set that gives the same preprocessed text
# for the public headers, the header libs and the usual ESP-IDF headers of an app (COMMON_IDF) as the full set of the
# build. [include_all] is the full set (an app with raw ESP-IDF names: "idf": "all" in app.json).
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import tfn_pkg  # noqa: E402

KINDS = ('lcd349', 'cores3', 'am18', 'watcher', 'tab5', 'am175')
HEADER_LIBS = ('tfdraw', 'tficon', 'kscroll', 'jingle', 'quest_tilt', 't5view', 'sndrel', 'rndlay')
# the module libs: firmware modules (main/apps.json "modules"); their functions are ABI names, the package has only
# their headers (an app includes "tfnet/tfnet.h" and calls the firmware), and the files of MODULE_SRC
MODULE_LIBS = ('tfsfx', 'tfphys', 'tfnet', 'tfmqtt', 'zimread', 'zstd', 'rfpulse', 'rfdecode', 'irdecode', 'rfsrc',
               'sdrdsp', 'strokefont', 'drumkit')
# the parts of a module lib that the app compiles (the wiring sketch of I2C SCANNER and SERIAL MONITOR)
MODULE_SRC = {'rfpulse': ('rfp_sketch.inc',)}
# the source libs: an app includes their code (they are not firmware modules)
SOURCE_LIBS = ('minimp3', 'simplewebp', 'interfont')
# the licence of a lib with no LICENSE file in its folder (the font data of interfont: NOTICE.md): it goes into the
# package as libs/<lib>/LICENSE
LIB_LICENCES = {'interfont': 'docs/licenses/Inter-OFL.txt'}
# the ESP-IDF headers that most apps include next to the public headers
COMMON_IDF = ('esp_heap_caps.h', 'esp_random.h', 'esp_timer.h', 'esp_log.h', 'esp_err.h', 'freertos/FreeRTOS.h',
              'freertos/task.h', 'freertos/queue.h', 'freertos/semphr.h')
HOST_FILES = ('elf_entry.c', 'watch_entry.c', 'riscv_rt.c', 'app.ld', 'app_riscv.ld')
TOOL_FILES = ('tfn_app.py', 'tfn_pkg.py', 'elf_end_check.py', 'store_index.py', 'release_pack.py', 'release_check.py')
# the signature tool of the signed OTA (release_check.py uses it)
FW_SIGN = ('sdk/tools/fw_sign.py',)
# the docs of an app in its own folder, and the docs that they and the examples point to
DOC_FILES = ('app_repo.md', 'app_json.md', 'api.md', 'behave.md', 'visual.md', 'raw.md', 'store.md', 'notify.md')
EXAMPLES = ('counter', 'tapper', 'tilt', 'wifiscan', 'watchdemo')
# the app_t layout that tfn_app.py reads from app.elf (the offsets of the flags); the tool checks it on each kind
APP_T_LAYOUT = {'landscape': 4, 'native': 48, 'round': 49, 'round_fit': 50, 'wheel': 51, 'key': 52, 'run': 53,
                'needs': 54}


def die(msg):
    print('STOP: ' + msg)
    sys.exit(1)


def norm(p):
    return os.path.abspath(p).replace('\\', '/').rstrip('/')


def template_entry(cc):
    # the options of a plain app: the entry of app_stack.c, else main.c (the same choice as mk_app.py)
    def find(name):
        return next((x for x in cc if x['file'].replace('\\', '/').endswith('/' + name)), None)
    e = find('app_stack.c') or find('main.c')
    if not e:
        die('the build has no app_stack.c and no main.c')
    return e


def args_of(e):
    return e.get('arguments') or tfn_pkg.cmd_split(e['command'])


def path_under(p, base):
    p, base = norm(p).lower(), norm(base).lower()
    return p == base or p.startswith(base + '/')


def rel_under(p, base):
    return norm(p)[len(norm(base)):].lstrip('/')


class Kind:
    """The compile options of one kind, made portable."""

    def __init__(self, kind, build_root):
        self.kind = kind
        self.bdir = os.path.join(build_root, kind)
        cc = json.load(open(os.path.join(self.bdir, 'compile_commands.json')))
        e = template_entry(cc)
        args = args_of(e)
        self.gcc = args[0].replace('\\', '/')
        parts = self.gcc.split('/')
        # <tools>/<toolchain>/<version>/<toolchain>/bin/<compiler>
        if len(parts) < 5 or parts[-2] != 'bin' or parts[-3] != parts[-5]:
            die('%s: the compiler %s is not in an ESP-IDF tools folder' % (kind, self.gcc))
        self.compiler = re.sub(r'\.exe$', '', parts[-1])
        self.toolchain, self.toolchain_version = parts[-3], parts[-4]
        self.idf = self.work = None
        for a in args:
            m = re.match(r'-fmacro-prefix-map=(.*)=(.*)$', a)
            if m and m.group(2) == '/IDF':
                self.idf = norm(m.group(1))
            elif m and m.group(2) == '.':
                self.work = norm(m.group(1))
        if not (self.idf and self.work):
            die('%s: no -fmacro-prefix-map of ESP-IDF and of the source in the build' % kind)
        self.idf_ver = next((re.sub(r'[\\"]', '', a.split('=', 1)[1]) for a in args if a.startswith('-DIDF_VER=')), '')
        self.specs = ''
        self.cflags, self.inc = [], []
        src = e['file'].replace('\\', '/')
        skip = False
        for a in self.expand(args[1:]):
            if skip:
                skip = False
                continue
            if a in ('-o', '-c'):
                skip = a == '-o'
                continue
            if a.replace('\\', '/').endswith(src) or a.replace('\\', '/') == src:
                continue
            if a.startswith('-I'):
                self.inc.append(norm(a[2:]))
            elif a.startswith('-fmacro-prefix-map='):
                # (__FILE__ of an ESP-IDF header as in the firmware; tfn_app.py maps the app folder to .)
                if a.endswith('=/IDF'):
                    self.cflags.append('-fmacro-prefix-map=$IDF_PATH=/IDF')
            elif a.startswith('-fdiagnostics-color'):
                continue
            elif re.match(r'-DSCN_HAS_', a):
                continue                    # (the scenes of the SCENES app: not an option of the kind)
            elif a.startswith('-specs='):
                self.specs = a.split('=', 1)[1].strip('"')
                self.cflags.append('-specs=$SDK/kinds/%s/picolibc.specs' % kind)
            else:
                self.cflags.append(a)
        # the include folders of each component of the build (the order: the template first, then the others)
        self.inc_build = list(self.inc)
        seen = set(self.inc_build)
        for x in cc:
            xa = args_of(x)
            for i, a in enumerate(xa):
                p = a[2:] or (xa[i + 1] if i + 1 < len(xa) else '') if a.startswith('-I') else ''
                if p and norm(p) not in seen:
                    seen.add(norm(p))
                    self.inc_build.append(norm(p))
        self.config = os.path.join(self.bdir, 'config')
        for a in self.cflags:
            if re.search(r'[A-Za-z]:/', a.replace('\\', '/')) or ('/' in a and '$' not in a and not a.startswith('-D')):
                die('%s: a compile option with a path of the build PC: %s' % (kind, a))

    def expand(self, args):
        out = []
        for a in args:
            if a.startswith('@'):
                f = a[1:].strip('"')
                out += tfn_pkg.cmd_split(open(f).read().replace('\n', ' '))
            else:
                out.append(a)
        return out

    def idf_dirs(self, lst):
        # the ESP-IDF folders of a list (in its order) as paths under the ESP-IDF folder
        return [rel_under(p, self.idf) for p in lst if path_under(p, self.idf)]

    def dropped(self):
        # the include folders of the build that are not in the package: not ESP-IDF, not the configuration, not the
        # source of the firmware (main/, apps/, libs/, third_party/ and the made app lists: the package has the public part of them)
        own = [self.work + '/main', self.work + '/apps', self.work + '/libs', self.work + '/third_party',
               norm(self.bdir) + '/apps']
        return [p for p in self.inc_build if not path_under(p, self.idf) and norm(p) != norm(self.config)
                and not any(path_under(p, o) for o in own)]

    def dropped_names(self):
        # the component of each dropped folder (managed_components/<name>, components/<name>, esp-idf/<name>)
        out = set()
        for p in self.dropped():
            r = rel_under(p, self.work) if path_under(p, self.work) else rel_under(p, self.bdir)
            out.add('/'.join(r.split('/')[:2]))
        return sorted(out)


def lib_dir(src, lib):
    """The folder of a lib in the source tree: libs/<lib>/, or third_party/<lib>/ (the code of other projects)."""
    for top in ('libs', 'third_party'):
        d = os.path.join(src, top, lib)
        if os.path.isdir(d):
            return d
    return os.path.join(src, 'libs', lib)


def flags_text(k, base, full, pkg_rev):
    lines = ['# TFN OS SDK: the compile options of the kind %s (sdk_pack.py made this file from a DEV build).' % k.kind,
             '# $SDK = the folder of the SDK package, $IDF_PATH = the ESP-IDF folder (%s).' % k.idf_ver,
             'kind=' + k.kind, 'idf=' + k.idf_ver, 'compiler=' + k.compiler, 'toolchain=' + k.toolchain,
             'toolchain_version=' + k.toolchain_version, 'arch=' + ('riscv' if 'riscv' in k.compiler else 'xtensa'),
             'sdk=' + pkg_rev, '[cflags]'] + k.cflags
    head = ['-I$SDK/kinds/%s/config' % k.kind, '-I$SDK/include', '-I$SDK/libs']
    lines += ['[include]'] + head + ['-I$IDF_PATH/' + d for d in base]
    lines += ['[include_all]'] + head + ['-I$IDF_PATH/' + d for d in full]
    return '\n'.join(lines) + '\n'


def real_flags(k, pkg, dirs):
    sub = lambda s: s.replace('$SDK', norm(pkg)).replace('$IDF_PATH', k.idf)
    head = ['-I' + norm(os.path.join(pkg, 'kinds', k.kind, 'config')), '-I' + norm(os.path.join(pkg, 'include')),
            '-I' + norm(os.path.join(pkg, 'libs'))]
    return [sub(a) for a in k.cflags] + head + ['-I' + k.idf + '/' + d for d in dirs]


def probe_text(pkg, libs=True, common=True):
    hdrs = sorted(f for f in os.listdir(os.path.join(pkg, 'include', 'tfn')) if f.endswith('.h'))
    t = ['#include <stdint.h>', '#include <stdbool.h>', '#include <string.h>']
    if common:
        t += ['#include "%s"' % h for h in COMMON_IDF]
    t += ['#include "tfn/%s"' % h for h in hdrs]
    if libs:
        # (the main header of each lib; a lib with no <lib>.h: each of its headers)
        for lib in HEADER_LIBS + MODULE_LIBS:
            d = os.path.join(pkg, 'libs', lib)
            if not os.path.isdir(d):
                continue                    # (a lib that the source tree does not have: see main())
            main = lib + '.h'
            t += ['#include "%s/%s"' % (lib, h)
                  for h in ([main] if os.path.exists(os.path.join(d, main)) else sorted(f for f in os.listdir(d) if f.endswith('.h')))]
    return '\n'.join(t) + '\n'


def preprocess(k, pkg, dirs, probe, work, show=False):
    out = os.path.join(work, 'probe.i')
    cmd = [k.gcc] + real_flags(k, pkg, dirs) + ['-E', '-dD'] + (['-H'] if show else []) + [probe, '-o', out]
    r = subprocess.run(cmd, capture_output=True, cwd=work)
    if r.returncode:
        return None, r.stderr.decode(errors='replace').replace('\r', '')
    return open(out, 'rb').read(), r.stderr.decode(errors='replace').replace('\r', '')


def smallest(k, pkg, full, probe, work):
    """The smallest list of the ESP-IDF folders of full (in its order) that gives the same preprocessed text."""
    ref, err = preprocess(k, pkg, full, probe, work, show=True)
    if ref is None:
        print(err[-3000:])
        die('%s: the probe of the public headers does not compile with the full include list' % k.kind)
    opened = [norm(m.group(1)) for m in re.finditer(r'^\.+ (.+)$', err, re.M)]
    cand = [d for d in full if any(path_under(f, k.idf + '/' + d) for f in opened)]
    got, _ = preprocess(k, pkg, cand, probe, work)
    if got != ref:
        cand = list(full)
    for d in list(cand):
        trial = [x for x in cand if x != d]
        got, _ = preprocess(k, pkg, trial, probe, work)
        if got == ref:
            cand = trial
    return cand


def check_kind(k, pkg, dirs, work):
    # board.h of the package must select boards/<kind>/board_caps.h with the sdkconfig.h of the kind, and the app_t
    # layout must be APP_T_LAYOUT
    probe = os.path.join(work, 'layout.c')
    asserts = ''.join('_Static_assert(offsetof(app_t, %s) == %d, "%s");\n' % (f, o, f) for f, o in APP_T_LAYOUT.items())
    open(probe, 'w', newline='\n').write('#include <stddef.h>\n#include "tfn/app.h"\n#include "tfn/board.h"\n'
                                          '_Static_assert(sizeof(void *) == 4, "ptr");\n' + asserts)
    cmd = [k.gcc] + real_flags(k, pkg, dirs) + ['-fsyntax-only', '-H', probe]
    r = subprocess.run(cmd, capture_output=True, cwd=work)
    err = r.stderr.decode(errors='replace').replace('\r', '')
    if r.returncode:
        print(err[-3000:])
        die('%s: the app_t layout of the package is not the layout that tfn_app.py reads' % k.kind)
    caps = [norm(m.group(1)) for m in re.finditer(r'^\.+ (.+board_caps\.h)$', err, re.M)]
    want = norm(os.path.join(pkg, 'boards', k.kind, 'board_caps.h')).lower()
    if not caps or norm(os.path.abspath(caps[0])).lower() != want:
        die('%s: tfn/board.h of --src selects %s with the sdkconfig.h of this build (it must be boards/%s): the source '
            'does not fit the build (an older or a newer state); give the --src of the build'
            % (k.kind, caps[0] if caps else 'no board_caps.h', k.kind))


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)


def private_scan(pkg, roots):
    """Stop when a text file of the package has a path of the build PC, a private address or a private name. The
    private names (tests/secrets/private_names.txt) apply to all lines of a document and to the comments of a C file
    (as tests/style/check_comments.py: a number in the code is not a name)."""
    pats = [re.compile(re.escape(norm(r)), re.I) for r in roots] + [
        re.compile(r'[A-Za-z]:[\\/]Users[\\/]', re.I), re.compile(r'Dropbox', re.I),
        re.compile(r'\b(192\.168|10\.\d{1,3}|172\.(1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b')]
    names = None
    path = os.path.join(ROOT, 'tests', 'secrets', 'private_names.txt')
    if os.path.exists(path):
        parts = [ln.strip() for ln in open(path, encoding='utf-8') if ln.strip() and not ln.startswith('#')]
        if parts:
            names = re.compile('|'.join('(?:%s)' % p for p in parts))
    sys.path.insert(0, os.path.join(ROOT, 'tests', 'style'))
    from check_comments import c_comments
    hits = []
    for d, _, files in os.walk(pkg):
        for f in files:
            p = os.path.join(d, f)
            try:
                t = open(p, encoding='utf-8').read()
            except UnicodeDecodeError:
                continue
            lines = list(enumerate(t.splitlines(), 1))
            text = c_comments(t) if f.endswith(('.c', '.h', '.inc')) else lines
            for i, line in lines:
                if any(pat.search(line) for pat in pats):
                    hits.append('%s:%d: %s' % (os.path.relpath(p, pkg), i, line.strip()[:120]))
            for i, line in text:
                if names and names.search(line):
                    hits.append('%s:%d: (a private name) %s' % (os.path.relpath(p, pkg), i, line.strip()[:120]))
    return hits


def git_state(src):
    r = subprocess.run(['git', '-C', src, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build-root', default=os.path.join(ROOT, 'build_dev'))
    ap.add_argument('--src', default=ROOT)
    ap.add_argument('--kinds', default='')
    ap.add_argument('--out', default=os.path.join(ROOT, 'sdk', 'out', 'tfn-sdk'))
    ap.add_argument('--force', action='store_true', help='write over an old package in --out')
    ap.add_argument('--no-zip', action='store_true')
    args = ap.parse_args()
    pkg = os.path.abspath(args.out)
    if os.path.isdir(pkg) and os.listdir(pkg):
        if not (args.force and os.path.exists(os.path.join(pkg, 'sdk.json'))):
            die('%s is not empty (give --force to write over an old package)' % pkg)
        shutil.rmtree(pkg)
    src = os.path.abspath(args.src)
    kinds = [k for k in (args.kinds.split(',') if args.kinds else KINDS)
             if os.path.exists(os.path.join(args.build_root, k, 'compile_commands.json'))]
    if not kinds:
        die('no DEV build in %s' % args.build_root)
    abi = json.load(open(os.path.join(src, 'main', 'tfn_abi.json')))
    # the files of the package
    for f in sorted(os.listdir(os.path.join(src, 'main', 'tfn'))):
        if f.endswith('.h'):
            copy(os.path.join(src, 'main', 'tfn', f), os.path.join(pkg, 'include', 'tfn', f))
    for kind in kinds:
        copy(os.path.join(src, 'boards', kind, 'board_caps.h'), os.path.join(pkg, 'boards', kind, 'board_caps.h'))
    # a source tree with fewer libs (for example a tree with fewer apps): the package has only the libs of --src, and
    # sdk.json lists them
    absent = [lib for lib in HEADER_LIBS + MODULE_LIBS + SOURCE_LIBS
              if not os.path.isdir(lib_dir(src, lib))]
    if absent:
        print('NOTE: the libs %s are not in %s: the package does not have them' % (', '.join(absent), src))
    for lib in HEADER_LIBS + MODULE_LIBS + SOURCE_LIBS:
        if lib in absent:
            continue
        # (a module lib: the headers, the parts of MODULE_SRC and the licence; the code is in the firmware)
        if lib in LIB_LICENCES:
            copy(os.path.join(src, *LIB_LICENCES[lib].split('/')), os.path.join(pkg, 'libs', lib, 'LICENSE'))
        for f in sorted(os.listdir(lib_dir(src, lib))):
            if f == 'LICENSE' or f in MODULE_SRC.get(lib, ()) or \
                    f.endswith(('.h',) if lib in MODULE_LIBS else ('.h', '.c', '.inc')):
                copy(os.path.join(lib_dir(src, lib), f), os.path.join(pkg, 'libs', lib, f))
    for f in HOST_FILES:
        copy(os.path.join(ROOT, 'sdk', 'tools', 'host', f), os.path.join(pkg, 'host', f))
    for f in TOOL_FILES:
        copy(os.path.join(HERE, f), os.path.join(pkg, 'tools', f))
    fws = [p for p in (os.path.join(ROOT, *x.split('/')) for x in FW_SIGN) if os.path.isfile(p)]
    if not fws:
        die('no fw_sign.py (%s)' % ' or '.join(FW_SIGN))
    copy(fws[0], os.path.join(pkg, 'tools', 'fw_sign.py'))
    for f in DOC_FILES:
        copy(os.path.join(ROOT, 'sdk', 'docs', f), os.path.join(pkg, 'docs', f))
    for ex in EXAMPLES:
        for f in sorted(os.listdir(os.path.join(ROOT, 'sdk', 'examples', ex))):
            copy(os.path.join(ROOT, 'sdk', 'examples', ex, f), os.path.join(pkg, 'examples', ex, f))
    copy(os.path.join(src, 'main', 'tfn_abi.json'), os.path.join(pkg, 'tfn_abi.json'))
    copy(os.path.join(ROOT, 'LICENSE'), os.path.join(pkg, 'LICENSE'))
    open(os.path.join(pkg, 'README.md'), 'w', newline='\n').write(
        '# The TFN OS SDK package\n\nWith this package, an app of TFN OS builds in its own folder, out of the TFN OS repo. '
        'Start at\n[docs/app_repo.md](docs/app_repo.md). The first build:\n\n```\n'
        'python tools/tfn_app.py build examples/counter --kinds lcd349 --idf <esp-idf folder>\n```\n')
    rev = 'abi%d.%d' % (abi['version'], abi.get('rev', 0))
    info = {'tfn_sdk': 1, 'abi': abi['version'], 'abi_rev': abi.get('rev', 0), 'source': git_state(src),
            'made': datetime.date.today().isoformat(), 'kinds': {},
            'header_libs': [x for x in HEADER_LIBS if x not in absent],
            'module_libs': [x for x in MODULE_LIBS if x not in absent],
            'source_libs': [x for x in SOURCE_LIBS if x not in absent],
            'common_idf': list(COMMON_IDF), 'app_t': APP_T_LAYOUT}
    work = tempfile.mkdtemp(prefix='tfn_sdk_')
    roots = {args.build_root, src, ROOT, os.path.expanduser('~')}
    for kind in kinds:
        k = Kind(kind, args.build_root)
        roots |= {k.idf, k.work, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(k.gcc))))}
        copy(os.path.join(k.config, 'sdkconfig.h'), os.path.join(pkg, 'kinds', kind, 'config', 'sdkconfig.h'))
        if k.specs:
            copy(k.specs, os.path.join(pkg, 'kinds', kind, 'picolibc.specs'))
        full = k.idf_dirs(k.inc_build)
        check_kind(k, pkg, full, work)
        probe = os.path.join(work, 'probe.c')
        open(probe, 'w', newline='\n').write(probe_text(pkg))
        base = smallest(k, pkg, full, probe, work)
        open(probe, 'w', newline='\n').write(probe_text(pkg, libs=False, common=False))
        public = smallest(k, pkg, base, probe, work)
        open(os.path.join(pkg, 'kinds', kind + '.flags'), 'w', newline='\n').write(flags_text(k, base, full, rev))
        info['kinds'][kind] = {'idf': k.idf_ver, 'compiler': k.compiler, 'toolchain': k.toolchain,
                               'toolchain_version': k.toolchain_version, 'include': len(base),
                               'include_public': public, 'include_all': len(full)}
        print('%-7s %s %s: %d ESP-IDF include folders of %d (the public headers alone: %d); not in the package: %s'
              % (kind, k.compiler, k.toolchain_version, len(base), len(full), len(public),
                 ', '.join(k.dropped_names()) or '-'))
        info['kinds'][kind]['not_in_package'] = k.dropped_names()
    open(os.path.join(pkg, 'sdk.json'), 'w', newline='\n').write(json.dumps(info, indent=1) + '\n')
    hits = private_scan(pkg, [r for r in roots if r and len(norm(r)) > 3])
    if hits:
        print('\n'.join(hits[:40]))
        die('the package has %d lines with a private path, address or name' % len(hits))
    print('package %s (%s, kinds %s)' % (pkg, rev, ','.join(kinds)))
    if not args.no_zip:
        z = pkg + '.zip'
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
            for d, _, files in sorted(os.walk(pkg)):
                for f in sorted(files):
                    p = os.path.join(d, f)
                    zf.write(p, os.path.join(os.path.basename(pkg), os.path.relpath(p, pkg)).replace('\\', '/'))
        print('zip %s (%d KB)' % (z, os.path.getsize(z) // 1024))


if __name__ == '__main__':
    main()
