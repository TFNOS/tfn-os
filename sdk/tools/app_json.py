# TFN OS SDK: write app.json (sdk/docs/app_json.md) for an app of the repo, from its entry in main/apps.json and its
# store.json. With app.json, the app folder builds out of the repo with tfn_app.py and the SDK package.
#
#   python sdk/tools/app_json.py <APP>                       the app of main/apps.json (its name or its sym)
#   python sdk/tools/app_json.py --src <file.c> --sym app_x --name X [--group UTIL] [--watch <w.c> --watch-sym watch_x]
#                                                            a source out of main/apps.json (an SDK example)
#   --out <file>   (default: app.json in the folder of the first source);  --print   (to the screen only)
#   python sdk/tools/app_json.py --check                     each apps/<app>/app.json agrees with main/apps.json and
#                                                            its store.json (CI)
#
# The tool also tells what stops the folder from a build out of the repo: a source out of the folder, an include of a
# file of main/ or of another app, an include of a lib by its path in the repo (write "<lib>/<file>": the SDK package
# has the libs).
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import mk_app  # noqa: E402
from sdk_pack import COMMON_IDF, HEADER_LIBS, MODULE_LIBS, MODULE_SRC, SOURCE_LIBS  # noqa: E402

PKG_LIBS = HEADER_LIBS + SOURCE_LIBS + MODULE_LIBS
# the lib folders of the repo: libs/<lib>/ and third_party/<lib>/ (the code of other projects); the SDK package has
# both as libs/<lib>/
LIB_ROOTS = [os.path.join(ROOT, 'libs'), os.path.join(ROOT, 'third_party')]
# the file name -> the lib, for a bare include that the build of the repo finds in libs/<lib>/ or third_party/<lib>/
# (the first lib in the order of the build folders)
BARE_LIB = {}
for _root in LIB_ROOTS:
    for _lib in sorted(os.listdir(_root)) if os.path.isdir(_root) else []:
        for _f in sorted(os.listdir(os.path.join(_root, _lib))) if os.path.isdir(os.path.join(_root, _lib)) else []:
            BARE_LIB.setdefault(_f, _lib)

KINDS = mk_app.KINDS


def rel(p, folder):
    return os.path.relpath(p, folder).replace('\\', '/')


def lib_dir(lib):
    """The folder of a lib (libs/<lib>/ or third_party/<lib>/), else None."""
    return next((os.path.join(r, lib) for r in LIB_ROOTS if os.path.isdir(os.path.join(r, lib))), None)


def lib_of(path):
    """(lib, file) when path is a file of libs/<lib>/ or third_party/<lib>/, else None."""
    for root in LIB_ROOTS:
        r = os.path.relpath(os.path.abspath(path), root).replace('\\', '/')
        if not r.startswith('..') and '/' in r:
            return tuple(r.split('/', 1))
    return None


def pkg_file(lib, name):
    """True when the SDK package has libs/<lib>/<name> (sdk_pack.py: a module lib has only its headers)."""
    if lib not in PKG_LIBS:
        return False
    return lib not in MODULE_LIBS or name.endswith('.h') or name in MODULE_SRC.get(lib, ())


