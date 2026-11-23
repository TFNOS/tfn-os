#!/usr/bin/env python3
"""The app list generator: main/apps.json -> the lists of the firmware and of the host tests.

main/apps.json is the ONE list of the apps (the menu order, the groups, the sources, the self-test tap points). This
script makes the lists that the C code and the build use. The C code never names the apps one by one:

  --cmake DIR   (main/CMakeLists.txt runs this at each CMake configure; DIR = build*/apps)
                DIR/apps_gen.h      ARCADE_APP_<ID> 1 or 0 for each app (ID = the sym without "app_", in capitals),
                                    and the old switches ARCADE_HAS_SIDEB / _BOOKS
                DIR/apps_list.inc   one line ARCADE_APP(ID, sym, group, is_new, x349, y349, xs3, ys3, test_ms) for
                                    each app of this build, in the menu order (app_menu.c, main.c, fuzz.c)
                DIR/apps.cmake      ARCADE_SRCS (the sources of this build), ARCADE_DEFS, ARCADE_LDFRAG
                DIR/apps.lf         the PSRAM .bss mapping of the objects of this build (scheme: main/linker.lf)
                DIR/apps_built.json what this build has (the size tool and the composer of the manager read it)
  --host DIR    (tests/host/build.sh) the same headers for the host fuzz with ALL apps (also the apps that are
                off), and DIR/srcs.txt (the sources, with --prefix) and DIR/flags.txt (the -D flags). With --on-flags,
                --select, --add or --remove: the host files of that selection (a host test of the menu of a set)
  --all         only refresh main/apps_all.inc
  --check       check main/apps.json (names, files, links between the entries) and stop
  --list        show the apps and the selection

Each mode also keeps main/apps_all.inc current: all apps, for the host tests that have no build folder (app.h takes
it when it does not find apps_gen.h). A file is written only when its text changes (no needless rebuilds).

THE SLIM FIRMWARE: a board kind of "slim": {"kinds": [...]} of apps.json gets only the core apps (the
selection does not count for it), and the code of ALL modules and of all the scene art: their names are ABI names that
the app files call. The other apps reach such a board as app files (the manager store; the keeper installs the apps
that the board had before, except "skip").

THE SELECTION (--cmake, --list): the "on" flags of apps.json, or a file (--select FILE; idf.py -D ARCADE_SELECT=FILE):
  {"apps": [...]}            exactly these apps (names as in the menu; the core apps are always in), or
  {"add": [...], "remove": [...]}   changes to the "on" flags (both can be with "apps"); "about" is a note.
--add NAME and --remove NAME change the selection after the file (CMake:
-D ARCADE_SIDEB=1 = --add "SIDE B"). An app with "requires" builds only when that file exists in main/.
A core app cannot leave. An app that is off but that a selected app "needs" (or holds its app_t:
"with") is compiled and linked, but it is not in the menu.

Exit code 0 = good; 1 = an error in apps.json, in the selection or in the arguments (the message says what).
"""
import argparse
import json
import os
import re
import sys
import time

MAIN = os.path.dirname(os.path.abspath(__file__))
GROUPS = ('GAMES', 'UTIL', 'SETTINGS', 'SYSTEM')
APP_KEYS = {'name', 'sym', 'group', 'on', 'new', 'core', 'why_core', 'with', 'srcs', 'uses', 'needs', 'scn_ready',
            'psram', 'psram_scenes', 'private', 'requires', 'guard', 'host', 'test349', 'tests3', 'test_ms', 'emoji',
            'about', 'watch', 'form', 'form_help', 'oauth',    # (these four: only sdk/tools/mk_app.py reads them)
            'link',     # prebuilt libraries (.a) of the ESP build of an app file (only sdk/tools/mk_app.py reads it)
            'class',    # "b" = a class B game (not installed by default; sdk/tools/store_class.py sends it to the manager)
            'only',     # the board kinds that have the app (the store apps of the 3.49 only): the host fuzz skips the others
            'extra',    # the extra (core.extras) that the app belongs to: the app leaves when that extra is off
            'store_on'}  # the slim kinds where a core app is an app file of the store, not in the firmware (a big core
                         # app on a kind with a small app slot)
