#!/usr/bin/env python3
"""The check of a public release of TFN OS as a stranger sees it: the web site (the install page, the firmware files,
the store) and the files of the GitHub release. It needs no file of the TFN OS repo: only the files of the release,
this tool and its neighbours store_index.py and fw_sign.py (they are in the SDK package of the release).

  python release_check.py --site <URL or folder> [--release <folder>] [--store-pub <key>] [--ota-pub <key>]
                          [--node node] [--tree <folder>]

  --site       the web site (for example https://<owner>.github.io/tfn-os/) or a folder with the same files
  --release    a folder with the downloaded files of the GitHub release (SHA256SUMS, tfn-keys.json, the firmware ...)
  --store-pub  the public store key (a PEM file, a file with the hex text, or 130 hex digits); else the key of
               tfn-keys.json of the release
  --ota-pub    the public release key of the signed OTA (the same forms); else the key of tfn-keys.json
  --node       the Node.js program: the check runs the logic of the install page (js/flasher/core.js of the site) on
               each firmware manifest. "" = no Node step.
  --tree       the TFN OS tree of the release (the CI job of the release): each file of the pool manager zip must be
               byte-equal to its source in the tree. With no tree, the check compares the shared modules with the
               copies in the SDK package of the release.

The checks:
  1. the site: each page, script, style sheet, font and picture that the pages and the scripts of js/ name is there;
  2. the firmware: firmware/index.json has each board kind of js/boards.js with a web install; each manifest has the
     size and the SHA-256 of each file, and buildPlan of the page makes an INSTALL and an UPDATE plan with no fault;
  3. the store: the checks of store_index.py verify (the signature of the index, each file, each app signature);
  4. the release: SHA256SUMS; the OTA file of each kind is the app of the site and has a good signature (the check of
     the board, fw_sign.py); the full image holds the same files at their offsets; the store zip is the store of the
     site;
  5. the pool manager: channel.json has the keys of tfn-keys.json (and of --store-pub / --ota-pub) and the kinds of
     the firmware; tfn-poolmgr-<version>.zip has the program, the shared modules (byte-equal to the tree, or to the
     SDK package) and the same channel.json; the unpacked zip starts with no repo near it ("poolmgr.py --help").
Exit 0 = all good (RELEASE CHECK OK); 1 = a fault (each fault has a line FAULT). Python 3.9 or later, and the package
"cryptography" or the openssl program (store_index.py uses the program when the package is not there).
"""
import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import store_index  # noqa: E402

import fw_sign  # noqa: E402

PAGES = ('index.html', 'install.html', 'store.html', 'licences.html')
LINK_RX = re.compile(r'''(?:src|href)\s*=\s*"([^"]+)"''')
# (an import or export statement at the start of a line: "import x from '...'", "import '...'", "export ... from")
IMPORT_RX = re.compile(r'''^import\b[^;'"()]*?['"]([^'"]+)['"]|^export\b[^;'"(){}=]*?\bfrom\s*['"]([^'"]+)['"]''',
                       re.M)
CSS_URL_RX = re.compile(r'''url\(\s*['"]?([^'")]+)['"]?\s*\)''')
NODE_SCRIPT = r'''
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
const [root, mansPath] = process.argv.slice(2);
const core = await import(pathToFileURL(root + '/js/flasher/core.js').href);
const boards = await import(pathToFileURL(root + '/js/boards.js').href);
const mans = JSON.parse(fs.readFileSync(mansPath, 'utf8'));
const out = {};
for (const [kind, man] of Object.entries(mans)) {
  const b = boards.boardByKind(kind);
  const errs = [];
  if (!b) {
    out[kind] = ['the page does not know the kind'];
    continue;
  }
  const usb = b.usb[0];
  const d = core.decide(null, { chip: b.chip, flashMB: b.flashMB, vid: usb.vid, pid: usb.pid });
  if (!d.candidates.some((c) => c.kind === kind)) {
    errs.push('a stock board of this kind gets no offer of the page');
  }
  for (const mode of core.MODES) {
    const p = core.buildPlan(man, mode, b);
    errs.push(...p.errors.map((e) => mode + ': ' + e));
    if (!p.writes.length) {
      errs.push(mode + ': the plan writes nothing');
    }
  }
  out[kind] = errs;
}
console.log(JSON.stringify(out));
'''
# the logic of the store page (js/store/core.js of the site) on the store index: the form and the signature
NODE_STORE = r'''
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
const [root, idxPath, sigPath] = process.argv.slice(2);
const store = await import(pathToFileURL(root + '/js/store/core.js').href);
const data = fs.readFileSync(idxPath);
const index = JSON.parse(data.toString('utf8'));
const errs = store.checkIndex(index);
if (!(await store.verifySig(new Uint8Array(data), new Uint8Array(fs.readFileSync(sigPath)), index.pubkey))) {
  errs.push('the page finds the signature of the index not good');
}
console.log(JSON.stringify(errs));
'''


