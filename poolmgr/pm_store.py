"""The store of the pool (design 2.11): the mode (official or local), the mirror of the official store, the routes
of the local store, the pool store key and the pool's own apps.

The two modes:
  official  each member reads the official static store itself (HTTPS; main/tfn_sstore.c). poolmgr sends op=mgr with
            empty values: the board has no manager store, so AUTO = the static store.
  local     each member reads the manager store routes of "serve" on this computer (main/tfn_store.c): op=mgr with
            lan=http://<the address of this computer toward the board>:<port>. The routes give a checked mirror of
            the official store and the pool's own apps. The board checks the "sig" of each package as always.

The token of op=mgr: the first op=mgr gives the board a random token (newtok=); each later op=mgr must give it
(auth=). The tokens are in pool/store_tok.json (MAC -> token, mode 0600): a backup of the pool folder keeps them.

The files of the store in the data folder:
  cache/store/files/<sha256>        each file of a package, by its SHA-256 (the mirror and the pool's apps)
  cache/store/official/index.json   the index of the official store, written after all checks (index.sig beside it)
  cache/store/official/mirror.json  {"kinds": [...], "release", "t"}: the kinds in the mirror
  cache/store/own.json              the pool's own apps: {id: {kind: {"m": the manifest fields, "files": {...}}}}
  cache/store/kinds.json            the kinds of the members (the last that each member gave)

The checks of "store sync" before the mirror changes: index.sig with the official store key (the openssl program,
fw_sign.verify_raw), the form of index.json (store_index.check_index_form), the key of the index, the size and the
SHA-256 of each file, the facts of each manifest.txt (store_index.check_package), the "sig" of each package. One fault
stops the sync, and the mirror stays as it was.
"""
import base64
import http.client
import json
import os
import re
import secrets
import time
import urllib.error
import urllib.parse
import urllib.request

import pm_board
import pm_data
import pm_net
from pm_data import write_file
from pm_shared import Failed, InputError, channel_hint, fw_sign, pool, pool_tool, store_index

DONE = True

MODES = ('official', 'local')
PROFILE = 'pool'  # the profile name in line 1 of the catalog
OLD_MAX = 64  # a request with no d=1 (an old firmware) gets 64 apps at most (the same limit as the LAN manager, CAT_OLD_MAX)
INDEX_MAX = 8 * 1024 * 1024  # the most bytes of index.json
ALLOW_WAIT = 125  # the seconds to wait for a tap of ALLOW (the board waits 120 s: TFN_PUB_WAIT_MS)
CONSOLE_WAIT = 20  # the seconds to wait for the key after "pub allow" on the console
TIMEOUT = 10
SERVE_FILES = ('manifest.txt', 'app.elf', 'icon.bin', 'sig', 'watch.elf')  # main/tfn_sstore.h TFN_SS_FILE_NAMES
ID_RX = re.compile(r'^[a-z0-9_]{1,23}$')
KIND_RX = re.compile(r'^[a-z0-9]{1,15}$')
SHA_RX = re.compile(r'^[0-9a-f]{64}$')


# ---------------------------------------------------------------- the files of the store
def store_dir(data):
    return os.path.join(data.cache, 'store')


def files_dir(data):
    return os.path.join(store_dir(data), 'files')


def official_dir(data):
    return os.path.join(store_dir(data), 'official')


def read_json(path, default):
    try:
        with open(path, 'rb') as h:
            return json.loads(h.read().decode('utf-8'))
    except (FileNotFoundError, ValueError):
        return default


def write_json(path, value):
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    write_file(path, (json.dumps(value, indent=1, sort_keys=True, ensure_ascii=False) + '\n').encode('utf-8'))


def sha256(b):
    return store_index().sha256(b)


def blob_put(data, b):
    """Keep the bytes in cache/store/files/<sha256>: the SHA-256."""
    h = sha256(b)
    p = os.path.join(files_dir(data), h)
    if not os.path.isfile(p):
        os.makedirs(files_dir(data), mode=0o700, exist_ok=True)
        write_file(p, b)
    return h


def blob_get(data, ent):
    """The bytes of a file entry {"size", "sha256"}, or None when the file is not there or not the same."""
    h = str(ent.get('sha256', ''))
    if not SHA_RX.match(h):
        return None
    try:
        with open(os.path.join(files_dir(data), h), 'rb') as f:
            b = f.read()
    except OSError:
        return None
    if len(b) != ent.get('size') or sha256(b) != h:
        return None
    return b


