#!/usr/bin/env python3
"""The test of two developer tools of the tree (no board, no ESP-IDF, no gitleaks):

1. tfn.py build --version: build_version accepts a release version (<major>.<minor>.<patch>, at most 4, 5 and 5
   digits) and refuses each other text; each version that it accepts is also a tag version of
   .github/workflows/release.yml; with no version it gives a DEV identifier. A release build stops with no
   main/tfn_release_keys.h, with main/tfn_local.h, with a file that git ignores in the source folders, with a change
   against the commit (a changed, staged or removed tracked file, an untracked file that git does not ignore), with
   --select or TFN_DEVICE_PUBLIC_HEADER, and in a tree that is not the top of a git work tree; cmd_build stops before
   it changes a file. With an idf.py of the test (no ESP-IDF): reconfigure, the OTA mode and the release values
   (release_cfg.py force), the build, the check of the build (a lost value stops the build with no tfn_build.json),
   the channel RELEASE (TEST for the OTA mode report), the commit of HEAD and the key IDs; a DEV build gets the
   channel DEV. The keys: main/tfn_release_keys.h must be the header of --store-pub and --ota-pub (TEST keys, a
   missing or an extra key, other lines: a stop); an image with no key ends .failed. The build folder: a folder with
   an old sdkconfig or CMake cache stops the build (no old value goes into a release); a release folder starts with
   no sdkconfig. After the build: a change of the tree or of HEAD gives an image .failed. The OTA mode report stops
   at a version with a release tag.
2. tests/ci_local.py, the files of the step gitleaks: tracked_tree copies each file that git tracks (a changed file
   with its content of the working tree, a file of the index that is not in the working tree with its content of the
   index, a new file after "git add") and no file that git ignores or does not track. gitleaks_tree ends the step with
   FAIL for a copy with no file or with no .gitleaks.toml. gitleaks_exe gives a relative GITLEAKS as an absolute path.
Prints "DEV TOOLS PY OK" at the end. Needs git. Usage: python tests/unit/test_dev_tools.py
"""
import argparse
import contextlib
import importlib.util
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import ci_local  # noqa: E402
import tfn  # noqa: E402

FAILS = []
COUNT = [0]
# the texts of the version checks: (the text, a release version)
VERSIONS = [('1.2.3', True), ('0.0.0', True), ('1.0.106', True), ('10.20.30', True), ('9999.99999.99999', True),
            ('1.2', False), ('1', False), ('1.0.0.1000', False), ('v1.2.3', False), ('1.2.3-rc1', False),
            ('1.2.3 ', False), (' 1.2.3', False), ('1.2.3\n', False), ('1..3', False), ('a.b.c', False),
            ('12345.1.1', False), ('1.123456.1', False), ('1.1.123456', False), ('-1.2.3', False), ('1.2.+3', False),
            ('١.٢.٣', False), ('DEV-261105120000-0123abcd', False)]


def expect(ok, what):
    COUNT[0] += 1
    if not ok:
        FAILS.append(what)
        print('FAIL %s' % what)