SELECT_KEYS = {'apps', 'add', 'remove', 'about'}
HEADER_NOTE = 'Made by main/apps_gen.py from main/apps.json: do not edit (change main/apps.json).'
TEST_MS = 6000          # the self-test time of an app when apps.json gives no test_ms


class AppsError(Exception):
    pass


# ------------------------------------------------------------------------------------------------ the source folders
# main/ = TFN OS; libs/<module>/ = the shared code of the apps; third_party/<name>/ = the code of other projects
# (NOTICE.md); apps/<app>/ = one folder for each app. apps.json keeps the plain file names: this index finds each
# name in its folder.
CODE_EXT = ('.c', '.cc', '.h', '.inc')
_INDEX = {}


def source_dirs(main_dir):
    """The source folders: main/, then each folder of libs/, of third_party/ and of apps/ (one level)."""
    root = os.path.dirname(os.path.abspath(main_dir))
    out = [os.path.abspath(main_dir)]
    for top in ('libs', 'third_party', 'apps'):
        d = os.path.join(root, top)
        if os.path.isdir(d):
            out += [os.path.join(d, n) for n in sorted(os.listdir(d)) if os.path.isdir(os.path.join(d, n))]
    return out


def source_index(main_dir):
    """(name -> full path, the code names that are in more than one folder)."""
    key = os.path.abspath(main_dir)
    if key not in _INDEX:
        idx, dup = {}, []
        for d in source_dirs(main_dir):
            for n in sorted(os.listdir(d)):
                if not os.path.isfile(os.path.join(d, n)):
                    continue
                if n in idx:
                    if n.endswith(CODE_EXT):
                        dup.append('%s (%s and %s)' % (n, os.path.relpath(idx[n], os.path.dirname(key)), os.path.relpath(os.path.join(d, n), os.path.dirname(key))))
                    continue
                idx[n] = os.path.join(d, n)
        _INDEX[key] = (idx, dup)
    return _INDEX[key]


def locate(main_dir, f):
    """The full path of a source name (None: not there)."""
    return source_index(main_dir)[0].get(f)


def exists(main_dir, f):
    return locate(main_dir, f) is not None


def rel_src(main_dir, f):
    """The path of a source name relative to main/ (with /): x.c in main/, ../apps/<app>/x.c in an app folder."""
    full = locate(main_dir, f)
    return os.path.relpath(full, os.path.abspath(main_dir)).replace(os.sep, '/') if full else f


def source_path(name, main_dir=MAIN):
    """(for sdk/tools/mk_app.py and the host tools) the full path of a source name, or main/<name>."""
    return locate(main_dir, name) or os.path.join(main_dir, name)


def app_id(a):
    """The C name of an app: the sym without "app_", in capitals (app_touchlab -> TOUCHLAB)."""
    return a['sym'][4:].upper()


def load(path):
    try:
        with open(path, encoding='utf-8-sig') as f:       # (-sig: a file from PowerShell can start with a BOM)
            return json.load(f)
    except (OSError, ValueError) as e:
        raise AppsError('cannot read %s: %s' % (path, e))