def blob_has(data, ent):
    return blob_get(data, ent) is not None


def prune(data):
    """Remove the files that neither the mirror nor the pool's apps name. The number of files removed."""
    keep = set()
    idx = read_json(os.path.join(official_dir(data), 'index.json'), {})
    for a in idx.get('apps', []) if isinstance(idx, dict) else []:
        for e in a.get('kinds', {}).values():
            keep.update(v.get('sha256') for v in e.get('files', {}).values())
    for kinds in own_index(data).values():
        for e in kinds.values():
            keep.update(v.get('sha256') for v in e.get('files', {}).values())
    n = 0
    d = files_dir(data)
    for name in os.listdir(d) if os.path.isdir(d) else ():
        if SHA_RX.match(name) and name not in keep:
            os.remove(os.path.join(d, name))
            n += 1
    return n


def own_index(data):
    v = read_json(os.path.join(store_dir(data), 'own.json'), {})
    return v if isinstance(v, dict) else {}


def tokens_file(data):
    return os.path.join(data.pool_dir, 'store_tok.json')


def load_tokens(data):
    """The store tokens of the members (MAC -> token; {} with no file). InputError for a damaged file: the program
    stops with its name, and it does not write over the tokens of the other members."""
    return pm_data.read_strict(tokens_file(data), {}, dict, 'MAC -> store token')


# ---------------------------------------------------------------- the members
def need_pool(data):
    return data.need_pool()


def members_of(data, names):
    """The board members (no manager member): all, or the ones that names give."""
    p = need_pool(data)
    if not names:
        return pool_tool.boards(p)
    out = []
    for w in names:
        try:
            out.append(pool_tool.member_arg(p, w))
        except pool_tool.ToolError as e:
            raise InputError(str(e))
    return out


def call(b, method, path, body=None, ctype='text/plain'):
    """A request to a member with the request header (X-TFN-Auth) and a plain body: the answer as a dict. One more
    try with a new board nonce after a 401 (as pool_tool.Board.signed_post)."""
    for again in (False, True):
        if b.bn is None:
            b.bn = b.nonce()
        req = urllib.request.Request('http://%s%s' % (b.ip, path), data=body, method=method)
        if body is not None:
            req.add_header('Content-Type', ctype)
        if b.password:
            req.add_header('Authorization',
                           'Basic ' + base64.b64encode(('admin:%s' % b.password).encode()).decode())
        req.add_header(pool.HDR, pool.web_header(b.wk, b.bn, b.p.mid(), b.ctr.next(), method, path))
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                text = r.read().decode('utf-8', 'replace')
        except urllib.error.HTTPError as e:
            try:
                ans = json.loads(e.read().decode('utf-8', 'replace'))
            except ValueError:
                ans = {'error': 'HTTP %d' % e.code}
            if e.code == 401 and not again:
                b.bn = None
                continue
            raise pool_tool.Refused(e.code, ans)
        except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
            raise pool_tool.ToolError('no answer from %s: %s' % (b.ip, e))
        try:
            ans = json.loads(text or '{}')
        except ValueError:
            ans = None
        if not isinstance(ans, dict):
            raise pool_tool.NotBoard('%s answers %s %s with no JSON object: it is not a TFN board' % (b.ip, method,
                                                                                                       path))
        return ans


# the faults of one member: the run goes on with the next member
MEMBER_FAULTS = (pool_tool.Refused, pool_tool.ToolError, Failed, ValueError, KeyError)


def finish(rows, bad, what):
    """The rows of a command; when a member failed: print the rows, then Failed (exit 1)."""
    if bad:
        for r in rows:
            print('  '.join('%s=%s' % (k, r[k]) for k in r) if isinstance(r, dict) else r)
        raise Failed('%s: %d member(s) failed' % (what, bad))
    return rows


# ---------------------------------------------------------------- the mode: op=mgr
def lan_url(data, b):
    """http://<the address of this computer toward the board>:<the port of serve>."""
    return 'http://%s:%d' % (pm_net.lan_ip_toward(b.ip), int(data.cfg.get('port') or 47480))