def scan(folder, srcs):
    """The notes about the includes of the sources (and of the local files that they include), and True when the app
    includes ESP-IDF headers that are not in COMMON_IDF (it needs "idf": "all")."""
    notes, raw, seen, todo = [], False, set(), list(srcs)
    while todo:
        f = todo.pop()
        if f in seen or not os.path.exists(f):
            continue
        seen.add(f)
        for m in re.finditer(r'^\s*#\s*include\s*"([^"]+)"', open(f, encoding='utf-8', errors='replace').read(), re.M):
            inc = m.group(1)
            loc = os.path.normpath(os.path.join(os.path.dirname(f), inc))
            lib = re.match(r'(?:\.\./)+(?:libs|third_party)/([^/]+)/(.+)$', inc)
            sdk_form = re.match(r'([^./][^/]*)/(.+)$', inc)
            if inc.startswith('tfn/'):
                continue
            in_lib = lib_of(loc)
            if lib:
                notes.append('%s: #include "%s": write "%s/%s" (the SDK form)' % (rel(f, folder), inc, lib.group(1),
                                                                                  lib.group(2)))
                todo.append(loc)
            elif sdk_form and lib_dir(sdk_form.group(1)):
                if not pkg_file(sdk_form.group(1), sdk_form.group(2)):
                    notes.append('%s: #include "%s": the SDK package does not have libs/%s' % (rel(f, folder), inc, inc))
                todo.append(os.path.join(lib_dir(sdk_form.group(1)), sdk_form.group(2)))
            elif os.path.exists(loc) and in_lib and lib_of(f):
                if not pkg_file(*in_lib):
                    notes.append('%s: #include "%s": the SDK package does not have libs/%s/%s'
                                 % (rel(f, folder), inc, in_lib[0], in_lib[1]))
                todo.append(loc)            # (a lib file that includes a part of its own lib or of another lib)
            elif os.path.exists(loc):
                if rel(loc, folder).startswith('..'):
                    notes.append('%s: #include "%s" is a file out of the app folder (not in the SDK package)'
                                 % (rel(f, folder), inc))
                todo.append(loc)
            elif inc in COMMON_IDF:
                continue
            elif '/' not in inc and os.path.exists(os.path.join(ROOT, 'main', inc)):
                notes.append('%s: #include "%s" is a file of main/ (not in the SDK package)' % (rel(f, folder), inc))
            elif '/' not in inc and BARE_LIB.get(inc):
                notes.append('%s: #include "%s": write "%s/%s" (the SDK form)' % (rel(f, folder), inc, BARE_LIB[inc], inc))
                todo.append(os.path.join(lib_dir(BARE_LIB[inc]), inc))
            elif re.match(r'(\.\./)+', inc):
                notes.append('%s: #include "%s" is not in the SDK package' % (rel(f, folder), inc))
            else:
                raw = True                  # (an ESP-IDF header out of COMMON_IDF)
    return notes, raw


def from_repo(name):
    a, srcs = mk_app.app_of(name)
    paths = [mk_app.src_path(s) for s in srcs]
    folder = os.path.dirname(paths[0])
    j = {'tfn_app': 1, 'name': a['name'], 'sym': a['sym'], 'group': a.get('group', 'GAMES')}
    for k in ('emoji', 'new', 'form', 'form_help', 'oauth', 'class'):
        if a.get(k):
            j[k] = a[k]
    j['srcs'] = [rel(p, folder) for p in paths]
    if a.get('link'):
        j['link'] = [rel(mk_app.src_path(f), folder) for f in a['link']]
    if a.get('watch'):
        w = a['watch']
        j['watch'] = {'srcs': [rel(mk_app.src_path(s), folder) for s in w['srcs']], 'sym': w['sym'],
                      'every': w.get('every', 15)}
    if a.get('only'):
        j['kinds'] = list(a['only'])
    return j, folder, paths + ([mk_app.src_path(s) for s in a['watch']['srcs']] if a.get('watch') else [])


def from_src(args):
    src = os.path.abspath(args.src)
    folder = os.path.dirname(src)
    j = {'tfn_app': 1, 'name': args.name, 'sym': args.sym, 'group': args.group, 'srcs': [rel(src, folder)]}
    paths = [src]
    if args.watch:
        w = os.path.abspath(args.watch)
        j['watch'] = {'srcs': [rel(w, folder)], 'sym': args.watch_sym or 'watch_' + args.sym[4:],
                      'every': args.watch_every}
        paths.append(w)
    return j, folder, paths