# ------------------------------------------------------------------------------------------------ the checks
def check(doc, main_dir, files=True):
    """The errors of apps.json (a list of text). files=True also checks that each source file exists (an app with a
    "requires" file that is not there is not checked: it cannot build here)."""
    err = []
    for k in ('core', 'modules', 'apps'):
        if k not in doc:
            err.append('apps.json has no "%s"' % k)
    if err:
        return err
    core = doc['core']
    mods = doc['modules']
    apps = doc['apps']
    owner = {}

    def own(f, who):
        if not isinstance(f, str) or not f or '/' in f or '\\' in f:
            err.append('%s: the source "%s" must be a file name (in main/, libs/<module>/, third_party/<name>/ or apps/<app>/)' % (who, f))
            return
        if f in owner:
            err.append('%s: the source %s is also in %s (each file has one owner)' % (who, f, owner[f]))
        owner[f] = who

    lo = doc.get('link_order', [])
    if not isinstance(lo, list) or len(set(lo)) != len(lo) or \
            not all(isinstance(f, str) and f and '/' not in f and '\\' not in f for f in lo):
        err.append('link_order must be a list of file names in main/, each one time')
    for f in core.get('srcs', []):
        own(f, 'core')
    for p in core.get('psram', []):
        if p not in core.get('srcs', []):
            err.append('core: psram %s is not a core source' % p)
    for a in apps:
        if a.get('extra') and a['extra'] not in core.get('extras', {}):
            err.append('%s: the extra %s is not in core.extras' % (a.get('name'), a['extra']))
    for x, e in core.get('extras', {}).items():
        if not re.fullmatch(r'CONFIG_[A-Z0-9_]+', str(e.get('option', ''))):
            err.append('core: the extra %s needs an option CONFIG_<NAME>' % x)
        for f in e.get('srcs', []):
            if f not in core.get('srcs', []):
                err.append('core: the extra %s: %s is not a core source' % (x, f))
    for m, d in mods.items():
        for f in d.get('srcs', []):
            own(f, 'module ' + m)
        for u in d.get('uses', []):
            if u not in mods:
                err.append('module %s: it uses %s, which is not a module' % (m, u))
        for p in d.get('psram', []):
            if p not in d.get('srcs', []):
                err.append('module %s: psram %s is not one of its sources' % (m, p))
    names, syms, ids = {}, {}, {}
    for i, a in enumerate(apps):
        who = 'app %d (%s)' % (i, a.get('name', '?'))
        for k in ('name', 'sym', 'group', 'srcs'):
            if k not in a:
                err.append('%s: no "%s"' % (who, k))
        if any(k not in a for k in ('name', 'sym', 'group', 'srcs')):
            continue
        for k in a:
            if k not in APP_KEYS:
                err.append('%s: unknown field "%s"' % (who, k))
        n = a['name'].upper()
        if n in names:
            err.append('%s: the name is also app %d' % (who, names[n]))
        names[n] = i
        if not a['sym'].startswith('app_') or not a['sym'][4:].replace('_', 'a').isalnum():
            err.append('%s: the sym must be app_<letters, digits, _>' % who)
        if a['sym'] in syms:
            err.append('%s: the sym is also app %d' % (who, syms[a['sym']]))
        syms[a['sym']] = i
        if app_id(a) in ids:
            err.append('%s: the ID %s is also app %d' % (who, app_id(a), ids[app_id(a)]))
        ids[app_id(a)] = i
        if a['group'] not in GROUPS:
            err.append('%s: the group must be one of %s' % (who, ', '.join(GROUPS)))
        if a.get('core') and not a.get('on', True):
            err.append('%s: a core app must be on' % who)
        for f in a['srcs']:
            own(f, a['name'])
        for u in a.get('uses', []):
            if u not in mods:
                err.append('%s: it uses %s, which is not a module' % (who, u))
        for p in a.get('psram', []):
            if p not in a['srcs']:
                err.append('%s: psram %s is not one of its sources' % (who, p))
        for k in ('test349', 'tests3'):
            v = a.get(k)
            if v is None:
                if a['group'] != 'SYSTEM':
                    err.append('%s: no %s (the self-test tap point)' % (who, k))
            elif not (isinstance(v, list) and len(v) == 2 and all(isinstance(x, int) and 0 <= x < 1000 for x in v)):
                err.append('%s: %s must be [x, y]' % (who, k))
        t = a.get('test_ms', TEST_MS)
        if not isinstance(t, int) or t < 500:
            err.append('%s: test_ms must be a number of ms (500 or more)' % who)
        if 'guard' in a and not str(a['guard']).replace('_', 'A').isalnum():
            err.append('%s: the guard must be a C name' % who)
    for a in apps:
        if 'name' not in a:
            continue
        for k in ('needs',):
            for b in a.get(k, []):
                if b.upper() not in names:
                    err.append('app %s: it needs %s, which is not an app' % (a['name'], b))
        if a.get('with') and a['with'].upper() not in names:
            err.append('app %s: with %s, which is not an app' % (a['name'], a['with']))
    if files:
        for a in apps:
            if 'srcs' not in a:
                continue
            if a.get('requires') and not exists(main_dir, a['requires']):
                continue
            for f in a['srcs']:
                if not exists(main_dir, f):
                    err.append('app %s: the file main/%s is not there' % (a['name'], f))
            for k in a.get('scn_ready', []):
                if not exists(main_dir, 'scn_%s.c' % k):
                    err.append('app %s: the scene file main/scn_%s.c is not there' % (a['name'], k))
        for f in core.get('srcs', []):
            if not exists(main_dir, f):
                err.append('core: the file main/%s is not there' % f)
        for m, d in mods.items():
            for f in d.get('srcs', []):
                if not exists(main_dir, f):
                    err.append('module %s: the file main/%s is not there' % (m, f))
    if files:
        for d in source_index(main_dir)[1]:
            err.append('the code file %s is in two folders (each name in one folder only)' % d)
    return err