def mgr_send(data, m, lan):
    """op=mgr to one member: lan = the address of the local store, or '' (the official store). Returns the
    message of the board. The token: a new one at the first op=mgr, else auth= the kept one."""
    toks = load_tokens(data)
    mac = m['mac'].upper()
    tok = toks.get(mac, '')
    new = '' if tok else secrets.token_hex(12)
    b = pm_board.board(data, m['id'])
    q = 'op=mgr&lan=%s&vpn=&newtok=%s&auth=%s' % (lan, new, tok)
    ans = call(b, 'POST', '/api/apps?' + q)
    if not ans.get('ok'):
        msg = ans.get('msg', '')
        if 'token' in msg:
            msg += ' (the board has the token of another manager: on the board SETUP > POOL > LEAVE, then adopt)'
        raise Failed(msg or 'the board refused op=mgr')
    if new:
        toks[mac] = new
        os.makedirs(data.pool_dir, mode=0o700, exist_ok=True)
        write_file(tokens_file(data), (json.dumps(toks, indent=1, sort_keys=True) + '\n').encode('utf-8'))
    return ans.get('msg', 'ok')


def set_member(data, member, mode):
    """The store mode of one member (a member dict of the pool, or its name, ID, MAC or address). The other
    commands use it: "remove" sends 'official' before the member leaves. Returns the message of the board."""
    m = member if isinstance(member, dict) else members_of(data, [member])[0]
    if mode == 'official':
        return mgr_send(data, m, '')
    return mgr_send(data, m, lan_url(data, pm_board.board(data, m['id'])))


def set_mode(data, mode):
    """mode 'official' (op=mgr with empty values) or 'local' (op=mgr with lan_url and a new token) to each member."""
    if mode not in MODES:
        raise InputError('the store mode is official or local, not %r' % mode)
    with data.change():
        ms = members_of(data, [])
        rows, bad = [], 0
        for m in ms:
            try:
                b = pm_board.board(data, m['id'])
                lan = lan_url(data, b) if mode == 'local' else ''
                msg = mgr_send(data, m, lan)
                rows.append({'member': m['name'], 'store': mode, 'lan': lan or '-', 'board': msg})
                if lan:
                    data.cfg['lan_url'] = lan
            except MEMBER_FAULTS as e:
                bad += 1
                rows.append({'member': m['name'], 'store': 'FAILED', 'why': str(e)})
        data.cfg['store'] = mode
        data.save_cfg()
        data.log('store %s: %d members, %d failed' % (mode, len(ms), bad))
        if mode == 'local':
            if not os.path.isfile(os.path.join(official_dir(data), 'index.json')):
                rows.append('the mirror is empty: poolmgr store sync')
            rows.append('the boards read the local store of "poolmgr serve" (it must run)')
        return finish(rows, bad, 'store %s' % mode)


# ---------------------------------------------------------------- the mirror: store sync
def store_pub(data):
    ch = data.cfg.get('channel') or {}
    try:
        pt = fw_sign.parse_pub(str(ch.get('store_pub', '')))
    except fw_sign.SignError:
        raise InputError('the channel has no official store key (store_pub): %s' % channel_hint(data))
    pages = str(ch.get('pages', '')).rstrip('/')
    if not pages.startswith(('https://', 'http://')):
        raise InputError('the channel has no pages address (the web site of the releases): %s' % channel_hint(data))
    return pages, pt


def member_kinds(data):
    """The kinds of the members: GET /api/apps of each member that answers, and the kinds that the members gave
    before (cache/store/kinds.json)."""
    path = os.path.join(store_dir(data), 'kinds.json')
    known = read_json(path, {})
    if not isinstance(known, dict):
        known = {}
    for m in pool_tool.boards(need_pool(data)):
        try:
            k = call(pm_board.board(data, m['id']), 'GET', '/api/apps').get('kind', '')
        except MEMBER_FAULTS + (InputError,):
            continue
        if isinstance(k, str) and KIND_RX.match(k):
            known[m['mac'].upper()] = k
    write_json(path, known)
    return sorted(set(known.values()))


def fetch_package(data, pages, pt, a, kind, e):
    """The checked files of one package: each file from the cache (the same SHA-256) or from the pages."""
    si = store_index()
    files = {}
    for f, ent in e['files'].items():
        b = blob_get(data, ent)
        if b is None:
            b = pm_board.http_get('%s/store/%s%s' % (pages, e['path'], f), limit=ent['size'])
            if len(b) != ent['size'] or sha256(b) != ent['sha256']:
                raise Failed('%s/%s: %s is not the file of the index' % (a['id'], kind, f))
        files[f] = b
    try:
        m = si.check_package(a['id'], kind, files)
    except si.StoreError as err:
        raise Failed(str(err))
    if (m['ver'], m['name'], int(m['code'])) != (a['ver'], a['name'], a['code']):
        raise Failed('%s/%s: manifest.txt and index.json do not agree (ver, name, code)' % (a['id'], kind))
    if not fw_sign.verify_raw(pt, files['manifest.txt'], files['sig']):
        raise Failed('%s/%s: sig is not a good signature of manifest.txt' % (a['id'], kind))
    return files