def put(root, rel, data):
    p = os.path.join(root, *rel.split('/'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data)


def git(repo, *args):
    r = subprocess.run(['git', '-C', repo, '-c', 'user.name=test', '-c', 'user.email=test@invalid', '-c',
                        'core.autocrlf=false', '-c', 'init.defaultBranch=main'] + list(args), capture_output=True)
    if r.returncode:
        raise RuntimeError('git %s: %s' % (' '.join(args), r.stderr.decode('utf-8', 'replace').strip()))
    return r.stdout.decode('utf-8', 'replace')


def release_tag_rule():
    """The version rule of the tag of .github/workflows/release.yml (the pattern of its grep -Eq)."""
    with open(os.path.join(ROOT, '.github', 'workflows', 'release.yml'), encoding='utf-8') as f:
        m = re.search(r"""echo "\$v" \| grep -Eq '([^']+)'""", f.read())
    return re.compile(m.group(1)) if m else None


def accepts(text):
    """True when tfn.py takes text as a release version."""
    try:
        return tfn.build_version(text) == text
    except tfn.Stop:
        return False


def test_versions(tmp):
    rule = release_tag_rule()
    expect(rule is not None, 'the version rule of the tag in .github/workflows/release.yml')
    for text, ok in VERSIONS:
        expect(accepts(text) == ok, 'build_version %r: %s' % (text, 'accepted' if ok else 'refused'))
        if ok and rule:
            expect(rule.fullmatch(text) is not None, 'the tag rule of release.yml accepts %r too' % text)
    dev = tfn.build_version('')
    expect(re.fullmatch(r'DEV-[0-9]{12}-[0-9a-f]{8}', dev) is not None, 'no version: a DEV identifier (%s)' % dev)


# the .gitignore of the test trees: the private files of a tree (as the .gitignore of the repo), the keys of a release,
# the build folders and the caches
TREE_IGNORE = (b'main/tfn_local.h\nmain/chat_local.h\nmain/fonts_local.c\napps/demo/demo_local.h\n'
               b'libs/demo/demo_data.c\nmain/tfn_release_keys.h\nbuild_dev/\nbuild_rel/\n__pycache__/\n*.pyc\n.DS_Store\n')


def git_tree(tmp, name, ignore=TREE_IGNORE):
    """A git repo with boards.json and main/app.c (committed) and a .gitignore: a clean clone for a release build."""
    root = os.path.join(tmp, name)
    os.makedirs(root)
    git(root, 'init', '-q')
    shutil.copyfile(os.path.join(ROOT, 'boards.json'), os.path.join(root, 'boards.json'))
    put(root, '.gitignore', ignore)
    put(root, 'main/app.c', b'int app;\n')
    git(root, 'add', 'boards.json', '.gitignore', 'main/app.c')
    git(root, 'commit', '-q', '-m', 'start')
    return root


# three points of the curve P-256 (G, -G and 2G): the public keys of the tests
P256_G = ('046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce3357'
          '6b315ececbb6406837bf51f5')
P256_NEG_G = ('046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296b01cbd1c01e58065711814b583f061e9d431'
              'cca994cea1313449bf97c840ae0a')
P256_2G = ('047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc4766997807775510db8ed040293d9ac69f7430dbba7dade6'
           '3ce982299e04b79d227873d1')
# the keys of the release of the tests (store, OTA), and the TEST keys of a dry run (another OTA key)
REL_KEYS = (P256_G, P256_2G)
TEST_KEYS = (P256_G, P256_NEG_G)


def keys_header(store, ota, ota2=''):
    """The text of main/tfn_release_keys.h for these keys (release_cfg.py header)."""
    return tfn.release_tool().header_text(store, ota, ota2).encode()


def build_args(version='1.2.3', mode='', dry=True, keys=REL_KEYS, build_dir=''):
    return argparse.Namespace(kind='lcd349', version=version, ota_mode=mode, dry_run=dry, build_dir=build_dir,
                              select='', any_idf=False, store_pub=keys[0], ota_pub=keys[1],
                              ota_pub2=keys[2] if len(keys) > 2 else '')


def stop_of(fn, *args):
    """The message of the Stop of fn(*args), or ''."""
    try:
        fn(*args)
        return ''
    except tfn.Stop as e:
        return str(e)


def test_release_guard(tmp):
    """release_mode: a release build needs main/tfn_release_keys.h, a git clone with no file that git ignores in the
    source folders (except the keys, the caches and the files of the file managers) and no main/tfn_local.h;
    --ota-mode only with --version. cmd_build stops before it changes a file."""
    old = tfn.ROOT
    try:
        tfn.ROOT = root = git_tree(tmp, 'guard')
        expect(stop_of(tfn.release_mode, build_args('')) == '' and tfn.release_mode(build_args('')) == '',
               'release_mode: no --version: a DEV build')
        stop = stop_of(tfn.release_mode, build_args('', 'report'))
        expect('--ota-mode is for a release build' in stop, '--ota-mode with no --version: a stop (%s)' % stop)
        stop = stop_of(tfn.release_mode, build_args())
        expect('no main/tfn_release_keys.h' in stop, '--version with no main/tfn_release_keys.h: a stop (%s)' % stop)
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        for f in ('main/__pycache__/apps_gen.cpython-312.pyc', 'apps/x/tools/y.pyc', 'main/.DS_Store'):
            put(root, f, b'0')
        stop = stop_of(tfn.release_mode, build_args())
        expect(stop == '' and tfn.release_mode(build_args()) == 'enforce',
               'release_mode: the keys, the caches, a clean tree: enforce (%s)' % stop)
        expect(tfn.release_mode(build_args(mode='report')) == 'report', 'release_mode: --ota-mode report')
        expect(tfn.build_channel('1.2.3', 'enforce') == 'RELEASE' and tfn.build_channel('1.2.3', 'report') == 'TEST'
               and tfn.build_channel('') == 'DEV', 'build_channel: RELEASE, TEST (the OTA mode report), DEV')
        for opt, env, word in (('apps.txt', '', '--select'), ('', 'guard/device_public.h', 'TFN_DEVICE_PUBLIC_HEADER')):
            a = build_args()
            a.select = opt
            old_env = os.environ.get('TFN_DEVICE_PUBLIC_HEADER')
            os.environ['TFN_DEVICE_PUBLIC_HEADER'] = env
            try:
                stop = stop_of(tfn.release_mode, a)
            finally:
                if old_env is None:
                    os.environ.pop('TFN_DEVICE_PUBLIC_HEADER', None)
                else:
                    os.environ['TFN_DEVICE_PUBLIC_HEADER'] = old_env
            expect(word in stop, 'release_mode: %s with --version: a stop (%s)' % (word, stop))
        for f in ('apps/demo/demo_local.h', 'libs/demo/demo_data.c', 'main/fonts_local.c'):
            put(root, f, b'#define TFN_TEST 1\n')
            stop = stop_of(tfn.release_mode, build_args())
            expect(f in stop and 'git ignores' in stop, 'release_guard: the ignored file %s: a stop (%s)' % (f, stop))
            os.remove(os.path.join(root, *f.split('/')))
        put(root, 'main/tfn_local.h', b'#define TFN_TEST 1\n')
        stop = stop_of(tfn.release_mode, build_args())
        expect('main/tfn_local.h is in the tree' in stop, 'release_guard: main/tfn_local.h: a stop (%s)' % stop)
        files = sorted(os.listdir(root))
        for v in ('1.2.3', '1.2'):
            stop = stop_of(tfn.cmd_build, build_args(v, dry=False))
            want = 'main/tfn_local.h' if v == '1.2.3' else 'is not <major>.<minor>.<patch>'
            expect(want in stop, 'cmd_build --version %s: a stop (%s)' % (v, stop))
        expect(sorted(os.listdir(root)) == files, 'cmd_build stopped before it changed a file: %s'
               % sorted(os.listdir(root)))
        tfn.ROOT = plain = os.path.join(tmp, 'plain')
        put(plain, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        stop = stop_of(tfn.release_mode, build_args())
        expect('not the top of a git work tree' in stop, 'release_guard: a tree with no git: a stop (%s)' % stop)
        tfn.ROOT = os.path.join(root, 'main')
        stop = stop_of(tfn.release_guard)
        expect('not the top of a git work tree' in stop,
               'release_guard: a folder inside a git tree: a stop (%s)' % stop)
    finally:
        tfn.ROOT = old


def test_tree_changes(tmp):
    """release_guard stops at each change against the commit (git status): a changed, a staged, a removed tracked
    file, an untracked file that git does not ignore (also outside the source folders). The keys of the release and
    the files of SOURCE_OK can be untracked (here git does not ignore them)."""
    old = tfn.ROOT
    try:
        tfn.ROOT = root = git_tree(tmp, 'changes', b'build_dev/\n')
        put(root, 'main/tfn_release_keys.h', b'#define TFN_TEST 1\n')
        for f in ('main/Thumbs.db', 'apps/x/desktop.ini', 'main/__pycache__/apps_gen.cpython-312.pyc'):
            put(root, f, b'0')
        stop = stop_of(tfn.release_guard)
        expect(stop == '' and tfn.tree_changes() == [],
               'tree_changes: only the keys and the files of SOURCE_OK are untracked: no stop (%s)' % stop)
        head = git(root, 'rev-parse', 'HEAD').strip()
        expect(tfn.release_guard() == head and tfn.release_commit() == head, 'release_guard: the commit of HEAD')
        app = os.path.join(root, 'main', 'app.c')
        cases = (('M main/app.c', lambda: put(root, 'main/app.c', b'int app2;\n'),
                  lambda: git(root, 'checkout', '-q', '--', 'main/app.c')),
                 ('?? notes.txt', lambda: put(root, 'notes.txt', b'a note\n'),
                  lambda: os.remove(os.path.join(root, 'notes.txt'))),
                 ('?? main/new.c', lambda: put(root, 'main/new.c', b'int x;\n'),
                  lambda: os.remove(os.path.join(root, 'main', 'new.c'))),
                 ('A main/staged.c',
                  lambda: (put(root, 'main/staged.c', b'int y;\n'), git(root, 'add', 'main/staged.c')),
                  lambda: (git(root, 'rm', '-q', '--cached', 'main/staged.c'),
                           os.remove(os.path.join(root, 'main', 'staged.c')))),
                 ('D main/app.c', lambda: os.remove(app), lambda: git(root, 'checkout', '-q', '--', 'main/app.c')))
        for want, make, undo in cases:
            make()
            stop = stop_of(tfn.release_guard)
            expect(want in tfn.tree_changes() and 'change(s) against the commit' in stop and want in stop,
                   'release_guard: the change "%s": a stop (%s)' % (want, stop))
            undo()
            expect(tfn.tree_changes() == [], 'tree_changes: no change after the undo of "%s"' % want)
    finally:
        tfn.ROOT = old


# the options of the OTA mode and their Kconfig default (main/Kconfig.projbuild)
OTA_DEFAULT = {'CONFIG_TFN_OTA_SIG_REPORT': 'y', 'CONFIG_TFN_OTA_SIG_ENFORCE': 'n', 'CONFIG_TFN_OTA_SIG_OFF': 'n',
               'CONFIG_TFN_POOL_STRICT': 'n'}


class FakeIdf:
    """idf.py of a test: reconfigure writes a sdkconfig as ESP-IDF 6 does (the mark "# default:" above each Kconfig
    default); build computes each marked option again (its default), writes config/sdkconfig.h and an image with the
    kind, the version and the keys of main/tfn_release_keys.h. lose = an option that the build sets back to its
    default; drop = a define of the keys that the image does not get; during = a function that the build calls (a
    change of the tree during the build)."""

    def __init__(self, lose='', drop='', during=None):
        self.calls = []
        self.lose = lose
        self.drop = drop
        self.during = during

    def __call__(self, cmd, dry, **kw):
        self.calls.append(cmd[-1])
        if dry:
            return 0
        bdir = cmd[cmd.index('-B') + 1]
        cfg = next(c[len('SDKCONFIG='):] for c in cmd if c.startswith('SDKCONFIG='))
        if not os.path.isfile(cfg):
            lines = []
            for k, v in OTA_DEFAULT.items():
                lines += ['# default:', '%s=%s' % (k, v) if v != 'n' else '# %s is not set' % k]
            put(bdir, 'sdkconfig', ('\n'.join(lines + ['CONFIG_LWIP_MAX_SOCKETS=16', ''])).encode())
        if cmd[-1] == 'build':
            if self.during:
                self.during()
            self.build(bdir, cfg, next(c[len('PROJECT_VER='):] for c in cmd if c.startswith('PROJECT_VER=')),
                       cmd[cmd.index('-C') + 1])
        return 0

    def keys(self, src):
        """The bytes of each define of main/tfn_release_keys.h of the tree src (except drop)."""
        path = os.path.join(src, 'main', 'tfn_release_keys.h')
        text = open(path, encoding='utf-8').read() if os.path.isfile(path) else ''
        return b''.join(bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', v))
                        for k, v in re.findall(r'#define (\w+) (.*)', text) if k != self.drop)

    def build(self, bdir, cfg, version, src):
        values, mark = {}, False
        for ln in open(cfg, encoding='utf-8').read().split('\n'):
            if ln == '# default:':
                mark = True
                continue
            off = ln.startswith('# CONFIG_') and ln.endswith(' is not set')
            k, v = (ln[2:-11], 'n') if off else ln.partition('=')[::2]
            if k.startswith('CONFIG_'):
                values[k] = OTA_DEFAULT.get(k, v) if mark or k == self.lose else v
            mark = False
        defs = ['#define %s %s' % (k, '1' if v == 'y' else v) for k, v in values.items() if v != 'n']
        put(bdir, 'config/sdkconfig.h', ('\n'.join(defs) + '\n').encode())
        img = bytearray(0x200)
        img[0] = 0xE9
        img[0x20:0x24] = (0xABCD5432).to_bytes(4, 'little')
        img[0x30:0x30 + len(version)] = version.encode()
        img[0x100:0x100 + 22] = b'@ARCADE-BOARD:lcd349@\0'
        put(bdir, 'tfn_os.bin', bytes(img) + self.keys(src))


def fake_build(a, idf):
    """cmd_build with the idf.py of the test (no ESP-IDF): (the message of a Stop or '', the exit code)."""
    real_run, real_tools = tfn.run, tfn.idf_tools
    tfn.run, tfn.idf_tools = idf, (lambda a, b: 'idf.py')
    try:
        return '', tfn.cmd_build(a)
    except tfn.Stop as e:
        return str(e), None
    finally:
        tfn.run, tfn.idf_tools = real_run, real_tools


def test_release_build(tmp):
    """cmd_build --version: "idf.py reconfigure", then the release values (release_cfg.py force) with the OTA mode, the
    build and the check of its config/sdkconfig.h. A build that loses a value stops with no tfn_build.json; a DEV build
    has no reconfigure and the channel DEV."""
    old = tfn.ROOT
    try:
        tfn.ROOT = root = git_tree(tmp, 'build')
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        facts = os.path.join(root, 'build_dev', 'lcd349', 'tfn_build.json')
        head = git(root, 'rev-parse', 'HEAD').strip()
        ids = {'TFN_STORE_PUBKEY_BYTES': tfn.key_id(REL_KEYS[0]), 'TFN_OTA_PUBKEY1_BYTES': tfn.key_id(REL_KEYS[1])}
        for mode, want, chan in (('', 'CONFIG_TFN_OTA_SIG_ENFORCE', 'RELEASE'),
                                 ('report', 'CONFIG_TFN_OTA_SIG_REPORT', 'TEST')):
            idf = FakeIdf()
            stop, code = fake_build(build_args(mode=mode, dry=False), idf)
            got = json.load(open(facts)) if os.path.isfile(facts) else {}
            hp = os.path.join(root, 'build_dev', 'lcd349', 'config', 'sdkconfig.h')
            header = open(hp).read() if os.path.isfile(hp) else ''
            expect(stop == '' and code == 0 and idf.calls == ['reconfigure', 'build'], 'release build %s: reconfigure, '
                   'build (%s %s %s)' % (mode or 'enforce', stop, code, idf.calls))
            expect(got.get('channel') == chan and got.get('ota_mode') == (mode or 'enforce') and
                   got.get('version') == '1.2.3' and got.get('commit') == head and '#define %s 1' % want in header
                   and got.get('keys') == ids,
                   'release build %s: tfn_build.json %s, the commit, the key IDs, the OTA mode in the build (%s)'
                   % (mode or 'enforce', chan, got))
        for lose in ('CONFIG_TFN_OTA_SIG_ENFORCE', 'CONFIG_TFN_POOL_STRICT'):
            idf = FakeIdf(lose)
            if lose == 'CONFIG_TFN_POOL_STRICT':
                OTA_DEFAULT[lose] = 'y'
            try:
                stop, code = fake_build(build_args(dry=False), idf)
            finally:
                OTA_DEFAULT['CONFIG_TFN_POOL_STRICT'] = 'n'
            image = os.path.join(root, 'build_dev', 'lcd349', 'tfn_os.bin')
            expect('it is not an image of a release' in stop and lose in stop and not os.path.isfile(facts) and
                   not os.path.isfile(image) and os.path.isfile(image + '.failed'),
                   'release build that loses %s: a stop, no tfn_build.json, the image is .failed (%s)' % (lose, stop))
        idf = FakeIdf()
        stop, code = fake_build(build_args('', dry=False), idf)
        got = json.load(open(facts)) if os.path.isfile(facts) else {}
        expect(stop == '' and code == 0 and idf.calls == ['build'] and got.get('channel') == 'DEV' and
               'ota_mode' not in got and 'commit' not in got,
               'DEV build: no reconfigure, the channel DEV (%s %s)' % (idf.calls, got))
        idf = FakeIdf()
        stop, code = fake_build(build_args(dry=True), idf)
        expect(stop == '' and code == 0 and idf.calls == ['reconfigure', 'build'], 'release build --dry-run: the '
               'commands only (%s %s)' % (stop, idf.calls))
    finally:
        tfn.ROOT = old


def no_release(root, bdir, stop, word, what):
    """Check a release build that must stop: the message has the word, and no tfn_build.json says RELEASE."""
    facts = os.path.join(root, bdir, 'tfn_build.json')
    got = json.load(open(facts)) if os.path.isfile(facts) else {}
    expect(word in stop and got.get('channel') != 'RELEASE', '%s: a stop, no RELEASE (%s; %s)' % (what, stop, got))


def test_release_keys(tmp):
    """release_keys: a release build needs --store-pub and --ota-pub (points of P-256, as hex or in a file), and
    main/tfn_release_keys.h must be the header of these keys. A header with TEST keys, with only an OTA key, with one
    more key or with other lines stops the build in each OTA mode; a build that gives the image no key ends .failed."""
    old = tfn.ROOT
    try:
        tfn.ROOT = root = git_tree(tmp, 'keys')
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        expect(tfn.release_keys(build_args()) == [('TFN_STORE_PUBKEY_BYTES', REL_KEYS[0]),
                                                  ('TFN_OTA_PUBKEY1_BYTES', REL_KEYS[1])],
               'release_keys: the keys of the options')
        put(tmp, 'pubs/ota.pub', (REL_KEYS[1].upper() + '\n').encode())
        stop = stop_of(tfn.release_keys, build_args(keys=(REL_KEYS[0], os.path.join(tmp, 'pubs', 'ota.pub'))))
        expect(stop == '', 'release_keys: a key in a file (%s)' % stop)
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS).replace(b'\n', b'\r\n'))
        expect(stop_of(tfn.release_keys, build_args()) == '', 'release_keys: a header with CRLF line ends')
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        bad = (((REL_KEYS[0], ''), 'needs the public keys'), (('', REL_KEYS[1]), 'needs the public keys'),
               ((REL_KEYS[0], '04' + '11' * 64), 'not a point of the curve P-256'),
               ((REL_KEYS[0], REL_KEYS[1][:-2]), '130 hex digits'),
               ((REL_KEYS[0], REL_KEYS[0]), 'the same key'))
        for keys, word in bad:
            stop = stop_of(tfn.release_mode, build_args(keys=keys))
            expect(word in stop, 'release_mode: the keys %s: a stop "%s" (%s)' % ([k[:8] for k in keys], word, stop))
        headers = (('TEST keys', keys_header(*TEST_KEYS)),
                   ('only an OTA key', tfn.release_tool().header_text(REL_KEYS[0], REL_KEYS[1]).encode().replace(
                       b'#define TFN_STORE_PUBKEY_BYTES', b'#define TFN_OTHER')),
                   ('one more key', keys_header(REL_KEYS[0], REL_KEYS[1], P256_NEG_G)),
                   ('a hand-written file', b'#define TFN_TEST 1\n'),
                   ('one more line', keys_header(*REL_KEYS) + b'#define TFN_STORE_PUBKEY2_BYTES 0x04\n'))
        for what, text in headers:
            put(root, 'main/tfn_release_keys.h', text)
            for mode in ('', 'report'):
                stop, code = fake_build(build_args(mode=mode, dry=False), FakeIdf())
                no_release(root, 'build_dev/lcd349', stop, 'is not the header of the keys', 'a header with %s, '
                           'OTA mode %s' % (what, mode or 'enforce'))
        put(root, 'main/tfn_release_keys.h', keys_header(*TEST_KEYS))
        stop, code = fake_build(build_args(dry=False, keys=TEST_KEYS), FakeIdf())
        expect(stop == '' and code == 0, 'release build: the header and the options have the same keys (%s)' % stop)
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        image = os.path.join(root, 'build_dev', 'lcd349', 'tfn_os.bin')
        stop, code = fake_build(build_args(dry=False), FakeIdf(drop='TFN_STORE_PUBKEY_BYTES'))
        no_release(root, 'build_dev/lcd349', stop, 'does not hold the key(s) TFN_STORE_PUBKEY_BYTES',
                   'an image with no store key')
        expect(not os.path.isfile(image) and os.path.isfile(image + '.failed'), 'an image with no store key: .failed')
    finally:
        tfn.ROOT = old


