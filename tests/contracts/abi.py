"""The ABI contract: the firmware names that the app files can call, for each board kind (main/tfn_abi.json).

An installed app file links to these names by their hash. A name that leaves the ABI of a kind stops each app file
that calls it on the boards of that kind. A permission class (its bit) and the class that a name needs are part of
the contract too (the store shows them; the board policy uses the bits).

This check also makes sure that main/tfn_abi.inc (the table that the firmware compiles) is the text that
sdk/tools/abi_gen.py makes from main/tfn_abi.json, and that no two names have the same hash.
"""
import importlib.util
import json
import os
import tempfile

import csrc

ABI_JSON = os.path.join(csrc.ROOT, 'main', 'tfn_abi.json')
ABI_INC = os.path.join(csrc.ROOT, 'main', 'tfn_abi.inc')
ABI_GEN = os.path.join(csrc.ROOT, 'sdk', 'tools', 'abi_gen.py')


def _abi_gen():
    spec = importlib.util.spec_from_file_location('abi_gen', ABI_GEN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def extract():
    with open(ABI_JSON, encoding='utf-8') as f:
        abi = json.load(f)
    kinds = {}
    for name, ks in abi['symbols'].items():
        for k in ks:
            kinds.setdefault(k, set()).add(name)
    return {
        'version': [str(abi['version'])],
        'kinds': {k: sorted(v) for k, v in sorted(kinds.items())},
        'perms': sorted('%s=%d' % (p, v['bit']) for p, v in abi.get('perms', {}).items()),
        'need': sorted('%s:%s' % (n, p) for n, p in abi.get('need', {}).items()),
        'conditions': sorted('%s:%s' % (n, c) for n, c in abi.get('conditions', {}).items()),
        'rev': abi.get('rev', 0),
    }


def self_check():
    """The faults of main/tfn_abi.inc against main/tfn_abi.json (an empty list = good)."""
    faults = []
    with open(ABI_JSON, encoding='utf-8') as f:
        abi = json.load(f)
    gen = _abi_gen()
    seen = {}
    for name in abi['symbols']:
        h = gen.h64(name)
        if h in seen:
            faults.append('the names %s and %s have the same hash' % (seen[h], name))
        seen[h] = name
    with tempfile.TemporaryDirectory() as tmp:
        gen.MAIN = tmp
        try:
            gen.write_inc(abi)
        except ValueError as e:
            return faults + ['abi_gen.write_inc: %s' % e]
        with open(os.path.join(tmp, 'tfn_abi.inc'), encoding='utf-8') as f:
            want = f.read()
    with open(ABI_INC, encoding='utf-8', newline='') as f:
        have = f.read().replace('\r\n', '\n')
    if have != want:
        a, b = have.splitlines(), want.splitlines()
        diff = [i for i in range(max(len(a), len(b))) if i >= len(a) or i >= len(b) or a[i] != b[i]]
        line = diff[0] if diff else 0
        faults.append('main/tfn_abi.inc is not the text of main/tfn_abi.json (first change at line %d: %r, expected '
                      '%r): make both files with sdk/tools/abi_gen.py, never by hand'
                      % (line + 1, a[line] if line < len(a) else '', b[line] if line < len(b) else ''))
    return faults


CONTRACT = ('version', 'kinds', 'perms', 'need', 'conditions')
INFO = ('rev',)
