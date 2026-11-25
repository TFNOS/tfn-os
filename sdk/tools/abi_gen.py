# TFN OS: the ABI of the apps. The ABI is the list of firmware functions and data that an app
# file (a package) can call. The loader links an app only to these names (main/tfn_abi.inc, made from
# main/tfn_abi.json), so the list is a promise: inside one ABI version names are only added.
#
# What it does, for each board kind of a DEV build (build_dev/<kind>):
#   1. each app of main/apps.json that is "on" and not "core" is one unit: its srcs, the srcs of the modules that it
#      uses (no: modules stay in the firmware, see below), the srcs of the apps that it "needs";
#   2. a partial link of the objects of the unit (gcc -r), then its undefined names (nm -u);
#   3. each name must be defined in the firmware of that kind (nm of tfn_os.elf); a name that only the object of
#      another (not core) app defines is a CROSS name: the tool lists it, and it does not go into the ABI (that app
#      can leave the firmware);
#   4. the new list = the old list (main/tfn_abi.json) + the new names. A name of the old list that the firmware does
#      not define any more stops the tool (that breaks installed apps: a new ABI version with --major).
#   5. the extra names (main/tfn_abi_extra.json): names that no built-in app calls, for the app packages
#      (raw ESP-IDF functions too). They go into the list for each board kind; the ABI table of the next build links
#      them (a name that the firmware does not have yet is not a GONE fault: it comes with that build).
#   6. the revision: each run that adds names gives the ABI the next revision (rev), and each new name that rev. The
#      manifest of an app has the highest rev of its names: a board with a lower rev does not get the app (the
#      manager store and the board loader check it). --major starts a new version at rev 0.
#   7. the permissions: an extra name can have a permission class (wifi.scan). mk_app.py writes the classes of the
#      names of an app into its manifest (perms=), the STORE shows their text before an install, and the loader links
#      such a name only to an app whose manifest has its class. Inside one version, a class keeps its bit and a name
#      keeps its class. These classes are the DEFAULT policy: the manager tab PERMISSIONS changes the classes later
#      with a signed policy file (no firmware; main/tfn_app.c).
#   8. the raw ESP-IDF names (main/tfn_abi_raw.json): each global function of the DEV build of a kind
#      whose name matches a family and no deny pattern goes into the list of that kind (its code is already in the
#      firmware: only the table grows). A new raw name gets the class of its family.
#   9. the table of the firmware (main/tfn_abi.inc) has no names: ELFSYM(name, lo, hi) = the FNV-1a 64 hash of the
#      name (lo and hi 32 bits) and its address; the loader finds a name by its hash. Two names with one hash stop
#      the tool.
#   --extra-only (the RELEASE build on the manager host: the approved requests of the manager): only the extra
#      names go in (each kind), from main/tfn_abi.json; no DEV build is needed.
# The shared modules of apps.json (midi_bg, toon, duel, duo, scn, strokefont, drumkit, instr) stay in the firmware:
# their functions are ABI names.
#
#   python sdk/tools/abi_gen.py                  check and write main/tfn_abi.json + main/tfn_abi.inc
#   python sdk/tools/abi_gen.py --check          only check (exit 1 when a name is missing from the ABI)
#   python sdk/tools/abi_gen.py --major          a new ABI version (names can go)
#   python sdk/tools/abi_gen.py --kinds lcd349   only these kinds (the others keep their old lists)
#   python sdk/tools/abi_gen.py --extra-only     only main/tfn_abi_extra.json (+ --extra FILE: more entries)
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MAIN = os.path.join(ROOT, 'main')
KINDS = ('lcd349', 'cores3', 'am18', 'watcher', 'tab5', 'am175', 'crow7')
KIND_IF = {'lcd349': 'TFN_KIND_LCD349', 'cores3': 'TFN_KIND_CORES3', 'am18': 'TFN_KIND_AM18', 'watcher': 'TFN_KIND_WATCHER',
           'tab5': 'TFN_KIND_TAB5', 'am175': 'TFN_KIND_AM175', 'crow7': 'TFN_KIND_CROW7'}


def tools(kind):
    cc = json.load(open(os.path.join(ROOT, 'build_dev', kind, 'compile_commands.json')))
    e = next(x for x in cc if x['file'].replace('\\', '/').endswith('/main/main.c'))
    gcc = (e.get('arguments') or shlex.split(e['command'].replace('\\', '/')))[0]
    pre = gcc[:-len('gcc.exe')] if gcc.endswith('gcc.exe') else gcc[:-len('gcc')]
    return gcc, pre + 'nm'


