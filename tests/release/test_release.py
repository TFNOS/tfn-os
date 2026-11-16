#!/usr/bin/env python3
"""The test of the release tools of the public flow: site/tools/fw_manifest.py, site/tools/pages_build.py,
sdk/tools/store_index.py, sdk/tools/release_pack.py and sdk/tools/release_check.py.

It makes TEST keys in a temporary folder (they never go into git), a fake ESP-IDF build folder of each board kind with
a web install (small images with the real partition tables), fake app packages, and then does the steps of
.github/workflows/release.yml: the firmware manifests, the OTA signature (fw_sign.py), the store, the web site and the
release files (with channel.json and the zip of the pool manager). release_check.py must say OK for them, and a
FAULT for each damaged copy.

Usage: python tests/release/test_release.py [--node node]   (needs the package "cryptography" and the openssl program;
with Node.js the check also runs the logic of the install page; Pillow is not necessary)
"""
import argparse
import contextlib
import hashlib
import io
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'site', 'tools'))
import fw_manifest  # noqa: E402
import pages_build  # noqa: E402
import release_check  # noqa: E402
import release_pack  # noqa: E402
import store_index  # noqa: E402
from release_check import fw_sign  # noqa: E402

VERSION = '9.8.7'
FAILS = []


def expect(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        FAILS.append(what)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def keygen(path):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    key = ec.generate_private_key(ec.SECP256R1())
    write(path, key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                  serialization.NoEncryption()))
    return store_index.point_of(key).hex()


def image(kind, version, size_code, seed, app=True, body=9000):
    """A small ESP-IDF image of the ESP32-S3: an app (the app description and the board tag) or a bootloader."""
    rnd = random.Random(seed)
    desc = bytearray(256)
    if app:
        struct.pack_into('<I', desc, 0, fw_manifest.APP_DESC_MAGIC)
        desc[0x10:0x30] = version.encode().ljust(32, b'\0')
    data = bytearray(rnd.getrandbits(8) | 1 for _ in range(body))
    if app:
        tag = b'@ARCADE-BOARD:' + kind.encode() + b'@'
        data[3000:3000 + len(tag)] = tag
    head = bytearray(24)
    head[0], head[1], head[2], head[3] = 0xE9, 2, 2, (size_code << 4) | 0xF
    struct.pack_into('<I', head, 4, 0x40380000)
    struct.pack_into('<H', head, 12, 9)
    head[23] = 1
    img = bytes(head) + struct.pack('<II', 0x3C000020, len(desc)) + bytes(desc)
    img += struct.pack('<II', 0x3C001000, len(data)) + bytes(data)
    pad = ((len(img) + 1 + 15) & ~15) - len(img)
    img += b'\0' * (pad - 1) + b'\xef'
    return img + hashlib.sha256(img).digest()


def table(rows):
    out = bytearray()
    for name, typ, sub, off, size in rows:
        out += b'\xaa\x50' + struct.pack('<BBII', typ, sub, off, size) + name.encode().ljust(16, b'\0') + b'\0' * 4
    return bytes(out) + b'\xff' * (0xC00 - len(out))


def fake_build(folder, kind, seed):
    """The files of an ESP-IDF build folder of one kind that fw_manifest.py reads."""
    b = fw_manifest.board(kind)
    size = b['flash_size']
    code = {v: k for k, v in fw_manifest.FLASH_SIZES.items()}[size]
    rows = fw_manifest.parse_csv(os.path.join(ROOT, b['partition_table']))
    ota = [r for r in rows if r[1] == 1 and r[2] == 0][0]
    write(os.path.join(folder, 'bootloader', 'bootloader.bin'), image(kind, '', code, seed, app=False, body=3000))
    write(os.path.join(folder, 'partition_table', 'partition-table.bin'), table(rows))
    write(os.path.join(folder, 'tfn_os.bin'), image(kind, VERSION, code, seed + 1))
    write(os.path.join(folder, 'ota_data_initial.bin'), b'\xff' * ota[4])
    args = {'flash_files': {'0x0': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin',
                            hex(ota[3]): 'ota_data_initial.bin', '0x10000': 'tfn_os.bin'},
            'flash_settings': {'flash_mode': 'dio', 'flash_size': size, 'flash_freq': '80m'}}
    write(os.path.join(folder, 'flasher_args.json'), json.dumps(args).encode())


