"""The board names stay out of the shared code (P3 step R9).

The shared code (main/ except the board drivers, libs/, apps/) tests a capability of boards/<kind>/board_caps.h
(BOARD_ROUND, BOARD_UI_TABLET ...), never the name of a board (CONFIG_TFN_TARGET_TAB5 ...). A new board is then a
new folder boards/<kind>/, with no change of the shared code.

tests/style/board_if_allow.txt lists the old places that still test a board name, with their count of tests: a file
that is not in the list, or a count that grows, is a fault. The list only gets shorter.

Usage:
  python tests/style/check_board_if.py            check
  python tests/style/check_board_if.py --update   write the list again (only with fewer or the same tests)
  python tests/style/check_board_if.py --init     write the first list
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALLOW = os.path.join(ROOT, 'tests', 'style', 'board_if_allow.txt')
NAME = re.compile(r'\bCONFIG_(TFN|ARCADE)_TARGET_[A-Z0-9_]+')  # (ARCADE: the names before R12)
# the board layer: the drivers, the board header that picks boards/<kind>/board_caps.h, the stand-ins of the Tab5
BOARD_LAYER = re.compile(r'^main/(tfn/)?(board(_[a-z0-9]+)?\.(c|h)|tab5_none\.c)$')
# the generated ABI table: its conditions are Kconfig names by design (sdk/tools/abi_gen.py)
GENERATED = {'main/tfn_abi.inc'}
EXT = ('.c', '.h', '.inc', '.cc', '.cpp')


def code_only(t):
    """The text without its comments (a comment can name a board)."""
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def counts():
    out = subprocess.run(['git', 'ls-files', '-z', '--', 'main', 'libs', 'apps'], cwd=ROOT, capture_output=True,
                         check=True).stdout.decode('utf-8').split('\0')
    res = {}
    for rel in out:
        if not rel.endswith(EXT) or BOARD_LAYER.match(rel) or rel in GENERATED:
            continue
        with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as f:
            n = len(NAME.findall(code_only(f.read())))
        if n:
            res[rel] = n
    return res


def read_allow():
    allow = {}
    if os.path.exists(ALLOW):
        for ln in open(ALLOW, encoding='utf-8'):
            ln = ln.strip()
            if ln and not ln.startswith('#'):
                rel, n = ln.rsplit(' ', 1)
                allow[rel] = int(n)
    return allow


def main():
    now = counts()
    allow = read_allow()
    bad = []
    for rel, n in sorted(now.items()):
        if rel not in allow:
            bad.append('%s: %d test(s) of a board name (use a capability of boards/<kind>/board_caps.h)' % (rel, n))
        elif n > allow[rel]:
            bad.append('%s: %d tests of a board name, the list allows %d' % (rel, n, allow[rel]))
    if '--update' in sys.argv or '--init' in sys.argv:
        if bad and '--init' not in sys.argv:
            print('\n'.join(bad))
            sys.exit('BOARD IF: not written: the list only gets shorter')
        with open(ALLOW, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# The old places that still test a board name (tests/style/check_board_if.py): file, count.\n')
            f.write('# The list only gets shorter.\n')
            for rel, n in sorted(now.items()):
                f.write('%s %d\n' % (rel, n))
        print('BOARD IF: wrote the list (%d files, %d tests)' % (len(now), sum(now.values())))
        return 0
    for b in bad:
        print(b)
    print('BOARD IF CHECK: %s (%d files with old tests, %d tests)' % ('FAULT' if bad else 'OK', len(now),
                                                                      sum(now.values())))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