def test_release_folder(tmp):
    """A release build starts with no old sdkconfig: in a folder with the sdkconfig or the CMake cache of another
    build (the DEV folder) it stops and changes nothing; a new folder gets the mark, and a later release build there
    removes the sdkconfig before "idf.py reconfigure" (a value that menuconfig put there does not go into the image)."""
    old = tfn.ROOT
    old_cfg = b'CONFIG_TFN_OTA_CRASH_TEST=y\nCONFIG_TFN_TOUCH_DEBUG=y\n'
    try:
        tfn.ROOT = root = git_tree(tmp, 'folder')
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        for name in ('sdkconfig', 'CMakeCache.txt'):
            dev = os.path.join(root, 'build_dev', 'lcd349')
            shutil.rmtree(dev, ignore_errors=True)
            put(dev, name, old_cfg if name == 'sdkconfig' else b'CMAKE_HOME_DIRECTORY:INTERNAL=%s\n' % root.encode())
            for dry in (True, False):
                idf = FakeIdf()
                stop, code = fake_build(build_args(dry=dry), idf)
                no_release(root, 'build_dev/lcd349', stop, 'starts in a new folder', 'the DEV folder with a %s '
                           '(dry run %s)' % (name, dry))
                expect(idf.calls == [] and os.listdir(dev) == [name], 'the DEV folder with a %s: no change (%s %s)'
                       % (name, idf.calls, os.listdir(dev)))
        rel = os.path.join(root, 'build_rel', 'lcd349')
        for step in ('a new folder', 'a marked folder with an old sdkconfig'):
            if step != 'a new folder':
                put(rel, 'sdkconfig', old_cfg)
            stop, code = fake_build(build_args(dry=False, build_dir=rel), FakeIdf())
            fp, hp = os.path.join(rel, 'tfn_build.json'), os.path.join(rel, 'config', 'sdkconfig.h')
            got = json.load(open(fp)) if os.path.isfile(fp) else {}
            header = open(hp).read() if os.path.isfile(hp) else ''
            expect(stop == '' and got.get('channel') == 'RELEASE' and 'CRASH_TEST' not in header and 'TOUCH_DEBUG' not
                   in header and os.path.isfile(os.path.join(rel, tfn.RELEASE_MARK)),
                   'release build in %s: RELEASE with no old value (%s %s)' % (step, stop, header))
    finally:
        tfn.ROOT = old


