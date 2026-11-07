#!/usr/bin/env python3
"""The licence check: each third-party part of the repo has its entry in NOTICE.md.

The rules (each one reads only the files that git tracks):
1. Each vendored folder or file (VENDORED below, each folder of third_party/ and of components/) is named in
   NOTICE.md.
2. Each folder that has a licence file (LICENSE*, LICENCE*, COPYING*, license.txt) is named in NOTICE.md.
3. Each file of main/, libs/ and apps/ that says in its first lines that it holds font data under the SIL Open Font
   License is named in NOTICE.md, and each licence file that its header names exists.
4. Each file in docs/licenses/ is named in NOTICE.md.
5. The files that must never be in the repo (the list FORBIDDEN) are not tracked.

Exit code 0 = good, 1 = a fault (the lines tell what to add). Python 3.8 or later, the standard library only.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
NOTICE = 'NOTICE.md'

# (each folder of third_party/ is vendored too: main() adds them)
VENDORED = ['third_party', 'main/tjpgd_rom.h', 'tests/host/common/miniz.c']
LICENCE_FILE = re.compile(r'(^|/)(LICEN[CS]E[^/]*|COPYING[^/]*|license\.txt)$', re.I)
FONT_HEAD = re.compile(r'Open Font License|\bOFL\b')
FONT_LICENCE_REF = re.compile(r'docs/licenses/[\w.-]+')
FORBIDDEN = []  # (the regexes of the paths that a tree must never track)


def tracked():
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True).stdout
    return sorted(f for f in out.decode('utf-8').split('\0') if f)


def named(notice, path):
    """True when NOTICE.md names the path (a folder can have a '/' at its end)."""
    p = path.rstrip('/')
    return re.search(r'(?<![\w/.-])' + re.escape(p) + r'(?![\w.-])', notice) is not None


def main():
    with open(os.path.join(ROOT, NOTICE), encoding='utf-8') as f:
        notice = f.read()
    files = tracked()
    fset = set(files)
    faults = []
    need = [v for v in VENDORED if v != 'third_party']
    need += sorted({'/'.join(f.split('/')[:2]) for f in files if f.startswith('third_party/') and f.count('/') >= 2})
    need += sorted({'/'.join(f.split('/')[:2]) for f in files if f.startswith('components/') and f.count('/') >= 2})
    need += sorted({os.path.dirname(f) for f in files if LICENCE_FILE.search(f) and os.path.dirname(f)})
    for p in dict.fromkeys(need):
        exists = p in fset or any(f.startswith(p + '/') for f in files)
        if not exists:
            continue
        if not named(notice, p):
            faults.append('%s: a third-party part with no entry in NOTICE.md (name the path, the source, the '
                          'licence)' % p)
    for f in files:
        if not f.startswith(('main/', 'libs/', 'apps/')) or not f.endswith(('.c', '.h', '.inc')):
            continue
        with open(os.path.join(ROOT, f), encoding='utf-8', errors='replace') as fh:
            head = fh.read(600)
        if not FONT_HEAD.search(head):
            continue
        if not named(notice, f):
            faults.append('%s: font data under the SIL Open Font License with no entry in NOTICE.md' % f)
        for ref in FONT_LICENCE_REF.findall(head):
            ref = ref.rstrip('.')
            if ref not in fset:
                faults.append('%s: its header names %s, but that file is not in the repo' % (f, ref))
    for f in files:
        if f.startswith('docs/licenses/') and not named(notice, f):
            faults.append('%s: a licence file that NOTICE.md does not name' % f)
    for f in files:
        if any(p.search(f) for p in FORBIDDEN):
            faults.append('%s: this file must never be in the repo (see AGENTS.md section 2)' % f)
    for line in faults:
        print('LICENCE FAULT', line)
    if faults:
        print('LICENCE CHECK: %d faults' % len(faults))
        return 1
    print('LICENCE CHECK OK (%d tracked files, %d third-party parts named in NOTICE.md)' % (len(files), len(need)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
