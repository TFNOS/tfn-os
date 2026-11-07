#!/usr/bin/env python3
"""The fake boards of the pool tests (tests/unit/test_pool_tool.py, test_pool_gate.py and the tests of poolmgr/):
small HTTP servers on 127.0.0.1 that speak /api/pool (the board side of the design 2.6-2.7, made with pool.py) and a
fake USB console. No real board and no network.

FakeBoard(name, mac, folder) starts its server at once; board.ip = "127.0.0.1:<port>". FakeConsole(board) is the
console of one board (write_line, read_line, close: the form of pool_tool.SerialConsole).

The parts of the poolmgr tests: FakeOtaBoard (GET /api/boards, POST /api/ota with the signature check of the board),
StoreBoard (/api/apps, /api/pub, the console "pub allow" and the client of the manager store), PoolBoard (all parts,
also /api/friends and the UDP packets of a board).

The web gate of a fake board is the one of main/web_auth.c: with a web password, a request with no good request header
and no good Basic password gets 401 {"error": "login"}; after WRONG_MAX wrong passwords, 429 {"error": "wait"}.
GET /api/pool needs no password.
"""
import base64
import hashlib
import http.server
import json
import os
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import fw_sign  # noqa: E402
import pool  # noqa: E402
import pool_crypto as pc  # noqa: E402

WRONG_MAX = 5   # the wrong web passwords before the web gate of a fake board answers 429 "wait"