def finish(j, folder, paths):
    """The fields of app.json that come from the files: "idf" (the includes) and "store" (store.json). Returns the
    notes of scan()."""
    notes, raw = scan(folder, paths)
    if raw:
        j['idf'] = 'all'
    pid = j['sym'][4:]
    st = os.path.join(folder, 'store.json')
    if os.path.exists(st):
        d = json.load(open(st, encoding='utf-8')).get(pid, {})
        if d:
            j['store'] = {k: d[k] for k in ('en', 'he') if d.get(k)}
    for s in j['srcs'] + j.get('watch', {}).get('srcs', []):
        if s.startswith('..'):
            notes.append('the source %s is out of the app folder' % s)
    return notes


# the fields of app.json that the developer can write and that main/apps.json does not have
OWN_KEYS = ('version', 'icon', 'abi_rev', 'about', 'defines', 'id')


def check():
    """Each apps/<app>/app.json agrees with main/apps.json and the store.json of its folder: the same fields as
    `app_json.py <APP>` writes (the fields of OWN_KEYS can be there too)."""
    faults, n = [], 0
    apps = os.path.join(ROOT, 'apps')
    for name in sorted(os.listdir(apps)):
        p = os.path.join(apps, name, 'app.json')
        if not os.path.isfile(p):
            continue
        n += 1
        where = 'apps/%s/app.json' % name
        try:
            have = json.load(open(p, encoding='utf-8'))
        except ValueError as e:
            faults.append('%s: not JSON (%s)' % (where, e))
            continue
        sym = have.get('sym', '')
        try:
            want, folder, paths = from_repo(sym)
        except SystemExit:
            faults.append('%s: "sym" %s is not a store app of main/apps.json' % (where, sym or '(none)'))
            continue
        if os.path.normcase(os.path.abspath(folder)) != os.path.normcase(os.path.join(apps, name)):
            faults.append('%s: the app %s is in %s' % (where, sym, os.path.relpath(folder, ROOT).replace(os.sep, '/')))
        finish(want, folder, paths)
        for k, v in want.items():
            if have.get(k) != v:
                faults.append('%s: "%s" is %s, main/apps.json or store.json gives %s'
                              % (where, k, json.dumps(have.get(k), ensure_ascii=False)[:80],
                                 json.dumps(v, ensure_ascii=False)[:80]))
        for k in sorted(set(have) - set(want) - set(OWN_KEYS)):
            faults.append('%s: "%s" is not in main/apps.json or store.json (write it there, then run '
                          'python sdk/tools/app_json.py %s)' % (where, k, sym))
    for f in faults:
        print('  ' + f)
    print('APP JSON CHECK: %s (%d app.json files)' % ('OK' if not faults else '%d FAULTS' % len(faults), n))
    return 1 if faults else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='each apps/<app>/app.json agrees with main/apps.json')
    ap.add_argument('app', nargs='?', default='')
    ap.add_argument('--src', default='')
    ap.add_argument('--sym', default='')
    ap.add_argument('--name', default='')
    ap.add_argument('--group', default='GAMES')
    ap.add_argument('--watch', default='')
    ap.add_argument('--watch-sym', default='')
    ap.add_argument('--watch-every', type=int, default=15)
    ap.add_argument('--version', default='')
    ap.add_argument('--out', default='')
    ap.add_argument('--print', action='store_true')
    args = ap.parse_args()
    if args.check:
        sys.exit(check())
    if args.src:
        if not (args.sym and args.name):
            ap.error('--src needs --sym and --name')
        j, folder, paths = from_src(args)
    elif args.app:
        j, folder, paths = from_repo(args.app)
    else:
        ap.error('give an app of main/apps.json, or --src')
    if args.version:
        j['version'] = args.version
    notes = finish(j, folder, paths)
    txt = json.dumps(j, indent=1, ensure_ascii=False) + '\n'
    if args.print:
        sys.stdout.buffer.write(txt.encode('utf-8'))
    else:
        out = args.out or os.path.join(folder, 'app.json')
        open(out, 'w', encoding='utf-8', newline='\n').write(txt)
        print('wrote %s' % out)
    for n in notes:
        print('NOTE: ' + n)
    if notes:
        print('NOTE: %d includes or sources stop a build of this folder out of the repo (see above)' % len(notes))


if __name__ == '__main__':
    main()