def fake_package(folder, pid, kind, seed):
    rnd = random.Random(seed)
    elf = bytes(rnd.getrandbits(8) for _ in range(4000))
    icon = bytes(rnd.getrandbits(8) for _ in range(500))
    lines = ['id=' + pid, 'name=' + pid.upper(), 'ver=1.2.3', 'code=26123', 'abi=1', 'rev=23', 'perms=net',
             'kind=' + kind, 'group=UTIL', 'emoji=', 'saves=' + pid + '_', 'bytes=%d' % len(elf), 'ram=1000',
             'elf_sha256=' + hashlib.sha256(elf).hexdigest(), 'icon_sha256=' + hashlib.sha256(icon).hexdigest(),
             'desc=A test app.']
    d = os.path.join(folder, pid, kind)
    write(os.path.join(d, 'manifest.txt'), ('\n'.join(lines) + '\n').encode())
    write(os.path.join(d, 'app.elf'), elf)
    write(os.path.join(d, 'icon.bin'), icon)


def run_check(site, rel, store_pub, ota_pub, node):
    argv = ['--site', site, '--release', rel, '--store-pub', store_pub, '--ota-pub', ota_pub, '--node', node]
    return release_check.main(argv)


def damaged(src, work, rel, fn):
    """A copy of the folder src with the file rel changed by fn(bytes) -> bytes."""
    dst = os.path.join(work, 'bad_%d' % len(os.listdir(work)))
    shutil.copytree(src, dst)
    p = os.path.join(dst, *rel.split('/'))
    with open(p, 'rb') as f:
        data = f.read()
    write(p, fn(data))
    return dst


