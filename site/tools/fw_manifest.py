#!/usr/bin/env python3
"""The firmware files of the web flasher from an ESP-IDF build folder of one board kind.

  python site/tools/fw_manifest.py kind <kind> --build <build folder> --version V --out <firmware folder>
                                   [--app <signed app file>] [--channel RELEASE]
  python site/tools/fw_manifest.py index --out <firmware folder>

"kind" checks the build and writes <out>/<kind>/ (bootloader.bin, partition-table.bin, app.bin,
ota_data_initial.bin) and <out>/<kind>/manifest.json (the form that buildPlan of site/js/flasher/core.js reads).
The checks:
  - the bootloader and the app are images for the chip of the kind (boards.json); the app says the version V;
  - the bootloader says the flash size of the build (flasher_args.json);
  - the partition table gives the partitions of the CSV file of the kind (boards.json "partition_table");
  - the offsets of the build are the offsets of the table, and the otadata image is blank (all 0xFF);
  - --app (the app with the signature trailer of sdk/tools/fw_sign.py): it starts with the app image of the build.
"index" writes <out>/index.json with each <out>/<kind>/manifest.json.
Python 3.9 or later, the standard library only.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
# the chip names of the site (site/js/boards.js) and the chip IDs of the image header
CHIPS = {'esp32s3': ('ESP32-S3', 9), 'esp32p4': ('ESP32-P4', 18)}
FLASH_SIZES = {0: '1MB', 1: '2MB', 2: '4MB', 3: '8MB', 4: '16MB', 5: '32MB', 6: '64MB', 7: '128MB'}
TYPES = {'app': 0, 'data': 1}
SUBTYPES = {'factory': 0x00, 'ota_0': 0x10, 'ota_1': 0x11, 'ota_2': 0x12, 'ota': 0x00, 'phy': 0x01, 'nvs': 0x02,
            'fat': 0x81, 'spiffs': 0x82}
APP_DESC_MAGIC = 0xABCD5432


class ManifestError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def parse_csv(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for ln in f:
            ln = ln.split('#', 1)[0].strip()
            if not ln:
                continue
            p = [x.strip() for x in ln.split(',')]
            rows.append((p[0], TYPES[p[1]], SUBTYPES[p[2]], int(p[3], 0), int(p[4], 0)))
    return rows


def parse_table(data):
    rows = []
    for i in range(0, len(data), 32):
        e = data[i:i + 32]
        if e[:2] != b'\xaa\x50':
            break
        typ, sub, off, size = struct.unpack('<BBII', e[2:12])
        rows.append((e[12:28].rstrip(b'\0').decode('ascii'), typ, sub, off, size))
    return rows


def check_image(data, what, chip_id):
    """The facts of an ESP image header: the flash mode and the size. Raises ManifestError when it is not one."""
    if len(data) < 64 or data[0] != 0xE9:
        raise ManifestError('%s is not an ESP image' % what)
    got_chip = struct.unpack('<H', data[12:14])[0]
    if got_chip != chip_id:
        raise ManifestError('%s is for the chip id %d, not %d' % (what, got_chip, chip_id))
    return {'mode': data[2], 'size': FLASH_SIZES.get(data[3] >> 4, '?')}


def app_version(data):
    magic, = struct.unpack('<I', data[32:36])
    if magic != APP_DESC_MAGIC:
        raise ManifestError('the app image has no app description')
    return data[48:80].split(b'\0')[0].decode('ascii', 'replace')


def board(kind):
    with open(os.path.join(REPO, 'boards.json'), encoding='utf-8') as f:
        kinds = json.load(f)['kinds']
    if kind not in kinds:
        raise ManifestError('boards.json has no kind %s' % kind)
    return kinds[kind]


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def build_files(kind, build, version, app_file=''):
    """The checked files of one kind: [(name, path, offset, bytes, modes)], the flash settings, the chip name."""
    b = board(kind)
    chip_name, chip_id = CHIPS[b['chip']]
    args = json.loads(read(os.path.join(build, 'flasher_args.json')))
    offs = {v: int(k, 16) for k, v in args['flash_files'].items()}
    image = next((v for v in offs if v.endswith('.bin') and '/' not in v and v != 'ota_data_initial.bin'), None)
    if not image:
        raise ManifestError('%s: flasher_args.json names no app image' % kind)
    plain = read(os.path.join(build, image))
    app = read(app_file) if app_file else plain
    if not app.startswith(plain):
        raise ManifestError('%s: %s does not start with the app image of the build' % (kind, app_file))
    boot = read(os.path.join(build, 'bootloader', 'bootloader.bin'))
    table = read(os.path.join(build, 'partition_table', 'partition-table.bin'))
    otadata = read(os.path.join(build, 'ota_data_initial.bin'))
    head = check_image(boot, kind + ' bootloader', chip_id)
    check_image(plain, kind + ' app', chip_id)
    if app_version(plain) != version:
        raise ManifestError('%s: the app says version %s, not %s' % (kind, app_version(plain), version))
    if head['size'] != args['flash_settings']['flash_size']:
        raise ManifestError('%s: the bootloader says %s flash' % (kind, head['size']))
    want = parse_csv(os.path.join(REPO, b['partition_table']))
    if parse_table(table) != want:
        raise ManifestError('%s: the partition table is not %s' % (kind, b['partition_table']))
    ota = [r for r in want if r[1] == 1 and r[2] == 0][0]
    app0 = [r for r in want if r[1] == 0 and r[2] == 0x10][0]
    if offs['ota_data_initial.bin'] != ota[3] or offs[image] != app0[3] or len(otadata) != ota[4]:
        raise ManifestError('%s: the offsets of the build are not the offsets of the table' % kind)
    if set(otadata) != {0xFF}:
        raise ManifestError('%s: the otadata image is not blank' % kind)
    if len(app) > app0[4]:
        raise ManifestError('%s: the app (%d bytes) is bigger than its slot (%d bytes)' % (kind, len(app), app0[4]))
    files = [
        ('bootloader', 'bootloader.bin', offs['bootloader/bootloader.bin'], boot, ['install']),
        ('partition-table', 'partition-table.bin', offs['partition_table/partition-table.bin'], table, ['install']),
        ('app', 'app.bin', app0[3], app, ['install', 'update']),
        ('otadata', 'ota_data_initial.bin', ota[3], otadata, ['install', 'update']),
    ]
    return files, args['flash_settings'], chip_name


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)


def save_kind(out, kind, version, files, flash, chip, channel='RELEASE', released=0):
    entries = []
    for name, path, off, data, modes in files:
        write_file(os.path.join(out, kind, path), data)
        entries.append({'name': name, 'path': path, 'offset': off, 'size': len(data), 'sha256': sha256(data),
                        'modes': modes})
    man = {'format': 1, 'kind': kind, 'chip': chip, 'version': version, 'channel': channel, 'flash': flash,
           'released': int(released or time.time()), 'files': entries}
    write_file(os.path.join(out, kind, 'manifest.json'), (json.dumps(man, indent=1) + '\n').encode())
    return man


def write_index(out):
    idx = {'format': 1, 'kinds': {}, 'made': int(time.time())}
    for kind in sorted(os.listdir(out)):
        p = os.path.join(out, kind, 'manifest.json')
        if os.path.isfile(p):
            man = json.loads(read(p))
            if man.get('kind') != kind:
                raise ManifestError('%s says the kind %s' % (p, man.get('kind')))
            idx['kinds'][kind] = {'version': man['version'], 'manifest': kind + '/manifest.json'}
    if not idx['kinds']:
        raise ManifestError('no manifest.json in %s' % out)
    write_file(os.path.join(out, 'index.json'), (json.dumps(idx, indent=1) + '\n').encode())
    return idx


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    k = sub.add_parser('kind', help='the files and the manifest of one kind')
    k.add_argument('kind')
    k.add_argument('--build', required=True, help='the ESP-IDF build folder of the kind')
    k.add_argument('--version', required=True, help='the version that the app must say')
    k.add_argument('--out', required=True, help='the firmware folder of the site')
    k.add_argument('--app', default='', help='the signed app file (else the app image of the build)')
    k.add_argument('--channel', default='RELEASE')
    i = sub.add_parser('index', help='index.json of the firmware folder')
    i.add_argument('--out', required=True)
    a = ap.parse_args(argv)
    try:
        if a.cmd == 'kind':
            files, flash, chip = build_files(a.kind, a.build, a.version, a.app)
            man = save_kind(a.out, a.kind, a.version, files, flash, chip, a.channel)
            print('OK %-8s %s  %s' % (a.kind, man['version'], '  '.join('%s@0x%x' % (f['name'], f['offset'])
                                                                     for f in man['files'])))
        else:
            idx = write_index(a.out)
            print('INDEX %s' % ', '.join('%s %s' % (k, v['version']) for k, v in idx['kinds'].items()))
    except (ManifestError, OSError, KeyError, ValueError) as e:
        print('STOP: %s' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