class Check:
    def __init__(self):
        self.faults = []
        self.notes = []

    def fault(self, text):
        self.faults.append(text)
        print('FAULT ' + text)

    def ok(self, text):
        print('ok    ' + text)


class Source:
    """The files of a site: a URL or a folder. get(rel) -> bytes, or None when the file is not there."""

    def __init__(self, base):
        self.url = bool(re.match(r'^https?://', base))
        self.base = base.rstrip('/') + '/' if self.url else base
        self.cache = {}

    def get(self, rel):
        if rel in self.cache:
            return self.cache[rel]
        data = None
        if self.url:
            try:
                with urllib.request.urlopen(self.base + urllib.parse.quote(rel), timeout=60) as r:
                    data = r.read()
            except urllib.error.HTTPError as e:
                if e.code != 404:
                    raise
        else:
            p = os.path.join(self.base, *store_index.safe_rel(rel).split('/'))
            if os.path.isfile(p):
                with open(p, 'rb') as f:
                    data = f.read()
        self.cache[rel] = data
        return data


def resolve(base_rel, link):
    """The site path of a link in the file base_rel ('' for a link to another site or to the same page)."""
    link = link.split('#', 1)[0].split('?', 1)[0]
    if not link or re.match(r'^[a-z][a-z0-9+.-]*:', link, re.I) or link.startswith('//'):
        return ''
    if link.startswith('/'):
        return ''
    parts = base_rel.split('/')[:-1]
    for p in link.split('/'):
        if p == '..':
            if not parts:
                return ''
            parts.pop()
        elif p not in ('', '.'):
            parts.append(p)
    return '/'.join(parts)


def links_of(rel, data):
    text = data.decode('utf-8', 'replace')
    if rel.endswith('.html'):
        found = LINK_RX.findall(text)
    elif rel.endswith('.css'):
        found = CSS_URL_RX.findall(text)
    elif rel.endswith('.js') and rel.startswith('js/'):
        found = [a or b for a, b in IMPORT_RX.findall(text)]
    else:
        found = []
    return [r for r in (resolve(rel, x) for x in found) if r]


def check_site(site, chk, mirror):
    """Step 1: the pages and each file that they name. The files go into the folder mirror."""
    todo, seen = list(PAGES), set()
    while todo:
        rel = todo.pop(0)
        if rel in seen:
            continue
        seen.add(rel)
        data = site.get(rel)
        if data is None:
            chk.fault('the site has no %s' % rel)
            continue
        p = os.path.join(mirror, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)
        todo += [x for x in links_of(rel, data) if x not in seen]
    chk.ok('the site: %d files (the pages %s and each file that they name)' % (len(seen), ', '.join(PAGES)))


def web_kinds(mirror):
    """The kinds of js/boards.js with a web install."""
    p = os.path.join(mirror, 'js', 'boards.js')
    if not os.path.isfile(p):
        return []
    text = open(p, encoding='utf-8').read()
    return [k for k, web in re.findall(r"kind: '([a-z0-9]+)',.*?web: (true|false)", text, re.S) if web == 'true']