def test_release_after(tmp):
    """The checks after the build: a change of a tracked file, a new file or a new commit during the build gives no
    tfn_build.json and an image .failed. The OTA mode report stops at a version with a release tag."""
    old = tfn.ROOT
    try:
        tfn.ROOT = root = git_tree(tmp, 'after')
        put(root, 'main/tfn_release_keys.h', keys_header(*REL_KEYS))
        image = os.path.join(root, 'build_dev', 'lcd349', 'tfn_os.bin')
        cases = (('a changed source', lambda: put(root, 'main/app.c', b'int app3;\n'),
                  lambda: git(root, 'checkout', '-q', '--', 'main/app.c'), 'the tree changed during the build'),
                 ('a new file that git ignores', lambda: put(root, 'main/chat_local.h', b'#define X 1\n'),
                  lambda: os.remove(os.path.join(root, 'main', 'chat_local.h')), 'the tree changed during the build'),
                 ('a new commit', lambda: (put(root, 'main/app.c', b'int app4;\n'),
                                           git(root, 'commit', '-q', '-am', 'during')),
                  lambda: None, 'HEAD changed during the build'))
        for what, make, undo, word in cases:
            stop, code = fake_build(build_args(dry=False), FakeIdf(during=make))
            no_release(root, 'build_dev/lcd349', stop, word, 'release build with %s during the build' % what)
            expect(not os.path.isfile(image) and os.path.isfile(image + '.failed'), '%s: the image is .failed' % what)
            undo()
        git(root, 'tag', 'v1.2.3')
        stop = stop_of(tfn.release_mode, build_args(mode='report'))
        expect('has the release tag v1.2.3' in stop, 'the OTA mode report with a tagged version: a stop (%s)' % stop)
        expect(stop_of(tfn.release_mode, build_args(version='1.2.4', mode='report')) == '' and
               tfn.release_mode(build_args()) == 'enforce', 'a version with no tag (report), the tag (enforce): go on')
    finally:
        tfn.ROOT = old