def sync(data):
    """The mirror of the official store into cache/store/official/: the number of the packages."""
    si = store_index()
    need_pool(data)  # (it also reads config.json: the channel)
    pages, pt = store_pub(data)
    with data.change():
        kinds = member_kinds(data)
        raw = pm_board.http_get(pages + '/store/index.json', limit=INDEX_MAX)
        sig = pm_board.http_get(pages + '/store/index.sig', limit=64)
        if len(sig) != 64 or not fw_sign.verify_raw(pt, raw, sig):
            raise Failed('index.sig is not a good signature of index.json by the official store key %s: the '
                         'mirror stays as it was' % fw_sign.key_id(pt).hex())
        try:
            idx = json.loads(raw.decode('utf-8'))
            si.check_index_form(idx)
        except (ValueError, si.StoreError) as e:
            raise Failed('index.json: %s' % e)
        if idx['pubkey'] != pt.hex():
            raise Failed('index.json names the key %s, not the official store key' % idx['key_id'])
        if not kinds:
            kinds = sorted({k for a in idx['apps'] for k in a['kinds']})
        n = 0
        for a in idx['apps']:
            for kind, e in sorted(a['kinds'].items()):
                if kind not in kinds:
                    continue
                for b in fetch_package(data, pages, pt, a, kind, e).values():
                    blob_put(data, b)
                n += 1
        od = official_dir(data)
        os.makedirs(od, mode=0o700, exist_ok=True)
        write_file(os.path.join(od, 'index.sig'), sig)
        write_file(os.path.join(od, 'index.json'), raw)
        write_json(os.path.join(od, 'mirror.json'), {'kinds': kinds, 'release': idx.get('release', ''),
                                                     't': int(time.time())})
        gone = prune(data)
        data.log('store sync: %d packages of %d apps, kinds %s, %d old files removed' %
                 (n, len(idx['apps']), ','.join(kinds), gone))
        return n


# ---------------------------------------------------------------- the routes of the local store
def q1(query, key):
    v = query.get(key, '')
    if isinstance(v, (list, tuple)):
        v = v[0] if v else ''
    return str(v)


def qint(query, key):
    try:
        return int(q1(query, key) or 0)
    except ValueError:
        return 0


def clean(v):
    return str(v).replace('\t', ' ').replace('\n', ' ').replace('\r', ' ')


def packages(data, kind):
    """{id: (the entry of the catalog, the file entries)} of the kind: the mirror, then the pool's apps in their
    place."""
    out = {}
    idx = read_json(os.path.join(official_dir(data), 'index.json'), {})
    mir = read_json(os.path.join(official_dir(data), 'mirror.json'), {})
    if isinstance(idx, dict) and kind in mir.get('kinds', []):
        for a in idx.get('apps', []):
            e = a.get('kinds', {}).get(kind)
            if e:
                out[a['id']] = ({'name': a['name'], 'ver': a['ver'], 'code': a['code'], 'group': a['group'],
                                 'emoji': a.get('emoji', ''), 'desc': a.get('desc', ''),
                                 'desc_he': a.get('desc_he', ''), 'abi': e.get('abi', 0), 'rev': e.get('rev', 0),
                                 'bytes': e.get('bytes', 0), 'ram': e.get('ram', 0), 'perms': e.get('perms', [])},
                                e.get('files', {}))
    for aid, kinds in sorted(own_index(data).items()):
        e = kinds.get(kind)
        if e:
            out[aid] = (e['m'], e['files'])
    return out