# ------------------------------------------------------------------------------------------------ the selection
def finder(doc):
    """A function: a name (the menu name, the ID or the sym, any case) -> the app."""
    by = {}
    for a in doc['apps']:
        for k in (a['name'], app_id(a), a['sym']):
            by[k.upper()] = a

    def find(n, where):
        a = by.get(str(n).strip().upper())
        if a is None:
            raise AppsError('%s: "%s" is not an app of main/apps.json' % (where, n))
        return a
    return find


def available(a, main_dir):
    return not a.get('requires') or exists(main_dir, a['requires'])


def slim_kind(doc, kind):
    return bool(kind) and kind in (doc.get('slim') or {}).get('kinds', [])


def select(doc, main_dir, select_file=None, add=(), remove=(), host=False, with_=(), without=(), kind=''):
    """The apps of the build (a list in the menu order) and the notes (a list of text).

    host=True: all apps (also the apps that are off), but not the apps with "host": false (unless with_ has them).
    """
    find = finder(doc)
    apps = doc['apps']
    notes = []
    if host:
        sel = {a['name'] for a in apps if a.get('host', True)}
        for n in with_:
            sel.add(find(n, '--with')['name'])
        for n in without:
            a = find(n, '--without')
            if a.get('core'):
                raise AppsError('--without: %s is a core app (%s)' % (a['name'], a.get('why_core', 'system code')))
            sel.discard(a['name'])
    else:
        sel = {a['name'] for a in apps if a.get('on', True)}
        if select_file:
            s = load(select_file)
            if not isinstance(s, dict):
                raise AppsError('%s: the selection must be a JSON object' % select_file)
            bad = set(s) - SELECT_KEYS
            if bad:
                raise AppsError('%s: unknown keys %s (use apps, add, remove, about)' % (select_file, sorted(bad)))
            where = os.path.basename(select_file)
            if 'apps' in s:
                sel = {find(n, where)['name'] for n in s['apps']}
            for n in s.get('add', []):
                sel.add(find(n, where)['name'])
            for n in s.get('remove', []):
                a = find(n, where)
                if a.get('core'):
                    raise AppsError('%s: %s is a core app, it cannot leave (%s)' %
                                    (where, a['name'], a.get('why_core', 'system code calls it')))
                sel.discard(a['name'])
        for n in add:
            sel.add(find(n, '--add')['name'])
        for n in remove:
            a = find(n, '--remove')
            if a.get('core'):
                raise AppsError('--remove: %s is a core app, it cannot leave (%s)' %
                                (a['name'], a.get('why_core', 'system code calls it')))
            sel.discard(a['name'])
    if not host and slim_kind(doc, kind):
        keep_for_kind = set((doc.get('slim') or {}).get('keep', {}).get(kind, []))
        sel = {a['name'] for a in apps if (a.get('core') or a['name'] in keep_for_kind) and kind not in a.get('store_on', [])}
        notes.append('the slim firmware of %s: only the core apps (the others are app files)' % kind)
    for a in apps:
        if not host and slim_kind(doc, kind) and kind in a.get('store_on', []):
            notes.append('%s: an app file of the store on %s (store_on)' % (a['name'], kind))
            continue
        if a.get('core') and a['name'] not in sel:
            notes.append('%s is a core app: it stays in' % a['name'])
            sel.add(a['name'])
    out = []
    for a in apps:
        if a['name'] not in sel:
            continue
        if not available(a, main_dir):
            notes.append('%s is left out: main/%s is not there%s' % (a['name'], a['requires'],
                                                                      ' (%s)' % a['about'] if a.get('about') else ''))
            continue
        out.append(a)
    return out, notes