# ---------------------------------------------------------------- a fake board
class FakeBoard:
    """The board side of /api/pool, /api/id and the console "pool" (the contract of the vector file notes)."""

    def __init__(self, name, mac, d, legacy=True, token=None):
        self.name, self.mac = name, mac
        self.pem = os.path.join(d, 'dev_%s.pem' % name)
        self.pk = fw_sign.keygen(self.pem)
        self.id = pc.device_id(self.pk)
        self.bx_priv, self.bx_pub = pc.x25519_keypair()
        self.bxs = fw_sign.sign_raw(self.pem, pool.box_msg(self.id, self.bx_pub))
        self.legacy, self.token = legacy, token
        self.roster = self.pool_k = self.wk = self.prev = None
        self.epoch = self.rv = 0
        self.webpw = None
        self.wrong_pw = 0          # the wrong web passwords (main/web_auth.c: a wait after WRONG_MAX)
        self.join_open = False
        self.auto_tap = None
        self.auto_no = None        # the seconds to a tap on NO (the board ends the JOIN)
        self.lie_sas = False
        self.out = []
        self.lock = threading.RLock()
        self.restart()
        self.srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), make_handler(self))
        self.ip = '127.0.0.1:%d' % self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def restart(self):
        """A new start: a new board nonce, no counter, no JOIN."""
        self.bn = pc.rand(16)
        self.last = 0
        self.j = None

    def print(self, text):
        self.out.append(text)

    def id_text(self):
        """The device ID as the board gives it in /api/id (tfn_id_text: XXXX-XXXX-XXXX-XXXX)."""
        h = self.id.hex().upper()
        return '-'.join(h[i:i + 4] for i in range(0, 16, 4))

    def state(self):
        st = 'ok' if self.roster else ('join' if self.join_open or self.j else 'none')
        r = self.roster
        me = r.members[r.member_by_id(self.id)] if r else None
        return {'state': st, 'pool': r.pool_id.hex() if r else '', 'name': r.name if r else '',
                'v': r.version if r else 0, 'epoch': self.epoch if r else 0,
                'role': pool.ROLES[me.role] if me else '', 'strict': False, 'legacy': self.legacy,
                'members': len(r.members) if r else 0, 'mgr': pc.device_id(r.admins[0]).hex() if r else '',
                'bx': self.bx_pub.hex(), 'bxs': self.bxs.hex(), 'bn': self.bn.hex(), 'id': self.id.hex(),
                'mac': self.mac.hex(), 'join': (self.j or {}).get('step', 'none'),
                'v4': {'ok': 0, 'other': 0, 'who': 0, 'bad': 0, 'old': 0}}

    def keys(self):
        return pool.derive(self.pool_k, self.roster.pool_id, self.epoch)

    # ---- the console
    def console(self, line):
        with self.lock:
            w = line.split()
            if w[:2] == ['pool', 'join']:
                self.join_open = True
                self.print('pool: JOIN open')
            elif w[:2] == ['pool', 'allow'] and len(w) == 3:
                if self.j and self.j['step'] == 'wait' and w[2] == '%06d' % self.j['sas']:
                    self.j['step'] = 'allowed'
                    self.print('pool: ALLOW')
                else:
                    self.print('pool: wrong digits or no JOIN')
            elif w == ['pool']:
                self.print('pool: state %s' % self.state()['state'])
                if self.j and self.j['step'] == 'wait':
                    self.print('pool: SAS %s' % pool.sas_text(self.shown_sas()))
            elif w[:2] == ['webpass', 'set'] and len(w) == 3:
                self.webpw = w[2]
                self.print('webpass: on')

    def shown_sas(self):
        return (self.j['sas'] + 1) % 1000000 if self.lie_sas else self.j['sas']

    # ---- the web
    def auth_ok(self, method, uri, hdr):
        """The X-TFN-Auth check (design 2.6) with the wk of this board. As hdr_good of main/tfn_pool_flow.c, the ID of
        the header must be a MANAGER member of the roster of the board."""
        if not hdr or not self.roster or not self.wk:
            return False
        for m in self.roster.members:
            if m.role != pool.ROLE_MANAGER:
                continue
            ok, ctr = pool.web_check(self.wk, self.bn, m.id, self.last, method, uri, hdr)
            if ok:
                self.last = ctr
                return True
        return False

    def gate(self, headers, signed):
        """The web gate of main/web_auth.c: None = the request passes; else the answer of the gate (401 "login",
        or 429 "wait" after WRONG_MAX wrong web passwords)."""
        if not self.webpw or signed:
            return None
        got = headers.get('Authorization')
        if got == 'Basic ' + base64.b64encode(('admin:%s' % self.webpw).encode()).decode():
            return None
        if got:
            self.wrong_pw += 1
        if self.wrong_pw > WRONG_MAX:
            return 429, {'error': 'wait', 'secs': 30}
        return 401, {'error': 'login', 'login': '/login'}

    def handle(self, method, uri, headers, body):
        with self.lock:
            path, _, query = uri.partition('?')
            op = urllib.parse.parse_qs(query).get('op', [''])[0]
            signed = self.auth_ok(method, uri, headers.get(pool.HDR))
            if not (method == 'GET' and path == '/api/pool'):
                deny = self.gate(headers, signed)
                if deny:
                    return deny
            if method == 'GET' and path == '/api/id':
                return 200, {'id': self.id_text(), 'pk': self.pk.hex(), 'x25519': '', 'xs': '', 'alg': 'p256'}
            if method == 'GET' and path == '/api/pool':
                return 200, self.state()
            if method != 'POST' or path != '/api/pool':
                return 404, {'error': 'route'}
            try:
                b = json.loads(body.decode('utf-8') or '{}')
            except ValueError:
                return 400, {'error': 'form'}
            fn = {'hello': self.op_hello, 'reveal': self.op_reveal, 'set': self.op_set, 'key': self.op_key,
                  'webpass': self.op_webpass}.get(op)
            if fn is None:
                return 400, {'error': 'op'}
            try:
                return fn(b, signed)
            except (KeyError, ValueError, TypeError, pc.CryptoError):
                return 400, {'error': 'form'}

    def op_hello(self, b, signed):
        tok = b.get('auth')
        if tok is not None:
            if not self.legacy or not self.token or tok != self.token or self.roster:
                return 403, {'error': 'token'}
        elif not self.join_open:
            return 409, {'error': 'no_join'}
        eb_priv, eb_pub = pc.x25519_keypair()
        nb = pc.rand(16)
        c = bytes.fromhex(b['c'])
        self.j = {'step': 'hello', 'admin': bytes.fromhex(b['admin']), 'c': c, 'eb_priv': eb_priv, 'eb': eb_pub,
                  'nb': nb, 'token': tok is not None}
        sig = fw_sign.sign_raw(self.pem, pool.msg_b(c, eb_pub, nb))
        return 200, {'eb': eb_pub.hex(), 'nb': nb.hex(), 'pk': self.pk.hex(), 'id': self.id.hex(),
                     'bx': self.bx_pub.hex(), 'bxs': self.bxs.hex(), 'sig': sig.hex()}

    def op_reveal(self, b, signed):
        j = self.j
        if not j or j['step'] != 'hello':
            return 409, {'error': 'no_join'}
        em, nm, sig = bytes.fromhex(b['em']), bytes.fromhex(b['nm']), bytes.fromhex(b['sig'])
        if pool.commit(em, nm) != j['c']:
            self.j = None
            return 403, {'error': 'commit'}
        if not pc.p256_verify(j['admin'], pool.msg_m(j['c'], j['eb'], j['nb'], em, nm), sig):
            self.j = None
            return 403, {'error': 'sig'}
        j.update(em=em, sas=pool.sas(em, j['eb'], nm, j['nb']))
        if j['token']:
            j['step'] = 'allowed'
        else:
            j['step'] = 'wait'
            self.print('pool: MANAGER WANTS TO MANAGE THIS BOARD: SAS %s - ALLOW / NO' %
                       pool.sas_text(self.shown_sas()))
            if self.auto_tap is not None:
                threading.Timer(self.auto_tap, self.tap).start()
            if self.auto_no is not None:
                threading.Timer(self.auto_no, self.tap_no).start()
        return 200, {'ok': True, 'join': j['step']}

    def tap(self):
        with self.lock:
            if self.j and self.j['step'] == 'wait':
                self.j['step'] = 'allowed'

    def tap_no(self):
        """NO on the screen (or the end of the 2 minutes of a step): the JOIN ends; op=set then needs the header."""
        with self.lock:
            self.j = None
            self.join_open = False

    def take(self, r, data):
        self.roster, self.rv = r, r.version

    def erase(self):
        self.roster = self.pool_k = self.wk = self.prev = None
        self.epoch = 0

    def apply_roster(self, data):
        """The roster of op=set or op=key (no JOIN): (None, the answer) or (an error answer, None)."""
        if self.roster and data == self.roster.raw:
            return None, 'same'
        res, r = pool.roster_accept(self.roster, data, self.id, self.rv)
        if res == pool.TAKE:
            self.take(r, data)
            return None, 'take'
        if res == pool.REMOVED:
            self.erase()
            self.print('pool: THIS BOARD LEFT THE POOL %s' % r.name)
            return None, 'removed'
        return (403, {'error': res}), None

    def op_set(self, b, signed):
        data = bytes.fromhex(b['roster'])
        j = self.j
        if j and 'env' in b:
            if j['step'] != 'allowed':
                return 409, {'error': 'wait'}
            res, r = pool.roster_accept(self.roster, data, self.id, self.rv, j['admin'])
            if res != pool.TAKE:
                return 403, {'error': res}
            k = pool.seal_key(pc.x25519_shared(j['eb_priv'], j['em']), j['c'])
            sec = pool.env_open(k, data, bytes.fromhex(b['env']))
            if sec is None:
                return 403, {'error': 'env'}
            self.take(r, data)
            self.epoch, self.pool_k, self.wk = sec
            self.prev = None
            self.token = None
            self.j = None
            self.join_open = False
            return 200, {'ok': True, 'state': 'ok', 'v': r.version}
        if not signed:
            return 401, {'error': 'auth'}
        err, what = self.apply_roster(data)
        if err:
            return err
        return 200, {'ok': True, 'state': self.state()['state'], 'roster': what}

    def op_key(self, b, signed):
        if not signed:
            return 401, {'error': 'auth'}
        if 'roster' in b:
            err, what = self.apply_roster(bytes.fromhex(b['roster']))
            if err:
                return err
            if what == 'removed':
                return 200, {'ok': True, 'state': 'none'}
        k = pool.seal_key(pc.x25519_shared(self.bx_priv, bytes.fromhex(b['em'])), self.id)
        sec = pool.env_open(k, self.roster.raw, bytes.fromhex(b['env']))
        if sec is None:
            return 403, {'error': 'env'}
        epoch, pk, wk = sec
        if epoch < self.epoch:
            return 403, {'error': 'old'}
        if epoch > self.epoch:
            self.prev = (self.epoch, self.pool_k)
        self.epoch, self.pool_k, self.wk = sec
        return 200, {'ok': True, 'state': 'ok', 'epoch': epoch}

    def op_webpass(self, b, signed):
        if not signed:
            return 401, {'error': 'auth'}
        self.webpw = b['pw']
        return 200, {'ok': True}


