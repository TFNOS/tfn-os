#!/usr/bin/env python3
"""The check of boards.json: each value agrees with the files of the repo.

It checks, for each board kind:
  - the sdkconfig defaults exist, and their chain gives the chip, the flash size, the partition table and the board
    option of boards.json (a later file wins, as in ESP-IDF);
  - the partition table exists, and the app and otadata addresses of the flash plan are those of the table;
  - boards/<kind>/board_caps.h exists, has the kind name and the frame sizes of boards.json;
  - the kind lists of main/apps.json, sdk/tools/abi_gen.py, sdk/tools/mk_app.py,
    main/CMakeLists.txt and main/tfn/board.h name only kinds of boards.json (and the full lists name all of them);
  - the USB IDs agree with site/js/boards.js;
  - the public release: the kinds with "release": true are the kinds of .github/workflows/release.yml and the web
    install kinds of site/js/boards.js.

Usage: python tests/boards/check_boards.py      (exit 1 when a check fails)
"""
import ast
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
FAULTS = []
SKIPPED = []


def fault(msg):
    FAULTS.append(msg)


def private(rel):
    """True when the private file REL is there; else the tool skips its checks (once a note)."""
    if exists(rel):
        return True
    if rel not in SKIPPED:
        SKIPPED.append(rel)
    return False