def check_firmware(site, chk, mirror, node):
    """Step 2. Returns {kind: manifest}."""
    raw = site.get('firmware/index.json')
    if raw is None:
        chk.fault('the site has no firmware/index.json')
        return {}
    idx = json.loads(raw)
    mans = {}
    for kind in web_kinds(mirror):
        if kind not in idx.get('kinds', {}):
            chk.fault('firmware/index.json has no release for the kind %s (the page offers it)' % kind)
    for kind, e in sorted(idx.get('kinds', {}).items()):
        rel = 'firmware/' + store_index.safe_rel(e['manifest'])
        mraw = site.get(rel)
        if mraw is None:
            chk.fault('the site has no %s' % rel)
            continue
        man = json.loads(mraw)
        if man.get('kind') != kind or man.get('version') != e.get('version'):
            chk.fault('%s: the kind or the version is not the one of index.json' % rel)
            continue
        good = True
        for f in man.get('files', []):
            data = site.get('firmware/%s/%s' % (kind, store_index.safe_rel(f['path'])))
            if data is None or len(data) != f['size'] or hashlib.sha256(data).hexdigest() != f['sha256']:
                chk.fault('%s/%s: the file is not there or not the file of the manifest' % (kind, f['path']))
                good = False
        mans[kind] = man
        if good:
            chk.ok('firmware %s %s: %d files, the size and the SHA-256 of each' % (kind, man['version'],
                                                                               len(man['files'])))
    if node and mans:
        with tempfile.TemporaryDirectory() as d:
            mp = os.path.join(d, 'mans.json')
            open(mp, 'w', encoding='utf-8').write(json.dumps(mans))
            try:
                res = node_run(node, NODE_SCRIPT, [mirror, mp])
            except RuntimeError as e:
                res = None
                chk.fault('the logic of the install page did not run: %s' % e)
        if res is not None:
            for kind, errs in sorted(res.items()):
                if errs:
                    chk.fault('the install page, %s: %s' % (kind, '; '.join(errs)))
                else:
                    chk.ok('the install page, %s: a stock board gets the offer; the INSTALL and UPDATE plans' % kind)
    return mans


def node_run(node, script, args):
    """Run a Node script with args; the last line of its output as JSON, or raise RuntimeError."""
    with tempfile.TemporaryDirectory() as d:
        sp = os.path.join(d, 'check.mjs')
        open(sp, 'w', encoding='utf-8').write(script)
        r = subprocess.run([node, sp] + args, capture_output=True, text=True, timeout=120)
    if r.returncode:
        raise RuntimeError(r.stderr.strip()[-400:])
    return json.loads(r.stdout.strip().splitlines()[-1])


def check_store(site, chk, pub, mirror, node):
    """Step 3."""
    def get(rel):
        return site.get('store/' + rel)
    try:
        n, idx = store_index.check_store(get, pub)
    except store_index.StoreError as e:
        chk.fault('the store: %s' % e)
        return None
    chk.ok('the store: %d apps, %d packages, the signatures of the key %s' % (len(idx['apps']), n,
                                                                            store_index.key_id(pub)))
    if node:
        with tempfile.TemporaryDirectory() as d:
            ip, sp = os.path.join(d, 'index.json'), os.path.join(d, 'index.sig')
            open(ip, 'wb').write(get('index.json'))
            open(sp, 'wb').write(get('index.sig'))
            try:
                errs = node_run(node, NODE_STORE, [mirror, ip, sp])
            except RuntimeError as e:
                errs = ['the logic of the store page did not run: %s' % e]
        if errs:
            chk.fault('the store page: %s' % '; '.join(errs))
        else:
            chk.ok('the store page: the form and the signature of the index')
    return idx


def read_sums(folder, chk):
    p = os.path.join(folder, 'SHA256SUMS')
    if not os.path.isfile(p):
        chk.fault('the release has no SHA256SUMS')
        return {}
    sums = {}
    for line in open(p, encoding='utf-8').read().splitlines():
        m = re.match(r'^([0-9a-f]{64})  \*?(\S+)$', line)
        if not m:
            chk.fault('SHA256SUMS: a line that is not "<sha256>  <file>": %r' % line[:80])
            continue
        sums[m.group(2)] = m.group(1)
    for name, want in sorted(sums.items()):
        f = os.path.join(folder, name)
        if not os.path.isfile(f):
            chk.fault('the release has no %s (SHA256SUMS names it)' % name)
        elif hashlib.sha256(open(f, 'rb').read()).hexdigest() != want:
            chk.fault('%s: the SHA-256 is not the one of SHA256SUMS' % name)
    extra = sorted(set(os.listdir(folder)) - set(sums) - {'SHA256SUMS'})
    if extra:
        chk.fault('SHA256SUMS does not name %s' % ', '.join(extra))
    if sums:
        chk.ok('SHA256SUMS: %d files' % len(sums))
    return sums