def catalog(data, query):
    """The catalog text of main/tfn_store.c mgr_catalog: "TFN1<TAB>pool<TAB>count", then one line for each app:
    id, name, ver, code, group, state, bytes, ram, emoji, perms (+ the descriptions in English and Hebrew with
    d=1). Only the packages of the kind, of the ABI of the board and of its ABI revision or an older one."""
    kind, abi, rev, desc = q1(query, 'kind'), qint(query, 'abi'), qint(query, 'rev'), q1(query, 'd') == '1'
    rows = []
    for aid, (m, _) in packages(data, kind).items():
        if (abi and m.get('abi') and int(m['abi']) != abi) or int(m.get('rev') or 0) > rev:
            continue
        perms = m.get('perms', [])
        cols = [aid, m['name'], m['ver'], m['code'], m['group'], 'available', m.get('bytes', 0), m.get('ram', 0),
                m.get('emoji', ''), ','.join(perms) if isinstance(perms, list) else perms]
        if desc:
            cols += [m.get('desc', ''), m.get('desc_he', '')]
        rows.append('\t'.join(clean(c) for c in cols))
    if not desc:
        rows = rows[:OLD_MAX]
    return ('TFN1\t%s\t%d\n' % (PROFILE, len(rows)) + ''.join(r + '\n' for r in rows)).encode('utf-8')


def route(data, path, query):
    """One request of the local store (serve): (HTTP code, content type, bytes). path = '/api/store/policy',
    '/api/store/catalog' or '/api/store/file'; query = the dict of urllib.parse.parse_qs. Any other path = 404."""
    text = 'text/plain; charset=utf-8'
    nf = (404, 'text/plain', b'not found\n')
    if path == '/api/store/policy':
        return 200, 'text/plain', b'TFNP0\n'  # (nothing newer: a pool has no permission policy)
    kind = q1(query, 'kind')
    if not KIND_RX.match(kind):
        return nf
    if path == '/api/store/catalog':
        return 200, text, catalog(data, query)
    if path != '/api/store/file':
        return nf
    aid, f = q1(query, 'id'), q1(query, 'file')
    if not ID_RX.match(aid) or f not in SERVE_FILES:
        return nf
    pk = packages(data, kind).get(aid)
    ent = pk[1].get(f) if pk else None
    b = blob_get(data, ent) if ent else None
    if b is None:
        return nf
    return 200, 'application/octet-stream', b


# ---------------------------------------------------------------- the pool store key and the pool's own apps
def key_text(pt):
    """The key ID as the board shows it (tfn_pub_id_text): XXXX-XXXX-XXXX-XXXX."""
    h = fw_sign.key_id(pt).hex().upper()
    return '-'.join(h[i:i + 4] for i in range(0, 16, 4))


def store_key(data):
    if not os.path.isfile(data.key_store):
        raise InputError('no pool store key: poolmgr store key')
    return fw_sign.pub_of_pem(data.key_store)


def make_key(data):
    """The pool store key keys/store_pool.pem (0600): its key ID. A key that exists stays (never overwritten)."""
    with data.change():
        need_pool(data)
        if os.path.isfile(data.key_store):
            return key_text(fw_sign.pub_of_pem(data.key_store)) + ' (the key exists: no change)'
        data.private_parts()
        pt = fw_sign.keygen(data.key_store)
        data.log('store key: the pool store key %s' % key_text(pt))
        return key_text(pt)


def pub_name(name):
    """The name of the key on the board (tfn_pub_name_set): 1-15 of A-Z a-z 0-9 space - _ ."""
    s = re.sub(r'[^A-Za-z0-9 ._-]', '-', name or '').strip()[:15].strip()
    return s or 'POOL'


def flag_set(flags):
    """The flags of a key as a set of small words: the board writes "STORE+DEV" (tfn_pub_flags_text)."""
    return {f for f in re.split(r'[+,\s]+', str(flags or '').lower()) if f}


def pub_state(b, kid):
    """The flags of the key on the board ('' = not in the list), and True when it waits for a tap."""
    j = call(b, 'GET', '/api/pub')
    for k in j.get('keys', []):
        if str(k.get('id', '')).upper() == kid:
            return str(k.get('flags', '')), False
    w = j.get('wait') or {}
    return '', str(w.get('id', '')).upper() == kid


def open_console(console):
    return pool_tool.open_console(console) if isinstance(console, str) else console