def obj_of(kind, src):
    d = os.path.join(ROOT, 'build_dev', kind, 'esp-idf', 'main', 'CMakeFiles', '__idf_main.dir')
    p = os.path.join(d, src + '.obj')
    return p if os.path.exists(p) else None


def h64(name):
    # FNV-1a 64 (main/tfn_app.c fnv64 is the same)
    h = 0xcbf29ce484222325
    for b in name.encode():
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h & 0xFFFFFFFF, h >> 32


def functions(nmexe, path):
    # the global functions of a firmware (T, W) with a plain C name
    s = set()
    for l in nm(nmexe, path, ['--defined-only', '-g']):
        p = l.split()
        if len(p) >= 3 and p[1] in ('T', 'W') and re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', p[2]):
            s.add(p[2])
    return s


def raw_rules():
    f = os.path.join(MAIN, 'tfn_abi_raw.json')
    if not os.path.exists(f):
        return {}, [], []
    r = json.load(open(f, encoding='utf-8'))
    return (r.get('classes', {}), [(re.compile(x['re']), x.get('class', '')) for x in r.get('families', [])],
            [re.compile(x) for x in r.get('deny', [])])


def nm(nmexe, path, flags):
    out = subprocess.run([nmexe] + flags + [path], capture_output=True, text=True).stdout
    return out.splitlines()


def defined(nmexe, path):
    s = set()
    for l in nm(nmexe, path, ['--defined-only', '-g']):
        p = l.split()
        if len(p) >= 3:
            s.add(p[2])
    return s


