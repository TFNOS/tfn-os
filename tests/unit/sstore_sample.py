#!/usr/bin/env python3
"""Make the sample static store of the unit test tests/unit/test_sstore.c with sdk/tools/store_index.py (the tool of
the public release that makes the store of GitHub Pages).

It makes a TEST key in a temporary folder (the private key never goes into git or into the output), fake app
packages of two board kinds and a data pack of the app demo (the keys "packs" and "pack" of the index: the board
parser must skip them), and then runs "store_index.py make" and "store_index.py verify". The output folder gets
the files that the C test reads:

  index.json, index.sig       the signed index, as the board gets it
  store_pub.txt               the public TEST key (130 hex digits)
  demo_manifest.txt, demo_sig the manifest.txt and the sig of the package demo/lcd349 (the check of the install)

Usage: python tests/unit/sstore_sample.py [--tool <store_index.py>] [--out <folder>]
(default: sdk/tools/store_index.py of this repo, out = tests/unit/sstore). The package "cryptography" is necessary.
"""
import argparse
import hashlib
import importlib.util
import os
import random
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

ABI = 12345  # the ABI of the test board (test_sstore.c TEST_ABI)
# id, kinds, rev, abi, perms, a watcher: demo is for the test board; watchy needs a newer rev; abix is for another ABI;
# fut has a permission class of a newer firmware
APPS = [
    ('demo', ('lcd349', 'cores3'), 20, ABI, 'net,storage', False),
    ('watchy', ('lcd349',), 99, ABI, '', True),
    ('abix', ('lcd349',), 20, 999, '', False),
    ('fut', ('lcd349', 'cores3'), 20, ABI, 'future', False),
]
DESC = 'A demo app: "quotes" and a back slash \\ in the text.'
DESC_HE = 'משחק לבדיקה'


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def fake_package(folder, pid, kind, rev, abi, perms, watch, seed):
    rnd = random.Random(seed)
    elf = bytes(rnd.getrandbits(8) for _ in range(3000 + seed))
    icon = bytes(rnd.getrandbits(8) for _ in range(400))
    lines = ['id=' + pid, 'name=' + pid.upper(), 'ver=1.2.3', 'code=26123', 'abi=%d' % abi, 'rev=%d' % rev,
             'perms=' + perms, 'kind=' + kind, 'group=UTIL', 'emoji=\U0001F3B2', 'saves=' + pid + '_',
             'bytes=%d' % len(elf), 'ram=1000', 'elf_sha256=' + hashlib.sha256(elf).hexdigest(),
             'icon_sha256=' + hashlib.sha256(icon).hexdigest(), 'desc=' + DESC,
             'desc_he=' + DESC_HE]
    d = os.path.join(folder, pid, kind)
    if watch:
        we = bytes(rnd.getrandbits(8) for _ in range(700))
        lines.append('watch_sha256=' + hashlib.sha256(we).hexdigest())
        write(os.path.join(d, 'watch.elf'), we)
    write(os.path.join(d, 'manifest.txt'), ('\n'.join(lines) + '\n').encode('utf-8'))
    write(os.path.join(d, 'app.elf'), elf)
    write(os.path.join(d, 'icon.bin'), icon)


def load_tool(path):
    spec = importlib.util.spec_from_file_location('store_index', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--tool', default=os.path.join(ROOT, 'sdk', 'tools', 'store_index.py'))
    ap.add_argument('--out', default=os.path.join(HERE, 'sstore'))
    a = ap.parse_args()
    if not os.path.isfile(a.tool):
        sys.exit('no store_index.py at %s (give --tool)' % a.tool)
    si = load_tool(a.tool)
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    with tempfile.TemporaryDirectory(prefix='tfn_sstore_') as t:
        key = ec.generate_private_key(ec.SECP256R1())
        pem = os.path.join(t, 'test_store.pem')
        write(pem, key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                     serialization.NoEncryption()))
        pkgs = os.path.join(t, 'pkgs')
        seed = 1
        for pid, kinds, rev, abi, perms, watch in APPS:
            for kind in kinds:
                fake_package(pkgs, pid, kind, rev, abi, perms, watch, seed)
                seed += 1
        packs = os.path.join(t, 'packs')
        write(os.path.join(packs, 'packs.json'), b'[{"name": "demo", "dir": "demo", "app": "demo"}]\n')
        write(os.path.join(packs, 'demo', 'world.bin'), bytes(range(256)) * 8)
        write(os.path.join(packs, 'demo', 'sfx', 'click.mp3'), b'ID3 a fake effect')
        store = os.path.join(t, 'store')
        idx = si.make(pkgs, store, pem, release='9.8.7', packs=packs)
        pub = si.point_of(key).hex()
        if si.main(['verify', store, '--pub', pub]) != 0:
            sys.exit('store_index.py verify: FAULT')
        os.makedirs(a.out, exist_ok=True)
        for src, dst in (('index.json', 'index.json'), ('index.sig', 'index.sig'),
                         ('apps/demo/lcd349/manifest.txt', 'demo_manifest.txt'),
                         ('apps/demo/lcd349/sig', 'demo_sig')):
            shutil.copyfile(os.path.join(store, *src.split('/')), os.path.join(a.out, dst))
        write(os.path.join(a.out, 'store_pub.txt'), (pub + '\n').encode())
        print('SSTORE SAMPLE: %d apps, key %s -> %s' % (len(idx['apps']), idx['key_id'], a.out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
