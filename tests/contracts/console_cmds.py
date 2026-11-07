"""The console contract: each command of the board console and the words that it takes.

The manager tools and the test tools send these commands over USB. A lost command (or a lost word of a command)
stops them.
- main/console.c: each name of the command table CON_CMDS ({"x", con_x}) is a command "x", and each strcmp of a
  value with a literal in con_x() is a word of that command: "x y". The con_x() functions are in console.c and in
  its command files main/console_*.c. The old form (an if chain) still works: each
  'strcmp(cmd, "x")' (or strncmp) is a command "x", and a later compare (until the next command) is its word.
- A function NAME_console() in another file of main/ (called from a command of console.c) gives the words of that
  command; a function that console.c does not call is shown with its own name: "[NAME] y".
- Any other function of main/ that tests 'strcmp(cmd, "x")' gives "[function] x" (for example the commands that need
  no login on a protected board).
"""
import os
import re

import csrc

CONSOLE = 'main/console.c'
CMP_RE = re.compile(r'\bstrn?cmp\s*\(\s*([^,()]+(?:\([^()]*\))?[^,()]*)\s*,\s*"([^"]*)"')
TABLE_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*(\w+)\s*\}')


def _table(src):
    """The command table CON_CMDS of console.c: the command names in order, and each command function -> its
    LAST name (a function with two names, such as "tap" and "ltap", gives its words to the last one, as the old
    test chain did)."""
    m = re.search(r'\bCON_CMDS\s*\[\s*\]\s*=\s*\{(.*?)\n\};', src.text, re.S)
    if not m:
        return [], {}
    names, last = [], {}
    for e in TABLE_RE.finditer(m.group(1)):
        names.append(e.group(1))
        last[e.group(2)] = e.group(1)
    return names, last


def _owner(src, pos, tops, last):
    """the command that a place of console.c belongs to: the command function round it, else the last test of cmd
    before it"""
    f = src.func_at(pos)
    if f and f[2] in last:
        return last[f[2]]
    before = [t for t in tops if t[0] < pos]
    return before[-1][1] if before else None


def _tops(src):
    """The commands of the old if chain of a file: (position, name)."""
    return [(m.start(), m.group(2)) for m in CMP_RE.finditer(src.text) if m.group(1).strip() == 'cmd']


def _owner_map(srcs):
    """The commands of console.c in order: (position, name); and the *_console() function -> its command.
    srcs: console.c first, then its command files."""
    tops = _tops(srcs[0])
    owners = {}
    names, last = _table(srcs[0])
    for src in srcs:
        t = _tops(src)
        for m in re.finditer(r'(?<![\w.>])(\w+_console)\s*\(', src.text):
            f = src.func_at(m.start())
            if not f or f[2] == m.group(1):
                continue
            owners.setdefault(m.group(1), _owner(src, m.start(), t, last) or '(before the command)')
    return tops, owners, names, last


def extract():
    files = [f for f in csrc.tracked(('main/',))]
    console_files = [CONSOLE] + sorted(f for f in files if re.fullmatch(r'main/console_\w+\.c', f))
    srcs = [csrc.Source(f) for f in console_files]
    tops, owners, names, last = _owner_map(srcs)
    commands = sorted({t[1] for t in tops} | set(names))
    words = set()
    for src in srcs:
        t = _tops(src)
        for m in CMP_RE.finditer(src.text):
            if m.group(1).strip() == 'cmd':
                continue
            owner = _owner(src, m.start(), t, last)
            if owner:
                words.add('%s %s' % (owner, m.group(2)))
    for rel in files:
        if rel in console_files:
            continue
        with open(os.path.join(csrc.ROOT, rel), encoding='utf-8', errors='replace') as fh:
            raw = fh.read()
        if not re.search(r'_console\s*\(|strn?cmp\s*\(\s*cmd\b', raw):
            continue
        src = csrc.Source(rel)
        for f in src.funcs:
            body = src.text[f[0]:f[1]]
            if f[2].endswith('_console'):
                owner = owners.get(f[2], '[%s]' % f[2])
                for m in CMP_RE.finditer(body):
                    words.add('%s %s' % (owner, m.group(2)))
            else:
                for m in CMP_RE.finditer(body):
                    if m.group(1).strip() == 'cmd':
                        words.add('[%s] %s' % (f[2], m.group(2)))
    return {
        'commands': commands,
        'words': sorted(words),
        'handlers': {k: v for k, v in sorted(owners.items())},
    }


CONTRACT = ('commands', 'words')
INFO = ('handlers',)
