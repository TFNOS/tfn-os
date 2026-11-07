#!/usr/bin/env python3
"""tfn: the developer tool of TFN OS (build, flash, monitor, test, simulator, apps). docs/tfn_tool.md tells more.

  python tfn.py doctor                          check the tools of this computer
  python tfn.py boards                          the board kinds of boards.json
  python tfn.py ports [--mac]                   the serial ports with their USB IDs (--mac resets each board)
  python tfn.py build <kind> [--dry-run]        build the firmware into build_dev/<kind>
                [--version X.Y.Z]               (a release build: the version text of a release, the keys of
                [--ota-mode enforce|report]     main/tfn_release_keys.h, the OTA mode; else DEV-<time>-<random>)
                [--store-pub K --ota-pub K]     (a release build: the public keys that main/tfn_release_keys.h holds)
  python tfn.py flash <kind> --port P [--first-time] [--dry-run]
  python tfn.py monitor --port P                the console of a board (DTR and RTS stay low: no reset)
  python tfn.py test [--quick] [--only a,b]     the host checks of CI
  python tfn.py sim [--headless] [args]         the desktop simulator (simulator/run.py, or smoke.py --headless)
  python tfn.py app new <id> [--dir D]          a new app from sdk/templates/hello
  python tfn.py app build <app> [args]          build an app file (sdk/tools/mk_app.py)

Python 3.9 or newer, the standard library, esptool and pyserial (only for ports, flash and monitor). The board facts
come from boards.json.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid

ROOT = os.path.dirname(os.path.abspath(__file__))
IDF_WANT = (6, 1)
# the cross compiler of each chip (the export script of ESP-IDF puts it on PATH)
COMPILERS = {'esp32s3': 'xtensa-esp32s3-elf-gcc', 'esp32p4': 'riscv32-esp-elf-gcc'}
# the oldest esptool for each chip (the P4 plan uses --after watchdog_reset: esptool 4.8 does not have it)
ESPTOOL_MIN = {'esp32s3': (4, 0), 'esp32p4': (5, 0)}
FAIL, WARN, OK = 'FAIL', 'WARN', 'OK'
# the version of a release build (build --version): the rule of the release tools (build_version)
VERSION_RE = re.compile(r'[0-9]{1,4}\.[0-9]{1,5}\.[0-9]{1,5}')
# the setup file of a private tree: main/tfn_defaults.h reads it first, and its values stay in the image
LOCAL_HEADER = 'main/tfn_local.h'
# the public keys of a release (sdk/tools/release_cfg.py header writes this file)
RELEASE_KEYS = 'main/tfn_release_keys.h'
# the tool of the release values (the OTA mode and release_cfg of boards.json), next to this file
RELEASE_TOOL = os.path.join(ROOT, 'sdk', 'tools', 'release_cfg.py')
# the mark of a build folder that a release build made: each release build in that folder removes its sdkconfig first
RELEASE_MARK = 'tfn_release_folder.txt'
# the curve P-256: the field prime and the value b of y^2 = x^3 - 3x + b (a public key of a release is a point of it)
P256_P = 2 ** 256 - 2 ** 224 + 2 ** 192 + 2 ** 96 - 1
P256_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
# the OTA modes of a release build: enforce = the board installs only an update with a good signature (the release has
# its OTA key); report = a dry run with TEST keys
OTA_MODES = ('enforce', 'report')
# the folders of the sources of a firmware build, and the files there that git can ignore in a release build: the caches
# of Python and the files of the file managers (a build cannot include them)
SOURCE_DIRS = ('main', 'apps', 'libs', 'components', 'boards', 'third_party')
SOURCE_OK = re.compile(r'(^|/)(__pycache__/.*|[^/]+\.pyc|\.DS_Store|Thumbs\.db|desktop\.ini)$')


class Stop(Exception):
    pass


def boards():
    with open(os.path.join(ROOT, 'boards.json'), encoding='utf-8') as f:
        return json.load(f)


def kind_of(name):
    doc = boards()
    if name not in doc['kinds']:
        raise Stop('no board kind "%s" (the kinds: %s)' % (name, ', '.join(doc['kinds'])))
    return doc, doc['kinds'][name]


def quote(cmd):
    return ' '.join(shlex.quote(str(c)) for c in cmd)


def run(cmd, dry, **kw):
    """Run a command (or only print it with --dry-run). Returns the exit code."""
    print(('DRY RUN: ' if dry else '> ') + quote(cmd), flush=True)
    if dry:
        return 0
    return subprocess.run([str(c) for c in cmd], **kw).returncode


def md5_file(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def boot_hash(path):
    """The MD5 of a bootloader with no build date (0x48..0x63) and no checksum + SHA-256 (the last 33 bytes)."""
    with open(path, 'rb') as f:
        b = bytearray(f.read())
    b[0x48:0x64] = bytes(0x1C)
    b[-33:] = bytes(33)
    return hashlib.md5(b).hexdigest()


def image_facts(path):
    """The kind and the version that a firmware image tells."""
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 0x100 or data[0] != 0xE9 or struct.unpack_from('<I', data, 0x20)[0] != 0xABCD5432:
        raise Stop('%s is not a firmware image' % path)
    m = re.search(rb'@ARCADE-BOARD:([a-z0-9]{1,15})@', data)
    return {'kind': m.group(1).decode() if m else '', 'size': len(data),
            'version': data[0x30:0x50].split(b'\0')[0].decode('ascii', 'replace')}


# ---------------------------------------------------------------- the tools of this computer
def idf_path():
    p = os.environ.get('IDF_PATH', '')
    return p if p and os.path.isdir(p) else ''


def idf_version(path):
    """The (major, minor, patch) of the ESP-IDF at path (tools/cmake/version.cmake), or None."""
    try:
        with open(os.path.join(path, 'tools', 'cmake', 'version.cmake'), encoding='utf-8') as f:
            t = f.read()
    except OSError:
        return None
    got = [re.search(r'IDF_VERSION_%s\s+(\d+)' % n, t) for n in ('MAJOR', 'MINOR', 'PATCH')]
    return tuple(int(m.group(1)) for m in got) if all(got) else None


def idf_python():
    """The Python of the ESP-IDF environment (IDF_PYTHON_ENV_PATH), else this Python."""
    env = os.environ.get('IDF_PYTHON_ENV_PATH', '')
    for rel in (('Scripts', 'python.exe'), ('bin', 'python3'), ('bin', 'python')):
        p = os.path.join(env, *rel) if env else ''
        if p and os.path.isfile(p):
            return p
    return sys.executable


def tool_line(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return ''
    return (r.stdout or r.stderr).strip().splitlines()[0] if r.returncode == 0 and (r.stdout or r.stderr) else ''


def py_module(python, name):
    """The version of a module in the Python at python, or ''."""
    code = 'import %s as m; print(getattr(m, "__version__", None) or getattr(m, "VERSION", "?"))' % name
    return tool_line([python, '-c', code])


def cmd_doctor(a):
    rows = []

    def row(level, what, text):
        rows.append((level, what, text))

    v = sys.version_info
    row(OK if v >= (3, 9) else FAIL, 'Python', '%d.%d.%d (%s)' % (v[0], v[1], v[2], sys.executable))
    for mod in ('serial', 'esptool'):
        ver = py_module(sys.executable, mod)
        row(OK if ver else WARN, mod, ver or 'not in this Python (pip install %s): ports, flash and monitor need it'
            % ('pyserial' if mod == 'serial' else mod))
    p = idf_path()
    if not p:
        row(WARN, 'ESP-IDF', 'IDF_PATH is not set: build and flash need ESP-IDF %d.%d (run its export script)' % IDF_WANT)
    else:
        ver = idf_version(p)
        good = ver is not None and ver[:2] == IDF_WANT
        row(OK if good else WARN, 'ESP-IDF', '%s at %s%s' % ('.'.join(map(str, ver)) if ver else '?', p,
                                                            '' if good else ': TFN OS needs %d.%d' % IDF_WANT))
        if not os.path.isfile(os.path.join(p, 'tools', 'idf.py')):
            row(WARN, 'idf.py', 'not in %s/tools' % p)
        ip = idf_python()
        row(OK if ip != sys.executable else WARN, 'IDF Python',
            ip if ip != sys.executable else 'IDF_PYTHON_ENV_PATH is not set: this Python runs idf.py and esptool')
        if ip != sys.executable:
            ev = py_module(ip, 'esptool')
            row(OK if ev else WARN, 'IDF esptool', ev or 'not in the IDF Python')
        for chip, cc in sorted(COMPILERS.items()):
            line = tool_line([cc, '--version']) if shutil.which(cc) else ''
            row(OK if line else WARN, chip + ' gcc', line or '%s is not on PATH (the export script of ESP-IDF)' % cc)
    g = tool_line(['git', '--version'])
    row(OK if g else FAIL, 'git', g or 'not found')
    cc = os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc')
    ccv = tool_line([cc, '--version']) if cc else ''
    row(OK if ccv else WARN, 'host compiler', ('%s (%s)' % (ccv, shutil.which(cc) or cc)) if ccv else
        'no clang or gcc: tfn test needs one for the unit tests and the fuzz')
    for name, why in (('sh', 'the fuzz build (tests/host/build.sh)'), ('node', 'the web flasher test'),
                      ('clang-format', 'the format check (version 22.1.8, or set CLANG_FORMAT)')):
        exe = os.environ.get('CLANG_FORMAT') if name == 'clang-format' and os.environ.get('CLANG_FORMAT') else name
        line = tool_line([exe, '--version']) if shutil.which(exe) or os.path.isfile(exe) else ''
        if name == 'clang-format' and line and 'version 22.1.8' not in line:
            row(WARN, name, '%s: the format check needs 22.1.8 (pip install clang-format==22.1.8, or set CLANG_FORMAT)'
                % line)
            continue
        row(OK if line else WARN, name, line or 'not found: %s' % why)
    if sys.platform.startswith('linux'):
        row(*linux_port_rights())
    w = max(len(r[1]) for r in rows)
    for level, what, text in rows:
        print('%-4s  %-*s  %s' % (level, w, what, text))
    bad = sum(1 for r in rows if r[0] == FAIL)
    print('DOCTOR %s (%d fail, %d warn)' % ('OK' if not bad else 'FAILED', bad, sum(1 for r in rows if r[0] == WARN)))
    return 1 if bad else 0


def linux_port_rights():
    """The serial port rights on Linux: the group of the ports (dialout or uucp) and the ports that this user can open."""
    import glob
    import grp
    ports = sorted(glob.glob('/dev/ttyACM*') + glob.glob('/dev/ttyUSB*'))
    mine = {grp.getgrgid(g).gr_name for g in os.getgroups()}
    groups = []
    for p in ports:
        try:
            groups.append(grp.getgrgid(os.stat(p).st_gid).gr_name)
        except (OSError, KeyError):
            pass
    need = sorted(set(groups) or {'dialout'})
    closed = [p for p in ports if not os.access(p, os.R_OK | os.W_OK)]
    if closed:
        return (WARN, 'serial rights', 'no access to %s: sudo usermod -aG %s $USER, then log in again'
                % (', '.join(closed), need[0]))
    if not ports:
        return (OK if set(need) & mine else WARN, 'serial rights', 'no port now; you are %sin the group %s'
                % ('' if set(need) & mine else 'NOT ', need[0]))
    return (OK, 'serial rights', 'you can open %s' % ', '.join(ports))


# ---------------------------------------------------------------- boards and ports
def cmd_boards(a):
    doc = boards()
    for k, b in doc['kinds'].items():
        f = b['frame']
        print('%-8s %-8s %-5s %-22s %s  frame %dx%d  %s' % (k, b['chip'], b['flash_size'], b['partition_table'],
                                                         ','.join(b['console_usb']), f['w'], f['h'], b['name']))
    return 0


def cmd_ports(a):
    try:
        from serial.tools import list_ports
    except ImportError:
        raise Stop('pyserial is not in this Python: pip install pyserial')
    doc = boards()
    by_usb = {}
    for k, b in doc['kinds'].items():
        for u in b['console_usb']:
            by_usb.setdefault(u, []).append(k)
    found = 0
    print('%-14s %-9s %-8s %-30s %-18s %s' % ('PORT', 'USB', 'ROLE', 'KINDS', 'USB SERIAL', 'DESCRIPTION'))
    for p in sorted(list_ports.comports(), key=lambda x: (x.serial_number or '', x.device)):
        usb = '%04x:%04x' % (p.vid, p.pid) if p.vid is not None else ''
        kinds = by_usb.get(usb, [])
        if a.all is False and not kinds:
            continue
        found += 1
        desc = p.description or ''
        # (Windows gives two ports for the USB Serial/JTAG of an ESP32: interface 0 is the console, interface 2 the JTAG)
        role = 'jtag' if 'Interface 2' in desc else 'console' if kinds else '-'
        print('%-14s %-9s %-8s %-30s %-18s %s' % (p.device, usb or '-', role, ','.join(kinds) or '-',
                                                 p.serial_number or '-', desc))
        if a.mac and kinds and role == 'console':
            chip = doc['kinds'][kinds[0]]['chip']
            out = subprocess.run([idf_python(), '-m', 'esptool', '--chip', chip, '-p', p.device, '--after',
                                  'hard_reset', 'read_mac'], capture_output=True, text=True)
            macs = re.findall(r'MAC:\s*([0-9a-fA-F:]{17})', out.stdout)
            print('%-14s MAC %s' % ('', macs[0] if macs else 'not read (%s)' % (out.stderr.strip()[-120:] or out.returncode)))
    if not found:
        print('no port with a USB ID of boards.json (--all lists all ports)')
    print('(a USB ID can fit more than one kind: the console line "ota" of the firmware tells the kind; the serial of an '
          'Espressif USB port is the MAC of the chip)')
    return 0


# ---------------------------------------------------------------- build
def build_dir(a, kind):
    doc = boards()
    return os.path.abspath(a.build_dir) if getattr(a, 'build_dir', '') else os.path.join(ROOT, doc['build_root'], kind)


def force_cfg(path, values, dry):
    """Put the force_cfg values of boards.json into the sdkconfig at path (when it has the option)."""
    if not os.path.isfile(path):
        return 0
    with open(path, 'rb') as f:
        lines = f.read().decode('utf-8').split('\n')
    n = 0
    for i, ln in enumerate(lines):
        cr = '\r' if ln.endswith('\r') else ''
        off = ln.rstrip('\r')
        k, _, v = off.partition('=')
        if off.startswith('# ') and off.endswith(' is not set') and off[2:-11] in values:
            k, v = off[2:-11], ''
        if k in values and v != values[k]:
            lines[i] = k + '=' + values[k] + cr
            n += 1
    if n and not dry:
        with open(path, 'wb') as f:
            f.write('\n'.join(lines).encode('utf-8'))
    return n


def cache_home(bdir):
    """The source folder of the CMake cache in bdir, or ''."""
    try:
        with open(os.path.join(bdir, 'CMakeCache.txt'), encoding='utf-8', errors='replace') as f:
            for ln in f:
                if ln.startswith('CMAKE_HOME_DIRECTORY:'):
                    return ln.split('=', 1)[1].strip()
    except OSError:
        pass
    return ''


def same_dir(a, b):
    return os.path.normcase(os.path.normpath(os.path.abspath(a))) == os.path.normcase(os.path.normpath(os.path.abspath(b)))


def check_idf(a):
    p = idf_path()
    if not p:
        raise Stop('IDF_PATH is not set: run the export script of ESP-IDF %d.%d first (tfn doctor)' % IDF_WANT)
    ver = idf_version(p)
    if (ver is None or ver[:2] != IDF_WANT) and not a.any_idf:
        raise Stop('ESP-IDF %s at %s: TFN OS needs %d.%d (--any-idf goes on)' %
                   ('.'.join(map(str, ver)) if ver else '?', p, IDF_WANT[0], IDF_WANT[1]))
    return p


def build_version(version):
    """The version text of the image (PROJECT_VER): DEV-<UTC time>-<8 random hex digits>, or the version of a release.
    A release version has the form of the release tools: <major>.<minor>.<patch>, with at most 4 digits for major and 5
    digits for each other part (the tag v<version> of .github/workflows/release.yml, and the version rule of the family
    release builds; release_pack.py needs the same text in all kinds of a release; a pool manager compares the
    numbers)."""
    if not version:
        return 'DEV-' + time.strftime('%y%m%d%H%M%S', time.gmtime()) + '-' + uuid.uuid4().hex[:8]
    if not VERSION_RE.fullmatch(version):
        raise Stop('the version "%s" is not <major>.<minor>.<patch> (for example 1.2.3; at most 4 digits for major and '
                   '5 for minor and patch)' % version)
    return version


def release_git(args, timeout=300):
    """The output of "git -C ROOT <args>" (a release build). Stop when git does not run or fails, and when ROOT is not
    the top of a git work tree: then git cannot tell the files of the tree."""
    try:
        top = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--show-toplevel'], capture_output=True, text=True,
                             timeout=60)
        if top.returncode or not same_dir(top.stdout.strip(), ROOT):
            raise Stop('%s is not the top of a git work tree: a build with --version needs a clone of the repo (git '
                       'tells the files of the tree)' % ROOT)
        r = subprocess.run(['git', '-C', ROOT] + args, capture_output=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as e:
        raise Stop('git does not run (%s): a build with --version needs git' % e)
    if r.returncode:
        raise Stop('git %s failed: %s' % (args[0], r.stderr.decode('utf-8', 'replace').strip()[-200:]))
    return r.stdout.decode('utf-8', 'replace')


def private_sources():
    """The files of the source folders (SOURCE_DIRS) that git ignores, except SOURCE_OK and the keys of the release.
    A private tree keeps its private sources there, and a build takes them (__has_include: main/tfn_local.h,
    main/chat_local.h ...; the files of main/apps.json)."""
    out = release_git(['ls-files', '-z', '--others', '--ignored', '--exclude-standard', '--'] + list(SOURCE_DIRS))
    names = [n for n in out.split('\0') if n]
    return sorted(n for n in names if n != RELEASE_KEYS and not SOURCE_OK.search(n))


def tree_changes():
    """The changes of the tree against HEAD (git status, with each untracked file): each tracked file that is changed,
    added, removed or not merged, and each untracked file that git does not ignore. A build can take each of them, and
    the commit of the build (tfn_build.json) would not tell them. Not the keys of the release and not SOURCE_OK (an
    untracked file only). Returns the lines "<state> <path>"."""
    out = release_git(['status', '--porcelain=v1', '-z', '--untracked-files=all', '--no-renames'])
    found = []
    for ent in [e for e in out.split('\0') if e]:
        state, path = ent[:2], ent[3:]
        if state == '??' and (path == RELEASE_KEYS or SOURCE_OK.search(path)):
            continue
        found.append('%s %s' % (state.strip(), path))
    return sorted(found, key=lambda x: x.split(' ', 1)[1])


def release_commit():
    """The commit of a release build (git rev-parse HEAD): tfn_build.json tells it."""
    commit = release_git(['rev-parse', '--verify', 'HEAD'], 60).strip()
    if not re.fullmatch(r'[0-9a-f]{40}([0-9a-f]{24})?', commit):
        raise Stop('git rev-parse HEAD gave no commit (%s): a build with --version needs a commit' % commit[:80])
    return commit


def release_guard():
    """A build with a release version takes only the files of the commit of git and the keys of the release (the rule
    of the job firmware of .github/workflows/release.yml). It stops when the tree has main/tfn_local.h: that file comes
    first in main/tfn_defaults.h, so its private values (keys, addresses) would go into an image with a release
    number. It also stops at each other file of the source folders that git ignores (private_sources), and at each
    change against the commit (tree_changes). Returns the commit (release_commit)."""
    if os.path.isfile(os.path.join(ROOT, LOCAL_HEADER)):
        raise Stop('%s is in the tree: a build with --version takes only the keys of the release. Use a tree with no '
                   '%s (for example a clean clone), or build with no --version (a DEV build)'
                   % (LOCAL_HEADER, LOCAL_HEADER))
    extra = private_sources()
    if extra:
        raise Stop('the source folders have %d file(s) that git ignores (%s%s): a build with --version takes only the '
                   'files of git and %s. Use a clean clone, or build with no --version (a DEV build)'
                   % (len(extra), ', '.join(extra[:8]), ' ...' if len(extra) > 8 else '', RELEASE_KEYS))
    changed = tree_changes()
    if changed:
        raise Stop('the tree has %d change(s) against the commit (git status: %s%s): a build with --version takes only '
                   'the files of the commit and %s. Commit or remove the changes, or build with no --version (a DEV '
                   'build)' % (len(changed), ', '.join(changed[:8]), ' ...' if len(changed) > 8 else '', RELEASE_KEYS))
    return release_commit()


def release_mode(a):
    """The OTA mode of a release build (--version: --ota-mode, default enforce), or '' for a DEV build. A release
    build needs a clean tree (release_guard), no option that changes the sources of the build (--select, the
    environment variable TFN_DEVICE_PUBLIC_HEADER) and the public keys of the release in main/tfn_release_keys.h
    (release_keys). A build in the OTA mode report must not have the version of a release tag. The boards, the
    manager and a pool manager take an image with a release number as that release: with other keys, or in the OTA
    mode of the Kconfig default, it must not have that number."""
    mode = getattr(a, 'ota_mode', '') or ''
    if not a.version:
        if mode:
            raise Stop('--ota-mode is for a release build: give --version too')
        return ''
    if mode and mode not in OTA_MODES:
        raise Stop('the OTA mode "%s" is not %s' % (mode, ' or '.join(OTA_MODES)))
    if getattr(a, 'select', ''):
        raise Stop('--select is not for a build with --version: a release has the apps of main/apps.json')
    if os.environ.get('TFN_DEVICE_PUBLIC_HEADER', ''):
        raise Stop('TFN_DEVICE_PUBLIC_HEADER is set: a build with --version takes no header from outside the tree. '
                   'Remove the variable, or build with no --version (a DEV build)')
    release_guard()
    release_keys(a)
    if mode == 'report' and release_git(['tag', '--list', 'v' + a.version], 60).strip():
        raise Stop('the version %s has the release tag v%s: a build in the OTA mode report with that number looks like '
                   'that release to the boards, the manager and a pool manager. Give a test build a version that no '
                   'release has' % (a.version, a.version))
    return mode or 'enforce'


def key_id(hexkey):
    """The key ID of a public key: the first 16 hex digits of its SHA-256 (as the summary of release.yml)."""
    return hashlib.sha256(bytes.fromhex(hexkey)).hexdigest()[:16]


def p256_point(hexkey):
    """True when the key (130 hex digits, 04 x y) is a point of the curve P-256."""
    x, y = int(hexkey[2:66], 16), int(hexkey[66:], 16)
    if x >= P256_P or y >= P256_P:
        return False
    return (y * y - (x * x * x - 3 * x + P256_B)) % P256_P == 0


def pub_option(text, what):
    """A public key of an option: 130 hex digits that start with 04, or the name of a file that holds them (for
    example the pubs/*.pub of a release). Stop when it is not a point of P-256."""
    v = (text or '').strip()
    if v and os.path.isfile(v):
        with open(v, encoding='utf-8', errors='replace') as f:
            v = f.read().strip()
    rc = release_tool()
    try:
        v = rc.key_hex(v, what)
    except rc.Fault as e:
        raise Stop(str(e))
    if not p256_point(v):
        raise Stop('%s is not a point of the curve P-256' % what)
    return v


def header_keys(text):
    """The defines of a keys header and the key ID of each value (or the text "not a key")."""
    out = []
    for name, value in re.findall(r'^\s*#\s*define\s+(\w+)[ \t]*(.*)$', text, re.M):
        if name.endswith('_H') and not value.strip():
            continue
        digits = ''.join(re.findall(r'0x([0-9a-fA-F]{2})', value)).lower()
        good = re.fullmatch(r'04[0-9a-f]{128}', digits) and p256_point(digits)
        out.append('%s %s' % (name, key_id(digits) if good else 'not a key'))
    return out


def release_keys(a):
    """The public keys of a release build: [(the define, the key as hex)] of --store-pub, --ota-pub and --ota-pub2.
    main/tfn_release_keys.h must be the same text that "release_cfg.py header" writes for these keys. A file with other
    keys (the TEST keys of a dry run, a file of an earlier build), with a key less or more, or with other lines stops
    the build: the image trusts only the keys of that file."""
    store, ota, ota2 = (getattr(a, n, '') or '' for n in ('store_pub', 'ota_pub', 'ota_pub2'))
    if not store or not ota:
        raise Stop('a build with --version needs the public keys of the release: --store-pub <hex> --ota-pub <hex> '
                   '[--ota-pub2 <hex>] (tfn compares %s with them). Else build with no --version (a DEV build)'
                   % RELEASE_KEYS)
    keys = [('TFN_STORE_PUBKEY_BYTES', pub_option(store, 'the store key')),
            ('TFN_OTA_PUBKEY1_BYTES', pub_option(ota, 'the OTA key'))]
    if ota2:
        keys.append(('TFN_OTA_PUBKEY2_BYTES', pub_option(ota2, 'the second OTA key')))
    if len({v for _, v in keys}) != len(keys):
        raise Stop('two of the keys are the same key: a release has a store key and its own OTA key(s)')
    path = os.path.join(ROOT, RELEASE_KEYS)
    if not os.path.isfile(path):
        raise Stop('no %s: a build with --version needs the public keys of the release (python '
                   'sdk/tools/release_cfg.py header --out %s --store-pub <hex> --ota-pub <hex>). Else build with no '
                   '--version (a DEV build)' % (RELEASE_KEYS, RELEASE_KEYS))
    with open(path, 'rb') as f:
        have = f.read().decode('utf-8', 'replace').replace('\r\n', '\n')
    want = release_tool().header_text(*[v for _, v in keys])
    if have != want:
        raise Stop('%s is not the header of the keys of the options (it has: %s; the options: %s). Write it again: '
                   'python sdk/tools/release_cfg.py header --out %s --store-pub <hex> --ota-pub <hex>'
                   % (RELEASE_KEYS, ', '.join(header_keys(have)) or 'no define',
                      ', '.join('%s %s' % (n, key_id(v)) for n, v in keys), RELEASE_KEYS))
    return keys


def release_folder(bdir, dry):
    """A release build starts from the defaults of the kind, as the job firmware of release.yml (a new folder):
    "idf.py reconfigure" keeps each value of an old sdkconfig, and release_cfg.py force and check cover only the
    release values. So a release build stops in a folder with the sdkconfig or the CMake cache of another build (for
    example the DEV folder build_dev/<kind>). In a folder that a release build made (RELEASE_MARK), it removes the
    sdkconfig before "idf.py reconfigure"."""
    marked = os.path.isfile(os.path.join(bdir, RELEASE_MARK))
    old = [n for n in ('sdkconfig', 'CMakeCache.txt') if os.path.exists(os.path.join(bdir, n))]
    if old and not marked:
        raise Stop('%s has the %s of another build: a build with --version starts in a new folder (its old values '
                   'would go into the release). Give --build-dir <a new folder>, for example build_rel/<kind>'
                   % (bdir, ' and the '.join(old)))
    if dry:
        print('DRY RUN: a release build folder (%s): no sdkconfig before "idf.py reconfigure"' % bdir)
        return
    os.makedirs(bdir, exist_ok=True)
    with open(os.path.join(bdir, RELEASE_MARK), 'w', encoding='utf-8') as f:
        f.write('tfn.py build --version: each release build in this folder starts with no sdkconfig\n')
    for n in ('sdkconfig', 'sdkconfig.old'):
        p = os.path.join(bdir, n)
        if os.path.isfile(p):
            os.remove(p)
            print('%s: removed (a release build starts from the defaults of the kind)' % p)


def release_failed(image, msg):
    """Give the image the name <image>.failed ("tfn flash" does not take it) and stop with msg."""
    os.replace(image, image + '.failed')
    raise Stop('%s: it is not an image of a release. The image is now %s.failed (no tfn_build.json)' % (msg, image))


def release_after(image, commit, keys):
    """The checks of a release image after the build: the tree is still clean (release_guard: the build takes some
    minutes, and a sync, an editor or another tool can change a source), HEAD is still the commit of the start, and
    the image holds each public key of the release (release_cfg.py keys). Returns the key IDs for tfn_build.json."""
    try:
        now = release_guard()
    except Stop as e:
        release_failed(image, 'the tree changed during the build (%s)' % e)
    if now != commit:
        release_failed(image, 'HEAD changed during the build (%s, not %s)' % (now[:12], commit[:12]))
    with open(image, 'rb') as f:
        miss = release_tool().image_keys(f.read(), keys)
    if miss:
        release_failed(image, 'the image does not hold the key(s) %s' % ', '.join(miss))
    return {n: key_id(v) for n, v in keys}


def release_tool():
    """The module sdk/tools/release_cfg.py: the job firmware of release.yml uses the same force and check."""
    spec = importlib.util.spec_from_file_location('tfn_release_cfg', RELEASE_TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def release_prepare(cmd, cfg, mode, dry):
    """The steps of a release build before "idf.py build" (as the job firmware of release.yml): "idf.py reconfigure"
    makes the sdkconfig of the build folder, then release_cfg.py force puts in the force_cfg and release_cfg values of
    boards.json and the OTA mode. Returns the release_cfg values (for the check after the build), or None when
    reconfigure failed."""
    rc = release_tool()
    bj = os.path.join(ROOT, 'boards.json')
    try:
        rel = rc.release_values(bj)
        values = dict(rc.board_values(bj), **rel)
        values.update(rc.ota_values(mode))
    except rc.Fault as e:
        raise Stop(str(e))
    if run(cmd + ['reconfigure'], dry, cwd=ROOT):
        return None
    if dry:
        print('DRY RUN: release_cfg.py force %s --ota-mode %s (%d release values)' % (cfg, mode, len(rel)))
    else:
        if not os.path.isfile(cfg):
            raise Stop('no %s after "idf.py reconfigure"' % cfg)
        n = rc.force(cfg, values, rc.OTA + tuple(sorted(rel)))
        print('%s: OTA mode %s, %d release values, %d changes (release_cfg.py force)' % (cfg, mode, len(rel), n))
    return rel


def release_check(bdir, image, mode, rel):
    """Stop when the build does not have the OTA mode and the release_cfg values (release_cfg.py check of its
    config/sdkconfig.h): such an image must not get the channel RELEASE, and "tfn flash" must not find it (it gets
    the name <image>.failed)."""
    rc = release_tool()
    header = os.path.join(bdir, 'config', 'sdkconfig.h')
    if not os.path.isfile(header):
        raise Stop('no %s: the build did not end' % header)
    bad = rc.check(header, mode, rel)
    if bad:
        release_failed(image, 'the build does not have the OTA mode %s and the release values (%s)'
                       % (mode, ', '.join(bad)))


def build_channel(version, mode=''):
    """The channel of tfn_build.json: RELEASE for a build with a release version in the OTA mode enforce (release_mode
    and release_check let only a release build come so far), TEST for a build with a release version in the OTA mode
    report (a dry run with TEST keys: not an image of a release), else DEV."""
    if not version:
        return 'DEV'
    return 'RELEASE' if mode == 'enforce' else 'TEST'


def idf_tools(a, b):
    """The idf.py of the ESP-IDF. Stop when the ESP-IDF or a tool of the build is not there (a dry run only prints
    it)."""
    if a.dry_run:
        try:
            return os.path.join(check_idf(a), 'tools', 'idf.py')
        except Stop as e:
            print('DRY RUN: %s' % e)
            return os.path.join(idf_path() or '$IDF_PATH', 'tools', 'idf.py')
    idf = os.path.join(check_idf(a), 'tools', 'idf.py')
    for tool in ('cmake', 'ninja', COMPILERS[b['chip']]):
        if not shutil.which(tool):
            raise Stop('%s is not on PATH: run the export script of ESP-IDF first' % tool)
    return idf


def cmd_build(a):
    doc, b = kind_of(a.kind)
    ident = build_version(a.version)
    mode = release_mode(a)
    commit = release_commit() if mode else ''
    keys = release_keys(a) if mode else []
    bdir = build_dir(a, a.kind)
    home = cache_home(bdir)
    if home and not same_dir(home, ROOT):
        # (dev.ps1 builds a copy of the source in build_dev/work into the same folders: keep its cache)
        raise Stop('%s has the CMake cache of another source folder (%s): give --build-dir' % (bdir, home))
    if mode:
        release_folder(bdir, a.dry_run)
    idf = idf_tools(a, b)
    cfg = os.path.join(bdir, 'sdkconfig')
    sel = os.path.abspath(a.select) if a.select else ''
    guard = os.environ.get('TFN_DEVICE_PUBLIC_HEADER', '')
    cmd = [idf_python(), idf, '-C', ROOT, '-B', bdir, '-D', 'PROJECT_VER=' + ident,
           '-D', 'SDKCONFIG=' + cfg,
           '-D', 'SDKCONFIG_DEFAULTS=' + ';'.join(os.path.join(ROOT, x) for x in b['sdkconfig_defaults']),
           '-D', 'ARCADE_SELECT=' + sel, '-D', 'ARCADE_DEVICE_PUBLIC_HEADER=' + guard]
    if b['chip'] != 'esp32s3':
        cmd += ['-D', 'IDF_TARGET=' + b['chip']]
    facts_file = os.path.join(bdir, 'tfn_build.json')
    if not a.dry_run:
        os.makedirs(bdir, exist_ok=True)
        if os.path.isfile(facts_file):
            os.remove(facts_file)  # (the facts of the last good build: a new build that fails must not keep them)
    rel = None
    if mode:
        rel = release_prepare(cmd, cfg, mode, a.dry_run)
        if rel is None:
            print('BUILD FAILED (%s, idf.py reconfigure)' % a.kind)
            return 1
    else:
        values = {k: v for k, v in doc['force_cfg'].items() if not k.startswith('_')}
        n = force_cfg(cfg, values, a.dry_run)
        if n:
            print('%s: %d forced values (force_cfg of boards.json)%s' % (cfg, n, ' (not written: dry run)'
                                                                          if a.dry_run else ''))
    rc = run(cmd + ['build'], a.dry_run, cwd=ROOT)
    if rc:
        print('BUILD FAILED (%s, exit %d)' % (a.kind, rc))
        return rc
    if a.dry_run:
        return 0
    image = os.path.join(bdir, doc['app_image'])
    facts = image_facts(image)
    if facts['kind'] != a.kind or facts['version'] != ident:
        raise Stop('the image says kind %s, version %s (not %s, %s)' % (facts['kind'], facts['version'], a.kind, ident))
    if mode:
        release_check(bdir, image, mode, rel)
        facts.update(ota_mode=mode, commit=commit, keys=release_after(image, commit, keys))
    facts.update(channel=build_channel(a.version, mode), sha256=hashlib.sha256(open(image, 'rb').read()).hexdigest(),
                 made=int(time.time()))
    with open(facts_file, 'w', encoding='utf-8') as f:
        json.dump(facts, f, indent=1)
    print('%s OK: %s%s (%s)' % (a.kind, ident, ', OTA mode %s' % mode if mode else '', image))
    if mode == 'report':
        print('NOTE: a TEST build (the OTA mode report) with the version %s: keep it on this computer. Do not flash it '
              'to a board of a user and do not publish it' % ident)
    return 0


# ---------------------------------------------------------------- flash
def esptool_cmd(b, port, baud=None):
    cmd = [idf_python(), '-m', 'esptool', '--chip', b['chip'], '-p', port]
    if baud:
        cmd += ['-b', str(baud)]
    return cmd


def storage_region(csv_rel):
    """(offset, size) of the storage partition of a partition table CSV."""
    with open(os.path.join(ROOT, csv_rel), encoding='utf-8') as f:
        for ln in f:
            p = [x.strip() for x in ln.split('#', 1)[0].split(',')]
            if len(p) >= 5 and p[1] == 'data' and p[2] == 'fat':
                return p[3], p[4]
    raise Stop('%s has no storage partition' % csv_rel)


def slot_size(csv_rel):
    """The size of the ota_0 slot of a partition table CSV (bytes)."""
    with open(os.path.join(ROOT, csv_rel), encoding='utf-8') as f:
        for ln in f:
            p = [x.strip() for x in ln.split('#', 1)[0].split(',')]
            if len(p) >= 5 and p[1] == 'app' and p[2] == 'ota_0':
                s = p[4]
                return int(s[:-1], 0) * 1024 * 1024 if s.upper().endswith('M') else int(s, 0)
    return 0


def cmd_flash(a):
    doc, b = kind_of(a.kind)
    fl = b['flash']
    tables = {k: v for k, v in doc['tables'].items() if not k.startswith('_')}
    bdir = build_dir(a, a.kind)
    app = os.path.join(bdir, doc['app_image'])
    boot = os.path.join(bdir, 'bootloader', 'bootloader.bin')
    table = os.path.join(bdir, 'partition_table', 'partition-table.bin')
    otad = os.path.join(bdir, 'ota_data_initial.bin')
    have_files = os.path.isfile(app)
    if not have_files and not a.dry_run:
        raise Stop('no image %s: run tfn build %s first' % (app, a.kind))
    if a.first_time and not fl['first_time']:
        raise Stop('%s: no first-time flash (%s)' % (a.kind, '; '.join(b['notes'][:1])))
    # 1. the checks of the build (the image identity, the bootloader and the table of the known layout)
    if have_files:
        facts = image_facts(app)
        if facts['kind'] != a.kind:
            raise Stop('%s is an image of the kind "%s", not %s' % (app, facts['kind'], a.kind))
        print('the image: %s %s, %d bytes' % (facts['kind'], facts['version'], facts['size']))
        if fl.get('app_max') and facts['size'] > int(fl['app_max'], 16):
            raise Stop('the image is bigger than its slot (%s)' % fl['app_max'])
        if fl['first_time']:
            bh, th = boot_hash(boot), md5_file(table)
            if not b['boot_md5']:
                msg = 'no known bootloader MD5 for %s (the build gives %s)' % (a.kind, bh)
                if a.first_time and not a.force:
                    raise Stop(msg + ': --force writes it')
                print('NOTE: ' + msg)
            elif bh not in b['boot_md5'] and not a.force:
                raise Stop('the bootloader of the build (%s) is not the known one %s' % (bh, b['boot_md5']))
            if th not in tables or not tables[th]['flash'] or th != b['table_md5']:
                if not a.force:
                    raise Stop('the partition table of the build (%s) is not the known table of %s' % (th, a.kind))
            build_table = th
        else:
            build_table = b['table_md5']
    else:
        print('DRY RUN: no build in %s: the checks of the build are not done' % bdir)
        build_table = b['table_md5']
    base = esptool_cmd(b, a.port, fl['baud'])
    if not a.dry_run:
        line = tool_line([idf_python(), '-m', 'esptool', 'version'])
        got = re.search(r'(\d+)\.(\d+)', line or '')
        need = ESPTOOL_MIN.get(b['chip'], (4, 0))
        if not got or (int(got.group(1)), int(got.group(2))) < need:
            raise Stop('esptool %s in %s: %s needs %d.%d or newer' % (line or '(none)', idf_python(), b['chip'],
                                                                  need[0], need[1]))
    # 2. the plan of the kind
    if not fl['first_time']:
        # (the bootloader, the table and the boot menu belong to another project: the otadata blank, then the app)
        steps = [base + ['--after', 'no_reset', 'erase_region', fl['otadata'], '0x2000'],
                 base + ['--after', fl['after'], 'write_flash', fl['app'], app]]
    elif a.first_time:
        start, size = storage_region(b['partition_table'])
        print('FIRST TIME: this erases the settings and the files of the board on %s' % a.port)
        if not a.dry_run and not a.yes:
            if input('Flash %s on %s? [y/N] ' % (a.kind, a.port)).strip().lower() != 'y':
                raise Stop('no flash (the user)')
        steps = [esptool_cmd(b, a.port) + ['--after', 'no_reset', 'erase_region', '0x9000', '0x7000'],
                 esptool_cmd(b, a.port) + ['--after', 'no_reset', 'erase_region', start, size],
                 base + ['--before', 'default_reset', '--after', fl['after'], 'write_flash', fl['bootloader'], boot,
                         fl['table'], table, fl['app'], app, fl['otadata'], otad]]
    else:
        # the app + the otadata: the place of the otadata comes from the table that the board has now
        port_tag = re.sub(r'[^A-Za-z0-9]+', '_', a.port).strip('_')
        board_tbl = os.path.join(bdir, 'board_table_%s.bin' % port_tag)
        read = base + ['--before', 'default_reset', '--after', 'no_reset', 'read_flash', '0x8000', '0xc00', board_tbl]
        if run(read, a.dry_run, stdout=subprocess.DEVNULL):
            raise Stop('cannot read the partition table of %s' % a.port)
        have = build_table if a.dry_run else md5_file(board_tbl)
        if a.dry_run:
            print('DRY RUN: the board table is taken as the table of the build (%s)' % have)
        t = tables.get(have)
        if not t or not t['flash']:
            raise Stop('%s has an unknown partition table (%s): tfn flash --first-time writes the table' % (a.port, have))
        if have != build_table and t.get('upgrades_to') != build_table:
            raise Stop('%s has the table %s; the build has %s: use --first-time' % (a.port, t['csv'], build_table))
        if have != build_table:
            print('NOTE: %s has the old table: the firmware changes the layout by itself at its start (1-2 minutes)' % a.port)
        if have_files and facts['size'] > slot_size(t['csv']):
            raise Stop('the image (%d bytes) does not fit the app slot of the table of the board' % facts['size'])
        steps = [base + ['--before', 'default_reset', '--after', fl['after'], 'write_flash', fl['app'], app,
                         t['otadata'], otad]]
    for s in steps:
        rc = run(s, a.dry_run)
        if rc:
            print('FLASH FAILED (exit %d)' % rc)
            return rc
    print('FLASH %s (%s, %s)' % ('PLAN OK' if a.dry_run else 'OK', a.kind, a.port))
    return 0


# ---------------------------------------------------------------- monitor
def cmd_monitor(a):
    try:
        import serial
    except ImportError:
        raise Stop('pyserial is not in this Python: pip install pyserial')
    s = serial.serial_for_url(a.port, do_not_open=True)
    s.baudrate = a.baud
    s.timeout = 0.1
    # (DTR and RTS low before the open: a change of them resets an ESP32-S3 or sets its download mode)
    s.dtr = False
    s.rts = False
    s.open()
    print('--- %s at %d (Ctrl+C stops; a line that you type goes to the console) ---' % (a.port, a.baud), flush=True)
    stop = threading.Event()

    def keys():
        for line in sys.stdin:
            if stop.is_set():
                break
            s.write(line.rstrip('\r\n').encode('utf-8') + b'\n')

    if not a.no_input:
        threading.Thread(target=keys, daemon=True).start()
    t_end = time.time() + a.seconds if a.seconds else None
    out = sys.stdout.buffer if hasattr(sys.stdout, 'buffer') else None
    try:
        if a.send:
            s.write(a.send.encode('utf-8') + b'\n')
        while not stop.is_set() and (t_end is None or time.time() < t_end):
            data = s.read(4096)
            if data:
                if out:
                    out.write(data)
                    out.flush()
                else:
                    print(data.decode('utf-8', 'replace'), end='', flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        s.close()
    return 0


# ---------------------------------------------------------------- test
def fuzz_exe(board):
    return os.path.join(ROOT, 'tests', 'host', 'fuzz%s.exe' % board)


def test_steps(a):
    py = sys.executable
    s = [('apps', [py, 'main/apps_gen.py', '--check']),
         ('apps_all', [py, 'main/apps_gen.py', '--all'], ['git', 'diff', '--exit-code', '--', 'main/apps_all.inc']),
         ('contracts', [py, 'tests/contracts/check_all.py']),
         ('notice', [py, 'tests/licence/check_notice.py']),
         ('boards', [py, 'tests/boards/check_boards.py']),
         ('flasher', ['node', 'site/test/flasher.test.js']),
         ('same_code', [py, 'tests/refactor/test_same_code.py']),
         ('unit', [py, 'tests/unit/run.py']),
         ('comments', [py, 'tests/style/check_comments.py']),
         ('board_if', [py, 'tests/style/check_board_if.py']),
         ('api', [py, 'tests/style/check_api.py'], [py, 'sdk/tools/gen_api_doc.py', '--check']),
         ('fsize', [py, 'tests/style/check_fsize.py']),
         ('format', [py, 'tests/style/check_format.py']),
         ('fuzz', ['sh', 'tests/host/build.sh'])]
    return s


def skip_reason(name):
    if name == 'flasher' and not shutil.which('node'):
        return 'no node'
    if name in ('unit', 'fuzz') and not (os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc')):
        return 'no C compiler'
    if name == 'fuzz' and not shutil.which('sh'):
        return 'no sh'
    if name == 'format':
        exe = os.environ.get('CLANG_FORMAT') or shutil.which('clang-format')
        if not exe or 'version 22.1.8' not in tool_line([exe, '--version']):
            return 'no clang-format 22.1.8 (pip install clang-format==22.1.8, or set CLANG_FORMAT)'
    return ''


def cmd_test(a):
    steps = test_steps(a)
    names = [s[0] for s in steps]
    if a.list:
        print(' '.join(names))
        return 0
    only = [x for x in a.only.split(',') if x] if a.only else names
    for x in only:
        if x not in names:
            raise Stop('no test step "%s" (the steps: %s)' % (x, ' '.join(names)))
    if a.quick and not a.only:
        only = [x for x in only if x != 'fuzz']
    res = []
    for step in steps:
        name = step[0]
        if name not in only:
            continue
        why = skip_reason(name)
        if why:
            res.append((name, 'SKIP', why))
            print('=== %s: SKIP (%s)' % (name, why), flush=True)
            continue
        print('=== %s' % name, flush=True)
        t0 = time.time()
        env = dict(os.environ)
        rc = 0
        for cmd in step[1:]:
            rc = subprocess.run(cmd, cwd=ROOT, env=env).returncode
            if rc:
                break
        if name == 'fuzz' and rc == 0:
            env['ASAN_OPTIONS'] = 'detect_leaks=0'
            for board in ('349', 's3', 'wt'):
                exe = fuzz_exe(board)
                if not os.path.isfile(exe):
                    exe = exe[:-4]
                log = os.path.join(ROOT, 'tests', 'host', 'fuzz_%s.log' % board)
                with open(log, 'wb') as f:
                    rc = subprocess.run([exe, str(a.fuzz_frames), '1'], cwd=os.path.join(ROOT, 'tests', 'host'), env=env,
                                        stdout=f, stderr=subprocess.STDOUT).returncode
                with open(log, 'rb') as f:
                    tail = f.read().decode('utf-8', 'replace').strip().splitlines()[-3:]
                print('fuzz%s: %s' % (board, ' | '.join(tail)))
                if rc:
                    break
        res.append((name, 'OK' if rc == 0 else 'FAIL', '%.0f s' % (time.time() - t0)))
    print('\nTHE RESULT')
    for name, r, note in res:
        print('  %-10s %-4s %s' % (name, r, note))
    bad = [r for r in res if r[1] == 'FAIL']
    print('TFN TEST %s (%d ok, %d fail, %d skip)' % ('OK' if not bad else 'FAILED', sum(1 for r in res if r[1] == 'OK'),
                                                     len(bad), sum(1 for r in res if r[1] == 'SKIP')))
    return 1 if bad else 0


# ---------------------------------------------------------------- simulator
def cmd_sim(a, rest):
    script = 'smoke.py' if a.headless else 'run.py'
    cmd = [sys.executable, os.path.join(ROOT, 'simulator', script)] + (['--headless'] if a.headless else []) + rest
    return run(cmd, a.dry_run, cwd=ROOT)


# ---------------------------------------------------------------- apps
APP_ID = re.compile(r'[a-z][a-z0-9_]{1,23}$')


def cmd_app_new(a):
    if not APP_ID.match(a.id):
        raise Stop('the id "%s": 2..24 small letters, digits or _, a letter first' % a.id)
    name = (a.name or a.id.replace('_', ' ')).upper()
    dest = os.path.abspath(a.dir or a.id)
    src = os.path.join(ROOT, 'sdk', 'templates', 'hello', 'app_hello.c')
    target = os.path.join(dest, 'app_%s.c' % a.id)
    if os.path.exists(target):
        raise Stop('%s is there already' % target)
    with open(src, 'rb') as f:
        t = f.read().decode('utf-8')
    rel = os.path.relpath(target, ROOT).replace(os.sep, '/') if same_drive(target, ROOT) else target
    t = t.replace('TFN OS SDK template: HELLO, the smallest complete app. Copy this folder, change the name, build:',
                  'The app %s (made from the TFN OS SDK template HELLO). Build it:' % name)
    t = t.replace('sdk/templates/hello/app_hello.c', rel)
    t = t.replace('app_hello', 'app_' + a.id).replace('"HELLO"', '"%s"' % name).replace('--name HELLO',
                                                                                         '--name ' + shlex.quote(name))
    os.makedirs(dest, exist_ok=True)
    with open(target, 'wb') as f:
        f.write(t.encode('utf-8'))
    store = os.path.join(dest, 'store.json')
    if not os.path.exists(store):
        with open(store, 'wb') as f:
            f.write(json.dumps({a.id: {'en': '', 'he': ''}}, indent=1).encode('utf-8') + b'\n')
    print('APP NEW OK: %s, %s' % (target, store))
    print('Next: write the store text in store.json (sdk/docs/store.md), then: python tfn.py app build %s' % dest)
    return 0


def same_drive(p, q):
    return os.path.splitdrive(os.path.abspath(p))[0].lower() == os.path.splitdrive(os.path.abspath(q))[0].lower()


def app_source(path):
    """The source, the sym and the name of an app folder (one app_<id>.c with .name = "...")."""
    srcs = [os.path.join(path, f) for f in sorted(os.listdir(path)) if re.match(r'app_\w+\.c$', f)]
    if len(srcs) != 1:
        raise Stop('%s must have one app_<id>.c (it has %d)' % (path, len(srcs)))
    with open(srcs[0], 'rb') as f:
        t = f.read().decode('utf-8', 'replace')
    m = re.search(r'const app_t (app_\w+)\s*=\s*\{.*?\.name\s*=\s*"([^"]+)"', t, re.S)
    if not m:
        raise Stop('%s has no "const app_t app_<id> = { .name = ... }"' % srcs[0])
    return srcs[0], m.group(1), m.group(2)


def cmd_app_build(a, rest):
    mk = [sys.executable, os.path.join(ROOT, 'sdk', 'tools', 'mk_app.py')]
    if os.path.isdir(a.app):
        src, sym, name = app_source(a.app)
        cmd = mk + [src, '--sym', sym, '--name', name] + rest
    else:
        cmd = mk + [a.app] + rest
    return run(cmd, a.dry_run, cwd=ROOT)


# ---------------------------------------------------------------- the command line
def parser():
    p = argparse.ArgumentParser(prog='tfn', description='The developer tool of TFN OS (docs/tfn_tool.md).')
    sub = p.add_subparsers(dest='cmd')
    sub.add_parser('doctor', help='check Python, ESP-IDF, git, the host compiler and the serial port rights')
    sub.add_parser('boards', help='the board kinds of boards.json')
    s = sub.add_parser('ports', help='the serial ports with their USB IDs (no port is opened)')
    s.add_argument('--mac', action='store_true', help='read the MAC of each board with esptool (this resets the board)')
    s.add_argument('--all', action='store_true', help='also the ports with a USB ID that is not in boards.json')
    s = sub.add_parser('build', help='build the firmware of a kind into build_dev/<kind>')
    s.add_argument('kind')
    s.add_argument('--version', default='', metavar='X.Y.Z',
                   help='a release build: the version text of a release (<major>.<minor>.<patch>); it needs a clean '
                        'git clone and main/tfn_release_keys.h. The default is DEV-<UTC time>-<8 random hex digits>')
    s.add_argument('--ota-mode', default='', choices=OTA_MODES,
                   help='with --version: the OTA mode of the release (default enforce; report for a dry run with '
                        'TEST keys: tfn_build.json gets the channel TEST)')
    s.add_argument('--store-pub', default='', metavar='HEX|FILE',
                   help='with --version: the public store key of the release (130 hex digits that start with 04, or a '
                        'file that holds them); main/tfn_release_keys.h must hold this key')
    s.add_argument('--ota-pub', default='', metavar='HEX|FILE',
                   help='with --version: the public OTA key of the release (as --store-pub)')
    s.add_argument('--ota-pub2', default='', metavar='HEX|FILE',
                   help='with --version: the second public OTA key of the release, when the release has one')
    s.add_argument('--build-dir', default='', help='another build folder')
    s.add_argument('--select', default='', help='an app selection file (-D ARCADE_SELECT)')
    s.add_argument('--any-idf', action='store_true', help='go on with an ESP-IDF that is not %d.%d' % IDF_WANT)
    s.add_argument('--dry-run', action='store_true', help='print the commands only')
    s = sub.add_parser('flash', help='flash the build of a kind by USB')
    s.add_argument('kind')
    s.add_argument('--port', required=True)
    s.add_argument('--first-time', action='store_true',
                   help='a new board: the bootloader, the table, the app and the otadata (erases the settings and files)')
    s.add_argument('--build-dir', default='')
    s.add_argument('--force', action='store_true', help='write a bootloader or a table that has no known MD5')
    s.add_argument('--yes', action='store_true', help='no question before a first-time flash')
    s.add_argument('--dry-run', action='store_true', help='print the commands only (no port is opened)')
    s = sub.add_parser('monitor', help='the console of a board')
    s.add_argument('--port', required=True, help='the port (also a pyserial URL, for example loop://)')
    s.add_argument('--baud', type=int, default=115200)
    s.add_argument('--seconds', type=float, default=0, help='stop after this time (0 = at Ctrl+C)')
    s.add_argument('--send', default='', help='send this line at the start')
    s.add_argument('--no-input', action='store_true', help='do not send the lines that you type')
    s = sub.add_parser('test', help='the host checks of CI')
    s.add_argument('--quick', action='store_true', help='no fuzz build')
    s.add_argument('--only', default='', help='only these steps (a,b,c; see --list)')
    s.add_argument('--list', action='store_true', help='the names of the steps')
    s.add_argument('--fuzz-frames', type=int, default=300)
    s = sub.add_parser('sim', help='the desktop simulator (other arguments go to it)')
    s.add_argument('--headless', action='store_true', help='the smoke test of all apps with no window and no sound')
    s.add_argument('--dry-run', action='store_true')
    s = sub.add_parser('app', help='the app work')
    asub = s.add_subparsers(dest='app_cmd')
    n = asub.add_parser('new', help='a new app folder from sdk/templates/hello')
    n.add_argument('id', help='the app id: small letters, digits and _ (the symbol is app_<id>)')
    n.add_argument('--dir', default='', help='the folder (default: ./<id>)')
    n.add_argument('--name', default='', help='the name in the menu (default: the id in capitals)')
    n = asub.add_parser('build', help='build an app file (sdk/tools/mk_app.py; other arguments go to it)')
    n.add_argument('app', help='an app folder (one app_<id>.c), or an app name of main/apps.json')
    n.add_argument('--dry-run', action='store_true')
    return p


def main(argv=None):
    p = parser()
    a, rest = p.parse_known_args(argv)
    passthrough = a.cmd == 'sim' or (a.cmd == 'app' and getattr(a, 'app_cmd', '') == 'build')
    if rest and not passthrough:
        p.error('unknown arguments: %s' % ' '.join(rest))
    try:
        if a.cmd == 'doctor':
            return cmd_doctor(a)
        if a.cmd == 'boards':
            return cmd_boards(a)
        if a.cmd == 'ports':
            return cmd_ports(a)
        if a.cmd == 'build':
            return cmd_build(a)
        if a.cmd == 'flash':
            return cmd_flash(a)
        if a.cmd == 'monitor':
            return cmd_monitor(a)
        if a.cmd == 'test':
            return cmd_test(a)
        if a.cmd == 'sim':
            return cmd_sim(a, rest)
        if a.cmd == 'app' and a.app_cmd == 'new':
            return cmd_app_new(a)
        if a.cmd == 'app' and a.app_cmd == 'build':
            return cmd_app_build(a, rest)
        p.print_help()
        return 2
    except Stop as e:
        print('STOP: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main())