def code_of(doc, main_dir, in_build, slim=False, off=()):
    """What the build compiles: (the sources in order, the modules, the names of the apps whose code is in, the
    defines, the PSRAM objects). slim=True: also each module and the scene art of each app (ABI names). off = the
    extras that are off (their core sources are not in the build)."""
    off_srcs = extra_files_off(doc, off)[0]
    find = finder(doc)
    mods = doc['modules']
    srcs, seen = [], set()
    mod_list, app_code, defs, psram = [], [], [], []

    def add_src(f):
        if f not in seen:
            seen.add(f)
            srcs.append(f)

    def add_mod(m, stack=()):
        if m in mod_list or m in stack:
            return
        for u in mods[m].get('uses', []):
            add_mod(u, stack + (m,))
        mod_list.append(m)
        for f in mods[m]['srcs']:
            add_src(f)
        psram.extend(mods[m].get('psram', []))

    def add_app(a, stack=()):
        if a['name'] in app_code or a['name'] in stack:
            return
        if not available(a, main_dir):
            raise AppsError('%s needs %s, but main/%s is not there' % (stack[-1] if stack else '?', a['name'],
                                                                       a['requires']))
        app_code.append(a['name'])
        for f in a['srcs']:
            add_src(f)
        for k in a.get('scn_ready', []):
            add_src('scn_%s.c' % k)
            if exists(main_dir, 'scn_%s_data.c' % k):
                add_src('scn_%s_data.c' % k)
            defs.append('SCN_HAS_%s=1' % k.upper())
            if a.get('psram_scenes'):
                psram.append('scn_%s.c' % k)
        psram.extend(a.get('psram', []))
        for u in a.get('uses', []):
            add_mod(u)
        for b in a.get('needs', []) + ([a['with']] if a.get('with') else []):
            add_app(find(b, a['name']), stack + (a['name'],))

    for f in doc['core']['srcs']:
        if f not in off_srcs:
            add_src(f)
    psram.extend(p for p in doc['core'].get('psram', []) if p not in off_srcs)
    for a in in_build:
        add_app(a)
    if slim:
        # (the slim firmware: the modules and the scene art stay, with no app: the app files call their names)
        for m in mods:
            add_mod(m)
        for a in doc['apps']:
            if not available(a, main_dir):
                continue
            for k in a.get('scn_ready', []):
                if 'SCN_HAS_%s=1' % k.upper() in defs:
                    continue
                add_src('scn_%s.c' % k)
                if exists(main_dir, 'scn_%s_data.c' % k):
                    add_src('scn_%s_data.c' % k)
                defs.append('SCN_HAS_%s=1' % k.upper())
                if a.get('psram_scenes'):
                    psram.append('scn_%s.c' % k)
    # the link order: the order of link_order (1.0.32), then the other files in the order above. (The order of the
    # objects in libmain.a changes the image: the same apps in the menu order took 5.5 KB more flash.)
    rank = {f: i for i, f in enumerate(doc.get('link_order', []))}
    pos = {f: i for i, f in enumerate(srcs)}
    srcs.sort(key=lambda f: (0, rank[f]) if f in rank else (1, pos[f]))
    return srcs, mod_list, app_code, defs, psram


# ------------------------------------------------------------------------------------------------ the outputs
def write(path, text):
    """Write the file only when its text changes. Returns True when it wrote."""
    try:
        with open(path, encoding='utf-8', newline='') as f:
            if f.read() == text:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    tmp = path + '.tmp%d' % os.getpid()
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    for i in range(10):
        try:
            os.replace(tmp, path)
            return True
        except PermissionError:
            # (a sync tool or an editor can hold the file for a moment: try again, then give up)
            if i == 9:
                os.remove(tmp)
                raise
            time.sleep(0.3)
    return True