def flip(data):
    b = bytearray(data)
    b[len(b) // 2] ^= 0x01
    return bytes(b)


PAGES = 'https://example.github.io/tfn-os'
REPO = 'example/tfn-os'


def check_out(argv):
    """release_check.main(argv): (exit code, the FAULT lines)."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = release_check.main(argv)
    return rc, [ln for ln in buf.getvalue().splitlines() if ln.startswith('FAULT')]


def resum(folder):
    """SHA256SUMS again after a change (so that only the check of the pool manager can find the change)."""
    lines = []
    for n in sorted(os.listdir(folder)):
        if n != 'SHA256SUMS':
            with open(os.path.join(folder, n), 'rb') as f:
                lines.append('%s  %s\n' % (hashlib.sha256(f.read()).hexdigest(), n))
    write(os.path.join(folder, 'SHA256SUMS'), ''.join(lines).encode())


def rezip(src, dst, fn):
    """A copy of the zip src with each (name, bytes) changed by fn(name, bytes) -> bytes or None (= left out)."""
    buf = io.BytesIO()
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zo:
        for n in zi.namelist():
            d = fn(n, zi.read(n))
            if d is not None:
                zo.writestr(n, d)
    write(dst, buf.getvalue())


@contextlib.contextmanager
def no_crypto():
    """The package "cryptography" cannot be imported in this block: each "cryptography" and "cryptography.*" key of
    sys.modules is None (an import then fails, also of a module that is already loaded), and store_index looks for the
    package again. At the end the old entries come back."""
    keys = [k for k in sys.modules if k == 'cryptography' or k.startswith('cryptography.')]
    old = {k: sys.modules[k] for k in keys}
    for k in set(keys) | {'cryptography'}:
        sys.modules[k] = None
    store_index._CRYPTO = None
    try:
        yield
    finally:
        for k in [k for k in sys.modules if k == 'cryptography' or k.startswith('cryptography.')]:
            del sys.modules[k]
        sys.modules.update(old)
        store_index._CRYPTO = None


def other_curve_refused(t):
    """True when store_index.parse_pub refuses a PEM file of a secp256k1 key (its public key also ends with a 65-byte
    point); also True when this openssl cannot make such a key (then the check is not possible)."""
    pem = os.path.join(t, 'k256.pem')
    try:
        write(pem, fw_sign.run_ssl(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:secp256k1']))
    except fw_sign.SignError:
        print('(no secp256k1 key of this openssl: the check of another curve is not done)')
        return True
    try:
        store_index.parse_pub(pem)
    except store_index.StoreError:
        return True
    return False


def verify_late(pt, msg, rs):
    """The type of the error of store_index.verify when the openssl program does not end in time ('' for no error)."""
    old = subprocess.run

    def late(*args, **kw):
        raise subprocess.TimeoutExpired(args[0] if args else 'openssl', 60)
    subprocess.run = late
    try:
        store_index.verify(pt, msg, rs)
        return ''
    except Exception as e:  # noqa: BLE001  (the test tells the type of each error)
        return type(e).__name__
    finally:
        subprocess.run = old


def test_no_crypto(t, fw, store, sdk_zip, site, rel, store_pem, ota_pem, store_pub, ota_pub, kinds):
    """The release tools with no package "cryptography" (no_crypto: the import fails): the keys of the PEM files and
    the checks of the signatures use the openssl program."""
    pub_pem = os.path.join(t, 'store_public.pem')
    write(pub_pem, fw_sign.run_ssl(['pkey', '-in', store_pem, '-pubout']))
    hex_file = os.path.join(t, 'store.pub')
    write(hex_file, (store_pub + '\n').encode())
    want = bytes.fromhex(store_pub)
    msg = b'TFN OS: a test text'
    rs = store_index.sign(store_index.load_key(store_pem), msg)
    with no_crypto():
        expect(not store_index.have_crypto(), 'no cryptography: store_index.have_crypto() is False')
        try:
            import cryptography.hazmat.primitives.asymmetric.ec  # noqa: F401
            blocked = False
        except ImportError:
            blocked = True
        expect(blocked, 'no cryptography: an import of the package fails')
        for what, arg in (('a private PEM file', store_pem), ('a public PEM file', pub_pem),
                          ('a file with the hex text', hex_file)):
            expect(store_index.parse_pub(arg) == want, 'no cryptography: store_index.parse_pub of %s' % what)
        expect(other_curve_refused(t), 'no cryptography: store_index.parse_pub of a secp256k1 PEM file: refused')
        expect(store_index.verify(want, msg, rs), 'no cryptography: verify of a good signature (openssl)')
        expect(not store_index.verify(want, msg + b'.', rs), 'no cryptography: verify of another text: no')
        expect(not store_index.verify(want, msg, rs[:32] + bytes(32)), 'no cryptography: verify with s = 0: no')
        expect(verify_late(want, msg, rs) == 'StoreError',
               'no cryptography: openssl does not end in time: a StoreError (%s)' % verify_late(want, msg, rs))
        rel2 = os.path.join(t, 'release_no_crypto')
        release_pack.pack(VERSION, fw, store, sdk_zip, rel2, store_pem, ota_pem, poolmgr=os.path.join(ROOT, 'poolmgr'),
                          pages=PAGES + '/', repo=REPO)
        same = [n for n in ('tfn-keys.json', 'channel.json')
                if open(os.path.join(rel, n), 'rb').read() == open(os.path.join(rel2, n), 'rb').read()]
        expect(len(same) == 2, 'no cryptography: release_pack.py with PEM keys: the same tfn-keys.json and channel.json')
        expect(run_check(site, rel2, store_pem, ota_pem, '') == 0, 'no cryptography: release_check.py with PEM keys: OK')
        bad = damaged(rel2, os.path.join(t, 'bad'), 'tfn-os-%s-%s.bin' % (VERSION, kinds[0]), flip)
        expect(run_check(site, bad, store_pub, ota_pub, '') == 1, 'no cryptography: a changed OTA file: FAILED')
    expect(store_index.have_crypto(), 'after the block: the package "cryptography" is there again')


def test_poolmgr(t, fw, store, sdk_zip, site, rel, store_pem, ota_pem, store_pub, ota_pub, other_pub, kinds):
    """channel.json and tfn-poolmgr-V.zip (release_pack.py --poolmgr) and step 5 of release_check.py."""
    pz = os.path.join(rel, 'tfn-poolmgr-%s.zip' % VERSION)
    ch = json.load(open(os.path.join(rel, 'channel.json'), encoding='utf-8'))
    expect(ch == {'format': 1, 'pages': PAGES, 'repo': REPO, 'store_pub': store_pub, 'ota_pub': [ota_pub],
                  'release': VERSION, 'kinds': sorted(kinds)},
           'release_pack.py: channel.json = the public keys of the release, the site, the repo, the kinds')
    with zipfile.ZipFile(pz) as z:
        names = sorted(z.namelist())
        same = all(z.read('poolmgr/' + n) == open(p, 'rb').read()
                   for n, p in release_check.tree_sources(ROOT).items())
    want = sorted(['poolmgr/' + n for n in release_pack.poolmgr_files(os.path.join(ROOT, 'poolmgr'))] +
                  ['poolmgr/' + n for n in release_pack.SHARED] + ['poolmgr/channel.json'])
    expect(names == want and same, 'release_pack.py: the zip = poolmgr/ + the %d shared modules + channel.json, '
           'byte-equal to the tree (%d files)' % (len(release_pack.SHARED), len(names)))
    with open(pz, 'rb') as f:
        first = f.read()
    rel2 = os.path.join(t, 'release2')
    release_pack.pack(VERSION, fw, store, sdk_zip, rel2, store_pem, ota_pem, poolmgr=os.path.join(ROOT, 'poolmgr'),
                      pages=PAGES, repo=REPO)
    with open(os.path.join(rel2, 'tfn-poolmgr-%s.zip' % VERSION), 'rb') as f:
        expect(f.read() == first, 'release_pack.py: the zip of the pool manager is the same bytes each time')
    rc, faults = check_out(['--site', site, '--release', rel, '--node', '', '--tree', ROOT])
    expect(rc == 0, 'release_check.py --tree: the zip is the tree: OK %s' % faults)
    for bad_pages, bad_repo, what in (('http://example.org', REPO, 'an http:// site'), (PAGES, 'tfn-os', 'a repo name '
                                      'with no owner')):
        try:
            release_pack.pack(VERSION, fw, store, sdk_zip, os.path.join(t, 'rel_x%d' % len(what)), store_pem, ota_pem,
                              poolmgr=os.path.join(ROOT, 'poolmgr'), pages=bad_pages, repo=bad_repo)
            expect(False, 'release_pack.py stops for %s' % what)
        except release_pack.PackError:
            expect(True, 'release_pack.py stops for %s' % what)
    bad_dir = os.path.join(t, 'bad')

    def case(path, fn, want, what, tree=''):
        r = damaged(rel, bad_dir, path, fn) if path else rel
        if path:
            resum(r)
        argv = ['--site', site, '--release', r, '--node', ''] + (['--tree', tree] if tree else [])
        rc, faults = check_out(argv)
        expect(rc == 1 and any(want in f for f in faults), 'release_check.py: %s: FAILED (%s)' % (what, want))

    def zip_case(fn, want, what, tree=''):
        r = damaged(rel, bad_dir, 'SHA256SUMS', lambda d: d)
        rezip(os.path.join(rel, os.path.basename(pz)), os.path.join(r, os.path.basename(pz)), fn)
        resum(r)
        argv = ['--site', site, '--release', r, '--node', ''] + (['--tree', tree] if tree else [])
        rc, faults = check_out(argv)
        expect(rc == 1 and any(want in f for f in faults), 'release_check.py: %s: FAILED (%s)' % (what, want))

    other = json.dumps(dict(ch, ota_pub=[other_pub]), indent=1).encode() + b'\n'
    case('channel.json', lambda d: other, 'not the keys of tfn-keys.json', 'channel.json with another OTA key')
    case('channel.json', lambda d: json.dumps(dict(ch, kinds=['lcd349'])).encode(), 'not the firmware kinds',
         'channel.json with other kinds')
    zip_case(lambda n, d: d + b'\n# a change\n' if n == 'poolmgr/pool_crypto.py' else d, 'pool_crypto.py differs',
             'a changed shared module in the zip', tree=ROOT)
    zip_case(lambda n, d: d + b'\n# a change\n' if n == 'poolmgr/pm_ota.py' else d, 'pm_ota.py differs',
             'a changed program file in the zip', tree=ROOT)
    zip_case(lambda n, d: None if n == 'poolmgr/pool.py' else d, 'does not have pool.py', 'a zip with no pool.py')
    zip_case(lambda n, d: other if n == 'poolmgr/channel.json' else d, 'not the channel.json of the release',
             'another channel.json in the zip')
    zip_case(lambda n, d: b'raise SystemExit(3)\n' if n == 'poolmgr/pm_shared.py' else d, 'does not start',
             'a zip whose program does not start')
    r = os.path.join(bad_dir, 'no_zip')
    shutil.copytree(rel, r, ignore=lambda d, names: [x for x in names if x == os.path.basename(pz)])
    resum(r)
    rc, faults = check_out(['--site', site, '--release', r, '--node', ''])
    expect(rc == 1 and any('no pool manager' in f for f in faults), 'release_check.py: no pool manager zip: FAILED')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--node', default=shutil.which('node') or '')
    a = ap.parse_args()
    if not a.node:
        print('NOTE: no Node.js: the logic of the install page is not part of this test')
    kinds = release_check.web_kinds(os.path.join(ROOT, 'site'))
    with tempfile.TemporaryDirectory() as t:
        store_pem, ota_pem, other_pem = (os.path.join(t, n) for n in ('store.pem', 'ota.pem', 'other.pem'))
        store_pub, ota_pub, other_pub = keygen(store_pem), keygen(ota_pem), keygen(other_pem)

        # the firmware of each kind: the build, the OTA signature, the manifest of the site
        fw = os.path.join(t, 'firmware')
        for i, kind in enumerate(kinds):
            build = os.path.join(t, 'build', kind)
            fake_build(build, kind, 10 * i)
            signed = os.path.join(t, 'signed', kind + '.bin')
            os.makedirs(os.path.dirname(signed), exist_ok=True)
            fw_sign.sign_file(os.path.join(build, 'tfn_os.bin'), [ota_pem], out=signed)
            files, flash, chip = fw_manifest.build_files(kind, build, VERSION, signed)
            fw_manifest.save_kind(fw, kind, VERSION, files, flash, chip)
        fw_manifest.write_index(fw)
        expect(sorted(json.load(open(os.path.join(fw, 'index.json')))['kinds']) == sorted(kinds),
               'fw_manifest.py: a manifest for each kind with a web install (%s)' % ', '.join(kinds))
        try:
            fw_manifest.build_files(kinds[0], os.path.join(t, 'build', kinds[0]), '0.0.1')
            expect(False, 'fw_manifest.py stops at a version that is not the version of the app')
        except fw_manifest.ManifestError:
            expect(True, 'fw_manifest.py stops at a version that is not the version of the app')

        # the store
        pk = os.path.join(t, 'packages')
        for i, kind in enumerate(kinds):
            fake_package(pk, 'dice', kind, 100 + i)
            fake_package(pk, 'clock', kind, 200 + i)
        # a data pack of dice (store_pack.py: packs.json and the files of the pack)
        packs = os.path.join(t, 'packs')
        write(os.path.join(packs, 'packs.json'), b'[{"name": "dicepk", "dir": "dice", "app": "dice"}]\n')
        write(os.path.join(packs, 'dicepk', 'world.bin'), bytes(range(256)) * 40)
        write(os.path.join(packs, 'dicepk', 'sfx', 'roll.mp3'), b'ID3 a test effect')
        store = os.path.join(t, 'store')
        idx = store_index.make(pk, store, store_pem, VERSION, packs=packs)
        get = store_index.folder_getter(store)
        n, idx2 = store_index.check_store(get, bytes.fromhex(store_pub))
        expect(n == 2 * len(kinds), 'store_index.py: make, then verify: %d packages' % n)
        dice = [x for x in idx2['apps'] if x['id'] == 'dice'][0]
        pk1 = idx2.get('packs', [{}])[0]
        expect(dice.get('pack') == 'dicepk' and pk1.get('dir') == 'dice' and pk1.get('path') == 'packs/dicepk/' and
               [f['path'] for f in pk1.get('files', [])] == ['sfx/roll.mp3', 'world.bin'] and pk1.get('size') == 10257 and
               os.path.isfile(os.path.join(store, 'packs', 'dicepk', 'sfx', 'roll.mp3')),
               'store_index.py: the data pack in the index ("packs", "pack") and in packs/<name>/')
        expect(idx == idx2, 'store_index.py: the index of make is the index of verify')
        try:
            write(os.path.join(packs, 'packs.json'), b'[{"name": "dicepk", "dir": "dice", "app": "nosuch"}]\n')
            store_index.make(pk, os.path.join(t, 'store_x'), store_pem, VERSION, packs=packs)
            expect(False, 'store_index.py: a pack of an app that is not in the store: refused')
        except store_index.StoreError:
            expect(True, 'store_index.py: a pack of an app that is not in the store: refused')
        expect(store_index.main(['verify', store, '--pub', store_pub]) == 0, 'store_index.py verify: exit 0')
        expect(store_index.main(['verify', store, '--pub', other_pub]) == 1, 'store_index.py: another key: exit 1')
        os.makedirs(os.path.join(t, 'bad'))
        bad = damaged(store, os.path.join(t, 'bad'), 'apps/dice/%s/app.elf' % kinds[0], flip)
        expect(store_index.main(['verify', bad, '--pub', store_pub]) == 1, 'store_index.py: a changed app.elf: exit 1')
        bad = damaged(store, os.path.join(t, 'bad'), 'index.json', lambda d: d.replace(b'1.2.3', b'1.2.4'))
        expect(store_index.main(['verify', bad, '--pub', store_pub]) == 1, 'store_index.py: a changed index: exit 1')
        bad = damaged(store, os.path.join(t, 'bad'), 'apps/clock/%s/sig' % kinds[0], flip)
        expect(store_index.main(['verify', bad, '--pub', store_pub]) == 1, 'store_index.py: a changed sig: exit 1')
        bad = damaged(store, os.path.join(t, 'bad'), 'packs/dicepk/world.bin', flip)
        expect(store_index.main(['verify', bad, '--pub', store_pub]) == 1, 'store_index.py: a changed pack file: exit 1')
        bad = damaged(store, os.path.join(t, 'bad'), 'index.json', lambda d: d.replace(b'"pack": "dicepk"', b'"x": 1'))
        try:
            store_index.check_index_form(json.loads(open(os.path.join(bad, 'index.json'), 'rb').read()))
            expect(False, 'store_index.py: a pack that its app does not name: a fault of the form')
        except store_index.StoreError:
            expect(True, 'store_index.py: a pack that its app does not name: a fault of the form')

        # the web site and the release files
        site = os.path.join(t, 'site')
        pages_build.build(site, fw, store)
        expect(not os.path.exists(os.path.join(site, 'test')) and os.path.isfile(os.path.join(site, 'store.html')),
               'pages_build.py: the pages without the tests and the tools')
        sdk_zip = os.path.join(t, 'tfn-sdk.zip')
        write(sdk_zip, b'PK\x05\x06' + b'\0' * 18)
        rel = os.path.join(t, 'release')
        release_pack.pack(VERSION, fw, store, sdk_zip, rel, store_pem, ota_pem, poolmgr=os.path.join(ROOT, 'poolmgr'),
                          pages=PAGES + '/', repo=REPO)
        expect(run_check(site, rel, store_pub, ota_pub, a.node) == 0, 'release_check.py: the good release: OK')
        keys_only = ['--site', site, '--release', rel, '--node', '']
        expect(release_check.main(keys_only) == 0, 'release_check.py: the keys of tfn-keys.json: OK')
        test_poolmgr(t, fw, store, sdk_zip, site, rel, store_pem, ota_pem, store_pub, ota_pub, other_pub, kinds)
        test_no_crypto(t, fw, store, sdk_zip, site, rel, store_pem, ota_pem, store_pub, ota_pub, kinds)

        # each damaged copy must give a fault
        cases = [
            ('site', 'firmware/%s/app.bin' % kinds[0], flip, 'an app of the site changed'),
            ('site', 'js/flasher/flow.js', lambda d: None, 'a script of the install page is not there'),
            ('site', 'store/apps/dice/%s/app.elf' % kinds[-1], flip, 'an app file of the store changed'),
            ('site', 'store/packs/dicepk/sfx/roll.mp3', flip, 'a file of a data pack changed'),
            ('site', 'store/packs/dicepk/world.bin', lambda d: None, 'a file of a data pack is not there'),
            ('rel', 'tfn-os-%s-%s.bin' % (VERSION, kinds[0]), flip, 'an OTA file of the release changed'),
            ('rel', 'SHA256SUMS', lambda d: d[:-10], 'SHA256SUMS cut'),
        ]
        for where, path, fn, what in cases:
            if fn(b'x') is None:
                bad = os.path.join(t, 'bad', 'gone_%d' % len(os.listdir(os.path.join(t, 'bad'))))
                gone = os.path.normcase(os.path.join(site, *path.split('/')))
                shutil.copytree(site, bad, ignore=lambda d, names: [x for x in names
                                                                    if os.path.normcase(os.path.join(d, x)) == gone])
                s, r = bad, rel
            elif where == 'site':
                s, r = damaged(site, os.path.join(t, 'bad'), path, fn), rel
            else:
                s, r = site, damaged(rel, os.path.join(t, 'bad'), path, fn)
            expect(run_check(s, r, store_pub, ota_pub, '') == 1, 'release_check.py: %s: FAILED' % what)
        expect(run_check(site, rel, store_pub, other_pub, '') == 1, 'release_check.py: another OTA key: FAILED')

        # the release workflow stops with no key: the step of the workflow is a test of the secret, not of a tool
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'sdk', 'tools', 'store_index.py'), 'make', pk,
                            '--out', os.path.join(t, 'nokey'), '--key', os.path.join(t, 'none.pem')],
                           capture_output=True, text=True)
        expect(r.returncode == 1 and 'STORE FAULT' in r.stdout, 'store_index.py make with no key file: exit 1')
    print('RELEASE TOOLS TESTS %s' % ('FAILED: %d' % len(FAILS) if FAILS else 'OK'))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