def espcheck_module():
    """The module of the ESP compile check (it runs nothing at the import)."""
    spec = importlib.util.spec_from_file_location('tfn_espcheck', os.path.join(ROOT, 'tests', 'host', 'espcheck.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_espcheck(tmp):
    """The ESP compile check with a fake build of a copy of the source: source_home reads the CMake cache;
    to_project puts the source folders of the project in the place of those of the copy (not the build folder, not
    managed_components/, not a folder that the project does not have); from_copy finds the headers that came from the
    copy; template_entry gives None for a build with no template file, and main stops with exit 2 and no trace."""
    ec = espcheck_module()
    old = ec.PROJ, ec.BD
    try:
        proj = os.path.join(tmp, 'esp_proj').replace('\\', '/')
        bd = os.path.join(tmp, 'esp_builds').replace('\\', '/')
        home = bd + '/build_dev/work'
        os.makedirs(os.path.join(proj, 'apps', 'x'))
        put(proj, 'main/app_pong.c', b'int pong;\n')
        put(bd, 'build_dev/lcd349/CMakeCache.txt', ('CMAKE_HOME_DIRECTORY:INTERNAL=%s/\n' % home).encode())
        ec.PROJ, ec.BD = proj, bd
        expect(ec.source_home('build_dev/lcd349') == home, 'espcheck source_home: CMAKE_HOME_DIRECTORY with no /')
        args = ['gcc', '-I%s/main' % home, '-I%s/apps/x' % home, '-I%s/apps/gone' % home,
                '-I%s/managed_components/c' % home, '-I%s/build_dev/lcd349/config' % bd, '%s/main/app_pong.c' % home,
                '-DX=1']
        out, moved = ec.to_project(args, home, (bd + '/build_dev/lcd349',))
        want = ['gcc', '-I%s/main' % proj, '-I%s/apps/x' % proj, '-I%s/apps/gone' % home,
                '-I%s/managed_components/c' % home, '-I%s/build_dev/lcd349/config' % bd,
                '%s/main/app_pong.c' % proj, '-DX=1']
        expect(out == want and moved == ['%s/main' % home, '%s/apps/x' % home],
               'espcheck to_project: the folders of the project, the others stay (%s %s)' % (out, moved))
        expect(ec.to_project(args, '', ())[0] == args, 'espcheck to_project: no source folder of the build: no change')
        dep = os.path.join(tmp, 'esp.d')
        put(tmp, 'esp.d', ('x.o: %s/main/app_pong.c \\\n %s/main/old.h %s/apps/x/a\\ b.h \\\n'
                           ' %s/managed_components/c/c.h %s/main/new.h\n' % (proj, home, home, home, proj)).encode())
        expect(ec.from_copy(dep, moved, home) == ['apps/x/a b.h', 'main/old.h'],
               'espcheck from_copy: the headers of the copy (%s)' % ec.from_copy(dep, moved, home))
        expect(ec.from_copy(os.path.join(tmp, 'none.d'), moved, home) == [], 'espcheck from_copy: no dependency file')
        db = [{'file': home + '/main/other.c'}, {'file': home + '\\main\\app_settings.c'},
              {'file': home + '/apps/stack/app_stack.c'}]
        expect(ec.template_entry(db) is db[2] and ec.template_entry(db[:2]) is db[1] and
               ec.template_entry(db[:1]) is None, 'espcheck template_entry: the first template file, or None')
        put(bd, 'build_dev/lcd349/compile_commands.json', json.dumps(db[:1]).encode())
        buf = io.StringIO()
        real = sys.path[:]
        try:
            with contextlib.redirect_stdout(buf):
                ec.PROJ = ROOT
                code = ec.main(['espcheck.py', 'app_pong.c'])
        finally:
            sys.path[:] = real
        expect(code == 2 and 'has no compile command of app_pong.c' in buf.getvalue(),
               'espcheck main: a build with no template file: exit 2 (%s %s)' % (code, buf.getvalue()))
    finally:
        ec.PROJ, ec.BD = old


def test_tracked_tree(tmp):
    repo = os.path.join(tmp, 'repo')
    os.makedirs(repo)
    git(repo, 'init', '-q')
    put(repo, '.gitignore', b'work/\n')
    put(repo, '.gitleaks.toml', b'title = "a test"\n')
    put(repo, 'a.txt', b'committed\n')
    put(repo, 'gone.txt', b'in the index\n')
    put(repo, 'dir/sp ace.txt', b'a name with a space\n')
    git(repo, 'add', '.gitignore', '.gitleaks.toml', 'a.txt', 'gone.txt', 'dir/sp ace.txt')
    git(repo, 'commit', '-q', '-m', 'start')
    put(repo, 'work/ignored.txt', b'a file that git ignores\n')
    put(repo, 'untracked.txt', b'a file that git does not track\n')
    put(repo, 'a.txt', b'changed in the working tree\n')
    put(repo, 'staged.txt', b'a new file after git add\n')
    git(repo, 'add', 'staged.txt')
    os.remove(os.path.join(repo, 'gone.txt'))
    ctx = ci_local.Ctx(Path(repo))
    dest = Path(tmp) / 'copy'
    dest.mkdir()
    n = ci_local.tracked_tree(ctx, dest)
    files = sorted(p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file())
    want = ['.gitignore', '.gitleaks.toml', 'a.txt', 'dir/sp ace.txt', 'gone.txt', 'staged.txt']
    expect(files == want and n == len(want), 'tracked_tree: the tracked files only (%d: %s)' % (n, files))
    expect((dest / 'a.txt').read_bytes() == b'changed in the working tree\n',
           'tracked_tree: a changed file has its content of the working tree')
    expect((dest / 'gone.txt').read_bytes() == b'in the index\n',
           'tracked_tree: a file that is not in the working tree has its content of the index')
    expect(ci_local.gitleaks_tree(ctx, Path(tmp) / 'copy2') == len(want), 'gitleaks_tree: the count of the files')
    git(repo, 'rm', '-q', '--cached', '.gitleaks.toml')
    expect(gitleaks_fail(ctx, Path(tmp) / 'copy3', '.gitleaks.toml'),
           'gitleaks_tree: a copy with no .gitleaks.toml: FAIL')
    empty = os.path.join(tmp, 'empty')
    os.makedirs(empty)
    git(empty, 'init', '-q')
    expect(gitleaks_fail(ci_local.Ctx(Path(empty)), Path(tmp) / 'copy4', 'empty'),
           'gitleaks_tree: a copy with no file: FAIL')


def test_gitleaks_exe(tmp):
    """gitleaks_exe gives GITLEAKS as an absolute path: the dir scan runs in another folder, and a relative path would
    name another file there. A GITLEAKS that is not a program gives ''."""
    name = 'gitleaks.exe' if os.name == 'nt' else 'gitleaks'
    put(tmp, 'bin/' + name, b'#!/bin/sh\nexit 0\n')
    os.chmod(os.path.join(tmp, 'bin', name), 0o755)
    old_env, old_cwd = os.environ.get('GITLEAKS'), os.getcwd()
    try:
        os.chdir(tmp)
        os.environ['GITLEAKS'] = 'bin/' + name
        got = ci_local.gitleaks_exe()
        expect(os.path.isabs(got) and os.path.samefile(got, os.path.join(tmp, 'bin', name)),
               'gitleaks_exe: a relative GITLEAKS becomes an absolute path (%s)' % got)
        os.environ['GITLEAKS'] = 'bin/no_such_' + name
        expect(ci_local.gitleaks_exe() == '', 'gitleaks_exe: a GITLEAKS that is not there gives ""')
    finally:
        os.chdir(old_cwd)
        if old_env is None:
            os.environ.pop('GITLEAKS', None)
        else:
            os.environ['GITLEAKS'] = old_env


def gitleaks_fail(ctx, dest, word):
    """True when gitleaks_tree ends the step with FAIL, and the note has the word."""
    try:
        ci_local.gitleaks_tree(ctx, dest)
    except ci_local.Result as r:
        return r.status == 'FAIL' and word in r.note
    return False


def remove_tree(path):
    """Remove a folder of the test, also the read-only files that git writes (the objects of a repo on Windows)."""
    def writable(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    try:
        if sys.version_info >= (3, 12):
            shutil.rmtree(path, onexc=writable)
        else:
            shutil.rmtree(path, onerror=writable)
    except OSError as e:
        print('(the temporary folder %s stays: %s)' % (path, e))


def main():
    tmp = tempfile.mkdtemp(prefix='tfn_dev_tools_')
    try:
        test_versions(tmp)
        test_release_guard(tmp)
        test_tree_changes(tmp)
        test_release_build(tmp)
        test_release_keys(tmp)
        test_release_folder(tmp)
        test_release_after(tmp)
        test_espcheck(tmp)
        test_tracked_tree(tmp)
        test_gitleaks_exe(tmp)
    finally:
        remove_tree(tmp)
    print('DEV TOOLS PY %s' % ('OK (%d checks)' % COUNT[0] if not FAILS else 'FAILED (%d)' % len(FAILS)))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