def trust_one(data, m, pt, kid, console):
    b = pm_board.board(data, m['id'])
    flags, _ = pub_state(b, kid)
    if 'store' in flag_set(flags):
        return 'trusted already'
    if flags:
        raise Failed('the board has the key as %s, not store: remove it on the board first' % flags)
    q = 'op=add&name=%s&flags=store' % urllib.parse.quote(pub_name(need_pool(data).name))
    ans = call(b, 'POST', '/api/pub?' + q, pt.hex().encode('ascii'))
    if not ans.get('ok'):
        raise Failed(ans.get('msg') or 'the board refused the key')
    if console is not None:
        con = open_console(console)
        try:
            con.write_line('pub allow %s' % kid)
        finally:
            con.close()
        wait = CONSOLE_WAIT
    else:
        print('%s: tap ALLOW on the board (the key %s, %d s)' % (m['name'], kid, ALLOW_WAIT))
        wait = ALLOW_WAIT
    end = time.time() + wait
    while True:
        flags, waiting = pub_state(b, kid)
        if 'store' in flag_set(flags):
            return 'trusted'
        if not waiting or time.time() >= end:
            raise Failed('no ALLOW on the board (the key %s)' % kid)
        time.sleep(0.5)


def trust(data, members, console=None):
    """POST /api/pub?op=add&flags=store of the pool store key to the members; console: "pub allow <key ID>"."""
    need_pool(data)
    pt = store_key(data)
    kid = key_text(pt)
    with data.change():
        ms = members_of(data, members)
        if console is not None and len(ms) != 1:
            raise InputError('--console is the console of one board: give one member')
        rows, bad = [], 0
        for m in ms:
            try:
                rows.append({'member': m['name'], 'key': kid, 'result': trust_one(data, m, pt, kid, console)})
            except MEMBER_FAULTS as e:
                bad += 1
                rows.append({'member': m['name'], 'key': kid, 'result': 'FAILED', 'why': str(e)})
        data.log('store trust %s: %d members, %d failed' % (kid, len(ms), bad))
        return finish(rows, bad, 'store trust')


def package_dirs(folder):
    """The package folders under folder: folder itself (it has manifest.txt), <folder>/<kind>/ or
    <folder>/<id>/<kind>/ (the output of tfn_app.py build)."""
    out = []
    for d, subs, files in os.walk(folder):
        if 'manifest.txt' in files:
            out.append(d)
        if os.path.relpath(d, folder).count(os.sep) >= 1:
            subs[:] = []  # (<folder>/<id>/<kind>/ is the deepest place)
    return sorted(out)


def publish(data, folder):
    """Sign a package of "tfn_app.py build" with the pool store key and add it to cache/store/own/."""
    si = store_index()
    need_pool(data)
    pt = store_key(data)
    if not os.path.isdir(folder):
        raise InputError('no package folder %s' % folder)
    dirs = package_dirs(folder)
    if not dirs:
        raise InputError('%s has no package (manifest.txt)' % folder)
    with data.change():
        need_pool(data)
        own = own_index(data)
        rows, new = [], []
        for d in dirs:
            files = {}
            for f in si.FILES:
                p = os.path.join(d, f)
                if os.path.isfile(p) and f != 'sig':
                    with open(p, 'rb') as h:
                        files[f] = h.read()
            m = si.manifest_fields(files['manifest.txt'])
            aid, kind = m.get('id', ''), m.get('kind', '')
            if not ID_RX.match(aid) or not KIND_RX.match(kind):
                raise InputError('%s: manifest.txt has no good id= or kind=' % d)
            files['sig'] = fw_sign.sign_raw(data.key_store, files['manifest.txt'])
            try:
                m = si.check_package(aid, kind, files)
            except si.StoreError as e:
                raise InputError(str(e))
            new.append((aid, kind, m, files))
        for aid, kind, m, files in new:
            fl = {f: {'size': len(b), 'sha256': blob_put(data, b)} for f, b in files.items()}
            own.setdefault(aid, {})[kind] = {
                'm': {'name': m['name'], 'ver': m['ver'], 'code': int(m['code']), 'group': m['group'],
                      'emoji': m.get('emoji', ''), 'desc': m.get('desc', ''), 'desc_he': m.get('desc_he', ''),
                      'abi': int(m['abi']), 'rev': int(m['rev']), 'bytes': int(m['bytes']),
                      'ram': int(m.get('ram', '0') or 0), 'perms': [p for p in m.get('perms', '').split(',') if p]},
                'files': fl}
            rows.append({'app': aid, 'kind': kind, 'ver': m['ver'], 'key': key_text(pt)})
        write_json(os.path.join(store_dir(data), 'own.json'), own)
        data.log('store publish: %s' % ', '.join('%s/%s %s' % (r['app'], r['kind'], r['ver']) for r in rows))
        if data.cfg.get('store') != 'local':
            rows.append('the boards get the pool\'s apps in the local store: poolmgr store local')
        return rows