def make_handler(board):
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def go(self, method):
            n = int(self.headers.get('Content-Length') or 0)
            body = self.rfile.read(n) if n else b''
            code, ans = board.handle(method, self.path, self.headers, body)
            data = json.dumps(ans).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            self.go('GET')

        def do_POST(self):
            self.go('POST')
    return H


class FakeConsole:
    def __init__(self, board):
        self.b = board

    def write_line(self, text):
        self.b.console(text)

    def read_line(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            with self.b.lock:
                if self.b.out:
                    return self.b.out.pop(0)
            time.sleep(0.02)
        return None

    def close(self):
        pass


# ---------------------------------------------------------------- the parts of the poolmgr tests
# Each part is a mixin before FakeBoard: its handle() answers its own routes and gives the other routes to the next
# class. Its xxx_setup() sets its values (call it before FakeBoard.__init__: the server starts there).
class OtaPart:
    """GET /api/boards and POST /api/ota (both need the request header of the pool). The OTA check is the check of the
    board (fw_sign.check with its own key list, its kind and its minsv; ENFORCE); a good file = a restart (no answer
    for DOWN_S), then the new version with "pend" true for PEND_S."""
    DOWN_S = 0.4
    PEND_S = 0.4

    def ota_setup(self, kind, trust, ver):
        self.kind, self.ver, self.trust = kind, ver, list(trust)
        self.shown_keys = None  # (the key IDs that /api/boards gives; None = the IDs of trust)
        self.sigmode, self.minsv, self.app = 'enforce', 0, 'MENU'
        self.pend, self.pend_until, self.new_sv = False, 0.0, 0
        self.bad, self.own_sig, self.last_sig, self.ota_code = '', 'ok', '', ''
        self.t0 = time.time() - 100
        self.down_until = 0.0
        self.rollback = False
        self.sends, self.done_t = [], []

    def me(self):
        now = time.time()
        if self.pend and now >= self.pend_until:
            self.pend = False
            self.minsv = max(self.minsv, self.new_sv)
        keys = self.shown_keys if self.shown_keys is not None else [fw_sign.key_id(k).hex() for k in self.trust]
        return {'name': self.name, 'app': self.app, 'board': self.kind, 'ver': self.ver, 'kind': self.kind,
                'bad': self.bad, 'pend': self.pend, 'up': int(now - self.t0), 'ota_code': self.ota_code,
                'ota_fail': '', 'sig': self.sigmode, 'keys': keys, 'minsv': self.minsv, 'own_sv': self.new_sv,
                'own_key': '', 'own_sig': self.own_sig, 'last_sig': self.last_sig, 'self': ''}

    def handle(self, method, uri, headers, body):
        path = uri.partition('?')[0]
        if path not in ('/api/boards', '/api/ota'):
            return super().handle(method, uri, headers, body)
        with self.lock:
            if time.time() < self.down_until:
                return 503, {'error': 'restart'}
            if not self.auth_ok(method, uri, headers.get(pool.HDR)):
                return 401, {'error': 'auth'}
            if method == 'GET' and path == '/api/boards':
                return 200, {'me': self.me(), 'lan': []}
            if method != 'POST' or path != '/api/ota':
                return 404, {'error': 'route'}
            self.sends.append(time.time())
            code, kid, sv = fw_sign.check(body, self.trust, self.kind, None, self.minsv)
            self.last_sig = 'ok:' + kid if code == 'ok' else code
            if code != 'ok' and self.sigmode == 'enforce':
                self.ota_code = 'sig'
                return 400, {'ok': False, 'msg': 'the signature check says %s' % code, 'code': 'sig', 'sig': code}
            threading.Timer(0.1, self.reboot, [fw_sign.image_version(body[:-fw_sign.TRAILER]), sv or 0]).start()
            return 200, {'ok': True, 'msg': 'the board restarts'}

    def reboot(self, ver, sv):
        with self.lock:
            now = time.time()
            self.restart()
            self.t0, self.down_until = now, now + self.DOWN_S
            self.last_sig, self.ota_code = '', ''
            if self.rollback:
                self.bad = 'ota_1'
                return
            self.ver, self.new_sv = ver, sv
            self.pend, self.pend_until = True, self.down_until + self.PEND_S
            self.done_t.append(self.pend_until)


class FakeOtaBoard(OtaPart, FakeBoard):
    """The fake board + the OTA of a board (OtaPart)."""

    def __init__(self, name, mac, d, kind, trust, ver='1.0.1'):
        self.ota_setup(kind, trust, ver)
        FakeBoard.__init__(self, name, mac, d)


# the files of an app package (main/tfn_app.h TFN_APP_FILES)
APP_FILES = ('manifest.txt', 'app.elf', 'icon.bin', 'sig', 'watch.elf', 'sigs')


def key_text(pk):
    """The key ID as the board shows it: XXXX-XXXX-XXXX-XXXX."""
    h = hashlib.sha256(pk).hexdigest()[:16].upper()
    return '-'.join(h[i:i + 4] for i in range(0, 16, 4))


def parse_catalog(text):
    """The parser of main/tfn_store.c mgr_catalog: (the profile, {id: the fields}) or None."""
    lines = text.split('\n')
    if not lines or not lines[0].startswith('TFN1\t'):
        return None
    prof = lines[0][5:].split('\t')[0]
    out = {}
    for ln in lines[1:]:
        if not ln:
            continue
        f = ln.split('\t')[:12]
        if len(f) < 8:
            continue
        out[f[0]] = f
    return prof, out


class StorePart:
    """/api/apps (GET, op=mgr), /api/pub, the console "pub allow" and the client of the manager store
    (main/tfn_store.c: the catalog, the files, the install with the check of "sig")."""

    def store_setup(self, kind, builtin):
        self.kind, self.abi, self.rev = kind, 1, 5
        self.builtin = builtin
        self.lan = self.vpn = self.tok = ''
        self.pubs = []
        self.pub_wait = None
        self.pub_auto = None
        self.installed = {}
        self.policy_seen = None

    # ---- the web
    def handle(self, method, uri, headers, body):
        path, _, query = uri.partition('?')
        if path not in ('/api/apps', '/api/pub'):
            return super().handle(method, uri, headers, body)
        with self.lock:
            q = {k: v[0] for k, v in urllib.parse.parse_qs(query, keep_blank_values=True).items()}
            signed = self.auth_ok(method, uri, headers.get(pool.HDR))
            deny = self.gate(headers, signed)
            if deny:
                return deny
            if path == '/api/apps' and method == 'GET':
                return 200, {'kind': self.kind, 'abi': self.abi, 'rev': self.rev, 'mgr': bool(self.lan),
                             'apps': [{'id': a, 'ver': v} for a, v in self.installed.items()]}
            if path == '/api/apps' and q.get('op') == 'mgr':
                if not (self.roster and signed):
                    return 200, {'ok': False, 'msg': 'not the pool manager'}
                if self.tok and q.get('auth') != self.tok:
                    return 200, {'ok': False, 'msg': 'the token is not right'}
                lan = q.get('lan', '')
                if lan and (not lan.startswith('http://') or len(lan) >= 63 or '%' in lan):
                    return 200, {'ok': False, 'msg': 'bad values (the manager address or the token)'}
                self.lan, self.vpn = lan, q.get('vpn', '')
                if q.get('newtok'):
                    self.tok = q['newtok']
                return 200, {'ok': True, 'msg': 'saved'}
            if path == '/api/pub' and method == 'GET':
                w = self.pub_wait
                return 200, {'builtin': [{'id': key_text(self.builtin)}],
                             'keys': [{'id': key_text(p['pk']), 'name': p['name'], 'flags': p['flags'].upper()}
                                      for p in self.pubs],  # (the form of the board: "STORE", "STORE+DEV")
                             'max': 8, 'wait': {'id': key_text(w['pk']), 'name': w['name'], 'flags': w['flags'].upper(),
                                                'secs': 100} if w else None}
            if path == '/api/pub' and q.get('op') == 'add':
                try:
                    pk = bytes.fromhex(body.decode().strip())
                except ValueError:
                    pk = b''
                name, fl = q.get('name', ''), q.get('flags', '')
                if len(pk) != 65 or pk[0] != 4 or not 1 <= len(name) <= 15 or fl != 'store':
                    return 200, {'ok': False, 'msg': 'op=add: the body = the key (130 hex digits, 04...)'}
                if any(p['pk'] == pk for p in self.pubs):
                    return 200, {'ok': False, 'msg': 'this key is in the list already'}
                self.pub_wait = {'pk': pk, 'name': name.upper(), 'flags': fl}
                if self.pub_auto is not None:
                    threading.Timer(self.pub_auto, self.pub_tap).start()
                return 200, {'ok': True, 'msg': 'the key waits for the tap of the board owner'}
            return 404, {'error': 'route'}

    def pub_tap(self):
        with self.lock:
            if self.pub_wait:
                self.pubs.append(self.pub_wait)
                self.pub_wait = None

    def console(self, line):
        w = line.split()
        if w[:2] == ['pub', 'allow'] and len(w) == 3:
            with self.lock:
                if self.pub_wait and key_text(self.pub_wait['pk']) == w[2].upper():
                    self.pub_tap()
                    self.print('pub: added')
                else:
                    self.print('pub: no key waits with the ID %s' % w[2])
            return
        super().console(line)

    # ---- the client of the manager store
    def get(self, path):
        try:
            with urllib.request.urlopen(self.lan + path, timeout=5) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, b''

    def catalog(self, desc=True):
        code, b = self.get('/api/store/catalog?mac=%s&kind=%s&abi=%d&rev=%d%s' % (
            self.mac.hex().upper(), self.kind, self.abi, self.rev, '&d=1' if desc else ''))
        return parse_catalog(b.decode('utf-8')) if code == 200 else None

    def sig_ok(self, man, sig):
        keys = [self.builtin] + [p['pk'] for p in self.pubs if 'store' in p['flags'].split(',')]
        return any(fw_sign.verify_raw(k, man, sig) for k in keys)

    def install(self, aid):
        """(True, the version) or (False, the fault)."""
        if not self.lan:
            return False, 'THE MANAGER HAS NOT SEEN THIS BOARD YET'
        code, pol = self.get('/api/store/policy?have=0&abi=%d' % self.abi)
        self.policy_seen = pol
        cat = self.catalog()
        if not cat or aid not in cat[1]:
            return False, 'not in the catalog'
        files = {}
        for i, f in enumerate(APP_FILES):
            if f == 'sigs':
                continue  # (no publisher= in the manifest)
            code, b = self.get('/api/store/file?id=%s&kind=%s&file=%s' % (aid, self.kind, f))
            if code == 404 and i >= 2:
                continue
            if code != 200:
                return False, '%s: the manager answers %d' % (f, code)
            files[f] = b
        m = dict(ln.split('=', 1) for ln in files['manifest.txt'].decode().split('\n') if '=' in ln)
        if m.get('id') != aid or m.get('kind') != self.kind:
            return False, 'the manifest is of another app'
        if m.get('elf_sha256') != hashlib.sha256(files['app.elf']).hexdigest():
            return False, 'app.elf is damaged'
        if 'sig' not in files or not self.sig_ok(files['manifest.txt'], files['sig']):
            return False, 'NOT SIGNED'
        if m.get('ver') != cat[1][aid][2]:
            return False, 'the catalog and the manifest do not agree'
        self.installed[aid] = m['ver']
        return True, m['ver']


class StoreBoard(StorePart, FakeBoard):
    """The fake board + the store of a board (StorePart)."""

    def __init__(self, name, mac, d, kind, builtin):
        self.store_setup(kind, builtin)
        FakeBoard.__init__(self, name, mac, d)


class LanPart:
    """/api/friends (16 at most), the 'A' 'J' packet (an open JOIN screen) and the 'A' 'P' beacon each 0.2 s to the
    UDP port of poolmgr (pm_net.UDP_PORT on 127.0.0.1), and a board that is off (no answer, no packet)."""

    def lan_setup(self):
        self.friends = []          # [mac upper hex, name]
        self.off = False
        self.friends_down = False  # /api/friends does not answer (503); the rest of the board answers
        self.stop_ev = threading.Event()
        threading.Thread(target=self.beat, daemon=True).start()

    def beat(self):
        import pm_net  # (poolmgr/ is in sys.path of the poolmgr tests)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(('127.0.0.1', 0))
        try:
            while not self.stop_ev.wait(0.2):
                pkt = None
                with self.lock:
                    if self.off:
                        continue
                    if self.join_open and not self.roster:
                        pkt = pool.join_pkt(self.mac, self.id, self.name)
                    elif self.roster and self.pool_k:
                        pkt = pool.beacon_seal(self.keys(), pool.beacon_plain(self.mac, self.name, '', 0, self.id))
                if pkt:
                    try:
                        udp.sendto(pkt, ('127.0.0.1', pm_net.UDP_PORT))
                    except OSError:
                        pass
        finally:
            udp.close()

    def close(self):
        self.stop_ev.set()
        self.srv.shutdown()
        self.srv.server_close()

    def handle(self, method, uri, headers, body):
        if self.off:
            return 503, {'error': 'off'}
        path, _, query = uri.partition('?')
        if path != '/api/friends':
            return super().handle(method, uri, headers, body)
        if self.friends_down:
            return 503, {'error': 'busy'}
        with self.lock:
            signed = self.auth_ok(method, uri, headers.get(pool.HDR))
            deny = self.gate(headers, signed)
            if deny:
                return deny
            if method != 'GET':
                return 404, {'error': 'route'}
            q = urllib.parse.parse_qs(query, keep_blank_values=True)
            ans = {'name': self.name, 'mac': self.mac.hex().upper(), 'max': 16}
            if 'add' in q:
                mac, nm = q['add'][0].upper(), (q.get('name') or ['FRIEND'])[0]
                old = [f for f in self.friends if f[0] == mac]
                if old:
                    old[0][1] = nm
                    ans['add'] = 'ok'
                elif len(self.friends) >= 16:
                    ans['add'] = 'full'
                else:
                    self.friends.append([mac, nm])
                    ans['add'] = 'ok'
            if 'rm' in q:
                self.friends = [f for f in self.friends if f[0] != q['rm'][0].upper()]
            ans['friends'] = [{'name': n, 'mac': m, 'near': False} for m, n in self.friends]
            return 200, ans


class PoolBoard(LanPart, StorePart, OtaPart, FakeBoard):
    """A fake board with all parts of the poolmgr tests: the JOIN (FakeBoard), the friends and the UDP packets
    (LanPart), the store (StorePart) and the OTA (OtaPart). kind = the board kind, ota_trust = the OTA keys of its
    firmware, store_key = its built-in store key."""

    def __init__(self, name, mac, d, kind, ota_trust, store_key, ver='1.0.1', legacy=False, token=None):
        self.ota_setup(kind, ota_trust, ver)
        self.store_setup(kind, store_key)
        FakeBoard.__init__(self, name, mac, d, legacy=legacy, token=token)
        self.lan_setup()


# ---------------------------------------------------------------- the fake files of the poolmgr tests
def image(kind, version, n=24 * 1024):
    """A fake app image: the first byte E9, the version at 0x30, the board tag of the kind."""
    b = bytearray(hashlib.sha256((kind + version).encode()).digest() * (n // 32))
    b[0] = 0xE9
    b[0x30:0x50] = version.encode().ljust(32, b'\0')
    tag = b'@ARCADE-BOARD:' + kind.encode() + b'@'
    b[0x100:0x100 + len(tag)] = tag
    return bytes(b)


def signed_image(kind, version, pem, sec_ver=1, img_kind=None):
    """The image + the trailer TFNOTA1 (img_kind: the image of another kind with the trailer of kind)."""
    img = image(img_kind or kind, version)
    return img + fw_sign.make_trailer(img, kind, version, sec_ver, 1700000000, [pem])


def make_channel(folder, files, patch=None):
    """<folder>/firmware: the files of site/tools/fw_manifest.py for {kind: (version, app bytes)}; patch(kind, app
    entry) changes the manifest entry of the app. Returns the folder firmware/."""
    sys.path.insert(0, os.path.join(ROOT, 'site', 'tools'))
    import fw_manifest
    out = os.path.join(folder, 'firmware')
    for kind, (ver, app) in sorted(files.items()):
        parts = [('bootloader', 'bootloader.bin', 0x0, b'\xe9boot' * 64, ['install']),
                 ('partition-table', 'partition-table.bin', 0x8000, b'\xaa\x50' * 64, ['install']),
                 ('app', 'app.bin', 0x20000, app, ['install', 'update']),
                 ('otadata', 'ota_data_initial.bin', 0xd000, b'\xff' * 0x2000, ['install', 'update'])]
        fw_manifest.save_kind(out, kind, ver, parts, {'flash_size': '16MB'}, 'ESP32-S3', released=1700000000)
        if patch:
            p = os.path.join(out, kind, 'manifest.json')
            with open(p, 'rb') as h:
                man = json.loads(h.read().decode())
            for f in man['files']:
                if f['name'] == 'app':
                    patch(kind, f)
            with open(p, 'wb') as h:
                h.write(json.dumps(man, indent=1).encode())
    fw_manifest.write_index(out)
    return out


def make_pkg(root, aid, kind, ver='1.0.0', code=1, abi=1, rev=1, name=None, extra='', elf=None):
    """A package of the form of tfn_app.py build: <root>/<id>/<kind>/ manifest.txt, app.elf, icon.bin."""
    d = os.path.join(root, aid, kind)
    os.makedirs(d, exist_ok=True)
    elf = elf or ('ELF of %s %s %s' % (aid, kind, ver)).encode() * 40
    icon = ('ICON %s' % aid).encode() * 10
    m = ('id=%s\nkind=%s\nname=%s\nver=%s\ncode=%d\nabi=%d\nrev=%d\ngroup=GAMES\nbytes=%d\nelf_sha256=%s\n'
         'icon_sha256=%s\nram=1234\nemoji=X\nperms=net\ndesc=The app %s\ndesc_he=HE %s\n%s' %
         (aid, kind, name or aid.upper(), ver, code, abi, rev, len(elf), hashlib.sha256(elf).hexdigest(),
          hashlib.sha256(icon).hexdigest(), aid, aid, extra))
    for f, b in (('manifest.txt', m.encode()), ('app.elf', elf), ('icon.bin', icon)):
        with open(os.path.join(d, f), 'wb') as h:
            h.write(b)
    return d


def make_store(src, out, key):
    """store_index.py make with the TEST key; the signatures by the openssl program (no package "cryptography").
    poolmgr/ must be in sys.path (pm_shared finds store_index.py)."""
    import pm_shared
    si = pm_shared.store_index()
    old = si.load_key, si.point_of, si.sign
    si.load_key, si.point_of, si.sign = (lambda p: p), fw_sign.pub_of_pem, fw_sign.sign_raw
    try:
        return si.make(src, out, key)
    finally:
        si.load_key, si.point_of, si.sign = old


class QuietFiles(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def serve_folder(d):
    """A static web server of the folder d on 127.0.0.1: (the server, its URL)."""
    import functools
    s = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(QuietFiles, directory=d))
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s, 'http://127.0.0.1:%d' % s.server_address[1]


# ---------------------------------------------------------------- devices that are not TFN boards
class OtherDevice:
    """A device of the LAN that is not a TFN board, for example the web page of a router or a printer: an HTTP server
    on 127.0.0.1 with the same answer to each GET and POST (default: 200 with an HTML page). seen = (method, path,
    the Authorization header or None) of each request."""

    PAGE = b'<!doctype html><html><head><title>Printer</title></head><body>Log in</body></html>'

    def __init__(self, status=200, ctype='text/html', body=PAGE):
        self.status, self.ctype, self.body = status, ctype, body
        self.seen = []
        dev = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def answer(self):
                n = int(self.headers.get('Content-Length') or 0)
                if n:
                    self.rfile.read(n)
                dev.seen.append((self.command, self.path, self.headers.get('Authorization')))
                self.send_response(dev.status)
                self.send_header('Content-Type', dev.ctype)
                self.send_header('Content-Length', str(len(dev.body)))
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(dev.body)

            def do_GET(self):
                self.answer()

            def do_POST(self):
                self.answer()
        self.srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
        self.ip = '127.0.0.1:%d' % self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def close(self):
        self.srv.shutdown()
        self.srv.server_close()


class NotHttp:
    """A TCP server on 127.0.0.1 that answers each connection with bytes that are not HTTP (another service)."""

    def __init__(self, reply=b'SSH-2.0-OtherService\r\n'):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.bind(('127.0.0.1', 0))
        self.s.listen(8)
        self.ip = '127.0.0.1:%d' % self.s.getsockname()[1]
        self.reply = reply
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            try:
                c.settimeout(2)
                c.recv(4096)
                c.sendall(self.reply)
            except OSError:
                pass
            finally:
                c.close()

    def close(self):
        self.s.close()