def app_line(a):
    p349 = a.get('test349') or [-1, -1]
    ps3 = a.get('tests3') or [-1, -1]
    ms = a.get('test_ms', TEST_MS) if a.get('test349') else 0
    return 'ARCADE_APP(%s, %s, %s, %d, %d, %d, %d, %d, %d)' % (app_id(a), a['sym'], a['group'], 1 if a.get('new') else 0,
                                                            p349[0], p349[1], ps3[0], ps3[1], ms)


LIST_NOTE = ['// ARCADE_APP(ID, sym, group, is_new, x349, y349, xs3, ys3, test_ms): ID = the C name (ARCADE_APP_<ID>),',
             '// sym = the app_t, group = GAMES / UTIL / SETTINGS / SYSTEM (not in the menu), is_new = the NEW pill,',
             '// x349 y349 / xs3 ys3 = the self-test tap point (logical) on the 3.49 / the CoreS3 (-1 = no self-test),',
             '// test_ms = the self-test time of the app.']


def gen_list(apps):
    lines = ['// ' + HEADER_NOTE, '// The apps of this build, in the menu order.'] + LIST_NOTE
    lines += [app_line(a) for a in apps]
    return '\n'.join(lines) + '\n'


def gen_all(doc):
    lines = ['// ' + HEADER_NOTE,
             '// ALL apps of main/apps.json, in the menu order, also the apps that are off: app.h takes this list when',
             '// there is no apps_gen.h (a host test with only -I main). The firmware and tests/host/build.sh use the',
             '// list of their build folder (apps_list.inc). An app with a guard is in only when its switch is 1.'] + LIST_NOTE
    for a in doc['apps']:
        if a.get('guard'):
            lines += ['#if %s' % a['guard'], app_line(a), '#endif']
        else:
            lines.append(app_line(a))
    return '\n'.join(lines) + '\n'


def gen_header(doc, in_build, what):
    names = {a['name'] for a in in_build}
    lines = ['// ' + HEADER_NOTE, '// %s' % what, '#pragma once', '', '#define ARCADE_APPS_GEN 1',
             '#define ARCADE_APPS_COUNT %d' % len(in_build), '']
    for a in doc['apps']:
        lines.append('#define ARCADE_APP_%s %d' % (app_id(a), 1 if a['name'] in names else 0))
    lines.append('')
    for a in doc['apps']:
        if a.get('guard'):
            lines.append('#define %s %d' % (a['guard'], 1 if a['name'] in names else 0))
    return '\n'.join(lines) + '\n'


def cmake_list(name, items):
    return 'set(%s\n%s)\n' % (name, ''.join('    "%s"\n' % i for i in items))


def gen_lf(psram):
    objs = []
    for f in psram:
        o = os.path.splitext(f)[0]
        if o not in objs:
            objs.append(o)
    lines = ['# ' + HEADER_NOTE,
             '# The objects of this build whose static state (.bss) goes to PSRAM. The scheme is in main/linker.lf.',
             '[mapping:arcade_apps]', 'archive: libmain.a', 'entries:',
             '    if SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY = y:']
    lines += ['        %s (arcade_extram_bss)' % o for o in objs]
    return '\n'.join(lines) + '\n'


def extras_off(doc, options):
    """The ids of the extras (core.extras of apps.json) whose Kconfig option is in options (the options that are off)."""
    return {x for x, e in doc['core'].get('extras', {}).items() if e['option'] in set(options)}


def extra_files_off(doc, off):
    """(the core sources, the embedded pages) of the extras that are off."""
    srcs, pages = set(), []
    for x in sorted(off):
        e = doc['core']['extras'][x]
        srcs.update(e.get('srcs', []))
        pages += e.get('embed', [])
    return srcs, pages


def summary(doc, in_build, app_code, how):
    names = {a['name'] for a in in_build}
    off = [a['name'] for a in doc['apps'] if a['name'] not in names]
    code_only = [n for n in app_code if n not in names]
    s = 'Arcade apps: %d in this build (%s)' % (len(in_build), how)
    if off:
        s += '; %d out: %s' % (len(off), ', '.join(off))
    if code_only:
        s += '; built for other apps, not in the menu: %s' % ', '.join(code_only)
    return s


