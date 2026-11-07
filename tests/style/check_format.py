#!/usr/bin/env python3
"""The format check: the C and C++ files of main/, libs/ and apps/ follow .clang-format.

The files of .clang-format-ignore (made files, host tools) and the folders with their own .clang-format (vendored
code, DisableFormat) stay as they are. Each clang-format version formats a little differently, so the check needs
the version of PINNED: `pip install clang-format==22.1.8`, or set CLANG_FORMAT to the path of that binary.

Usage:
  python tests/style/check_format.py           (the check: exit 1 when a file needs a change)
  python tests/style/check_format.py --fix     (changes the files)
  python tests/style/check_format.py --fix main/app_menu.c ...   (only these files)
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PINNED = '22.1.8'
DIRS = ['main', 'boards', 'libs', 'apps']
EXT = ('.c', '.h', '.inc', '.cc', '.cpp')


def find_clang_format():
    exe = os.environ.get('CLANG_FORMAT') or shutil.which('clang-format')
    if not exe:
        sys.exit('no clang-format: pip install clang-format==%s (or set CLANG_FORMAT)' % PINNED)
    out = subprocess.run([exe, '--version'], capture_output=True, text=True).stdout
    if ('version %s' % PINNED) not in out:
        sys.exit('clang-format %s is necessary, this one is: %s (pip install clang-format==%s)'
                 % (PINNED, out.strip(), PINNED))
    return exe


def source_files(paths):
    r = subprocess.run(['git', 'ls-files', '-z', '--'] + (paths or DIRS), cwd=ROOT, capture_output=True, check=True)
    return [f for f in r.stdout.decode('utf-8').split('\0') if f.endswith(EXT)]


def run_chunks(exe, args, files):
    bad = []
    for k in range(0, len(files), 100):
        part = files[k:k + 100]
        r = subprocess.run([exe] + args + part, cwd=ROOT, capture_output=True, encoding='utf-8', errors='replace')
        if r.returncode:
            for ln in r.stderr.splitlines():
                if ': error: ' in ln or ': warning: ' in ln:
                    bad.append(ln.split(':')[0])
    return sorted(set(bad))


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('paths', nargs='*')
    p.add_argument('--fix', action='store_true')
    args = p.parse_args()
    exe = find_clang_format()
    files = source_files(args.paths)
    if args.fix:
        run_chunks(exe, ['-i', '--style=file'], files)
        print('FORMAT FIX: %d files given to clang-format %s' % (len(files), PINNED))
        return 0
    bad = run_chunks(exe, ['--dry-run', '--Werror', '--style=file'], files)
    for f in bad:
        print('NEEDS FORMAT %s' % f)
    print('FORMAT CHECK: %s (%d files)' % ('OK' if not bad else '%d files need clang-format' % len(bad), len(files)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
