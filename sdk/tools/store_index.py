#!/usr/bin/env python3
"""The static store of TFN OS: a signed JSON index and the app files, for a web host with no server code (GitHub
Pages, a release). A board or a person gets the files with plain GET requests.

  python store_index.py make <packages> --out <store> --key <pem> [--release V] [--kinds lcd349,cores3]
                         [--packs <folder>]
  python store_index.py verify <store> --pub <pem | 130 hex digits> [--kinds lcd349,cores3]
  python store_index.py pub <pem>

<packages> is the output folder of tfn_app.py build (or mk_app.py): <packages>/<id>/<kind>/ with manifest.txt,
app.elf, icon.bin, sig and, for some apps, watch.elf. The store gets:

  <store>/index.json                          the list of the apps (the form: STORE_FORMAT below)
  <store>/index.sig                           64 bytes: the ECDSA P-256 / SHA-256 signature (r || s) of index.json
  <store>/apps/<id>/<kind>/<file>             the files of each package, as the board install takes them
  <store>/apps/<id>/<id>-<kind>.zip           the same files in one zip (for the page "Install an app file" of the
                                              board web page)
  <store>/packs/<name>/<file>                 the files of each data pack (--packs: the output folder of
                                              store_pack.py: <folder>/packs.json and <folder>/<name>/...)

THE DATA PACKS (the big files of an app for the SD card). With packs, index.json also has
  "packs": [{"name": "wood", "dir": "wood", "app": "wood", "path": "packs/wood/", "size": <the bytes of all files>,
             "files": [{"path": "p_cabin_a.twp", "size": n, "sha256": "<hex>"}, ...]}, ...]
and the entry of each app with a pack has "pack": "<name>". A file of a pack goes to the SD card as
/sdcard/<dir>/<path> (<path> can have folders: "sfx/click.mp3"); the store has it at <path of the pack><path>. The
board parser of the index (main/tfn_sstore_core.c) skips these keys. The signature of the index covers the size and
the SHA-256 of each file of each pack.

The signature of each package is the file "sig" that the board checks (main/tfn_app.c tfa_sig_ok): the same signature
form over manifest.txt, by the same store key. manifest.txt holds the SHA-256 of app.elf, icon.bin and watch.elf, so
the signature covers all the files. The signature of the index covers the list (the versions and the hashes).

make signs each manifest.txt again with --key (the "sig" of the package is replaced), and checks each package before
it goes in. verify does all checks of a person who does not trust the host: the index signature, the size and the
SHA-256 of each file, each "sig" with the public key, and the facts of each manifest.txt. Exit 0 = good, 1 = a fault.
The private key never goes into a file of the store or into the output of this tool. Python 3.9 or later. make needs
the package "cryptography"; verify and pub use the package, or the openssl program (fw_sign.py) when the package is
not there.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile

STORE_FORMAT = 1
# the files of a package, in the order of the install (main/tfn_app.h TFN_APP_FILES; "sigs" = developer signatures:
# the official store has only the store signature "sig")
NEED = ('manifest.txt', 'app.elf', 'sig')
OPTIONAL = ('icon.bin', 'watch.elf')
FILES = ('manifest.txt', 'app.elf', 'icon.bin', 'sig', 'watch.elf')
ID_RX = re.compile(r'^[a-z0-9_]{1,23}$')
KIND_RX = re.compile(r'^[a-z0-9]{1,15}$')
HEX64 = re.compile(r'^[0-9a-f]{64}$')
# a fixed time in each zip: the same files give the same zip bytes
ZIP_TIME = (2020, 1, 1, 0, 0, 0)


class StoreError(Exception):
    pass


# ---------------------------------------------------------------- the keys and the signatures
_CRYPTO = None      # (the package "cryptography" is there: True / False; None = not looked at yet)


def have_crypto():
    """True when the Python package "cryptography" is there. Else the public keys and the checks of the signatures
    use the openssl program (fw_sign.py); only make (the signatures) needs the package."""
    global _CRYPTO
    if _CRYPTO is None:
        try:
            import cryptography.hazmat.primitives.asymmetric.ec  # noqa: F401
            _CRYPTO = True
        except ImportError:
            _CRYPTO = False
    return _CRYPTO


def _fw_sign():
    """The module fw_sign.py (the openssl program): the neighbour of this file."""
    import fw_sign
    return fw_sign


def _ec():
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec, utils
        from cryptography.exceptions import InvalidSignature
    except ImportError:
        raise StoreError('the Python package "cryptography" is not installed (pip install cryptography)')
    return hashes, serialization, ec, utils, InvalidSignature


def load_key(path):
    """The P-256 private key of a PEM file."""
    _, serialization, ec, _, _ = _ec()
    try:
        with open(path, 'rb') as f:
            key = serialization.load_pem_private_key(f.read(), None)
    except (OSError, ValueError, TypeError) as e:
        raise StoreError('the key %s: %s' % (path, e))
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise StoreError('the key %s is not a P-256 private key' % path)
    return key


def point_of(key):
    """The 65-byte uncompressed point of a public or a private key."""
    _, serialization, ec, _, _ = _ec()
    pub = key.public_key() if isinstance(key, ec.EllipticCurvePrivateKey) else key
    return pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def key_id(pt):
    """The key ID of a point: the first 8 bytes of its SHA-256, as hex (main/tfn_pub.h tfn_pub_key_id)."""
    return hashlib.sha256(pt).hexdigest()[:16]


def pem_pub(path, data):
    """The 65-byte point of a PEM file (a private or a public key): the package "cryptography", else the openssl
    program."""
    if not have_crypto():
        fs = _fw_sign()
        try:
            return fs.pub_of_pem(path)
        except fs.SignError as e:
            raise StoreError('the key %s: %s' % (path, e))
    _, serialization, ec, _, _ = _ec()
    if b'PRIVATE KEY' in data:
        return point_of(load_key(path))
    try:
        key = serialization.load_pem_public_key(data)
    except ValueError as e:
        raise StoreError('the public key %s: %s' % (path, e))
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise StoreError('the public key %s is not a P-256 key' % path)
    return point_of(key)


def hex_pub(text):
    """The 65-byte point of 130 hex digits or of the C bytes "0x04, 0x.."; None when the text is not hex."""
    digits = re.sub(r'0x|[\s,]', '', text)
    try:
        pt = bytes.fromhex(digits)
    except ValueError:
        return None
    if len(pt) != 65 or pt[0] != 4:
        raise StoreError('a public key has 65 bytes and starts with 04')
    return pt


def parse_pub(text):
    """A public key: a file (a PEM file, private or public, or a file with the hex text), 130 hex digits, or the C
    bytes "0x04, 0x..". -> 65 bytes. The content of a file tells its form. A PEM file needs the package
    "cryptography" or the openssl program."""
    if os.path.isfile(text):
        with open(text, 'rb') as h:
            data = h.read()
        if b'-----BEGIN' in data:
            return pem_pub(text, data)
        try:
            pt = hex_pub(data.decode('ascii'))
        except UnicodeDecodeError:
            pt = None
        if pt is None:
            raise StoreError('%s is not a PEM file and not the hex text of a public key' % text)
        return pt
    pt = hex_pub(text)
    if pt is None:
        raise StoreError('not a public key: %s' % text[:40])
    return pt


def sign(key, msg):
    """r || s (64 bytes) of the ECDSA P-256 / SHA-256 signature of msg: the form of the file "sig"."""
    hashes, _, ec, utils, _ = _ec()
    r, s = utils.decode_dss_signature(key.sign(msg, ec.ECDSA(hashes.SHA256())))
    return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')


def verify(pt, msg, rs):
    """True: rs (r || s) is a good signature of msg by the 65-byte point pt (the package "cryptography", else the
    openssl program)."""
    if len(rs) != 64:
        return False
    if not have_crypto():
        fs = _fw_sign()
        try:
            return len(pt) == 65 and pt[:1] == b'\x04' and fs.verify_raw(pt, msg, rs)
        except (fs.SignError, OSError, subprocess.SubprocessError) as e:
            raise StoreError('no check of a signature: %s' % e)
    hashes, _, ec, utils, bad = _ec()
    try:
        pub = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), pt)
        pub.verify(utils.encode_dss_signature(int.from_bytes(rs[:32], 'big'), int.from_bytes(rs[32:], 'big')),
                   msg, ec.ECDSA(hashes.SHA256()))
        return True
    except (bad, ValueError):
        return False


# ---------------------------------------------------------------- the packages
def sha256(data):
    return hashlib.sha256(data).hexdigest()


def manifest_fields(data):
    """The key=value lines of manifest.txt (the first value of each key)."""
    out = {}
    for line in data.decode('utf-8', 'replace').split('\n'):
        k, sep, v = line.partition('=')
        if sep and k not in out:
            out[k] = v
    return out


def check_package(pid, kind, files):
    """The checks of one package (a dict file name -> bytes). Returns the manifest fields; raises StoreError."""
    where = '%s/%s' % (pid, kind)
    for f in NEED:
        if f not in files:
            raise StoreError('%s: no %s' % (where, f))
    m = manifest_fields(files['manifest.txt'])
    if m.get('id') != pid:
        raise StoreError('%s: manifest.txt says id=%s' % (where, m.get('id')))
    if m.get('kind') != kind:
        raise StoreError('%s: manifest.txt says kind=%s' % (where, m.get('kind')))
    if m.get('elf_sha256') != sha256(files['app.elf']) or m.get('bytes') != str(len(files['app.elf'])):
        raise StoreError('%s: app.elf is not the file of manifest.txt' % where)
    if 'icon.bin' in files and m.get('icon_sha256') != sha256(files['icon.bin']):
        raise StoreError('%s: icon.bin is not the file of manifest.txt' % where)
    if 'watch.elf' in files and m.get('watch_sha256') != sha256(files['watch.elf']):
        raise StoreError('%s: watch.elf is not the file of manifest.txt' % where)
    if 'watch.elf' not in files and m.get('watch_sha256'):
        raise StoreError('%s: manifest.txt names a watch.elf, the package has none' % where)
    if m.get('publisher'):
        raise StoreError('%s: a package of a developer key (publisher=): the store signs only its own packages' % where)
    for k in ('name', 'ver', 'code', 'abi', 'rev', 'group'):
        if not m.get(k):
            raise StoreError('%s: manifest.txt has no %s=' % (where, k))
    return m


def read_packages(src, kinds):
    """{id: {kind: {file: bytes}}} of the package folder src."""
    out = {}
    if not os.path.isdir(src):
        raise StoreError('no package folder %s' % src)
    for pid in sorted(os.listdir(src)):
        if not os.path.isdir(os.path.join(src, pid)):
            continue
        if not ID_RX.match(pid):
            raise StoreError('the app id %r has other characters than a-z 0-9 _' % pid)
        for kind in sorted(os.listdir(os.path.join(src, pid))):
            d = os.path.join(src, pid, kind)
            if not os.path.isdir(d) or (kinds and kind not in kinds):
                continue
            if not KIND_RX.match(kind):
                raise StoreError('%s: the kind %r is not a kind name' % (pid, kind))
            files = {}
            for f in FILES:
                p = os.path.join(d, f)
                if os.path.isfile(p):
                    with open(p, 'rb') as h:
                        files[f] = h.read()
            if 'manifest.txt' in files:
                out.setdefault(pid, {})[kind] = files
    if not out:
        raise StoreError('no package in %s' % src)
    return out


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def zip_bytes(path, files):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in FILES:
            if f in files:
                info = zipfile.ZipInfo(f, ZIP_TIME)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, files[f])
    with open(path, 'rb') as h:
        return h.read()


def entry_of(m):
    return {'id': m['id'], 'name': m['name'], 'ver': m['ver'], 'code': int(m['code']), 'group': m['group'],
            'emoji': m.get('emoji', ''), 'desc': m.get('desc', ''), 'desc_he': m.get('desc_he', ''), 'kinds': {}}


PACK_PART = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')


def pack_rel(path):
    """A file path of a pack: folders and a name of A-Z a-z 0-9 . _ - (no "..", no back slash, no start "/")."""
    if not isinstance(path, str) or not all(PACK_PART.match(p) for p in path.split('/')):
        raise StoreError('a file of a pack has a path that is not safe: %r' % (path,))
    return path


def read_packs(src):
    """[(meta, [(rel path, bytes)])] of the output folder of store_pack.py."""
    p = os.path.join(src, 'packs.json')
    if not os.path.isfile(p):
        raise StoreError('no packs.json in %s (store_pack.py make)' % src)
    with open(p, encoding='utf-8') as h:
        meta = json.load(h)
    out = []
    for m in meta:
        d = os.path.join(src, m.get('name', ''))
        if not ID_RX.match(m.get('name', '')) or not ID_RX.match(m.get('dir', '')) or not os.path.isdir(d):
            raise StoreError('packs.json: the pack %r has no good name, dir or folder' % m.get('name'))
        files = []
        for root, dirs, names in os.walk(d):
            dirs.sort()
            for f in sorted(names):
                rel = os.path.relpath(os.path.join(root, f), d).replace(os.sep, '/')
                with open(os.path.join(root, f), 'rb') as h:
                    files.append((pack_rel(rel), h.read()))
        if not files:
            raise StoreError('the pack %s has no files' % m['name'])
        out.append((m, sorted(files)))
    return out


def make(src, out, key_path, release='', kinds=(), packs=''):
    key = load_key(key_path)
    pt = point_of(key)
    pkgs = read_packages(src, kinds)
    apps = []
    for pid, by_kind in pkgs.items():
        app = None
        for kind, files in by_kind.items():
            files['sig'] = sign(key, files['manifest.txt'])
            m = check_package(pid, kind, files)
            if app is None:
                app = entry_of(m)
            elif (m['ver'], m['name']) != (app['ver'], app['name']):
                raise StoreError('%s: the kinds have other versions or names (%s %s, %s %s)'
                                 % (pid, app['name'], app['ver'], m['name'], m['ver']))
            base = 'apps/%s/%s/' % (pid, kind)
            fl = {}
            for f in FILES:
                if f in files:
                    write(os.path.join(out, base + f), files[f])
                    fl[f] = {'size': len(files[f]), 'sha256': sha256(files[f])}
            zpath = 'apps/%s/%s-%s.zip' % (pid, pid, kind)
            zb = zip_bytes(os.path.join(out, zpath), files)
            app['kinds'][kind] = {'path': base, 'abi': int(m['abi']), 'rev': int(m['rev']),
                                  'perms': [p for p in m.get('perms', '').split(',') if p],
                                  'bytes': int(m['bytes']), 'ram': int(m.get('ram', '0') or 0), 'files': fl,
                                  'zip': {'path': zpath, 'size': len(zb), 'sha256': sha256(zb)}}
        apps.append(app)
    index = {'tfn_store': STORE_FORMAT, 'release': release, 'key_id': key_id(pt), 'pubkey': pt.hex(),
             'kinds': sorted({k for a in apps for k in a['kinds']}), 'apps': apps}
    if packs:
        by_id = {a['id']: a for a in apps}
        plist = []
        for m, files in read_packs(packs):
            if m.get('app') not in by_id:
                raise StoreError('the pack %s is for the app %r, which is not in the store' % (m['name'], m.get('app')))
            if 'pack' in by_id[m['app']]:
                raise StoreError('the app %s has two packs' % m['app'])
            base = 'packs/%s/' % m['name']
            fl = []
            for rel, data in files:
                write(os.path.join(out, *(base + rel).split('/')), data)
                fl.append({'path': rel, 'size': len(data), 'sha256': sha256(data)})
            by_id[m['app']]['pack'] = m['name']
            plist.append({'name': m['name'], 'dir': m['dir'], 'app': m['app'], 'path': base,
                          'size': sum(f['size'] for f in fl), 'files': fl})
        index['packs'] = plist
    data = (json.dumps(index, indent=1, ensure_ascii=False, sort_keys=False) + '\n').encode('utf-8')
    write(os.path.join(out, 'index.json'), data)
    write(os.path.join(out, 'index.sig'), sign(key, data))
    return index


# ---------------------------------------------------------------- the check of a store
def safe_rel(path):
    """A relative path in the store with no "..", no back slash and no start "/"."""
    if not isinstance(path, str) or not path or path.startswith('/') or '\\' in path or \
            any(p in ('', '.', '..') for p in path.rstrip('/').split('/')):
        raise StoreError('the index has a path that is not safe: %r' % (path,))
    return path


def check_index_form(index):
    """The form of index.json (no file access). Raises StoreError."""
    if not isinstance(index, dict) or index.get('tfn_store') != STORE_FORMAT:
        raise StoreError('index.json is not a store index of form %d' % STORE_FORMAT)
    if not re.match(r'^[0-9a-f]{16}$', str(index.get('key_id', ''))) or \
            not re.match(r'^04[0-9a-f]{128}$', str(index.get('pubkey', ''))):
        raise StoreError('index.json has no key_id or pubkey')
    if not isinstance(index.get('apps'), list) or not index['apps']:
        raise StoreError('index.json has no apps')
    seen = set()
    for a in index['apps']:
        for k, t in (('id', str), ('name', str), ('ver', str), ('code', int), ('group', str), ('kinds', dict)):
            if not isinstance(a.get(k), t):
                raise StoreError('an app of index.json has no %s' % k)
        if not ID_RX.match(a['id']) or a['id'] in seen:
            raise StoreError('index.json: the app id %r is not good or not unique' % a['id'])
        seen.add(a['id'])
        if not a['kinds']:
            raise StoreError('%s: no kind' % a['id'])
        for kind, e in a['kinds'].items():
            if not KIND_RX.match(kind):
                raise StoreError('%s: the kind %r' % (a['id'], kind))
            if safe_rel(e.get('path')) != 'apps/%s/%s/' % (a['id'], kind):
                raise StoreError('%s/%s: the path is not apps/<id>/<kind>/' % (a['id'], kind))
            fl = e.get('files')
            if not isinstance(fl, dict) or any(f not in fl for f in NEED) or any(f not in FILES for f in fl):
                raise StoreError('%s/%s: the file list is not %s (+ %s)' % (a['id'], kind, ', '.join(NEED),
                                                                           ', '.join(OPTIONAL)))
            for f, v in list(fl.items()) + [('zip', e.get('zip') or {})]:
                if not isinstance(v.get('size'), int) or v['size'] <= 0 or not HEX64.match(str(v.get('sha256', ''))):
                    raise StoreError('%s/%s: %s has no size or SHA-256' % (a['id'], kind, f))
            safe_rel(e['zip'].get('path'))
    check_packs_form(index, seen)
    return True


def check_packs_form(index, ids):
    """The form of "packs" of index.json and of the "pack" of each app (THE DATA PACKS at the top)."""
    packs = index.get('packs', [])
    if not isinstance(packs, list):
        raise StoreError('index.json: "packs" is not a list')
    names, dirs = set(), set()
    for p in packs:
        if not isinstance(p, dict) or not ID_RX.match(str(p.get('name', ''))) or p['name'] in names:
            raise StoreError('index.json: a pack has no good name, or the name is not unique')
        where = 'the pack ' + p['name']
        names.add(p['name'])
        if not ID_RX.match(str(p.get('dir', ''))) or p['dir'] in dirs:
            raise StoreError('%s: the card folder "dir" is not good or not unique' % where)
        dirs.add(p['dir'])
        if p.get('app') not in ids:
            raise StoreError('%s: "app" is not an app of the index' % where)
        if safe_rel(p.get('path')) != 'packs/%s/' % p['name']:
            raise StoreError('%s: the path is not packs/<name>/' % where)
        fl = p.get('files')
        if not isinstance(fl, list) or not fl:
            raise StoreError('%s: no files' % where)
        seen = set()
        for f in fl:
            if not isinstance(f, dict) or pack_rel(f.get('path')) in seen:
                raise StoreError('%s: a file has no path, or the path is not unique' % where)
            seen.add(f['path'])
            if not isinstance(f.get('size'), int) or f['size'] < 0 or not HEX64.match(str(f.get('sha256', ''))):
                raise StoreError('%s: %s has no size or SHA-256' % (where, f['path']))
        if p.get('size') != sum(f['size'] for f in fl):
            raise StoreError('%s: "size" is not the sum of the files' % where)
    for a in index['apps']:
        if 'pack' in a:
            want = [p for p in packs if p['name'] == a['pack']]
            if not want or want[0]['app'] != a['id']:
                raise StoreError('%s: "pack" names no pack of this app' % a['id'])
    for p in packs:
        if not any(a.get('pack') == p['name'] for a in index['apps']):
            raise StoreError('the pack %s: its app does not name it' % p['name'])


def check_store(get, pt, kinds=()):
    """All checks of a store. get(rel path) -> bytes (or None when the file is not there); pt: the store key (65
    bytes). Returns (the number of packages, the index). Raises StoreError at the first fault."""
    data, sig = get('index.json'), get('index.sig')
    if data is None or sig is None:
        raise StoreError('no index.json or index.sig')
    if not verify(pt, data, sig):
        raise StoreError('index.sig is not a good signature of index.json by the key %s' % key_id(pt))
    index = json.loads(data.decode('utf-8'))
    check_index_form(index)
    if index['pubkey'] != pt.hex():
        raise StoreError('index.json names the key %s, not %s' % (index['key_id'], key_id(pt)))
    n = 0
    for a in index['apps']:
        for kind, e in a['kinds'].items():
            if kinds and kind not in kinds:
                continue
            files = {}
            for f, v in e['files'].items():
                b = get(e['path'] + f)
                if b is None:
                    raise StoreError('%s/%s: no file %s' % (a['id'], kind, f))
                if len(b) != v['size'] or sha256(b) != v['sha256']:
                    raise StoreError('%s/%s: %s is not the file of the index' % (a['id'], kind, f))
                files[f] = b
            m = check_package(a['id'], kind, files)
            if not verify(pt, files['manifest.txt'], files['sig']):
                raise StoreError('%s/%s: sig is not a good signature of manifest.txt' % (a['id'], kind))
            if (m['ver'], m['name'], int(m['code'])) != (a['ver'], a['name'], a['code']):
                raise StoreError('%s/%s: manifest.txt and index.json do not agree (ver, name, code)' % (a['id'], kind))
            zb = get(e['zip']['path'])
            if zb is None or len(zb) != e['zip']['size'] or sha256(zb) != e['zip']['sha256']:
                raise StoreError('%s/%s: the zip is not the file of the index' % (a['id'], kind))
            n += 1
    for p in index.get('packs', []):
        for f in p['files']:
            b = get(p['path'] + f['path'])
            if b is None:
                raise StoreError('the pack %s: no file %s' % (p['name'], f['path']))
            if len(b) != f['size'] or sha256(b) != f['sha256']:
                raise StoreError('the pack %s: %s is not the file of the index' % (p['name'], f['path']))
    return n, index


def folder_getter(root):
    def get(rel):
        p = os.path.join(root, *safe_rel(rel).split('/'))
        if not os.path.isfile(p):
            return None
        with open(p, 'rb') as f:
            return f.read()
    return get


def packs_text(index):
    """'<n> data packs (<files> files, <bytes> bytes)' of an index."""
    p = index.get('packs', [])
    return '%d data packs (%d files, %d bytes)' % (len(p), sum(len(x['files']) for x in p), sum(x['size'] for x in p))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('make', help='make the store from a package folder')
    m.add_argument('packages')
    m.add_argument('--out', required=True)
    m.add_argument('--key', required=True, help='the store key (a P-256 private PEM file)')
    m.add_argument('--release', default='', help='the release of the store (only a fact in the index)')
    m.add_argument('--kinds', default='', help='only these kinds (a list with commas)')
    m.add_argument('--packs', default='', help='the data packs: the output folder of store_pack.py make')
    v = sub.add_parser('verify', help='check a store folder')
    v.add_argument('store')
    v.add_argument('--pub', required=True,
                   help='the public store key: a PEM file, a file with the hex text, or 130 hex digits')
    v.add_argument('--kinds', default='')
    p = sub.add_parser('pub', help='the public key (hex) and the key ID of a PEM file')
    p.add_argument('pem')
    a = ap.parse_args(argv)
    kinds = tuple(k for k in getattr(a, 'kinds', '').split(',') if k)
    try:
        if a.cmd == 'make':
            idx = make(a.packages, a.out, a.key, a.release, kinds, a.packs)
            n = sum(len(x['kinds']) for x in idx['apps'])
            print('STORE MADE: %d apps, %d packages, %s, key %s -> %s' % (len(idx['apps']), n, packs_text(idx),
                                                                         idx['key_id'], a.out))
        elif a.cmd == 'verify':
            pt = parse_pub(a.pub)
            n, idx = check_store(folder_getter(a.store), pt, kinds)
            print('STORE OK: %d apps, %d packages, %s, key %s' % (len(idx['apps']), n, packs_text(idx),
                                                                 key_id(pt)))
        else:
            pt = parse_pub(a.pem)
            print('%s\nkey ID %s' % (pt.hex(), key_id(pt)))
    except StoreError as e:
        print('STORE FAULT: %s' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