def units(apps_json):
    by_name = {a['name']: a for a in apps_json['apps']}
    res = {}
    for a in apps_json['apps']:
        if not a.get('on') or a.get('core') or a.get('group') == 'SYSTEM' or a.get('with'):
            continue
        srcs = list(a.get('srcs', []))
        for n in a.get('needs', []):
            srcs += [s for s in by_name[n].get('srcs', []) if s not in srcs]
        # the apps whose app_t is in this object (SUNSET in CRUISE) come with it
        res[a['name']] = {'srcs': srcs, 'also': [x['name'] for x in apps_json['apps'] if x.get('with') == a['name']]}
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--major', action='store_true')
    ap.add_argument('--kinds', default=','.join(KINDS))
    ap.add_argument('--extra-only', action='store_true', help='only the extra names (no DEV build)')
    ap.add_argument('--extra', default='', help='with --extra-only: a JSON file with more "names" entries')
    args = ap.parse_args()
    kinds = [k for k in args.kinds.split(',') if k]
    apps_json = json.load(open(os.path.join(MAIN, 'apps.json'), encoding='utf-8'))
    us = units(apps_json)
    app_srcs = set(s for a in apps_json['apps'] if not a.get('core') and a.get('group') != 'SYSTEM' for s in a.get('srcs', []))
    fn = os.path.join(MAIN, 'tfn_abi.json')
    old = json.load(open(fn)) if os.path.exists(fn) else {'version': 1, 'symbols': {}}
    syms = {} if args.major else {k: set(v) for k, v in old['symbols'].items()}
    xf = os.path.join(MAIN, 'tfn_abi_extra.json')
    extra = json.load(open(xf, encoding='utf-8')) if os.path.exists(xf) else {'perms': {}, 'names': []}
    if args.extra and os.path.exists(args.extra):
        more = json.load(open(args.extra, encoding='utf-8'))
        extra.setdefault('perms', {}).update(more.get('perms', {}))
        extra.setdefault('names', []).extend(more.get('names', []))
    xnames = {e['name']: e for e in extra.get('names', [])}
    raw_cls, raw_fam, raw_deny = raw_rules()
    rawcls = {}
    known_cls = set(extra.get('perms', {})) | set(old.get('perms', {})) | set(raw_cls)
    bad_x = [e for e in extra.get('names', []) if e.get('perm') and e['perm'] not in known_cls]
    for e in bad_x:
        print('EXTRA %s: the class %s is not in "perms"' % (e['name'], e['perm']))
    report = {'version': old['version'] + (1 if args.major and old['symbols'] else 0), 'kinds': {}}
    bad = 0
    tmp = os.path.join(ROOT, 'tests', 'host', 'abi_tmp')
    os.makedirs(tmp, exist_ok=True)
    for kind in ([] if args.extra_only else kinds):
        elf = os.path.join(ROOT, 'build_dev', kind, 'tfn_os.elf')
        if not os.path.exists(elf):
            print('%s: no DEV build (build_dev/%s): skipped' % (kind, kind))
            continue
        gcc, nmexe = tools(kind)
        fw = defined(nmexe, elf)
        # the names that only app objects (not core) define: they can leave the firmware
        app_def = {}
        for s in app_srcs:
            o = obj_of(kind, s)
            if o:
                for n in defined(nmexe, o):
                    app_def.setdefault(n, s)
        need, cross, missing, nunits = set(), {}, {}, 0
        for name, u in us.items():
            objs = [obj_of(kind, s) for s in u['srcs']]
            if not all(objs):
                continue                                    # (not in this build: a guard, a private file)
            nunits += 1
            part = os.path.join(tmp, '%s_%s.o' % (kind, re.sub(r'\W+', '_', name)))
            r = subprocess.run([gcc, '-r', '-nostdlib', '-o', part] + objs, capture_output=True, text=True)
            if r.returncode:
                print('%s %s: the partial link failed: %s' % (kind, name, r.stderr[-300:]))
                bad += 1
                continue
            und = set(l.split()[-1] for l in nm(nmexe, part, ['-u']) if l.strip())
            own = defined(nmexe, part)
            for n in und - own:
                src = app_def.get(n)
                if src and src not in u['srcs']:
                    cross.setdefault(n, set()).add(name)
                elif n in fw:
                    need.add(n)
                else:
                    missing.setdefault(n, set()).add(name)
        for n in need:
            syms.setdefault(n, set()).add(kind)
        later = []
        for n, e in xnames.items():
            if kind in e.get('kinds', KINDS):
                syms.setdefault(n, set()).add(kind)
                if n not in fw:
                    later.append(n)
        if later:
            print('   EXTRA names that the next %s build links: %s' % (kind, ' '.join(sorted(later))))
        # the raw ESP-IDF names of this firmware (step 8)
        nraw, nnew = 0, 0
        for n in sorted(functions(nmexe, elf)):
            if n in xnames or any(d.search(n) for d in raw_deny):
                continue
            fam = next((f for f in raw_fam if f[0].search(n)), None)
            if not fam:
                continue
            nraw += 1
            nnew += kind not in syms.get(n, ())
            syms.setdefault(n, set()).add(kind)
            rawcls.setdefault(n, fam[1])
        print('   %d raw ESP-IDF names of the families (%d new for %s)' % (nraw, nnew, kind))
        gone = sorted(n for n, ks in syms.items() if kind in ks and n not in fw and n not in xnames)
        report['kinds'][kind] = {'units': nunits, 'names': len(need), 'cross': {k: sorted(v) for k, v in cross.items()},
                                 'missing': {k: sorted(v) for k, v in missing.items()}, 'gone': gone}
        print('%s: %d app units, %d firmware names, %d cross names, %d missing, %d gone from the firmware'
              % (kind, nunits, len(need), len(cross), len(missing), len(gone)))
        for n, ap_ in sorted(cross.items()):
            print('   CROSS %-28s used by %s, defined in %s' % (n, ', '.join(sorted(ap_)), app_def[n]))
        for n, ap_ in sorted(missing.items()):
            print('   MISSING %-26s used by %s' % (n, ', '.join(sorted(ap_))))
        if gone:
            print('   GONE (an ABI name the firmware lost): %s' % ' '.join(gone[:20]))
            bad += 1 if not args.major else 0
    # (the extra names go to each kind, also to a kind with no DEV build now: the ABI table of each build links them)
    for n, e in xnames.items():
        for k in e.get('kinds', KINDS):
            syms.setdefault(n, set()).add(k)
    old_names = set() if args.major else set(old['symbols'])
    new_names = set(syms) - old_names
    # a name of the old list that comes to one more board kind is new for that kind: it gets the new revision too
    # (for example a name in the ABI of the CoreS3 at rev 2 that comes to the 3.49 at rev 15: with its old rev
    # an app that calls it says rev 14, and a 3.49 with rev 14 cannot link it). One rev for all kinds: the
    # store does not give such an app to the older firmware of the other kinds (the safe side).
    grown = set() if args.major else {n for n in syms if n in old_names and set(syms[n]) - set(old['symbols'][n])}
    # the revision and the permissions (steps 6 and 7)
    rev = 0 if args.major else old.get('rev', 0)
    revs = {} if args.major else dict(old.get('revs', {}))
    if new_names or grown:
        rev += 1
        for n in new_names | grown:
            revs[n] = rev
    perms = {} if args.major else dict(old.get('perms', {}))
    need_perm = {} if args.major else dict(old.get('need', {}))
    for p, v in extra.get('perms', {}).items():
        if 'bit' not in v:                              # (a class of an approved request: a free bit, or its old one)
            if p not in perms:
                free = sorted(set(range(32)) - {w['bit'] for w in perms.values()})
                if not free:
                    print('PERM %s: no free bit (32 classes at most)' % p)
                    bad += 1
                    continue
                perms[p] = {'bit': free[0], 'text': v.get('text') or p.upper()}
            continue
        if p in perms and perms[p]['bit'] != v['bit']:
            print('PERM %s: the bit changed (%d -> %d): only with --major' % (p, perms[p]['bit'], v['bit']))
            bad += 1
        if any(q != p and w.get('bit') == v['bit'] for q, w in extra['perms'].items()):
            print('PERM %s: two classes have the bit %d' % (p, v['bit']))
            bad += 1
        if not 0 <= v['bit'] < 32:
            print('PERM %s: the bit must be 0..31' % p)
            bad += 1
        perms[p] = {'bit': v['bit'], 'text': v['text']}
    for p, text in raw_cls.items():                    # (the classes of the raw families: the next free bits)
        if p not in perms:
            free = sorted(set(range(32)) - {v['bit'] for v in perms.values()})
            if not free:
                print('PERM %s: no free bit (32 classes at most)' % p)
                bad += 1
                continue
            perms[p] = {'bit': free[0], 'text': text}
    for n, e in xnames.items():
        p = e.get('perm', '')
        if (n in need_perm and need_perm[n] != p) or (n in old_names and n not in need_perm and p):
            print('PERM of %s changed (%s -> %s): only with --major (installed apps would break)'
                  % (n, need_perm.get(n, '-'), p or '-'))
            bad += 1
        if p:
            need_perm[n] = p
    for n in sorted(new_names):                        # (a NEW raw name: the class of its family)
        c = rawcls.get(n)
        if c and n not in need_perm and n not in xnames:
            if c not in perms:
                print('RAW %s: the class %s is not in "classes" of main/tfn_abi_raw.json' % (n, c))
                bad += 1
            need_perm[n] = c
    # the table finds a name by its hash (step 9): two names with one hash would link the wrong function
    seen = {}
    for n in syms:
        k = h64(n)
        if k in seen:
            print('HASH: %s and %s have the same hash' % (seen[k], n))
            bad += 1
        seen[k] = n
    bad += len(bad_x)
    stubs = os.path.join(ROOT, 'sdk', 'tools', 'host', 'raw', 'raw_stubs.c')
    stxt = open(stubs, encoding='utf-8').read() if os.path.exists(stubs) else ''
    for n in sorted(xnames):
        if not re.search(r'\b%s\s*\(' % re.escape(n), stxt):
            print('   NO PC STUB for the extra name %s in sdk/tools/host/raw/raw_stubs.c (mk_app.py cannot draw the icon '
                  'of an app that calls it)' % n)
    print('ABI version %d rev %d: %d names (%d new), %d with a permission'
          % (report['version'], rev, len(syms), len(new_names), len(need_perm)))
    if args.check:
        sys.exit(1 if new_names or bad else 0)
    if bad:
        sys.exit('stopped: %d faults (see above); nothing written' % bad)
    out = {'version': report['version'], 'rev': rev,
           'about': 'TFN OS: the firmware names that app packages can call (made by sdk/tools/abi_gen.py from the '
                    'built-in apps and main/tfn_abi_extra.json; add-only inside one version). symbols: name -> the '
                    'board kinds whose firmware has it. revs: the revision that added a name (none = 0). perms: the '
                    'permission classes. need: name -> the class that an app needs for it.',
           'perms': {p: perms[p] for p in sorted(perms)},
           'need': {n: need_perm[n] for n in sorted(need_perm)},
           'revs': {n: revs[n] for n in sorted(revs) if n in syms},
           'symbols': {n: sorted(syms[n]) for n in sorted(syms)}}
    conditions = dict(old.get('conditions', {}))
    for name, entry in xnames.items():
        if entry.get('if'):
            conditions[name] = entry['if']
    if conditions:
        out['conditions'] = {n: conditions[n] for n in sorted(conditions) if n in syms}
    with open(fn, 'w', newline='\n') as f:
        json.dump(out, f, indent=0)
        f.write('\n')
    write_inc(out)
    json.dump(report, open(os.path.join(ROOT, 'tests', 'host', 'abi_report.json'), 'w'), indent=1)
    print('wrote main/tfn_abi.json and main/tfn_abi.inc')


