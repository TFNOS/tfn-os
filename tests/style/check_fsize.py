"""No function over 150 lines (P3 step R11; the code standard of PLAN 8.1: the aim is 60 lines, the limit 150).

The tool reads the formatted code of main/, boards/, libs/ and apps/ (clang-format puts the "{" and the "}" of a
function at column 0) and counts the lines between them. Not read: the files of .clang-format-ignore (made files,
data), the vendored code (third_party/: not in DIRS), tools/ and test/.

tests/style/fsize_allow.txt lists the old functions that are still longer (file, function, lines). A function that is
not in the list and has more than 150 lines is a fault; a listed function that grows is a fault; an entry that no
long function needs any more is a fault too: the list only gets shorter.

Usage:
  python tests/style/check_fsize.py            check
  python tests/style/check_fsize.py --list     the long functions, the longest first
  python tests/style/check_fsize.py --update   write the list again (only with fewer or shorter functions)
  python tests/style/check_fsize.py --init     write the first list
"""
import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALLOW = os.path.join(ROOT, 'tests', 'style', 'fsize_allow.txt')
LIMIT = 150
DIRS = ['main', 'boards', 'libs', 'apps']
EXT = ('.c', '.h', '.inc', '.cc', '.cpp')
SKIP_WORDS = ('__attribute__', 'noinline', 'unused', 'aligned', 'section', 'always_inline', 'weak')


def files():
    out = subprocess.run(['git', 'ls-files'] + DIRS, cwd=ROOT, capture_output=True, text=True).stdout.split('\n')
    ign = []
    p = os.path.join(ROOT, '.clang-format-ignore')
    if os.path.exists(p):
        ign = [x.strip() for x in open(p, encoding='utf-8') if x.strip() and not x.startswith('#')]
    vend = set()
    for f in out:
        if f.endswith('/.clang-format'):
            vend.add(os.path.dirname(f) + '/')
    res = []
    for f in out:
        if not f.endswith(EXT) or '/tools/' in f or '/test/' in f:
            continue
        if any(fnmatch.fnmatch(f, x) for x in ign) or any(f.startswith(v) for v in vend):
            continue
        if os.path.exists(os.path.join(ROOT, f)):
            res.append(f)
    return res


def functions(rel):
    """(name, first line, lines) of each function of a formatted file."""
    with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as h:
        lines = h.read().replace('\r\n', '\n').split('\n')
    out = []
    start = None
    name = ''
    for i, line in enumerate(lines):
        if line == '{' and i > 0:
            j = i - 1
            head = ''
            while j >= 0 and j > i - 8 and lines[j].strip() and not lines[j].rstrip().endswith((';', '}')):
                if not lines[j].lstrip().startswith(('//', '/*', '*')):
                    head = re.sub(r'//.*', '', lines[j]) + ' ' + head
                j -= 1
            m = [x for x in re.findall(r'(\w+)\s*\(', head) if x not in SKIP_WORDS]
            if m and not re.match(r'\s*(typedef|struct|enum|union)\b', head) and '=' not in head.split('(')[0]:
                start = i
                name = m[0]
        elif line.startswith('}') and start is not None:
            out.append((name, start + 1, i - start - 1))
            start = None
    return out


def long_ones():
    res = []
    for f in files():
        for name, ln, n in functions(f):
            if n > LIMIT:
                res.append((f, name, ln, n))
    return res


def load_allow():
    allow = {}
    if os.path.exists(ALLOW):
        for line in open(ALLOW, encoding='utf-8'):
            line = line.split('#', 1)[0].strip()
            if line:
                f, name, n = line.split()
                allow[(f, name)] = max(int(n), allow.get((f, name), 0))
    return allow


def write_allow(items):
    m = {}
    for f, name, ln, n in items:
        m[(f, name)] = max(n, m.get((f, name), 0))
    with open(ALLOW, 'w', encoding='utf-8', newline='\n') as h:
        h.write('# The old functions over %d lines (tests/style/check_fsize.py): file, function, lines.\n' % LIMIT)
        h.write('# The list only gets shorter.\n')
        for (f, name), n in sorted(m.items()):
            h.write('%s %s %d\n' % (f, name, n))


def main():
    now = long_ones()
    if '--list' in sys.argv:
        for f, name, ln, n in sorted(now, key=lambda x: -x[3]):
            print('%5d %s:%d %s' % (n, f, ln, name))
        print('%d functions over %d lines' % (len(now), LIMIT))
        return 0
    if '--init' in sys.argv:
        write_allow(now)
        print('wrote %s: %d functions' % (os.path.relpath(ALLOW, ROOT), len(now)))
        return 0
    allow = load_allow()
    faults = []
    used = set()
    for f, name, ln, n in now:
        k = (f, name)
        if k not in allow:
            faults.append('%s:%d %s has %d lines (the limit is %d): split it' % (f, ln, name, n, LIMIT))
        elif n > allow[k]:
            faults.append('%s:%d %s grew to %d lines (the list says %d)' % (f, ln, name, n, allow[k]))
        used.add(k)
    stale = [k for k in allow if k not in used]
    if '--update' in sys.argv:
        if faults:
            for x in faults:
                print('  ' + x)
            print('FSIZE: no update with these faults')
            return 1
        write_allow(now)
        print('wrote %s: %d functions (%d lines out)' % (os.path.relpath(ALLOW, ROOT), len(now), len(stale)))
        return 0
    for f, name in sorted(stale):
        faults.append('fsize_allow.txt: %s %s is not over %d lines now: remove the line' % (f, name, LIMIT))
    for x in faults:
        print('  ' + x)
    print('FSIZE CHECK: %s (%d old functions over %d lines in the list)' %
          ('OK' if not faults else '%d FAULTS' % len(faults), len(allow), LIMIT))
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main())
