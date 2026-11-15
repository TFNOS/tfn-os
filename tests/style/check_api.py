"""The apps use only the public API of TFN OS (P3 step R10).

The public headers are in main/tfn/ (an app includes "tfn/gfx.h"). main/<name>.h is the OS view: it includes
tfn/<name>.h and adds the names that only the OS uses. The checks:
  1. a file of apps/, libs/, sdk/examples/ and sdk/templates/ (not tools/, not test/) includes no other file of
     main/ (no OS header, no .c or .inc file of the OS);
  2. a public header includes only public headers (main/tfn/), boards/<kind>/ files, and the C, ESP-IDF and
     FreeRTOS headers;
  3. each ABI alias (TFN_ABI_ALIAS(old, new) in main/) names two ABI names: the old name of the installed apps and
     the name of the public API;
  4. a file of apps/, sdk/examples/ and sdk/templates/ (not tools/, not test/) includes a lib of libs/ only in the
     SDK form "<lib>/<file>" ("tfdraw/tfdraw.h"): not by its path in the repo ("../../libs/tfdraw/tfdraw.h") and not
     by its bare name ("tfdraw.h", which the build finds through the folder list). The SDK package has libs/ as an
     include folder, so the same include works out of the repo (sdk/docs/app_repo.md). A file of libs/ can include
     a file of its own lib or of another lib by a relative path.

tests/style/api_allow.txt lists the old places that still include an OS file (file, the included name, the
reason): the extras of PLAN 5.2 and the private apps. A place that is not in the list is a fault, and an entry that
no file needs any more is a fault too: the list only gets shorter.

The apps are apart from each other (PLAN 7.2): a file of apps/<app>/, libs/, sdk/examples/ and sdk/templates/ (not
tools/, not test/) includes no file of another app folder: not by a relative path ("../clock/ck_fonts.inc") and not
by its bare name (the build has each apps/<app>/ folder in its include folders). A part that more than one app uses
goes into a lib (libs/<name>/). The same allow list (file, apps/<app>/<file>) takes an old place, and it only gets
shorter. A copy of the file of another app (its first line is "// A copy of <path> ...", made by a tool, for
example apps/cruise/tools/sv_tunes.py) must be the same as its source after that line.

Usage:
  python tests/style/check_api.py
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALLOW = os.path.join(ROOT, 'tests', 'style', 'api_allow.txt')
MAIN = os.path.join(ROOT, 'main')
PUB = os.path.join(MAIN, 'tfn')
EXT = ('.c', '.h', '.inc', '.cc', '.cpp')
APP_ROOTS = ('apps/', 'libs/', 'sdk/examples/', 'sdk/templates/')
INC = re.compile(r'^\s*#\s*(?:include|if|elif)\b.*?(?:#\s*include\s*|__has_include\s*\(\s*)"([^"]+)"', re.M)
INC2 = re.compile(r'(?:#\s*include\s*|__has_include\s*\(\s*)"([^"]+)"')
ALIAS = re.compile(r'^\s*TFN_ABI_ALIAS\(\s*(\w+)\s*,\s*(\w+)\s*\)', re.M)
# the lib folders: libs/<lib>/ and third_party/<lib>/ (the code of other projects); an app includes both in the SDK
# form "<lib>/<file>"
LIB_ROOTS = [os.path.join(ROOT, 'libs'), os.path.join(ROOT, 'third_party')]
SDK_FORM_ROOTS = ('apps/', 'sdk/examples/', 'sdk/templates/')
LIB_NAMES = set(n for r in LIB_ROOTS if os.path.isdir(r) for n in os.listdir(r))


def tracked():
    out = subprocess.run(['git', 'ls-files'], cwd=ROOT, capture_output=True,
                         text=True).stdout
    return [x for x in out.split('\n') if x and os.path.exists(os.path.join(ROOT, x))]


def code_only(t):
    """The text without its comments (a comment can name a header)."""
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def includes(path):
    with open(os.path.join(ROOT, path), encoding='utf-8', errors='replace') as f:
        t = code_only(f.read())
    return [m.group(1) for line in t.split('\n') if '#' in line for m in INC2.finditer(line)]


def os_file(path, name):
    """The main/ file (not main/tfn/) that '#include "name"' in path reaches, else None."""
    d = os.path.dirname(os.path.join(ROOT, path))
    local = os.path.normpath(os.path.join(d, name))
    if os.path.exists(local):
        target = local
    else:
        target = os.path.normpath(os.path.join(MAIN, name))  # -I main
        if not os.path.exists(target):
            return None  # C, ESP-IDF, FreeRTOS, a made file of the build
    rel = os.path.relpath(target, ROOT).replace(os.sep, '/')
    if rel.startswith('main/') and not rel.startswith('main/tfn/'):
        return rel
    return None


def search_dirs():
    """The folders where the build finds a bare include, in the order of the build: main/, libs/<lib>/,
    third_party/<lib>/, apps/<app>/."""
    out = [MAIN]
    for top in ('libs', 'third_party', 'apps'):
        d = os.path.join(ROOT, top)
        if os.path.isdir(d):
            out += [os.path.join(d, n) for n in sorted(os.listdir(d)) if os.path.isdir(os.path.join(d, n))]
    return out


def lib_fault(path, name, dirs):
    """The SDK form of '#include "name"' in path when it reaches a file of libs/ or third_party/ in another form,
    else None."""
    if name.startswith('/') or ':' in name:
        return None
    d = os.path.dirname(os.path.join(ROOT, path))
    if '/' in name:
        if not name.startswith('../'):
            return None  # the SDK form, a file of the app folder, tfn/<name>.h, an ESP-IDF header
        target = os.path.normpath(os.path.join(d, name))
    else:
        target = next((os.path.join(x, name) for x in [d] + dirs if os.path.isfile(os.path.join(x, name))), None)
        if not target:
            return None
    if not os.path.exists(target):
        return None
    for root in LIB_ROOTS:
        lib = os.path.dirname(os.path.relpath(target, root))
        if not lib.startswith('..') and lib:
            return os.path.relpath(target, root).replace(os.sep, '/')
    return None


def load_allow():
    allow = {}
    if not os.path.exists(ALLOW):
        return allow
    for line in open(ALLOW, encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if line:
            f, n = line.split()[:2]
            allow[(f, n)] = False
    return allow


def app_folder(rel):
    """The app folder (apps/<app>/) of a repo path, else None."""
    p = rel.split('/')
    return '/'.join(p[:2]) + '/' if len(p) > 2 and p[0] == 'apps' else None


def other_app_faults(files, allow):
    """The includes of an app, a lib or an example that reach a file of another app folder, and the old copies."""
    dirs = [MAIN]  # (the include folders of the build in their order: main/, libs/<lib>/, third_party/<lib>/, apps/<app>/)
    for top in ('libs', 'third_party', 'apps'):
        d = os.path.join(ROOT, top)
        if os.path.isdir(d):
            dirs += [os.path.join(d, n) for n in sorted(os.listdir(d)) if os.path.isdir(os.path.join(d, n))]
    faults = []
    for f in files:
        if not f.endswith(EXT) or not f.startswith(APP_ROOTS) or '/tools/' in f or '/test/' in f:
            continue
        here = os.path.dirname(os.path.join(ROOT, f))
        for n in includes(f):
            if n.startswith('/') or ':' in n:
                continue
            local = os.path.normpath(os.path.join(here, n))
            if os.path.isfile(local):
                target = local
            elif '/' in n:
                continue  # the SDK form of a lib ("tfdraw/tfdraw.h"), tfn/<name>.h, an ESP-IDF header
            else:
                target = next((os.path.join(x, n) for x in dirs if os.path.isfile(os.path.join(x, n))), None)
                if not target:
                    continue
            t = os.path.relpath(target, ROOT).replace(os.sep, '/')
            if not app_folder(t) or app_folder(t) == app_folder(f):
                continue
            if (f, t) in allow:
                allow[(f, t)] = True
            else:
                faults.append('%s includes %s, a file of another app: move the shared part into a lib (libs/<name>/)'
                              % (f, t))
    for f in files:
        if not f.startswith(('apps/', 'libs/')) or not f.endswith(EXT):
            continue
        with open(os.path.join(ROOT, f), 'rb') as h:
            data = h.read()
        m = re.match(rb'// A copy of (\S+) [^\n]*\n', data)
        if not m:
            continue
        t = re.search(rb'made by (\S+?);', m.group(0))
        src, tool = m.group(1).decode(), (t.group(1) if t else b'its tool').decode()
        path = os.path.join(ROOT, src)
        if not os.path.isfile(path) or open(path, 'rb').read() != data[m.end():]:
            faults.append('%s is not the same as its source %s: run python %s' % (f, src, tool))
    return faults


def main():
    files = tracked()
    allow = load_allow()
    faults = []
    dirs = search_dirs()
    nlib = 0
    for f in files:
        if not f.endswith(EXT):
            continue
        if f.startswith(SDK_FORM_ROOTS) and '/tools/' not in f and '/test/' not in f:
            for n in includes(f):
                want = lib_fault(f, n, dirs)
                if want:
                    faults.append('%s includes the lib file "%s": write "%s" (the SDK form)' % (f, n, want))
                elif n.split('/')[0] in LIB_NAMES and '/' in n:
                    nlib += 1
        if f.startswith(APP_ROOTS) and '/tools/' not in f and '/test/' not in f:
            for n in includes(f):
                t = os_file(f, n)
                if not t:
                    continue
                if (f, t) in allow:
                    allow[(f, t)] = True
                else:
                    b = os.path.basename(t)
                    hint = 'tfn/' + b if os.path.exists(os.path.join(PUB, b)) else 'a public header of main/tfn/'
                    faults.append('%s includes the OS file %s (use %s)' % (f, t, hint))
        if f.startswith('main/tfn/'):
            for n in includes(f):
                d = os.path.dirname(os.path.join(ROOT, f))
                if os.path.exists(os.path.normpath(os.path.join(d, n))):
                    continue  # a public sibling, or ../../boards/<kind>/
                if os.path.exists(os.path.join(MAIN, n)):
                    faults.append('the public header %s includes the OS header main/%s' % (f, n))
    # the apps are apart from each other
    faults += other_app_faults(files, allow)
    # the ABI aliases
    abi = json.load(open(os.path.join(MAIN, 'tfn_abi.json'), encoding='utf-8'))['symbols']
    nalias = 0
    for f in files:
        if f.startswith('main/') and f.endswith('.c'):
            with open(os.path.join(ROOT, f), encoding='utf-8', errors='replace') as fh:
                for old, new in ALIAS.findall(code_only(fh.read())):
                    nalias += 1
                    for x in (old, new):
                        if x not in abi:
                            faults.append('%s: the alias %s -> %s: %s is not an ABI name' % (f, old, new, x))
    for (f, n), used in sorted(allow.items()):
        # (a private file that is not in git is not in a clean checkout: its entry stays)
        if not used and os.path.exists(os.path.join(ROOT, n)):
            faults.append('api_allow.txt: %s %s is not necessary now: remove the line' % (f, n))
    for x in faults:
        print('  ' + x)
    print('API CHECK: %s (%d allowed OS includes, %d ABI aliases, %d lib includes in the SDK form)'
          % ('OK' if not faults else '%d FAULTS' % len(faults), len(allow), nalias, nlib))
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main())
