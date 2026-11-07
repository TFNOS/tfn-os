#!/usr/bin/env python3
"""The test of poolmgr/pm_store.py (P6 design 2.11, 3.1): the store of a pool against fake boards and a fake
official store. No real board and no network: all servers are on 127.0.0.1, all keys are TEST keys in a temporary
folder.

The steps: a fake Pages folder made with sdk/tools/store_index.py make (a TEST official store key); "store sync"
(the kinds of the members, the checks, the mirror); a bad index.sig, a store of another key and a changed file = no
change of the mirror; a new release (the old files go); the catalog text against the parser of main/tfn_store.c
(the kind, the ABI, the ABI revision, d=1); the 404 cases of the routes; "store local" (op=mgr with the address of
the local store and a token) and an install of a mirror app on each fake board; "store key", "store publish" (a
package of the form of tfn_app.py build), "store trust" (the console "pub allow", a tap, no tap) and an install of
the pool's apps (a pool app takes the place of an official app); "store official" (op=mgr with empty values); a
board with the token of another manager.

The fake board StoreBoard of tests/unit/poolmgr_fake.py has /api/apps (GET, op=mgr), /api/pub, the console "pub allow"
and a fake client of the manager store.

Usage: python tests/unit/test_poolmgr_store.py
"""
import contextlib
import hashlib
import http.server
import io
import json
import os
import shutil
import sys
import tempfile
import threading
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PM = os.path.join(ROOT, 'poolmgr')
sys.path.insert(0, HERE)
sys.path.insert(0, PM)
import pm_data  # noqa: E402
import pm_store  # noqa: E402
import poolmgr  # noqa: E402
from pm_shared import Failed, InputError, fw_sign, pool_tool  # noqa: E402
from poolmgr_fake import FakeConsole, StoreBoard, key_text, make_pkg, make_store, parse_catalog, serve_folder  # noqa: E402,E501

FAILS = []
CHECKS = [0]


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


def raises(fn, kind, text=''):
    """True when fn() raises kind with text in its message."""
    try:
        fn()
    except kind as e:
        return text in str(e)
    return False


