#!/usr/bin/env python3
"""The contract checks of TFN OS: the app ABI, the NVS keys, the HTTP routes and the console commands.

Each check reads the tracked source files, and compares the result with its baseline file in
tests/contracts/baseline/. An item of the baseline that the source does not have now is a fault (exit code 1): a
board, an installed app file, the manager or a test tool can use it. A new item is good: the check shows it, and
--update writes it into the baseline (commit the baseline with the change).

  python tests/contracts/check_all.py                  check all
  python tests/contracts/check_all.py --only nvs       check one (abi, nvs, routes, console)
  python tests/contracts/check_all.py --update         write the baselines (only when no item is lost)
  python tests/contracts/check_all.py --update --accept-loss   write the baselines also when items are lost
                                                       (a planned change of a contract: tell it in the commit)

Python 3.8 or later, the standard library only.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import abi            # noqa: E402
import console_cmds   # noqa: E402
import nvs_keys       # noqa: E402
import routes         # noqa: E402

CHECKS = {'abi': abi, 'nvs': nvs_keys, 'routes': routes, 'console': console_cmds}
BASE = os.path.join(HERE, 'baseline')


def flat(value, prefix=''):
    """The items of a contract part as a set of text lines (a dict of lists gives 'key: item')."""
    if isinstance(value, dict):
        out = set()
        for k, v in value.items():
            out |= flat(v, '%s%s: ' % (prefix, k))
        return out
    if isinstance(value, list):
        return {prefix + str(x) for x in value}
    return {prefix + str(value)}


def compare(name, mod, cur, old):
    """(lost, added) lines of the contract parts."""
    lost, added = [], []
    for part in mod.CONTRACT:
        a = flat(old.get(part, []))
        b = flat(cur.get(part, []))
        lost += ['%s %s' % (part, x) for x in sorted(a - b)]
        added += ['%s %s' % (part, x) for x in sorted(b - a)]
    if name == 'abi' and cur.get('rev', 0) < old.get('rev', 0):
        lost.append('rev %s (now %s): the ABI revision must not go down' % (old.get('rev'), cur.get('rev')))
    return lost, added


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, indent=1, ensure_ascii=False, sort_keys=True)
        f.write('\n')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', choices=sorted(CHECKS), action='append', help='run only this check (more than once OK)')
    ap.add_argument('--update', action='store_true', help='write the baselines')
    ap.add_argument('--accept-loss', action='store_true', help='with --update: also when items are lost')
    ap.add_argument('--show', type=int, default=40, help='the number of lines to show for each list (default 40)')
    a = ap.parse_args(argv)
    names = a.only or sorted(CHECKS)
    bad = 0
    for name in names:
        mod = CHECKS[name]
        cur = mod.extract()
        path = os.path.join(BASE, name + '.json')
        old = None
        if os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                old = json.load(f)
        faults = mod.self_check() if hasattr(mod, 'self_check') else []
        lost, added = compare(name, mod, cur, old) if old is not None else ([], [])
        size = sum(len(flat(cur.get(p, []))) for p in mod.CONTRACT)
        state = 'FAIL' if lost or faults or old is None and not a.update else 'OK'
        print('%-8s %s: %d items, %d lost, %d new%s' % (name, state, size, len(lost), len(added),
                                                         '' if old is not None else ' (no baseline)'))
        for line in faults:
            print('  FAULT', line)
        for title, lines in (('LOST', lost), ('NEW ', added)):
            for line in lines[:a.show]:
                print('  %s %s' % (title, line))
            if len(lines) > a.show:
                print('  ... and %d more %s lines' % (len(lines) - a.show, title.strip()))
        unresolved = cur.get('unresolved', [])
        if unresolved:
            print('  note: %d key expressions are not resolved (see "unresolved" in the baseline)' % len(unresolved))
        if a.update:
            if lost and not a.accept_loss:
                print('  the baseline is NOT written: items are lost (use --accept-loss for a planned change)')
                bad += 1
                continue
            write_json(path, cur)
            print('  wrote', os.path.relpath(path, os.getcwd()).replace(os.sep, '/'))
            bad += 1 if faults else 0
            continue
        if old is None:
            print('  no baseline: run python tests/contracts/check_all.py --update')
        if state == 'FAIL':
            bad += 1
    if bad:
        print('CONTRACTS: %d of %d checks FAIL. A lost item can stop the boards, the installed apps, the manager or '
              'the tools. Put it back, or make the change a planned one (--update --accept-loss).' % (bad, len(names)))
        return 1
    print('CONTRACTS OK (%s)' % ', '.join(names))
    return 0


if __name__ == '__main__':
    sys.exit(main())