def write_inc(abi):
    conditions = abi.get('conditions', {})
    for name, option in conditions.items():
        if name not in abi['symbols'] or not re.fullmatch(r'CONFIG_[A-Z0-9_]+', option):
            raise ValueError('Invalid ABI condition for ' + name)
    groups = {}
    for n, ks in abi['symbols'].items():
        groups.setdefault((tuple(ks), conditions.get(n, '')), []).append(n)
    lines = ['// made by sdk/tools/abi_gen.py from main/tfn_abi.json: do not edit. The firmware names that the app',
             '// packages can call (TFN OS ABI). ELFSYM(name, lo, hi) for each: lo and hi = the FNV-1a 64 hash of the',
             '// name (the table of main/tfn_app.c has no names). ELFPERM(bit, id, text): the default permission classes;',
             '// ELFNEED(name, lo, hi, bit): the names that need a class (the default policy).',
             '#define TFN_ABI_VERSION %d' % abi['version']]
    if conditions:
        base_rev = max((abi.get('revs', {}).get(n, 0) for n in abi['symbols'] if n not in conditions), default=0)
        feature_revs = {}
        for name, option in conditions.items():
            rev = abi.get('revs', {}).get(name, 0)
            if rev > base_rev:
                feature_revs.setdefault(rev, set()).add(option)
        for index, rev in enumerate(sorted(feature_revs, reverse=True)):
            test = ' || '.join('(defined(%s) && %s)' % (option, option) for option in sorted(feature_revs[rev]))
            lines += [('#if ' if index == 0 else '#elif ') + test, '#define TFN_ABI_REV %d' % rev]
        if feature_revs:
            lines.append('#else')
        lines.append('#define TFN_ABI_REV %d' % base_rev)
        if feature_revs:
            lines.append('#endif')
    else:
        lines.append('#define TFN_ABI_REV %d' % abi.get('rev', 0))
    lines.append('')
    for ks, option in sorted(groups, key=lambda key: (-len(key[0]), key[0], key[1])):
        tests = []
        if set(ks) != set(KINDS):
            tests.append(' || '.join(KIND_IF[k] for k in ks))
        if option:
            tests.append('defined(%s) && %s' % (option, option))
        if tests:
            test = ' && '.join('(' + item + ')' for item in tests) if len(tests) > 1 else tests[0]
            lines.append('#if ' + test)
        lines += ['ELFSYM(%s, 0x%08x, 0x%08x)' % ((n,) + h64(n)) for n in sorted(groups[(ks, option)])]
        if tests:
            lines.append('#endif')
    # the permission classes (bit, id, the text of the STORE) and the names that need one (ELFNEED: name, bit)
    lines += ['', '#ifdef ELFPERM'] + ['ELFPERM(%d, "%s", "%s")' % (v['bit'], p, v['text'])
                                      for p, v in sorted(abi.get('perms', {}).items(), key=lambda x: x[1]['bit'])]
    lines += ['#endif', '#ifdef ELFNEED']
    for n, p in sorted(abi.get('need', {}).items()):
        if n not in abi['symbols']:
            continue
        option = conditions.get(n)
        if option:
            lines.append('#if defined(%s) && %s' % (option, option))
        lines.append('ELFNEED(%s, 0x%08x, 0x%08x, %d)' % ((n,) + h64(n) + (abi['perms'][p]['bit'],)))
        if option:
            lines.append('#endif')
    lines.append('#endif')
    with open(os.path.join(MAIN, 'tfn_abi.inc'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