def check_full(full, man):
    """The faults of a full image (esptool merge_bin) against the files of the manifest."""
    out = []
    for f in man['files']:
        part = full[f['offset']:f['offset'] + f['size']]
        if f['name'] == 'otadata':
            if any(b != 0xFF for b in part):
                out.append('the otadata region is not blank')
        elif hashlib.sha256(part).hexdigest() != f['sha256']:
            out.append('%s at 0x%x is not the file of the site' % (f['name'], f['offset']))
    return out


def check_release(folder, site, chk, mans, ota_pub, store_idx):
    """Step 4."""
    names = set(read_sums(folder, chk))
    for kind, man in sorted(mans.items()):
        ver = man['version']
        app = next((f for f in man['files'] if f['name'] == 'app'), None)
        ota = 'tfn-os-%s-%s.bin' % (ver, kind)
        if ota not in names:
            chk.fault('the release has no %s' % ota)
            continue
        data = open(os.path.join(folder, ota), 'rb').read()
        if not app or hashlib.sha256(data).hexdigest() != app['sha256']:
            chk.fault('%s is not the app of the install page' % ota)
        if ota_pub:
            try:
                code, kid, sv = fw_sign.check(data, [ota_pub], kind, None, 0, verify=store_index.verify)
            except store_index.StoreError as e:
                code, kid, sv = str(e), None, None
            if code != 'ok':
                chk.fault('%s: the OTA check of the board says %s' % (ota, code))
            else:
                chk.ok('%s: the board check says ok (key %s, sec_ver %d)' % (ota, kid, sv))
        full = 'tfn-os-%s-%s-full.bin' % (ver, kind)
        if full in names:
            errs = check_full(open(os.path.join(folder, full), 'rb').read(), man)
            for e in errs:
                chk.fault('%s: %s' % (full, e))
            if not errs:
                chk.ok('%s: the same files at the same offsets as the install page' % full)
        else:
            chk.fault('the release has no %s' % full)
    zips = sorted(n for n in names if re.match(r'^tfn-store-.+\.zip$', n))
    if not zips:
        chk.fault('the release has no store zip (tfn-store-<version>.zip)')
    elif store_idx is not None:
        with zipfile.ZipFile(io.BytesIO(open(os.path.join(folder, zips[0]), 'rb').read())) as z:
            same = all(z.read('store/' + f) == site.get('store/' + f) for f in ('index.json', 'index.sig'))
        if same:
            chk.ok('%s: the same store index as the site' % zips[0])
        else:
            chk.fault('%s: the store index is not the one of the site' % zips[0])
    if not any(re.match(r'^tfn-sdk-.+\.zip$', n) for n in names):
        chk.fault('the release has no SDK package (tfn-sdk-<version>.zip)')
    return names


POOLMGR_NEED = ('poolmgr.py', 'pm_shared.py', 'channel.json')
POOLMGR_SHARED = ('fw_sign.py', 'pool.py', 'pool_crypto.py', 'pool_tool.py', 'store_index.py')
KEY_HEX_RX = re.compile(r'^04[0-9a-f]{128}$')


