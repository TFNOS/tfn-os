#!/usr/bin/env python3
"""TFN OS SDK: publish the firmware of the TFN COMPANIONS (companion/ of the repo) to the store.

The store keeps, for the boards (main/companion.c comp_fw_start, the app COMPANIONS):
  compfw/index.txt     "tfn_compfw 1", then one line for each chip: "<target> <fw> <size> <sha256 hex>"
  compfw/index.sig     64 bytes: the ECDSA P-256 / SHA-256 signature (r || s) of index.txt by the store key
  compfw/<target>.bin  the app image of the companion for that chip (esp32, esp32s3, esp32c3, esp32c6)
A board takes the index only with a signature of a trusted STORE key, and a file only with the size and the SHA-256
of the index. The version of an image is CC_FW of companion/main/comp_core.h (the version string of the image).

    python sdk/tools/mk_compfw.py                     the four targets of build_dev/companion_<target>, to the manager
    python sdk/tools/mk_compfw.py --out <folder>      also (or with --no-publish only) the files for a static store
    python sdk/tools/mk_compfw.py --targets esp32 esp32s3 --sign-key key.pem
Build the images first with idf.py (companion/README.md).
"""
import argparse
import hashlib
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import store_conf  # noqa: E402
import tfn_pkg  # noqa: E402

TARGETS = ('esp32', 'esp32s3', 'esp32c3', 'esp32c6')
CHIP_ID = {'esp32': 0, 'esp32s2': 2, 'esp32c3': 5, 'esp32s3': 9, 'esp32c6': 13}


def image_facts(b):
    # the image header: magic 0xE9, the chip id at 12; the app description at 32 (magic 0xABCD5432), its version at 48
    if len(b) < 96 or b[0] != 0xE9 or b[32:36] != bytes((0x32, 0x54, 0xCD, 0xAB)):
        return None, None
    chip = b[12] | b[13] << 8
    ver = b[48:80].split(b'\0')[0].decode('ascii', 'replace')
    return chip, ver


def cc_fw():
    for line in open(os.path.join(ROOT, 'companion', 'main', 'comp_core.h'), encoding='utf-8'):
        if line.startswith('#define CC_FW '):
            return int(line.split()[2])
    sys.exit('STOP: no CC_FW in companion/main/comp_core.h')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--targets', nargs='+', default=list(TARGETS))
    ap.add_argument('--build', default=os.path.join(ROOT, 'build_dev'), help='the folder of companion_<target>/')
    ap.add_argument('--manager', default=store_conf.manager_url())
    ap.add_argument('--sign-key', default='', help='a P-256 PEM file of the store key (else the signer of store_conf.py)')
    ap.add_argument('--out', default='', help='write compfw/ into this folder too (a static store)')
    ap.add_argument('--no-publish', action='store_true')
    a = ap.parse_args()
    want = cc_fw()
    files, lines = {}, ['tfn_compfw 1']
    for t in a.targets:
        if t not in CHIP_ID:
            sys.exit('STOP: an unknown target %s (%s)' % (t, ', '.join(CHIP_ID)))
        p = os.path.join(a.build, 'companion_%s' % t, 'tfn_companion.bin')
        if not os.path.exists(p):
            sys.exit('STOP: no %s: build the companion for %s first' % (p, t))
        b = open(p, 'rb').read()
        chip, ver = image_facts(b)
        if chip != CHIP_ID[t]:
            sys.exit('STOP: %s is not an app image for %s (chip id %s)' % (p, t, chip))
        if ver != str(want):
            sys.exit('STOP: %s has the version "%s", CC_FW is %d: build it again' % (p, ver, want))
        files['%s.bin' % t] = b
        lines.append('%s %d %d %s' % (t, want, len(b), hashlib.sha256(b).hexdigest()))
        print('%-8s firmware %d, %d bytes' % (t, want, len(b)))
    index = ('\n'.join(lines) + '\n').encode('ascii')
    sig = tfn_pkg.sign_local(a.sign_key, index) if a.sign_key else store_conf.sign(index)
    if len(sig) != 64:
        sys.exit('STOP: the signature has %d bytes (64 expected)' % len(sig))
    # (the images first, the index last: a board never reads an index that names a file that is not there yet)
    order = sorted(files) + ['index.sig', 'index.txt']
    files['index.txt'], files['index.sig'] = index, sig
    if a.out:
        d = os.path.join(a.out, 'compfw')
        os.makedirs(d, exist_ok=True)
        for fn in order:
            open(os.path.join(d, fn), 'wb').write(files[fn])
        print('the static store files: %s' % d)
    if a.no_publish:
        return
    if not a.manager:
        sys.exit('STOP: no manager: give --manager, or --no-publish with --out')
    for fn in order:
        req = urllib.request.Request('%s/api/store/compfw?file=%s' % (a.manager.rstrip('/'), fn), data=files[fn],
                                     method='POST', headers={'Content-Type': 'application/octet-stream'})
        with urllib.request.urlopen(req, timeout=120) as r:
            ans = r.read().decode('utf-8', 'replace')
        if '"ok": true' not in ans and '"ok":true' not in ans:
            sys.exit('STOP: %s: %s' % (fn, ans[:200]))
    print('COMPFW PUBLISHED: firmware %d for %s' % (want, ', '.join(a.targets)))


if __name__ == '__main__':
    main()
