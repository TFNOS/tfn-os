#!/usr/bin/env python3
"""The files of a GitHub release of TFN OS, from the firmware folder of the web site and the store folder.

  python release_pack.py --version V --firmware <folder> --store <folder> --sdk <tfn-sdk.zip> --out <folder>
                         --store-pub <key> --ota-pub <key> [--ota-pub2 <key>]
                         [--poolmgr <folder> --pages <URL> --repo <owner/name>]

<firmware> is the folder of site/tools/fw_manifest.py (index.json, <kind>/manifest.json and the files), <store> the
folder of store_index.py. A key is a PEM file (private or public), a file with the hex text, or 130 hex digits: only
the public values go out. A PEM file needs the package "cryptography" or the openssl program (store_index.parse_pub).
The release gets:
  tfn-os-V-<kind>.bin        the app with the OTA signature (the update file of a board, and the app of the site)
  tfn-os-V-<kind>-full.bin   bootloader + partition table + app + otadata in one image: write it at 0x0
  tfn-store-V.zip            the store (the folder store/ of the site)
  tfn-sdk-V.zip              the SDK package
  tfn-keys.json              the public keys and their key IDs (the store key, the OTA release key, the backup key)
  SHA256SUMS                 the SHA-256 of each file (the form of sha256sum)
The files of the web flasher (site/js/flasher), so that a web site can install a release from the release files:
  tfn-os-V-<kind>-manifest.json   the manifest of the kind (site/tools/fw_manifest.py); each "path" is the name of a
                                  release file in the same release (the app is tfn-os-V-<kind>.bin)
  tfn-os-V-<kind>-<part>.bin      the other parts of the manifest (bootloader, partition-table, ota-data ...)
  tfn-firmware.json               the index of the flasher (the form of firmware/index.json). Its name has no version:
                                  GitHub gives it at releases/latest/download/. Each "manifest" is the path from there
                                  to the manifest of this release: ../../download/vV/tfn-os-V-<kind>-manifest.json
With --poolmgr (the folder poolmgr/ of the tree), also:
  channel.json               the channel of the pool manager: {"format": 1, "pages", "repo", "store_pub",
                             "ota_pub": [...], "release", "kinds"} (the public keys of this release; --pages = the
                             web site, --repo = the GitHub repo of the releases)
  tfn-poolmgr-V.zip          the pool manager in one folder poolmgr/: the files of the program, copies of the shared
                             modules (SHARED: sdk/tools/ of the public tree, else manager/ of a private tree) and
                             channel.json. Unzip it and run "python poolmgr/poolmgr.py doctor": no repo is necessary.
The output folder must be empty or not there. release_check.py checks these files.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import store_index  # noqa: E402

ZIP_TIME = store_index.ZIP_TIME
# the modules that poolmgr shares with the tree (poolmgr/pm_shared.py): tfn-poolmgr-V.zip has a copy of each
SHARED = ('fw_sign.py', 'pool.py', 'pool_crypto.py', 'pool_tool.py', 'store_index.py')
# the files of the folder poolmgr/ that go into the zip (the top level only)
POOLMGR_EXT = ('.py', '.md', '.json')
CHANNEL = 'channel.json'
POOLMGR_PREFIX = 'poolmgr/'
REPO_RE = re.compile(r'^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')


class PackError(Exception):
    pass


def full_image(folder, man):
    """The parts of one kind at their offsets; the gaps are 0xFF (as esptool merge_bin)."""
    end = max(f['offset'] + f['size'] for f in man['files'])
    img = bytearray(b'\xff' * end)
    for f in man['files']:
        with open(os.path.join(folder, f['path']), 'rb') as h:
            data = h.read()
        if len(data) != f['size'] or hashlib.sha256(data).hexdigest() != f['sha256']:
            raise PackError('%s/%s is not the file of its manifest' % (man['kind'], f['path']))
        img[f['offset']:f['offset'] + len(data)] = data
    return bytes(img)


FLASH_INDEX = 'tfn-firmware.json'


def web_files(version, kind, folder, man, app_name, out):
    """The files of the web flasher of one kind: the parts of the manifest as release files and the manifest with the
    names of the release files. Returns the names (the app is the release file app_name, already in out)."""
    names = []
    rel = dict(man)
    rel['files'] = []
    for f in man['files']:
        with open(os.path.join(folder, f['path']), 'rb') as h:
            data = h.read()
        if len(data) != f['size'] or hashlib.sha256(data).hexdigest() != f['sha256']:
            raise PackError('%s/%s is not the file of its manifest' % (kind, f['path']))
        if f['name'] == 'app':
            name = app_name
        else:
            if not re.match(r'^[a-z0-9-]+$', f['name']):
                raise PackError('%s: the part name "%s" cannot be a file name' % (kind, f['name']))
            name = 'tfn-os-%s-%s-%s.bin' % (version, kind, f['name'])
            with open(os.path.join(out, name), 'wb') as h:
                h.write(data)
            names.append(name)
        e = dict(f)
        e['path'] = name
        rel['files'].append(e)
    mname = 'tfn-os-%s-%s-manifest.json' % (version, kind)
    with open(os.path.join(out, mname), 'w', encoding='utf-8', newline='\n') as h:
        h.write(json.dumps(rel, indent=1) + '\n')
    return names + [mname]


def flash_index(version, kinds):
    """tfn-firmware.json: the index of the web flasher for releases/latest/download/ (see the top)."""
    return {'format': 1, 'release': version,
            'kinds': {k: {'version': version,
                          'manifest': '../../download/v%s/tfn-os-%s-%s-manifest.json' % (version, version, k)}
                      for k in sorted(kinds)}}


def zip_folder(src, dst, prefix):
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
        for d, dirs, files in os.walk(src):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(d, f)
                info = zipfile.ZipInfo(prefix + os.path.relpath(p, src).replace('\\', '/'), ZIP_TIME)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                with open(p, 'rb') as h:
                    z.writestr(info, h.read())


def key_entry(text):
    pt = store_index.parse_pub(text)
    return {'pubkey': pt.hex(), 'key_id': store_index.key_id(pt)}


def shared_sources(tree):
    """{name: path} of each module of SHARED in the tree: sdk/tools/ (the public tree), else manager/ (the private
    tree keeps the pool modules and fw_sign.py there)."""
    out = {}
    for n in SHARED:
        for d in (os.path.join(tree, 'sdk', 'tools'), os.path.join(tree, 'manager')):
            if os.path.isfile(os.path.join(d, n)):
                out[n] = os.path.join(d, n)
                break
        else:
            raise PackError('the shared module %s is not in sdk/tools/ or manager/ of %s' % (n, tree))
    return out


def poolmgr_files(folder):
    """The files of the folder poolmgr/ that go into the zip: the top-level files with an extension of POOLMGR_EXT."""
    if not os.path.isfile(os.path.join(folder, 'poolmgr.py')):
        raise PackError('%s is not the folder poolmgr/ (no poolmgr.py)' % folder)
    names = sorted(n for n in os.listdir(folder)
                   if os.path.isfile(os.path.join(folder, n)) and n.endswith(POOLMGR_EXT))
    if CHANNEL in names:
        raise PackError('%s/%s is in the tree: the release makes the channel' % (folder, CHANNEL))
    both = sorted(set(names) & set(SHARED))
    if both:
        raise PackError('%s has a file with the name of a shared module: %s' % (folder, ', '.join(both)))
    return names


def channel(version, store_pub, ota_pubs, pages, repo, kinds):
    """The channel of the pool manager (poolmgr/channel.example.json has the form)."""
    if not re.match(r'^https://[^/\s]+(/\S*)?$', pages or ''):
        raise PackError('--pages "%s" is not an https:// URL' % pages)
    if not REPO_RE.match(repo or ''):
        raise PackError('--repo "%s" is not <owner>/<name>' % repo)
    return {'format': 1, 'pages': pages.rstrip('/'), 'repo': repo,
            'store_pub': store_index.parse_pub(store_pub).hex(),
            'ota_pub': [store_index.parse_pub(k).hex() for k in ota_pubs if k],
            'release': version, 'kinds': sorted(kinds)}


def channel_bytes(ch):
    return (json.dumps(ch, indent=1) + '\n').encode('utf-8')


def poolmgr_zip(folder, tree, chan, dst):
    """The zip of the pool manager: poolmgr/<the files of folder>, poolmgr/<the copies of SHARED>,
    poolmgr/channel.json (the bytes chan). Returns the names in the zip."""
    files = [(n, os.path.join(folder, n)) for n in poolmgr_files(folder)]
    files += sorted(shared_sources(tree).items())
    names = []
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, p in files + [(CHANNEL, None)]:
            info = zipfile.ZipInfo(POOLMGR_PREFIX + n, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            if p is None:
                data = chan
            else:
                with open(p, 'rb') as h:
                    data = h.read()
            z.writestr(info, data)
            names.append(POOLMGR_PREFIX + n)
    return names


def pack(version, firmware, store, sdk, out, store_pub, ota_pub, ota_pub2='', poolmgr='', pages='', repo=''):
    if os.path.exists(out) and os.listdir(out):
        raise PackError('the output folder %s is not empty' % out)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(firmware, 'index.json'), encoding='utf-8') as f:
        idx = json.load(f)
    names = []
    for kind, e in sorted(idx['kinds'].items()):
        with open(os.path.join(firmware, e['manifest']), encoding='utf-8') as f:
            man = json.load(f)
        if man['version'] != version:
            raise PackError('%s: the firmware says version %s, not %s' % (kind, man['version'], version))
        app = next(f for f in man['files'] if f['name'] == 'app')
        ota = 'tfn-os-%s-%s.bin' % (version, kind)
        shutil.copyfile(os.path.join(firmware, kind, app['path']), os.path.join(out, ota))
        full = 'tfn-os-%s-%s-full.bin' % (version, kind)
        with open(os.path.join(out, full), 'wb') as h:
            h.write(full_image(os.path.join(firmware, kind), man))
        names += [ota, full]
        names += web_files(version, kind, os.path.join(firmware, kind), man, ota, out)
    with open(os.path.join(out, FLASH_INDEX), 'w', encoding='utf-8', newline='\n') as h:
        h.write(json.dumps(flash_index(version, idx['kinds']), indent=1) + '\n')
    names.append(FLASH_INDEX)
    sz = 'tfn-store-%s.zip' % version
    zip_folder(store, os.path.join(out, sz), 'store/')
    sk = 'tfn-sdk-%s.zip' % version
    shutil.copyfile(sdk, os.path.join(out, sk))
    keys = {'tfn_keys': 1, 'release': version, 'store': key_entry(store_pub), 'ota': key_entry(ota_pub)}
    if ota_pub2:
        keys['ota_backup'] = key_entry(ota_pub2)
    with open(os.path.join(out, 'tfn-keys.json'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(json.dumps(keys, indent=1) + '\n')
    names += [sz, sk, 'tfn-keys.json']
    if poolmgr:
        tree = os.path.dirname(os.path.abspath(poolmgr))
        ch = channel_bytes(channel(version, store_pub, [ota_pub, ota_pub2], pages, repo, idx['kinds']))
        with open(os.path.join(out, CHANNEL), 'wb') as h:
            h.write(ch)
        pz = 'tfn-poolmgr-%s.zip' % version
        poolmgr_zip(poolmgr, tree, ch, os.path.join(out, pz))
        names += [CHANNEL, pz]
    lines = []
    for n in sorted(names):
        with open(os.path.join(out, n), 'rb') as h:
            lines.append('%s  %s\n' % (hashlib.sha256(h.read()).hexdigest(), n))
    with open(os.path.join(out, 'SHA256SUMS'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(''.join(lines))
    return sorted(names) + ['SHA256SUMS']


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--version', required=True)
    ap.add_argument('--firmware', required=True)
    ap.add_argument('--store', required=True)
    ap.add_argument('--sdk', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--store-pub', required=True)
    ap.add_argument('--ota-pub', required=True)
    ap.add_argument('--ota-pub2', default='')
    ap.add_argument('--poolmgr', default='', help='the folder poolmgr/ of the tree: make channel.json and the zip')
    ap.add_argument('--pages', default='', help='the web site of the release (with --poolmgr)')
    ap.add_argument('--repo', default='', help='<owner>/<name> of the GitHub repo of the releases (with --poolmgr)')
    a = ap.parse_args(argv)
    try:
        names = pack(a.version, a.firmware, a.store, a.sdk, a.out, a.store_pub, a.ota_pub, a.ota_pub2, a.poolmgr,
                     a.pages, a.repo)
    except (PackError, store_index.StoreError, OSError, KeyError, StopIteration) as e:
        print('STOP: %s' % e)
        return 1
    print('RELEASE FILES %s: %s' % (a.out, ', '.join(names)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