def cli(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = poolmgr.main(list(argv))
    return code, out.getvalue()


def serve_local(data):
    """The local store routes of pm_store.route (the part of "serve" that track E makes)."""
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            u = urllib.parse.urlsplit(self.path)
            code, ctype, body = pm_store.route(data, u.path, urllib.parse.parse_qs(u.query))
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    s = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s


# ---------------------------------------------------------------- the steps
def run(d):
    store_pem = os.path.join(d, 'test_store.pem')
    other_pem = os.path.join(d, 'test_other.pem')
    store_pt = fw_sign.keygen(store_pem)
    fw_sign.keygen(other_pem)

    # the fake official store: alpha (2 kinds), beta (a newer ABI revision), gamma (another ABI), zeta (a kind that
    # no member has)
    pk1 = os.path.join(d, 'pk1')
    make_pkg(pk1, 'alpha', 'lcd349')
    make_pkg(pk1, 'alpha', 'cores3')
    make_pkg(pk1, 'beta', 'lcd349', rev=9)
    make_pkg(pk1, 'gamma', 'cores3', abi=2)
    make_pkg(pk1, 'zeta', 'watcher')
    pages = os.path.join(d, 'pages')
    make_store(pk1, os.path.join(pages, 'store'), store_pem)
    srv, url = serve_folder(pages)

    # the pool: key A, the manager member, two members (lcd349 and cores3) by the console JOIN
    data_dir = os.path.join(d, 'data')
    data = pm_data.Data(data_dir)
    data.make()
    fw_sign.keygen(data.key_a)
    b1 = StoreBoard('s1', bytes.fromhex('0a0000000001'), d, 'lcd349', store_pt)
    b2 = StoreBoard('s2', bytes.fromhex('0a0000000002'), d, 'cores3', store_pt)
    cons = {'fake1': b1, 'fake2': b2}
    pool_tool.open_console = lambda port: FakeConsole(cons[port])
    pool_tool.ALLOW_WAIT = 8
    base = ['--dir', data.pool_dir, '--key', data.key_a]
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        c1 = pool_tool.main(base + ['create', 'TEST STORE POOL', '--manager'])
        c2 = pool_tool.main(base + ['adopt', b1.ip, '--console', 'fake1', '--name', 'ONE'])
        c3 = pool_tool.main(base + ['adopt', b2.ip, '--console', 'fake2', '--name', 'TWO'])
    expect((c1, c2, c3) == (0, 0, 0) and b1.roster and b2.roster, 'the pool and 2 members: %s' % out.getvalue())
    data.cfg['channel'] = {'format': 1, 'pages': url, 'repo': 'test/tfn-os', 'store_pub': store_pt.hex(),
                           'ota_pub': []}
    data.save_cfg()
    data = pm_data.Data(data_dir)
    data.load()

    # ---- store sync
    expect(raises(lambda: pm_store.sync(pm_data.Data(os.path.join(d, 'empty'))), InputError, 'no pool'),
           'sync with no pool: STOP')
    ch = data.cfg['channel']
    data.cfg['channel'] = {}
    expect(raises(lambda: pm_store.sync(data), InputError, 'store_pub'), 'sync with no channel key: STOP')
    data.cfg['channel'] = ch
    n = pm_store.sync(data)
    expect(n == 4, 'sync: 4 packages of the kinds of the members (not zeta of watcher): %d' % n)
    kinds = json.load(open(os.path.join(data.cache, 'store', 'kinds.json')))
    expect(sorted(kinds.values()) == ['cores3', 'lcd349'], 'sync: the kinds of the members %r' % kinds)
    idx1 = open(os.path.join(data.cache, 'store', 'official', 'index.json'), 'rb').read()
    expect(idx1 == open(os.path.join(pages, 'store', 'index.json'), 'rb').read(), 'sync: the index of the mirror')
    expect('sync' in open(data.log_file).read(), 'sync: a line in the log')

    # a bad index.sig, a store of another key, a changed file = no change of the mirror
    bad = os.path.join(d, 'pages_bad')
    shutil.copytree(pages, bad)
    sp = os.path.join(bad, 'store', 'index.sig')
    s = bytearray(open(sp, 'rb').read())
    s[10] ^= 1
    open(sp, 'wb').write(bytes(s))
    srv_bad, url_bad = serve_folder(bad)
    data.cfg['channel']['pages'] = url_bad
    expect(raises(lambda: pm_store.sync(data), Failed, 'index.sig'), 'sync: a bad index.sig = FAILED')
    other = os.path.join(d, 'pages_other')
    make_store(pk1, os.path.join(other, 'store'), other_pem)
    srv_o, url_o = serve_folder(other)
    data.cfg['channel']['pages'] = url_o
    expect(raises(lambda: pm_store.sync(data), Failed, 'index.sig'), 'sync: a store of another key = FAILED')
    pk2 = os.path.join(d, 'pk2')
    shutil.copytree(pk1, pk2)
    make_pkg(pk2, 'alpha', 'lcd349', ver='1.1.0', code=2)
    make_pkg(pk2, 'alpha', 'cores3', ver='1.1.0', code=2)
    p2 = os.path.join(d, 'pages2')
    make_store(pk2, os.path.join(p2, 'store'), store_pem)
    p2bad = os.path.join(d, 'pages2_bad')
    shutil.copytree(p2, p2bad)
    with open(os.path.join(p2bad, 'store', 'apps', 'alpha', 'lcd349', 'app.elf'), 'r+b') as h:
        h.seek(5)
        h.write(b'#')
    srv_2b, url_2b = serve_folder(p2bad)
    data.cfg['channel']['pages'] = url_2b
    expect(raises(lambda: pm_store.sync(data), Failed, 'not the file of the index'),
           'sync: a changed app.elf = FAILED')
    expect(open(os.path.join(data.cache, 'store', 'official', 'index.json'), 'rb').read() == idx1,
           'sync: after the faults the mirror stays as it was')
    data.cfg['channel']['pages'] = url
    data.save_cfg()

    # ---- the routes
    def get(path, **q):
        return pm_store.route(data, path, {k: [str(v)] for k, v in q.items()})
    code, ctype, body = get('/api/store/policy', have=0, abi=1)
    expect(code == 200 and body == b'TFNP0\n', 'route: policy = TFNP0')
    code, _, body = get('/api/store/catalog', mac='X', kind='lcd349', abi=1, rev=5, d=1)
    cat = parse_catalog(body.decode())
    expect(code == 200 and cat and cat[0] == 'pool' and sorted(cat[1]) == ['alpha'],
           'route: the catalog of lcd349 (not beta: a newer ABI revision): %r' % body)
    a = cat[1].get('alpha', []) if cat else []
    expect(a[1:] == ['ALPHA', '1.0.0', '1', 'GAMES', 'available', str(len(open(os.path.join(
        pk1, 'alpha', 'lcd349', 'app.elf'), 'rb').read())), '1234', 'X', 'net', 'The app alpha', 'HE alpha'],
        'route: the 12 fields of an app: %r' % a)
    expect(body.decode().split('\n')[0] == 'TFN1\tpool\t1', 'route: line 1 has the count')
    code, _, body = get('/api/store/catalog', kind='lcd349', abi=1, rev=9)
    cat = parse_catalog(body.decode())
    expect(cat and sorted(cat[1]) == ['alpha', 'beta'] and len(cat[1]['alpha']) == 10,
           'route: rev 9 gets beta; no d=1 = no descriptions')
    code, _, body = get('/api/store/catalog', kind='cores3', abi=1, rev=5, d=1)
    cat = parse_catalog(body.decode())
    expect(cat and sorted(cat[1]) == ['alpha'], 'route: cores3 with ABI 1 (not gamma of ABI 2)')
    code, _, body = get('/api/store/catalog', kind='cores3', abi=2, rev=5, d=1)
    expect(sorted(parse_catalog(body.decode())[1]) == ['gamma'], 'route: cores3 with ABI 2 = gamma')
    code, _, body = get('/api/store/catalog', kind='watcher', abi=1, rev=5)
    expect(code == 200 and body == b'TFN1\tpool\t0\n', 'route: a kind with no mirror = an empty catalog')
    code, _, body = get('/api/store/file', id='alpha', kind='lcd349', file='app.elf')
    expect(code == 200 and body == open(os.path.join(pk1, 'alpha', 'lcd349', 'app.elf'), 'rb').read(),
           'route: a file of the mirror')
    for what, path, q in (('no kind', '/api/store/catalog', {}), ('a bad kind', '/api/store/catalog',
                                                                  {'kind': 'LCD/../x'}),
                          ('another path', '/api/store/upload', {'kind': 'lcd349'}),
                          ('/admin', '/admin', {}),
                          ('a bad id', '/api/store/file', {'id': '../alpha', 'kind': 'lcd349', 'file': 'sig'}),
                          ('a file not of a package', '/api/store/file', {'id': 'alpha', 'kind': 'lcd349',
                                                                          'file': 'index.json'}),
                          ('a path as the file', '/api/store/file', {'id': 'alpha', 'kind': 'lcd349',
                                                                     'file': '../../keys/admin_a.pem'}),
                          ('sigs', '/api/store/file', {'id': 'alpha', 'kind': 'lcd349', 'file': 'sigs'}),
                          ('no watch.elf', '/api/store/file', {'id': 'alpha', 'kind': 'lcd349',
                                                               'file': 'watch.elf'}),
                          ('an app not in the store', '/api/store/file', {'id': 'nothing', 'kind': 'lcd349',
                                                                          'file': 'sig'}),
                          ('a kind not in the mirror', '/api/store/file', {'id': 'zeta', 'kind': 'watcher',
                                                                           'file': 'sig'}),
                          ('beta of cores3', '/api/store/file', {'id': 'beta', 'kind': 'cores3', 'file': 'sig'})):
        code = pm_store.route(data, path, {k: [v] for k, v in q.items()})[0]
        expect(code == 404, 'route: %s = 404 (%d)' % (what, code))

    # ---- store local: op=mgr with the address of the local store, then an install on each board
    local = serve_local(data)
    data.cfg['port'] = local.server_address[1]
    data.save_cfg()
    expect(raises(lambda: pm_store.set_mode(data, 'cloud'), InputError), 'set_mode: a bad mode = STOP')
    rows = pm_store.set_mode(data, 'local')
    lan = 'http://127.0.0.1:%d' % local.server_address[1]
    expect(b1.lan == lan and b2.lan == lan and b1.tok and b2.tok and b1.tok != b2.tok,
           'store local: the address and a token on each board: %r' % rows)
    toks = json.load(open(os.path.join(data.pool_dir, 'store_tok.json')))
    expect(toks == {b1.mac.hex().upper(): b1.tok, b2.mac.hex().upper(): b2.tok}, 'store local: the tokens are kept')
    expect(pm_data.Data(data_dir).load() and json.load(open(data.cfg_file))['store'] == 'local' and
           data.cfg['lan_url'] == lan, 'store local: config.json')
    t1 = b1.tok
    pm_store.set_mode(data, 'local')
    expect(b1.tok == t1, 'store local again: the same token (auth=)')
    ok1, v1 = b1.install('alpha')
    ok2, v2 = b2.install('alpha')
    expect(ok1 and ok2 and v1 == v2 == '1.0.0', 'install of a mirror app on both boards: %s %s' % (v1, v2))
    expect(b1.policy_seen == b'TFNP0\n', 'the board reads the policy TFNP0')
    expect(not b1.install('beta')[0] and not b2.install('gamma')[0], 'no install of a package not for the board')

    # ---- the pool's own apps
    expect(raises(lambda: pm_store.publish(data, pk1), InputError, 'store key'), 'publish with no key: STOP')
    expect(raises(lambda: pm_store.trust(data, []), InputError, 'store key'), 'trust with no key: STOP')
    kid = pm_store.make_key(data)
    expect(len(kid) == 19 and kid.count('-') == 3, 'store key: the key ID %s' % kid)
    expect(pm_store.make_key(data).startswith(kid), 'store key again: the same key (no overwrite)')
    if os.name != 'nt':
        expect(os.stat(data.key_store).st_mode & 0o077 == 0, 'store key: mode 0600')
    pool_pt = fw_sign.pub_of_pem(data.key_store)
    expect(kid == key_text(pool_pt), 'store key: the ID of the board form')
    own = os.path.join(d, 'own')
    make_pkg(own, 'own1', 'lcd349', ver='0.1.0')
    make_pkg(own, 'own1', 'cores3', ver='0.1.0')
    one = make_pkg(os.path.join(d, 'own_alpha'), 'alpha', 'lcd349', ver='2.0.0', code=9, name='ALPHA')
    rows = pm_store.publish(data, own)
    expect([(r['app'], r['kind']) for r in rows if isinstance(r, dict)] == [('own1', 'cores3'), ('own1', 'lcd349')],
           'publish: the 2 packages of a tfn_app.py build folder: %r' % rows)
    rows = pm_store.publish(data, one)
    expect(rows[0]['app'] == 'alpha' and rows[0]['ver'] == '2.0.0', 'publish: one package folder')
    pubd = os.path.join(d, 'own_pub')
    make_pkg(pubd, 'devapp', 'lcd349', extra='publisher=0011223344556677\n')
    expect(raises(lambda: pm_store.publish(data, pubd), InputError, 'publisher'),
           'publish: a package of a developer key = STOP')
    badd = make_pkg(os.path.join(d, 'own_bad'), 'badapp', 'lcd349')
    with open(os.path.join(badd, 'app.elf'), 'ab') as h:
        h.write(b'x')
    expect(raises(lambda: pm_store.publish(data, badd), InputError, 'app.elf'),
           'publish: an app.elf that is not the file of manifest.txt = STOP')
    expect(raises(lambda: pm_store.publish(data, os.path.join(d, 'none')), InputError), 'publish: no folder = STOP')
    code, _, body = get('/api/store/catalog', kind='lcd349', abi=1, rev=5, d=1)
    cat = parse_catalog(body.decode())[1]
    expect(sorted(cat) == ['alpha', 'own1'] and cat['alpha'][2] == '2.0.0' and cat['own1'][2] == '0.1.0',
           'catalog: the pool app takes the place of the official app')
    code, _, body = get('/api/store/catalog', kind='cores3', abi=1, rev=5, d=1)
    cat = parse_catalog(body.decode())[1]
    expect(cat['alpha'][2] == '1.0.0', 'catalog: the official alpha stays for cores3')
    man = get('/api/store/file', id='own1', kind='lcd349', file='manifest.txt')[2]
    sig = get('/api/store/file', id='own1', kind='lcd349', file='sig')[2]
    expect(fw_sign.verify_raw(pool_pt, man, sig), 'publish: sig = the pool store key over manifest.txt')
    expect(b1.install('own1') == (False, 'NOT SIGNED'), 'install of a pool app before the trust: NOT SIGNED')

    # trust: the console "pub allow" on board 1; on board 2 no tap (refused), then a tap
    expect(raises(lambda: pm_store.trust(data, [], 'fake1'), InputError, 'one member'),
           'trust: one console for 2 members = STOP')
    rows = pm_store.trust(data, ['ONE'], 'fake1')
    expect(rows[0]['result'] == 'trusted' and b1.pubs and b1.pubs[0]['pk'] == pool_pt and
           b1.pubs[0]['name'] == 'TEST STORE POOL' and b1.pubs[0]['flags'] == 'store',
           'trust with the console: %r' % rows)
    pm_store.ALLOW_WAIT = 1
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        ok = raises(lambda: pm_store.trust(data, ['TWO']), Failed, 'failed')
    expect(ok and 'no ALLOW' in out.getvalue() and not b2.pubs, 'trust: no tap = FAILED: %s' % out.getvalue())
    pm_store.ALLOW_WAIT = 8
    b2.pub_auto = 0.3
    with contextlib.redirect_stdout(io.StringIO()):
        rows = pm_store.trust(data, [])
    expect([r['result'] for r in rows] == ['trusted already', 'trusted'] and b2.pubs,
           'trust of all: board 1 has it, board 2 after a tap: %r' % rows)
    ok1, v1 = b1.install('own1')
    ok2, v2 = b2.install('own1')
    expect(ok1 and ok2 and v1 == v2 == '0.1.0', 'install of a pool app on both boards: %s %s' % (v1, v2))
    expect(b1.install('alpha') == (True, '2.0.0'), 'install: the pool alpha on lcd349')

    # a new official release: the mirror gets alpha 1.1.0, the old files go, the pool's files stay
    srv_2, url_2 = serve_folder(p2)
    data.cfg['channel']['pages'] = url_2
    data.save_cfg()
    old_elf = hashlib.sha256(open(os.path.join(pk1, 'alpha', 'cores3', 'app.elf'), 'rb').read()).hexdigest()
    expect(os.path.isfile(os.path.join(data.cache, 'store', 'files', old_elf)), 'the mirror has the old file')
    expect(pm_store.sync(data) == 4, 'sync of the new release')
    expect(not os.path.isfile(os.path.join(data.cache, 'store', 'files', old_elf)), 'sync: the old files go')
    expect(b2.install('alpha') == (True, '1.1.0') and b1.install('own1')[0] and b1.install('alpha') == (True, '2.0.0'),
           'after the new release: cores3 gets alpha 1.1.0, the pool apps stay')

    # ---- store official, and a board with the token of another manager (the command line: exit 1)
    b2.tok = 'a-token-of-another-manager'
    code, out = cli('--data', data_dir, 'store', 'official')
    expect(code == 1 and 'another manager' in out and b1.lan == '', 'store official: board 2 refuses: %s' % out)
    b2.tok = toks[b2.mac.hex().upper()]
    code, out = cli('--data', data_dir, 'store', 'official')
    expect(code == 0 and b1.lan == '' and b2.lan == '' and b1.tok == t1, 'store official: %s' % out)
    expect(json.load(open(data.cfg_file))['store'] == 'official', 'store official: config.json')
    code, out = cli('--data', data_dir, 'store', 'sync')
    expect(code == 0 and '4 packages' in out, 'the command line: store sync: %s' % out)
    # a board that is not a member of this pool takes no op=mgr
    b3 = StoreBoard('s3', bytes.fromhex('0a0000000003'), d, 'lcd349', store_pt)
    expect(raises(lambda: pm_store.set_member(data, b3.ip, 'local'), InputError), 'set_member: not a member = STOP')
    for s in (srv, srv_bad, srv_o, srv_2b, srv_2, local, b1.srv, b2.srv, b3.srv):
        s.shutdown()


def main():
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix='poolmgr_store_') as d:
        run(d)
    if FAILS:
        print('POOLMGR STORE PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('POOLMGR STORE PY OK (%d checks, %.1f s)' % (CHECKS[0], time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
