#!/usr/bin/env python3
"""The comment check: a comment tells WHAT the code does and WHY, as the code is now.

A comment must not have:
  dated    a date (year-month-day)
  quote    a quote of a person or a request as the reason ("the owner", "the user:", "the user asked")
  private  a private place: a COM port, a private path, a name from the local list
  agent    a note about the AI agent work ("the agent", "this session")

The names of private boards and people are not in this file. They are in tests/secrets/private_names.txt (one
regular expression on each line), which stays out of a public copy of the repo. The check works with no such file.

Usage:
  python tests/style/check_comments.py [paths...]     (default: the folders of DEFAULT_PATHS)
  python tests/style/check_comments.py --count         (only the number of hits in each folder)
The default run skips the files of tests/style/comments_allow.txt (old hits in files that other work changes now;
the list only gets shorter). A run with paths checks all the files.
Exit 0: no hit. Exit 1: hits (each line: path:line: [category] comment text).
"""
import argparse
import collections
import io
import os
import re
import subprocess
import sys
import tokenize

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'licence'))
from check_notice import VENDORED  # noqa: E402

DEFAULT_PATHS = ['main', 'boards', 'libs', 'apps', 'components', 'sdk', 'simulator', 'CMakeLists.txt', 'tests/host']
ALLOW_LIST = os.path.join(ROOT, 'tests', 'style', 'comments_allow.txt')
C_EXT = ('.c', '.h', '.inc', '.cpp')
HASH_EXT = ('.py', '.cmake', '.projbuild', '.lf', '.sh')
HASH_NAMES = ('CMakeLists.txt', 'Kconfig')
PRIVATE_LIST = os.path.join(ROOT, 'tests', 'secrets', 'private_names.txt')

CATS = {
    'dated': re.compile(r'\b20[2-3]\d-[01]\d(-[0-3]\d)?\b'),
    'quote': re.compile(r"\bthe owner\b|\bowner:|\bthe owner's\b|\(the user\b|\bthe user:|\bthe user (asked|said|"
                        r"chose|wanted|requested|reported|found)\b|\bthe user's (request|words|wish|list|idea)\b|"
                        r"\bthe user \(20", re.I),
    'private': re.compile(r'\bCOM\d+\b|Dropbox|[A-Z]:[\\/]Users[\\/]|/Users/[a-z]|/home/[a-z]'),
    'agent': re.compile(r'\bthe agent\b|\bmy (test|tool|fix)\b|\bthis session\b', re.I),
}


def load_allow():
    """The files of comments_allow.txt (one path on each line)."""
    if not os.path.exists(ALLOW_LIST):
        return set()
    with open(ALLOW_LIST, encoding='utf-8') as h:
        return {ln.strip() for ln in h if ln.strip() and not ln.startswith('#')}


def load_private():
    if not os.path.exists(PRIVATE_LIST):
        return None
    with open(PRIVATE_LIST, encoding='utf-8') as h:
        parts = [ln.strip() for ln in h if ln.strip() and not ln.startswith('#')]
    return re.compile('|'.join('(?:%s)' % p for p in parts)) if parts else None


def c_comments(src):
    """(line, text) of each comment line of a C file. Strings and character constants are skipped."""
    out = []
    i = 0
    n = len(src)
    line = 1
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
        elif c in '"\'':
            j = i + 1
            while j < n and src[j] != c and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            i = j + 1
        elif src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append((line, src[i + 2:j]))
            i = j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j
            for k, part in enumerate(src[i + 2:j].split('\n')):
                out.append((line + k, part))
            line += src.count('\n', i, j)
            i = j + 2
        else:
            i += 1
    return out


def py_comments(src):
    """(line, text) of each comment of a Python file (the tokenizer knows a comment from a string)."""
    out = []
    try:
        for t in tokenize.generate_tokens(io.StringIO(src).readline):
            if t.type == tokenize.COMMENT:
                out.append((t.start[0], t.string[1:]))
    except (tokenize.TokenError, SyntaxError):
        return hash_comments(src)
    return out


def hash_comments(src):
    out = []
    for k, ln in enumerate(src.split('\n')):
        m = re.search(r'(^|\s)#(?!include|define|if|endif|else|elif|undef|pragma)(.*)$', ln)
        if m:
            out.append((k + 1, m.group(2)))
    return out


def tracked(paths):
    r = subprocess.run(['git', 'ls-files', '-z', '--'] + paths, cwd=ROOT, capture_output=True, check=True)
    files = [f for f in r.stdout.decode('utf-8').split('\0') if f]
    return [f for f in files if not any(f == v or f.startswith(v + '/') for v in VENDORED)]


def comments_of(path):
    with open(os.path.join(ROOT, path), encoding='utf-8', errors='replace') as h:
        src = h.read().replace('\r\n', '\n')
    if path.endswith(C_EXT):
        return c_comments(src)
    if path.endswith('.py'):
        return py_comments(src)
    if path.endswith(HASH_EXT) or os.path.basename(path) in HASH_NAMES:
        return hash_comments(src)
    return []


def scan(paths):
    cats = dict(CATS)
    private = load_private()
    if private:
        cats['private-name'] = private
    hits = []
    for f in tracked(paths):
        for line, text in comments_of(f):
            for cat, rx in cats.items():
                if rx.search(text):
                    hits.append((f, line, cat, text.strip()))
    return hits


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('paths', nargs='*')
    p.add_argument('--count', action='store_true')
    args = p.parse_args()
    hits = scan(args.paths or DEFAULT_PATHS)
    if not args.paths:
        allow = load_allow()
        hits = [h for h in hits if h[0] not in allow]
    if args.count:
        per = collections.Counter('/'.join(f.split('/')[:2]) for f, _, _, _ in hits)
        for d, n in per.most_common():
            print('%5d %s' % (n, d))
    else:
        for f, line, cat, text in hits:
            print('%s:%d: [%s] %s' % (f, line, cat, text[:160]))
    lines = len({(f, ln) for f, ln, _, _ in hits})
    print('COMMENT CHECK: %s' % ('OK' if not hits else '%d comment lines to fix' % lines))
    return 1 if hits else 0


if __name__ == '__main__':
    sys.exit(main())
