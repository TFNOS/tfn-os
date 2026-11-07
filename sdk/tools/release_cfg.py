#!/usr/bin/env python3
"""The settings of a public release build (.github/workflows/release.yml).

  release_cfg.py header --out main/tfn_release_keys.h --store-pub <hex> --ota-pub <hex> [--ota-pub2 <hex>]
      Write the public keys of the release into a header. main/tfn_defaults.h reads it when it exists (git does not
      track it). Each key: 130 hex digits that start with 04 (an uncompressed P-256 point).
  release_cfg.py force <sdkconfig> --boards boards.json --ota-mode enforce|report
      Put the force_cfg values of boards.json, the release_cfg values of boards.json and the OTA mode into the
      sdkconfig of a build (after "idf.py reconfigure", before "idf.py build").
  release_cfg.py check <build>/config/sdkconfig.h --ota-mode enforce|report [--boards boards.json]
      Stop (exit 1) when the build does not have the OTA mode (and, with --boards, the release_cfg values).
  release_cfg.py keys <image> --ota-pub <hex> [--ota-pub2 <hex>] [--store-pub <hex>]
      Stop (exit 1) when the firmware image does not hold the 65 bytes of each key. The board trusts only the keys
      of its image: a build with a wrong key list must stop before the release.

The OTA mode: "enforce" when the release has its OTA key (the secret TFN_OTA_KEY): the board then installs only an
update with a good signature. "report" for a dry run and for a build with no key: an ENFORCE build with no OTA key
stops at the #error of main/ota.c.

ESP-IDF 6 writes the mark "# default:" above a value that came from the Kconfig default, and the build computes such a
value again. So "force" removes the mark above each forced option, or the build puts back the default.
"""
import argparse
import json
import os
import re
import sys

OTA = ('CONFIG_TFN_OTA_SIG_ENFORCE', 'CONFIG_TFN_OTA_SIG_REPORT', 'CONFIG_TFN_OTA_SIG_OFF')
MODES = {'enforce': 'CONFIG_TFN_OTA_SIG_ENFORCE', 'report': 'CONFIG_TFN_OTA_SIG_REPORT'}
KEY_RE = re.compile(r'^04[0-9a-f]{128}$')
MARK = '# default:'


class Fault(Exception):
    pass


def ota_values(mode):
    """The sdkconfig values of an OTA mode: one option of the choice on, the other two off."""
    if mode not in MODES:
        raise Fault('the OTA mode "%s" is not enforce or report' % mode)
    return {k: 'y' if k == MODES[mode] else 'n' for k in OTA}


def board_values(path, block='force_cfg'):
    """The values of a block of boards.json: force_cfg (each build) or release_cfg (a public release build). The keys
    that start with "_" are notes."""
    with open(path, encoding='utf-8') as f:
        d = json.load(f)
    return {k: str(v) for k, v in d.get(block, {}).items() if not k.startswith('_')}


def release_values(path):
    """The release_cfg values of boards.json. An OTA option there is a fault: the OTA mode comes from --ota-mode."""
    v = board_values(path, 'release_cfg')
    bad = sorted(k for k in v if k in OTA)
    if bad:
        raise Fault('boards.json release_cfg has %s: the OTA mode comes from --ota-mode only' % ', '.join(bad))
    return v


def line_of(k, v):
    return '# %s is not set' % k if v == 'n' else '%s=%s' % (k, v)


def name_of(ln):
    """The option of a sdkconfig line and its value ('n' for "is not set"), or ('', '')."""
    s = ln.rstrip('\r')
    if s.startswith('# ') and s.endswith(' is not set'):
        return s[2:-11], 'n'
    if s.startswith('CONFIG_') and '=' in s:
        k, _, v = s.partition('=')
        return k, v
    return '', ''


def force(path, values, add):
    """Put values into the sdkconfig at path. The options of add go into the file also when it does not have them; the
    other options change only when the file has them. Returns the count of changes. The line ends of the file stay."""
    with open(path, 'rb') as f:
        lines = f.read().decode('utf-8').split('\n')
    crlf = any(ln.endswith('\r') for ln in lines)
    n = 0
    seen = set()
    for i, ln in enumerate(lines):
        k, v = name_of(ln)
        if k not in values:
            continue
        seen.add(k)
        if v != values[k]:
            lines[i] = line_of(k, values[k]) + ('\r' if ln.endswith('\r') else '')
            n += 1
    keep = []
    for i, ln in enumerate(lines):
        if ln.rstrip('\r') == MARK and i + 1 < len(lines) and name_of(lines[i + 1])[0] in values:
            n += 1
            continue
        keep.append(ln)
    lines = keep
    more = [line_of(k, values[k]) + ('\r' if crlf else '') for k in add if k not in seen]
    if more:
        while lines and lines[-1].strip() == '':
            lines.pop()
        lines += more + ['']
        n += len(more)
    if n:
        with open(path, 'wb') as f:
            f.write('\n'.join(lines).encode('utf-8'))
    return n


