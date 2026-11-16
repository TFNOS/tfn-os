#!/usr/bin/env python3
"""The data packs of the static store: the big files of an app for the SD card (pictures, music), as one folder for
each pack. store_index.py make --packs <folder> puts them into the signed store.

  python store_pack.py make <apps folder> --out <folder> [--only <id>,<id>]
  python store_pack.py list <apps folder>

An app with a data pack has the file pack.json in its folder (next to app.json):

  {
   "tfn_pack": 1,
   "name": "seasons",                       the name of the pack: the store keeps it in packs/<name>/
   "dir": "seasons",                        the folder on the SD card: /sdcard/<dir>/
   "make": {"run": ["tools/bake.py", "--out", "{out}"], "same": ["fs_geo.inc"]},
   "files": [{"from": "sfx", "to": "sfx"}]
  }

  "make"   (optional) a Python script of the app folder that writes the files of the pack into {out}. The tool runs
           it with the Python of this tool, in the app folder. "same": the files of the app folder that the script
           also writes (the tables of the C code): they must not change, else the pack does not fit the code of the
           app and the tool stops.
  "files"  (optional) the files of the app folder that go into the pack: "from" is a folder or a file of the app
           folder, "to" is its place in the pack ("" = the root of the pack).

The output: <out>/<name>/... (the files of each pack) and <out>/packs.json, a list of {"name", "dir", "app"} ("app"
is the id of the app: the "sym" of app.json without "app_"). A file of a pack has a name of the letters A-Z a-z, the
digits, ".", "_" and "-", in folders of the same letters; the files of the folders of git (.gitignore) do not go in.
A file of GitHub Pages can have 100 MB at most: the tool stops at a bigger file. Exit 0 = good, 1 = a fault.
Python 3.9 or later; the "make" scripts can need more packages (FOUR SEASONS: numpy and Pillow).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

PACK_FORMAT = 1
NAME_RX = re.compile(r'^[a-z0-9_]{1,23}$')
PART_RX = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')
FILE_MAX = 100 * 1000 * 1000  # (the limit of one file of GitHub Pages)
SKIP = ('.gitignore', '.gitattributes')


class PackError(Exception):
    pass


def sha_of(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def read_spec(folder):
    """The pack.json of an app folder and the id of the app; None when the folder has no pack.json."""
    p = os.path.join(folder, 'pack.json')
    if not os.path.isfile(p):
        return None
    try:
        spec = json.load(open(p, encoding='utf-8'))
        app = json.load(open(os.path.join(folder, 'app.json'), encoding='utf-8'))
    except (OSError, ValueError) as e:
        raise PackError('%s: %s' % (folder, e))
    where = os.path.basename(os.path.abspath(folder)) + '/pack.json'
    if spec.get('tfn_pack') != PACK_FORMAT:
        raise PackError('%s: "tfn_pack" is not %d' % (where, PACK_FORMAT))
    bad = sorted(set(spec) - {'tfn_pack', 'name', 'dir', 'make', 'files', 'about'})
    if bad:
        raise PackError('%s: unknown keys %s' % (where, ', '.join(bad)))
    for k in ('name', 'dir'):
        if not NAME_RX.match(str(spec.get(k, ''))):
            raise PackError('%s: "%s" must be 1-23 of a-z 0-9 _' % (where, k))
    sym = str(app.get('sym', ''))
    if not sym.startswith('app_'):
        raise PackError('%s: app.json has no "sym"' % where)
    if not spec.get('make') and not spec.get('files'):
        raise PackError('%s: no "make" and no "files"' % where)
    return spec, sym[4:]


def walk(root):
    """The files below root as relative paths with "/" (sorted)."""
    out = []
    for d, dirs, files in os.walk(root):
        dirs.sort()
        for f in sorted(files):
            out.append(os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/'))
    return out


def copy_files(folder, spec, dst, where):
    for e in spec.get('files', []):
        src = os.path.normpath(os.path.join(folder, e.get('from', '')))
        to = e.get('to', '')
        if os.path.relpath(src, folder).startswith('..') or (to and not all(PART_RX.match(p) for p in to.split('/'))):
            raise PackError('%s: the file entry %s is not a path in the app folder' % (where, json.dumps(e)))
        if os.path.isfile(src):
            pairs = [(src, os.path.basename(src))]
        elif os.path.isdir(src):
            pairs = [(os.path.join(src, *r.split('/')), r) for r in walk(src)]
        else:
            raise PackError('%s: %s is not in the app folder' % (where, e.get('from')))
        for s, r in pairs:
            if os.path.basename(r) in SKIP:
                continue
            t = os.path.join(dst, *(to.split('/') if to else []), *r.split('/'))
            os.makedirs(os.path.dirname(t), exist_ok=True)
            shutil.copyfile(s, t)


def make_one(folder, spec, out):
    where = os.path.basename(os.path.abspath(folder)) + '/pack.json'
    dst = os.path.join(out, spec['name'])
    if os.path.exists(dst):
        raise PackError('%s: the pack %s is there already (two apps with one pack name?)' % (where, spec['name']))
    os.makedirs(dst)
    mk = spec.get('make')
    if mk:
        run = mk.get('run') or []
        if not run or not run[0].endswith('.py'):
            raise PackError('%s: "make" needs "run": [<a Python script of the app folder>, <args>]' % where)
        same = {f: sha_of(os.path.join(folder, f)) for f in mk.get('same', []) if os.path.isfile(os.path.join(folder, f))}
        if len(same) != len(mk.get('same', [])):
            raise PackError('%s: a file of "same" is not in the app folder' % where)
        cmd = [sys.executable] + [a.replace('{out}', os.path.abspath(dst)) for a in run]
        print('%s: %s' % (spec['name'], ' '.join(cmd[1:])))
        r = subprocess.run(cmd, cwd=folder)
        if r.returncode:
            raise PackError('%s: the script %s stopped with the code %d' % (where, run[0], r.returncode))
        for f, h in same.items():
            if sha_of(os.path.join(folder, f)) != h:
                raise PackError('%s: the script changed %s: the pack does not fit the code of the app (make the '
                                'tables again and commit them)' % (where, f))
    copy_files(folder, spec, dst, where)
    files = walk(dst)
    if not files:
        raise PackError('%s: the pack %s has no files' % (where, spec['name']))
    for r in files:
        if not all(PART_RX.match(p) for p in r.split('/')):
            raise PackError('%s: the file name %s has other characters than A-Z a-z 0-9 . _ -' % (where, r))
        n = os.path.getsize(os.path.join(dst, *r.split('/')))
        if n > FILE_MAX:
            raise PackError('%s: %s has %d bytes: a file of GitHub Pages has 100 MB at most' % (where, r, n))
    return files


def find(apps, only=()):
    """[(app folder, spec, app id)] of each app with a pack.json."""
    out = []
    for name in sorted(os.listdir(apps)):
        folder = os.path.join(apps, name)
        if not os.path.isdir(folder):
            continue
        got = read_spec(folder)
        if got and (not only or got[1] in only):
            out.append((folder, got[0], got[1]))
    return out


def make(apps, out, only=()):
    if os.path.exists(out) and os.listdir(out):
        raise PackError('the output folder %s is not empty' % out)
    os.makedirs(out, exist_ok=True)
    meta, total = [], 0
    for folder, spec, pid in find(apps, only):
        files = make_one(folder, spec, out)
        size = sum(os.path.getsize(os.path.join(out, spec['name'], *r.split('/'))) for r in files)
        total += size
        meta.append({'name': spec['name'], 'dir': spec['dir'], 'app': pid})
        print('PACK %s: %d files, %d bytes, /sdcard/%s/ for the app %s' % (spec['name'], len(files), size,
                                                                           spec['dir'], pid))
    with open(os.path.join(out, 'packs.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(meta, f, indent=1)
        f.write('\n')
    print('PACKS MADE: %d packs, %d bytes -> %s' % (len(meta), total, out))
    return meta


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('make', help='make the data packs of the apps')
    m.add_argument('apps', help='the folder of the app folders (apps/)')
    m.add_argument('--out', required=True)
    m.add_argument('--only', default='', help='only the packs of these app ids (a list with commas)')
    ls = sub.add_parser('list', help='the apps with a data pack')
    ls.add_argument('apps')
    a = ap.parse_args(argv)
    try:
        if a.cmd == 'make':
            make(a.apps, a.out, tuple(x for x in a.only.split(',') if x))
        else:
            for folder, spec, pid in find(a.apps):
                print('%-12s %-12s /sdcard/%s/ (%s)' % (pid, spec['name'], spec['dir'], os.path.basename(folder)))
    except PackError as e:
        print('PACK FAULT: %s' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