def channel_faults(ch, keys, store_pub, ota_pub, kinds):
    """The faults of channel.json against tfn-keys.json of the release, the keys of the person and the firmware."""
    out = []
    if not isinstance(ch, dict) or ch.get('format') != 1:
        return ['channel.json: not the format 1']
    if not re.match(r'^https://[^/\s]+(/\S*)?$', str(ch.get('pages', ''))) or str(ch['pages']).endswith('/'):
        out.append('channel.json: "pages" is not an https:// URL with no "/" at the end')
    if not re.match(r'^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$', str(ch.get('repo', ''))):
        out.append('channel.json: "repo" is not <owner>/<name>')
    sp, op = ch.get('store_pub'), ch.get('ota_pub')
    if not isinstance(sp, str) or not KEY_HEX_RX.match(sp):
        out.append('channel.json: "store_pub" is not a public key (130 hex digits)')
    if not isinstance(op, list) or not op or not all(isinstance(k, str) and KEY_HEX_RX.match(k) for k in op):
        out.append('channel.json: "ota_pub" is not a list of public keys')
        op = []
    want_ota = [keys[n]['pubkey'] for n in ('ota', 'ota_backup') if n in keys]
    if keys and (sp != keys.get('store', {}).get('pubkey') or op != want_ota):
        out.append('channel.json: the keys are not the keys of tfn-keys.json')
    if store_pub is not None and sp != store_pub.hex():
        out.append('channel.json: the store key is not the store key that you gave')
    if ota_pub is not None and ota_pub.hex() not in op:
        out.append('channel.json: the OTA keys do not have the OTA key that you gave')
    if kinds and sorted(ch.get('kinds') or []) != sorted(kinds):
        out.append('channel.json: the kinds %s are not the firmware kinds %s' % (ch.get('kinds'), sorted(kinds)))
    return out


def sdk_tools(folder, names):
    """{name: bytes} of the tools in the SDK package of the release (tfn-sdk/tools/)."""
    zs = sorted(n for n in names if re.match(r'^tfn-sdk-.+\.zip$', n))
    out = {}
    if zs:
        try:
            with zipfile.ZipFile(os.path.join(folder, zs[0])) as z:
                for n in z.namelist():
                    m = re.match(r'^[^/]+/tools/([^/]+\.py)$', n)
                    if m:
                        out[m.group(1)] = z.read(n)
        except zipfile.BadZipFile:
            pass
    return out


def tree_sources(tree):
    """{name in the zip: path} of the tree: the program files of poolmgr/ and the shared modules (release_pack.py
    makes the zip from the same lists)."""
    import release_pack
    srcs = {n: os.path.join(tree, 'poolmgr', n) for n in release_pack.poolmgr_files(os.path.join(tree, 'poolmgr'))}
    srcs.update(release_pack.shared_sources(tree))
    return srcs