def defines(header):
    """The CONFIG_ names of the sdkconfig.h of a build and their values ("1" for a bool option that is on)."""
    with open(header, encoding='utf-8', errors='replace') as f:
        return dict(re.findall(r'^#define (CONFIG_\w+) (.*?)\s*$', f.read(), re.M))


def defined(header):
    """The CONFIG_ names that the sdkconfig.h of a build defines as 1."""
    return {k for k, v in defines(header).items() if v == '1'}


def check(header, mode, more=None):
    """The faults of the OTA mode of a build, and of the values of more (an empty list = good). A value y = defined as
    1, n = not defined, another value = the same text."""
    d = defines(header)
    out = []
    for k, v in dict(more or {}, **ota_values(mode)).items():
        if v == 'y':
            good = d.get(k) == '1'
        elif v == 'n':
            good = k not in d
        else:
            good = d.get(k) == v
        if not good:
            out.append('%s=%s' % (k, v))
    return out


def image_keys(data, keys):
    """The keys (what, hex) that the image data does not hold as 65 bytes in a row."""
    return [what for what, v in keys if bytes.fromhex(v) not in data]


def key_hex(text, what):
    v = (text or '').strip().lower()
    if not KEY_RE.match(v):
        raise Fault('%s is not 130 hex digits that start with 04 (an uncompressed P-256 point)' % what)
    return v


def c_bytes(v):
    return ', '.join('0x%s' % v[i:i + 2] for i in range(0, len(v), 2))


def header_text(store_pub, ota_pub, ota_pub2=''):
    keys = [('TFN_STORE_PUBKEY_BYTES', key_hex(store_pub, 'the store key')),
            ('TFN_OTA_PUBKEY1_BYTES', key_hex(ota_pub, 'the OTA key'))]
    if ota_pub2:
        keys.append(('TFN_OTA_PUBKEY2_BYTES', key_hex(ota_pub2, 'the second OTA key')))
    out = ['// The public keys of a release build (.github/workflows/release.yml, sdk/tools/release_cfg.py header).',
           '// main/tfn_defaults.h reads this file. The build makes it: git does not track it.', '#pragma once']
    for name, v in keys:
        out += ['#ifndef %s' % name, '#define %s %s' % (name, c_bytes(v)), '#endif']
    return '\n'.join(out) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = p.add_subparsers(dest='cmd', required=True)
    h = sub.add_parser('header')
    h.add_argument('--out', required=True)
    h.add_argument('--store-pub', required=True)
    h.add_argument('--ota-pub', required=True)
    h.add_argument('--ota-pub2', default='')
    f = sub.add_parser('force')
    f.add_argument('sdkconfig')
    f.add_argument('--boards', required=True)
    f.add_argument('--ota-mode', required=True)
    c = sub.add_parser('check')
    c.add_argument('header')
    c.add_argument('--ota-mode', required=True)
    c.add_argument('--boards', default='')
    k = sub.add_parser('keys')
    k.add_argument('image')
    k.add_argument('--ota-pub', required=True)
    k.add_argument('--ota-pub2', default='')
    k.add_argument('--store-pub', default='')
    a = p.parse_args(argv)
    try:
        if a.cmd == 'header':
            text = header_text(a.store_pub, a.ota_pub, a.ota_pub2)
            with open(a.out, 'w', encoding='utf-8', newline='\n') as out:
                out.write(text)
            print('%s: %d keys' % (a.out, text.count('#define ')))
        elif a.cmd == 'force':
            if not os.path.isfile(a.sdkconfig):
                raise Fault('no %s (run "idf.py reconfigure" first)' % a.sdkconfig)
            rel = release_values(a.boards)
            values = dict(board_values(a.boards), **rel)
            values.update(ota_values(a.ota_mode))
            n = force(a.sdkconfig, values, OTA + tuple(sorted(rel)))
            print('%s: OTA mode %s, %d release values, %d changes' % (a.sdkconfig, a.ota_mode, len(rel), n))
        elif a.cmd == 'check':
            if not os.path.isfile(a.header):
                raise Fault('no %s (the build did not end)' % a.header)
            rel = release_values(a.boards) if a.boards else {}
            bad = check(a.header, a.ota_mode, rel)
            if bad:
                raise Fault('the build does not have the OTA mode %s and the release values: %s'
                            % (a.ota_mode, ', '.join(bad)))
            print('%s: OTA mode %s OK, %d release values OK' % (a.header, a.ota_mode, len(rel)))
        else:
            keys = [('the OTA key', key_hex(a.ota_pub, 'the OTA key'))]
            if a.ota_pub2:
                keys.append(('the second OTA key', key_hex(a.ota_pub2, 'the second OTA key')))
            if a.store_pub:
                keys.append(('the store key', key_hex(a.store_pub, 'the store key')))
            with open(a.image, 'rb') as f:
                miss = image_keys(f.read(), keys)
            if miss:
                raise Fault('%s does not hold %s: the build has a wrong key list' % (a.image, ', '.join(miss)))
            print('%s: the image holds %s' % (a.image, ', '.join(w for w, _ in keys)))
    except (Fault, OSError, ValueError) as e:
        print('::error::release_cfg.py %s: %s' % (a.cmd, e))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