def main(argv=None):
    # (a Windows console can have a code page with no emoji: a note must never stop a configure)
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(errors='replace')
        except (AttributeError, ValueError):
            pass
    p = argparse.ArgumentParser(description='The app list generator (main/apps.json).')
    m = p.add_mutually_exclusive_group(required=True)
    m.add_argument('--cmake', metavar='DIR', help='write the lists of a firmware build into DIR')
    m.add_argument('--host', metavar='DIR', help='write the lists of the host fuzz (all apps) into DIR')
    m.add_argument('--all', action='store_true', help='only refresh main/apps_all.inc')
    m.add_argument('--check', action='store_true', help='check main/apps.json')
    m.add_argument('--list', action='store_true', help='show the apps and the selection')
    p.add_argument('--apps', default=os.path.join(MAIN, 'apps.json'), help='the app list (default main/apps.json)')
    p.add_argument('--main', default=MAIN, help='the source folder (default: the folder of this script)')
    p.add_argument('--select', metavar='FILE', help='a selection file (JSON)')
    p.add_argument('--add', action='append', default=[], metavar='NAME', help='add an app to the selection')
    p.add_argument('--remove', action='append', default=[], metavar='NAME', help='take an app out')
    p.add_argument('--extra-off', action='append', default=[], metavar='CONFIG_TFN_EXTRA_X',
                   help='--cmake: a Kconfig option of an extra that is off (main/CMakeLists.txt gives them)')
    p.add_argument('--with', dest='with_', action='append', default=[], metavar='NAME',
                   help='--host: also an app with "host": false')
    p.add_argument('--without', action='append', default=[], metavar='NAME', help='--host: leave an app out')
    p.add_argument('--prefix', default='', help='--host: the path of main/ for srcs.txt (for example ../main/)')
    p.add_argument('--kind', default='', help='--cmake: the board kind (lcd349, cores3, am18, watcher)')
    p.add_argument('--project-ver', default='', help='--cmake: the firmware version (for apps_built.json)')
    p.add_argument('--no-all', action='store_true', help='do not refresh main/apps_all.inc')
    p.add_argument('--on-flags', action='store_true', help='--host: the "on" flags of apps.json, not all apps')
    a = p.parse_args(argv)
    try:
        doc = load(a.apps)
        err = check(doc, a.main, files=True)
        if err:
            raise AppsError('main/apps.json has %d faults:\n  ' % len(err) + '\n  '.join(err))
        if a.check:
            print('main/apps.json: %d apps, %d modules, %d core files: OK' %
                  (len(doc['apps']), len(doc['modules']), len(doc['core']['srcs'])))
            return 0
        if not a.no_all:
            try:
                write(os.path.join(a.main, 'apps_all.inc'), gen_all(doc))
            except OSError as e:
                # (only the host tests with no build folder read it: a lock must not stop a firmware configure)
                print('note: main/apps_all.inc is not refreshed (%s): run python main/apps_gen.py --all' % e,
                      file=sys.stderr)
        if a.all:
            return 0
        if a.host and not (a.select or a.add or a.remove or a.on_flags):
            in_build, notes = select(doc, a.main, host=True, with_=a.with_, without=a.without)
            how = 'the host fuzz: all apps' + (' + ' + ', '.join(a.with_) if a.with_ else '')
        else:
            # (--host with a selection: the host files of that set, for example a host test of the menu of a set)
            sel = os.path.abspath(a.select) if a.select else None
            in_build, notes = select(doc, a.main, select_file=sel, add=a.add, remove=a.remove, kind=a.kind)
            how = ('the selection ' + os.path.basename(sel)) if sel else 'the "on" flags of main/apps.json'
            if a.add or a.remove:
                how += ''.join(' +' + n for n in a.add) + ''.join(' -' + n for n in a.remove)
        off = extras_off(doc, a.extra_off)
        if off:
            gone = [x['name'] for x in in_build if x.get('extra') in off]
            in_build = [x for x in in_build if x.get('extra') not in off]
            notes.append('the extras that are off: %s%s' % (', '.join(sorted(off)),
                                                            ' (their apps leave: %s)' % ', '.join(gone) if gone else ''))
        slim = not a.host and slim_kind(doc, a.kind)
        srcs, mods, app_code, defs, psram = code_of(doc, a.main, in_build, slim, off)
        if slim:
            how = 'the slim firmware of ' + a.kind
        if a.list:
            names = {x['name'] for x in in_build}
            for x in doc['apps']:
                flag = 'IN ' if x['name'] in names else ('code' if x['name'] in app_code else 'out')
                print('%-4s %-14s %-8s %s' % (flag, x['name'], x['group'], ' '.join(x['srcs'])))
            print(summary(doc, in_build, app_code, how))
            for n in notes:
                print('note:', n)
            return 0
        d = a.cmake or a.host
        os.makedirs(d, exist_ok=True)
        write(os.path.join(d, 'apps_gen.h'), gen_header(doc, in_build, summary(doc, in_build, app_code, how)))
        write(os.path.join(d, 'apps_list.inc'), gen_list(in_build))
        if a.host:
            hs = set(doc.get('host_skip', []))
            write(os.path.join(d, 'srcs.txt'), ' '.join(a.prefix + rel_src(a.main, f) for f in srcs if f not in hs) + '\n')
            # (and libs/ and third_party/ themselves: an app includes a lib in the SDK form "tfdraw/tfdraw.h",
            # "minimp3/minimp3.h")
            incs = [os.path.relpath(x, os.path.abspath(a.main)).replace(os.sep, '/') for x in source_dirs(a.main)] + \
                ['../libs', '../third_party']
            write(os.path.join(d, 'incs.txt'), ' '.join('-I' + a.prefix + i for i in incs) + '\n')
            write(os.path.join(d, 'flags.txt'), ' '.join('-D' + x for x in defs) + '\n')
            # the apps that only some board kinds have ("only": the store apps of the 3.49): the host fuzz skips them
            # on the other kinds (tests/host/fuzz.c)
            write(os.path.join(d, 'only.txt'), ''.join('%s\t%s\n' % (x['name'], ','.join(x['only'])) for x in doc['apps'] if x.get('only')))
        else:
            lf = os.path.join(d, 'apps.lf').replace('\\', '/')
            write(os.path.join(d, 'apps.lf'), gen_lf(psram))
            cm = ['# ' + HEADER_NOTE, '# ' + summary(doc, in_build, app_code, how)]
            cm.append(cmake_list('ARCADE_SRCS', [rel_src(a.main, f) for f in srcs]).rstrip('\n'))
            pages_off = extra_files_off(doc, off)[1]
            if pages_off:
                # (main/CMakeLists.txt takes these pages out of ARCADE_EMBED)
                cm.append('set(ARCADE_EMBED_OFF %s)' % ' '.join('"%s"' % x for x in pages_off))
            cm.append(cmake_list('ARCADE_INCS', [x.replace(os.sep, '/') for x in source_dirs(a.main)]).rstrip('\n'))
            cm.append(cmake_list('ARCADE_DEFS', defs).rstrip('\n'))
            cm.append('set(ARCADE_LDFRAG "%s")' % lf)
            cm.append('set(ARCADE_APPS_IN "%s")' % ';'.join(x['name'] for x in in_build))
            write(os.path.join(d, 'apps.cmake'), '\n'.join(cm) + '\n')
            names = {x['name'] for x in in_build}
            built = {
                'made_by': 'main/apps_gen.py', 'kind': a.kind, 'project_ver': a.project_ver, 'slim': slim,
                'selection': os.path.abspath(a.select).replace('\\', '/') if a.select else None,
                'add': a.add, 'remove': a.remove,
                'apps': [{'name': x['name'], 'sym': x['sym'], 'group': x['group'], 'new': bool(x.get('new')),
                          'emoji': x.get('emoji', '')} for x in in_build],
                'off': [x['name'] for x in doc['apps'] if x['name'] not in names],
                'code_only': [n for n in app_code if n not in names],
                'modules': mods, 'srcs': srcs, 'defs': defs, 'notes': notes,
            }
            write(os.path.join(d, 'apps_built.json'), json.dumps(built, indent=1, ensure_ascii=False) + '\n')
        print(summary(doc, in_build, app_code, how))
        for n in notes:
            print('note:', n)
        return 0
    except AppsError as e:
        print('apps_gen.py: error: %s' % e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
