#!/usr/bin/env python3
"""The folder of the public web site (GitHub Pages): the pages of site/, the firmware files of a release and the
static store.

  python site/tools/pages_build.py --out <folder> [--firmware <folder>] [--store <folder>] [--golden <folder>]

  --firmware  the firmware folder of site/tools/fw_manifest.py (index.json, <kind>/manifest.json and the files):
              it goes to <out>/firmware/
  --store     the store folder of sdk/tools/store_index.py (index.json, index.sig, apps/): it goes to <out>/store/
  --golden    the golden frames of the simulator (tests/golden): the menu frame of each kind becomes the picture of
              the board on the install page (site/js/boards.js "shot"), as WebP (the package Pillow). A kind with no
              frame has no picture.
The tests, the tools, the package file and the README of site/ do not go to the web site. The output folder must be
empty or not there. Python 3.9 or later.
"""
import argparse
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SITE = os.path.dirname(HERE)
REPO = os.path.dirname(SITE)
# the parts of site/ that stay out of the web site
SKIP_DIRS = {'test', 'tools', 'firmware', 'store', 'node_modules'}
SKIP_FILES = {'package.json', 'README.md', '.gitignore', 'deploy.py', 'deploy.local.json', 'test.html'}
GOLDEN_MENU = 'menu_home.png'


class PagesError(Exception):
    pass


def copy_tree(src, dst, skip_dirs=(), skip_files=()):
    n = 0
    for d, dirs, files in os.walk(src):
        rel = os.path.relpath(d, src)
        dirs[:] = sorted(x for x in dirs if not (rel == '.' and x in skip_dirs) and not x.startswith('.'))
        for f in sorted(files):
            if rel == '.' and f in skip_files:
                continue
            if f.endswith(('.tmp', '.pyc')):
                continue
            os.makedirs(os.path.join(dst, rel), exist_ok=True)
            shutil.copyfile(os.path.join(d, f), os.path.join(dst, rel, f))
            n += 1
    return n


def board_shots(site):
    """{kind: the picture path} of site/js/boards.js."""
    with open(os.path.join(site, 'js', 'boards.js'), encoding='utf-8') as f:
        text = f.read()
    return dict(re.findall(r"kind: '([a-z0-9]+)',.*?shot: '([^']+)'", text, re.S))


def add_shots(out, golden, site):
    """The menu frame of the simulator for each kind that has one. Returns the kinds with a picture."""
    try:
        from PIL import Image
    except ImportError:
        print('NOTE: no Pillow: the board pictures are not made')
        return []
    done = []
    for kind, shot in sorted(board_shots(site).items()):
        src = os.path.join(golden, kind, GOLDEN_MENU)
        if not os.path.isfile(src):
            continue
        dst = os.path.join(out, *shot.split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with Image.open(src) as im:
            im.convert('RGB').save(dst, 'WEBP', lossless=True)
        done.append(kind)
    return done


def build(out, firmware='', store='', golden=''):
    if os.path.exists(out) and os.listdir(out):
        raise PagesError('the output folder %s is not empty' % out)
    n = copy_tree(SITE, out, SKIP_DIRS, SKIP_FILES)
    if not os.path.isfile(os.path.join(out, 'install.html')):
        raise PagesError('site/ has no install.html')
    if firmware:
        if not os.path.isfile(os.path.join(firmware, 'index.json')):
            raise PagesError('the firmware folder %s has no index.json' % firmware)
        n += copy_tree(firmware, os.path.join(out, 'firmware'))
    if store:
        for f in ('index.json', 'index.sig'):
            if not os.path.isfile(os.path.join(store, f)):
                raise PagesError('the store folder %s has no %s' % (store, f))
        n += copy_tree(store, os.path.join(out, 'store'))
    shots = add_shots(out, golden, SITE) if golden else []
    # (GitHub Pages: no Jekyll step, so each file goes out as it is)
    with open(os.path.join(out, '.nojekyll'), 'wb'):
        pass
    return n, shots


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', required=True)
    ap.add_argument('--firmware', default='')
    ap.add_argument('--store', default='')
    ap.add_argument('--golden', default='')
    a = ap.parse_args(argv)
    try:
        n, shots = build(a.out, a.firmware, a.store, a.golden)
    except (PagesError, OSError) as e:
        print('STOP: %s' % e)
        return 1
    print('PAGES %s: %d files; board pictures: %s' % (a.out, n, ', '.join(shots) or 'none'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