def run_poolmgr(files):
    """Unpack the files of the zip into a new folder and run "poolmgr.py --help" there (no repo near it): the
    faults."""
    with tempfile.TemporaryDirectory() as d:
        for n, data in files.items():
            p = os.path.join(d, *n.split('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(data)
        try:
            r = subprocess.run([sys.executable, '-I', os.path.join(d, 'poolmgr', 'poolmgr.py'), '--help'], cwd=d,
                               capture_output=True, text=True, timeout=120)
        except (OSError, subprocess.TimeoutExpired) as e:
            return ['the unpacked pool manager did not run: %s' % e]
    if r.returncode or 'doctor' not in r.stdout:
        return ['the unpacked pool manager does not start (exit %d): %s' % (r.returncode,
                                                                           (r.stderr or r.stdout).strip()[-300:])]
    return []


def check_poolmgr(folder, names, chk, keys, store_pub, ota_pub, kinds, tree):
    """Step 5."""
    zs = sorted(n for n in names if re.match(r'^tfn-poolmgr-.+\.zip$', n))
    if 'channel.json' not in names:
        chk.fault('the release has no channel.json (the channel of the pool manager)')
        return
    try:
        raw = open(os.path.join(folder, 'channel.json'), 'rb').read()
        ch = json.loads(raw)
    except (OSError, ValueError) as e:
        chk.fault('channel.json: %s' % e)
        return
    errs = channel_faults(ch, keys, store_pub, ota_pub, kinds)
    for e in errs:
        chk.fault(e)
    if not errs:
        chk.ok('channel.json: the keys of tfn-keys.json, %d OTA key(s), the kinds %s' % (len(ch['ota_pub']),
                                                                                       ', '.join(ch['kinds'])))
    if not zs:
        chk.fault('the release has no pool manager (tfn-poolmgr-<version>.zip)')
        return
    try:
        with zipfile.ZipFile(os.path.join(folder, zs[0])) as z:
            files = {n: z.read(n) for n in z.namelist() if not n.endswith('/')}
    except (zipfile.BadZipFile, OSError) as e:
        chk.fault('%s: %s' % (zs[0], e))
        return
    bad = [n for n in files if not n.startswith('poolmgr/') or '..' in n.split('/') or '\\' in n or n.count('/') != 1]
    if bad:
        chk.fault('%s: files out of the one folder poolmgr/: %s' % (zs[0], ', '.join(sorted(bad)[:5])))
        return
    have = {n[len('poolmgr/'):]: d for n, d in files.items()}
    miss = [n for n in POOLMGR_NEED + POOLMGR_SHARED if n not in have]
    if miss:
        chk.fault('%s does not have %s' % (zs[0], ', '.join(miss)))
        return
    if have['channel.json'] != raw:
        chk.fault('%s: its channel.json is not the channel.json of the release' % zs[0])
    if tree:
        try:
            srcs = tree_sources(tree)
        except Exception as e:  # (release_pack.PackError: the tree is not complete)
            chk.fault('the tree %s: %s' % (tree, e))
            srcs = {}
        diff = sorted(n for n, p in srcs.items() if have.get(n) != open(p, 'rb').read())
        extra = sorted(set(have) - set(srcs) - {'channel.json'})
        if diff or extra:
            chk.fault('%s is not the tree: %s' % (zs[0], ', '.join(['%s differs' % n for n in diff] +
                                                                  ['%s is not in the tree' % n for n in extra])))
        elif srcs:
            chk.ok('%s: %d files, each byte-equal to the tree' % (zs[0], len(srcs)))
    else:
        tools = sdk_tools(folder, names)
        same = [n for n in POOLMGR_SHARED if n in tools]
        diff = [n for n in same if tools[n] != have[n]]
        if diff:
            chk.fault('%s: %s is not the copy of the SDK package' % (zs[0], ', '.join(diff)))
        elif same:
            chk.ok('%s: %d files; %s = the copies of the SDK package' % (zs[0], len(have), ', '.join(same)))
        else:
            chk.ok('%s: %d files (the SDK package has no shared module to compare; --tree compares all)'
                   % (zs[0], len(have)))
    errs = run_poolmgr(files)
    for e in errs:
        chk.fault('%s: %s' % (zs[0], e))
    if not errs:
        chk.ok('%s: the unpacked program starts with no repo near it' % zs[0])


def release_keys(folder):
    p = os.path.join(folder, 'tfn-keys.json') if folder else ''
    if not p or not os.path.isfile(p):
        return {}
    return json.load(open(p, encoding='utf-8'))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--site', required=True)
    ap.add_argument('--release', default='')
    ap.add_argument('--store-pub', default='')
    ap.add_argument('--ota-pub', default='')
    ap.add_argument('--node', default='node')
    ap.add_argument('--tree', default='')
    a = ap.parse_args(argv)
    chk = Check()
    keys = release_keys(a.release)
    try:
        store_pub = store_index.parse_pub(a.store_pub or keys.get('store', {}).get('pubkey', ''))
    except store_index.StoreError as e:
        store_pub = None
        chk.fault('no store key: %s' % e)
    ota_pub = None
    if a.ota_pub or keys.get('ota'):
        try:
            ota_pub = store_index.parse_pub(a.ota_pub or keys['ota']['pubkey'])
        except (store_index.StoreError, KeyError) as e:
            chk.fault('no OTA key: %s' % e)
    if not (a.store_pub and (a.ota_pub or not a.release)):
        print('NOTE: a key came from tfn-keys.json of the release itself: compare its key ID with the key ID that '
              'the project publishes')
    site = Source(a.site)
    with tempfile.TemporaryDirectory() as mirror:
        check_site(site, chk, mirror)
        mans = check_firmware(site, chk, mirror, a.node)
        store_idx = check_store(site, chk, store_pub, mirror, a.node) if store_pub else None
    if a.release:
        names = check_release(a.release, site, chk, mans, ota_pub, store_idx)
        check_poolmgr(a.release, names, chk, keys, store_pub, ota_pub, sorted(mans), a.tree)
    print('RELEASE CHECK %s (%d faults)' % ('OK' if not chk.faults else 'FAILED', len(chk.faults)))
    return 1 if chk.faults else 0


if __name__ == '__main__':
    sys.exit(main())