def text(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def exists(rel):
    return os.path.isfile(os.path.join(ROOT, rel))


def py_assign(rel, name):
    """The value of a top-level assignment NAME = <literal> in a Python file (no import of the file)."""
    tree = ast.parse(text(rel))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    fault('%s: no %s' % (rel, name))
    return None


def defaults_chain(files):
    """The CONFIG values of the sdkconfig defaults files in their order (a later line wins)."""
    cfg = {}
    for rel in files:
        for ln in text(rel).splitlines():
            ln = ln.strip()
            m = re.match(r'#\s*(CONFIG_\w+) is not set$', ln)
            if m:
                cfg[m.group(1)] = 'n'
                continue
            if ln.startswith('CONFIG_') and '=' in ln:
                k, v = ln.split('=', 1)
                cfg[k] = v.strip().strip('"')
    return cfg


def csv_parts(rel):
    """The rows of a partition table CSV: {name: (type, subtype, offset, size)} (the numbers as int)."""
    rows = {}

    def num(s):
        s = s.strip()
        if s.upper().endswith('M'):
            return int(s[:-1], 0) * 1024 * 1024
        if s.upper().endswith('K'):
            return int(s[:-1], 0) * 1024
        return int(s, 0)

    for ln in text(rel).splitlines():
        ln = ln.split('#', 1)[0].strip()
        if not ln:
            continue
        p = [x.strip() for x in ln.split(',')]
        rows[p[0]] = (p[1], p[2], num(p[3]), num(p[4]))
    return rows


def kconfig_default():
    m = re.search(r'choice TFN_TARGET.*?default (\w+)', text('main/Kconfig.projbuild'), re.S)
    return 'CONFIG_' + m.group(1) if m else ''


def caps(kind):
    rel = 'boards/%s/board_caps.h' % kind
    if not exists(rel):
        fault('%s: not there' % rel)
        return {}
    out = {}
    for m in re.finditer(r'^#define\s+(\w+)\s+(.+?)\s*(?://.*)?$', text(rel), re.M):
        out[m.group(1)] = m.group(2).strip()
    return out


def check_kind(kind, b, tables):
    where = 'boards.json %s' % kind
    for rel in b['sdkconfig_defaults']:
        if not exists(rel):
            fault('%s: the defaults file %s is not there' % (where, rel))
            return
    cfg = defaults_chain(b['sdkconfig_defaults'])
    if cfg.get('CONFIG_IDF_TARGET') != b['chip']:
        fault('%s: chip %s, the defaults say %s' % (where, b['chip'], cfg.get('CONFIG_IDF_TARGET')))
    # (a choice: the last line that sets one of its options wins)
    sizes = re.findall(r'^CONFIG_ESPTOOLPY_FLASHSIZE_(\w+)=y', '\n'.join(text(r) for r in b['sdkconfig_defaults']), re.M)
    if not sizes or sizes[-1] != b['flash_size']:
        fault('%s: flash_size %s, the defaults say %s' % (where, b['flash_size'], sizes[-1:] or None))
    if b['flash_chip_mb'] * 1024 * 1024 < int(b['flash_size'][:-2]) * 1024 * 1024:
        fault('%s: flash_chip_mb is smaller than flash_size' % where)
    if cfg.get('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME') != b['partition_table']:
        fault('%s: partition_table %s, the defaults say %s' % (where, b['partition_table'],
                                                             cfg.get('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME')))
    tgt = b['kconfig_target']
    if cfg.get(tgt) != 'y' and tgt != kconfig_default():
        fault('%s: the defaults do not set %s (and it is not the Kconfig default)' % (where, tgt))
    console = 'usb_serial_jtag' if cfg.get('CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG') == 'y' else (
        'uart0' if cfg.get('CONFIG_ESP_CONSOLE_UART_DEFAULT') == 'y' else '?')
    if console != b['console']:
        fault('%s: console %s, the defaults say %s' % (where, b['console'], console))
    # the partition table and the flash plan
    if not exists(b['partition_table']):
        fault('%s: %s is not there' % (where, b['partition_table']))
        return
    rows = csv_parts(b['partition_table'])
    ota0 = [r for r in rows.values() if r[0] == 'app' and r[1] == 'ota_0']
    otad = [r for r in rows.values() if r[0] == 'data' and r[1] == 'ota']
    fl = b['flash']
    if not ota0 or int(fl['app'], 16) != ota0[0][2]:
        fault('%s: flash.app %s is not the ota_0 slot of %s' % (where, fl['app'], b['partition_table']))
    if not otad or int(fl['otadata'], 16) != otad[0][2]:
        fault('%s: flash.otadata %s is not the otadata of %s' % (where, fl['otadata'], b['partition_table']))
    if fl.get('app_max') and ota0 and int(fl['app_max'], 16) != ota0[0][3]:
        fault('%s: flash.app_max is not the size of the ota_0 slot' % where)
    t = tables.get(b['table_md5'])
    if not t or t.get('csv') != b['partition_table']:
        fault('%s: table_md5 is not a table of %s in "tables"' % (where, b['partition_table']))
    if fl['first_time'] and (fl['bootloader'] is None or fl['table'] is None):
        fault('%s: a first-time plan needs the bootloader and table addresses' % where)
    for md5 in b['boot_md5'] + [b['table_md5']] + ([b['boot_md5_build']] if b.get('boot_md5_build') else []):
        if not re.fullmatch(r'[0-9a-f]{32}', md5):
            fault('%s: %s is not an MD5 value' % (where, md5))
    # the capabilities of the board
    c = caps(kind)
    if c:
        if c.get('BOARD_KIND_NAME') != '"%s"' % kind:
            fault('%s: board_caps.h has BOARD_KIND_NAME %s' % (where, c.get('BOARD_KIND_NAME')))
        fr = b['frame']
        native_w = int(c.get('BOARD_NATIVE_W', '-1'))
        hi = int(c['BOARD_ROUND_HI']) if 'BOARD_ROUND_HI' in c else native_w   # (main/tfn/board.h: the default)
        have = {'w': int(c.get('LCD_W', '-1')), 'h': int(c.get('LCD_H', '-1')), 'native_w': native_w,
                'native_h': int(c.get('BOARD_NATIVE_H', '-1')), 'round_hi': hi}
        if have != fr:
            fault('%s: frame %s, board_caps.h says %s' % (where, fr, have))
    for u in b['console_usb']:
        if not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{4}', u):
            fault('%s: console_usb %s is not vid:pid (hex)' % (where, u))


def check_lists(kinds):
    names = set(kinds)
    # main/apps.json: the slim kinds and each "only" list
    apps = json.loads(text('main/apps.json'))
    used = set((apps.get('slim') or {}).get('kinds', []))

    def walk(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k == 'only' and isinstance(v, list):
                    used.update(v)
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(apps)
    for k in sorted(used - names):
        fault('main/apps.json: the kind %s is not in boards.json' % k)
    # the full lists: the same set as boards.json
    for rel, name in (('sdk/tools/abi_gen.py', 'KINDS'), ('sdk/tools/mk_app.py', 'KINDS')):
        v = py_assign(rel, name)
        if v is not None and set(v) != names:
            fault('%s %s %s is not the set of boards.json %s' % (rel, name, sorted(v), sorted(names)))
    check_os_lists(kinds, names)


def py_dict(rel, name):
    """The dict of a top-level assignment NAME = {...} in a Python file; a key can be the name of a top-level string
    (for example {POOL_STRICT: 'n'})."""
    tree = ast.parse(text(rel))
    names = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
            for t in node.targets:
                if isinstance(t, ast.Name):
                    names[t.id] = node.value.value
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            if not isinstance(node.value, ast.Dict):
                break
            out = {}
            for k, v in zip(node.value.keys, node.value.values):
                key = names.get(k.id) if isinstance(k, ast.Name) else ast.literal_eval(k)
                if key is None:
                    fault('%s %s: the key %s is not a string' % (rel, name, ast.dump(k)))
                    return None
                out[key] = ast.literal_eval(v)
            return out
    fault('%s: no %s = {...}' % (rel, name))
    return None


def release_cfg():
    return {k: v for k, v in BOARDS.get('release_cfg', {}).items() if not k.startswith('_')}


def check_release(kinds):
    """The public release: the kinds with "release": true are the kinds of .github/workflows/release.yml (KINDS and
    the matrix of the firmware job) and the kinds of site/js/boards.js with a web install; release_cfg has only
    CONFIG_ options (y, n or a number), and no OTA mode (release_cfg.py sets it)."""
    for k, b in kinds.items():
        if not isinstance(b.get('release'), bool):
            fault('boards.json %s: "release" is not true or false' % k)
    pub = sorted(k for k, b in kinds.items() if b.get('release'))
    for k, v in release_cfg().items():
        if not re.fullmatch(r'CONFIG_[A-Z0-9_]+', k) or not re.fullmatch(r'y|n|\d+', str(v)):
            fault('boards.json release_cfg: %s=%s is not a CONFIG_ option with y, n or a number' % (k, v))
        if k.startswith('CONFIG_TFN_OTA_SIG_'):
            fault('boards.json release_cfg: %s: the OTA mode comes from sdk/tools/release_cfg.py, not boards.json' % k)
    js = text('site/js/boards.js')
    web = sorted(k for k, w in re.findall(r"kind: '(\w+)',.*?web: (true|false)", js, re.S) if w == 'true')
    if web != pub:
        fault('site/js/boards.js "web: true" %s is not the release kinds of boards.json %s' % (web, pub))
    rel = '.github/workflows/release.yml'
    if exists(rel):
        y = text(rel)
        m = re.search(r'^\s+KINDS: ([\w,]+)$', y, re.M)
        if not m or sorted(m.group(1).split(',')) != pub:
            fault('%s KINDS %s is not the release kinds of boards.json %s' % (rel, m and m.group(1), pub))
        m = re.search(r'^\s+kind: \[([\w, ]+)\]$', y, re.M)
        if not m or sorted(x.strip() for x in m.group(1).split(',')) != pub:
            fault('%s: the matrix of the firmware job %s is not the release kinds %s' % (rel, m and m.group(1), pub))


def check_os_lists(kinds, names):
    """The kinds of main/CMakeLists.txt, main/tfn/board.h and site/js/boards.js."""
    cm = set(re.findall(r'set\(arcade_kind (\w+)\)', text('main/CMakeLists.txt')))
    if cm != names:
        fault('main/CMakeLists.txt arcade_kind %s is not the set of boards.json' % sorted(cm))
    bh = set(re.findall(r'boards/(\w+)/board_caps\.h', text('main/tfn/board.h')))
    if bh != names:
        fault('main/tfn/board.h includes %s, not the set of boards.json' % sorted(bh))
    # site/js/boards.js: the kinds and the USB IDs
    js = text('site/js/boards.js')
    vids = dict(re.findall(r'export const (\w+_VID) = (0x[0-9a-f]+);', js))
    for m in re.finditer(r"kind: '(\w+)'.*?usb: \[(.*?)\]", js, re.S):
        kind, usb = m.group(1), m.group(2)
        got = []
        for v, p in re.findall(r'vid: (\w+), pid: (0x[0-9a-f]+)', usb):
            got.append('%04x:%04x' % (int(vids.get(v, v), 16), int(p, 16)))
        if kind not in kinds:
            fault('site/js/boards.js: the kind %s is not in boards.json' % kind)
        elif sorted(got) != sorted(kinds[kind]['console_usb']):
            fault('site/js/boards.js %s: USB %s, boards.json %s' % (kind, got, kinds[kind]['console_usb']))


def check_tables(tables):
    for md5, t in tables.items():
        if md5.startswith('_'):
            continue
        if not re.fullmatch(r'[0-9a-f]{32}', md5):
            fault('boards.json tables: %s is not an MD5 value' % md5)
        if t.get('csv'):
            if not exists(t['csv']):
                fault('boards.json tables: %s is not there' % t['csv'])
                continue
            otad = [r for r in csv_parts(t['csv']).values() if r[0] == 'data' and r[1] == 'ota']
            if not otad or int(t['otadata'], 16) != otad[0][2]:
                fault('boards.json tables %s: otadata %s is not the one of %s' % (md5, t['otadata'], t['csv']))


def main():
    global BOARDS
    BOARDS = json.loads(text('boards.json'))
    kinds = BOARDS['kinds']
    tables = {k: v for k, v in BOARDS['tables'].items() if not k.startswith('_')}
    for kind, b in kinds.items():
        check_kind(kind, b, tables)
    check_lists(kinds)
    check_release(kinds)
    check_tables(tables)
    for rel in SKIPPED:
        print('NOTE: %s is not in this tree (a private file): its checks are skipped' % rel)
    for f in FAULTS:
        print('FAULT: ' + f)
    if FAULTS:
        print('BOARDS CHECK FAILED (%d)' % len(FAULTS))
        return 1
    print('BOARDS CHECK OK (%d kinds, %d tables)' % (len(kinds), len(tables)))
    return 0


BOARDS = {}

if __name__ == '__main__':
    sys.exit(main())
