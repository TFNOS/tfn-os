"""Preprocess the real ABI for every kind and execute the ordinary API fallback.

The kinds and their Kconfig options come from boards.json. Each name of main/tfn_abi.json "conditions" is in the
table only when its option is on: CONFIG_TFN_DEVICE_PROTECTION (OFF and ON), a CONFIG_TFN_TARGET_* option (on for its
own kind only) and the extras CONFIG_TFN_EXTRA_* (all on, as the default of the firmware, and all off).
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', nargs='+', default=['clang'])
args = parser.parse_args()
abi = json.loads((root / 'main/tfn_abi.json').read_text(encoding='utf-8'))
boards = json.loads((root / 'boards.json').read_text(encoding='utf-8'))['kinds']
conditions = abi.get('conditions', {})
guard = {name for name, option in conditions.items() if option == 'CONFIG_TFN_DEVICE_PROTECTION'}
assert guard == {'device_guard_setup_allowed', 'device_guard_accounts_read', 'device_guard_accounts_write'}, guard
for name, option in conditions.items():
    assert re.fullmatch(r'CONFIG_TFN_(DEVICE_PROTECTION|TARGET_\w+|EXTRA_\w+)', option), (name, option)
kinds = sorted(boards)
assert set(kinds) == {kind for available in abi['symbols'].values() for kind in available}, kinds
template = '''
#define ELFSYM(name, lo, hi) SYMBOL name
#define ELFPERM(bit, id, text)
#define ELFNEED(name, lo, hi, bit) NEED name
#include "tfn_abi.inc"
REV TFN_ABI_REV
'''


def option_on(option, kind, protection, extras):
    if option == 'CONFIG_TFN_DEVICE_PROTECTION':
        return protection
    if option.startswith('CONFIG_TFN_EXTRA_'):
        return extras
    return option == boards[kind]['kconfig_target']


def expected_rev(on):
    """The rule of sdk/tools/abi_gen.py: the highest rev of the names that are always there, or a higher rev of a
    conditional name whose option is on."""
    revs = abi.get('revs', {})
    base = max((revs.get(n, 0) for n in abi['symbols'] if n not in conditions), default=0)
    return max([base] + [revs.get(n, 0) for n in conditions if n in on])


runs = 0
for kind in kinds:
    flags = ['-DTFN_KIND_' + name.upper() + '=' + str(int(name == kind)) for name in kinds]
    for protection in (False, True):
        for extras in (True, False):
            options = sorted({o for o in conditions.values() if option_on(o, kind, protection, extras)})
            defines = ['-D%s=1' % o for o in options]
            result = subprocess.run(args.cc + ['-E', '-P', '-x', 'c', '-I' + str(root / 'main')] + flags + defines +
                                    ['-'], input=template, capture_output=True, text=True, check=True).stdout
            on = {n for n, o in conditions.items() if o in options}
            actual = set(re.findall(r'^SYMBOL (\w+)$', result, re.M))
            expected = {name for name, available in abi['symbols'].items()
                        if kind in available and (name not in conditions or name in on)}
            assert actual == expected, (kind, protection, extras, actual ^ expected)
            needs = set(re.findall(r'^NEED (\w+)$', result, re.M))
            # The permission table spans all kinds; only the symbol table is kind-specific. The conditions apply to
            # both tables.
            expected_needs = {n for n in set(abi['need']) & set(abi['symbols']) if n not in conditions or n in on}
            assert needs == expected_needs, (kind, protection, extras, needs ^ expected_needs)
            rev = re.search(r'^REV (\d+)$', result, re.M)
            assert rev and int(rev.group(1)) == expected_rev(on), (kind, protection, extras, rev and rev.group(1))
            runs += 1

with tempfile.TemporaryDirectory(prefix='guard-off-api-') as temporary:
    directory = Path(temporary)
    source = directory / 'ordinary.c'
    source.write_text('''
#include "device_guard.h"
int main(void) {
    char text[8];
    return device_guard_enabled() || !device_guard_setup_allowed() ||
        device_guard_accounts_read(text, sizeof text) != -2 ||
        device_guard_accounts_write("test", 4) != -2;
}
''', encoding='utf-8')
    target = directory / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run(args.cc + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'main'),
                             str(source), '-o', str(target)], check=True)
    subprocess.run([str(target)], check=True)
print('ABI OK: %d kinds, protection ON/OFF, the extras on/off (%d tables), permissions and revisions; ordinary '
      'fallback links without guard code' % (len(kinds), runs))
