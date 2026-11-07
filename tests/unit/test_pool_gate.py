#!/usr/bin/env python3
"""The test of tests/pool/gate.py (the P4 gate) against FAKE boards and a FAKE manager: HTTP servers and UDP sockets
on 127.0.0.1, fake USB consoles and a fake radio between the boards. No real board and no network.

The fake boards are the fake board of test_pool_tool.py (the /api/pool side of the design 2.6-2.7) with the parts that
the gate uses: the console (open, ltap, friends, lanonly, pool, webpass), a lobby that sends link frames (v4 in a pool,
else v3) and pairs with a friend, the counters of design 2.4.2, the receive rules of design 2.8.2 (RELAXED and STRICT),
the peer list (/api/boards), the calls and the invites on UDP, op=mgr. The fake manager has a family pool (made with
sdk/tools/pool.py): /api/devices, /api/pool (ADOPT, CONFIRM, the token JOIN), the WALKIE calls /wk/<MAC>/..., and the
requests through the manager /board/<MAC>/...

The runs: a family board that is a friend of a T board (the setup stops), G6 with a RELAXED family board (FAIL), G4
with a board that takes op=mgr (FAIL), then the full gate (all PASS, the boards back in the family pool, no address and
no key in the output).

Usage: python tests/unit/test_pool_gate.py
"""
import base64
import contextlib
import http.server
import io
import json
import os
import socket
import sys
import tempfile
import threading
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'pool'))
import fw_sign  # noqa: E402
import gate  # noqa: E402
import pool  # noqa: E402
import pool_crypto as pc  # noqa: E402
import pool_tool  # noqa: E402
import test_pool_tool as tpt  # noqa: E402

FAILS = []
CHECKS = [0]
GAME_APPS = ('PONG', 'ZAP')
MGR = pool.MANAGER_MAC
TICK = 0.1


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


# ---------------------------------------------------------------- a fake board of the gate
class GBoard(tpt.FakeBoard):
    def __init__(self, name, mac, d, world, kind='lcd349', strict=True, legacy=True, token=None):
        self.world, self.kind, self.strict = world, kind, strict
        self.rules_strict = strict  # (the rules that the board uses; a test fault: RELAXED rules in a STRICT build)
        self.log = []
        super().__init__(name, mac, d, legacy, token)
        self.friends = {}           # MAC -> name
        self.pinned = {}            # MAC -> the pool ID of a keyed friend
        self.app = None
        self.link = self.lanonly = False
        self.paired = None
        self.near_new = []
        self.cnt = {'ok': 0, 'other': 0, 'who': 0, 'bad': 0, 'old': 0}
        self.win = {}
        self.peer = {}              # MAC -> [name, (host, port), app, time]
        self.ctr = 1000
        self.n2_t = 0.0
        self.take_mgr = False       # (a fault for a test: the board takes op=mgr of anybody)
        self.u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.u.bind(('127.0.0.1', 0))
        self.u.settimeout(0.2)
        self.uaddr = self.u.getsockname()
        threading.Thread(target=self.udp_loop, daemon=True).start()

    def print(self, text):
        self.out.append(text)
        self.log.append('I (1) console: ' + text)

    def say(self, tag, text):
        self.log.append('I (1) %s: %s' % (tag, text))

    def state(self):
        s = super().state()
        s['strict'] = self.strict
        s['v4'] = dict(self.cnt)
        return s

    def take(self, r, data):
        super().take(r, data)
        for m in r.members:
            if m.mac in self.friends:
                self.pinned[m.mac] = r.pool_id

    # ---- the rules of design 2.8.2 (tfn_pool_take)
    def members(self):
        return [m.mac for m in self.roster.members] if self.roster else []

    def rule(self, what, mac):
        if not self.roster:
            return what != 'wake2' and (what != 'wake1' or mac in self.friends)
        member = mac in self.members()
        keyed_other = mac in self.pinned and self.pinned[mac] != self.roster.pool_id
        friend = mac in self.friends
        v3 = not self.rules_strict or (not member and not keyed_other)
        if what == 'v3':
            return v3
        if what == 'peer':
            return not self.rules_strict or member or friend
        if what == 'wake1':
            return friend and v3
        return any(x.mac == mac and x.role == pool.ROLE_MANAGER for x in self.roster.members)

    # ---- the radio and the LAN
    def frame(self):
        self.ctr += 7
        app = ord(self.app[0])
        if self.roster:
            return pool.frame_seal(self.keys(), self.mac, app, 1, 6, b'\x00' * 8, self.ctr)
        return b'AL\x03' + bytes([app, 1, 8, 6, self.ctr & 255, (self.ctr >> 8) & 255]) + b'\x00' * 8

    def deliver(self, mac, f):
        with self.lock:
            if not self.link or len(f) < 9:
                return
            if f[2] == 4:
                if not self.roster:
                    self.cnt['other'] += 1
                    return
                code, fl = pool.frame_open(self.keys(), mac, f, self.members())
                if code != pool.DROP_OK:
                    if code in self.cnt:
                        self.cnt[code] += 1
                    return
                if not self.win.setdefault(mac, pool.Window()).check(fl['ctr']):
                    self.cnt['old'] += 1
                    return
                self.cnt['ok'] += 1
            elif self.roster and not self.rule('v3', mac):
                return
            self.game_pkt(mac)

    def game_pkt(self, mac):
        if self.app not in GAME_APPS or self.paired:
            return
        if mac in self.friends:
            self.paired = mac
            host = self.mac > mac
            if self.app == 'PONG':
                self.say('pong', '2 BOARDS: %s, the other board %s, channel 6' % ('HOST' if host else 'CLIENT',
                                                                                  mac.hex()[-6:]))
            else:
                self.say('net2', '2 BOARDS (Z): %s, the other board %s, channel 6' % ('HOST' if host else 'CLIENT',
                                                                                    mac.hex()[-6:]))
            return
        name = self.world.name_of(mac)
        if name not in self.near_new:
            self.near_new.append(name)

    def peer_seen(self, mac, name, addr, app):
        if mac == self.mac or (self.roster and not self.rule('peer', mac)):
            return
        e = self.peer.setdefault(mac, ['?', addr, '', 0.0])
        if name:
            e[0] = name
        e[1] = addr
        if app is not None:
            e[2] = app
        e[3] = time.time()

    def udp_loop(self):
        while True:
            try:
                pkt, addr = self.u.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                return
            with self.lock:
                self.udp_in(pkt, addr)

    def udp_in(self, pkt, addr):
        if pkt[:3] == b'AP\x01' and self.roster:
            code, plain = pool.beacon_open(self.keys(), pkt)
            if code == pool.DROP_OK:
                f = pool.beacon_fields(plain)
                self.peer_seen(f['mac'], f['name'], addr, f['app'])
        elif pkt[:2] == b'AD' and len(pkt) > 8:
            self.peer_seen(pkt[2:8], None, addr, None)
            self.deliver(pkt[2:8], pkt[8:])
        elif pkt[:2] in (b'AC', b'AI') and len(pkt) >= 16:
            self.call_in(pkt, addr)

    def call_in(self, pkt, addr):
        frm, sealed = pkt[3:9], pkt[2] == 2
        if pkt[:2] == b'AC' and pkt[9:15] != self.mac:
            return
        if sealed:
            if not self.roster:
                self.cnt['other'] += 1
                return
            code, _ = pool.udp_open(self.keys(), pkt)
            if code != pool.DROP_OK:
                if code in self.cnt:
                    self.cnt[code] += 1
                return
            self.cnt['ok'] += 1
        elif self.roster and not self.rule('v3', frm):
            return
        self.peer_seen(frm, None, addr, None)
        if frm not in self.friends:
            return
        who = self.friends[frm]
        if pkt[:2] == b'AI':
            self.say('lan', 'invite of %s: PONG (notice 1)' % who)
        elif pkt[15] == gate.OP_WAKE:
            if self.rule('wake2' if sealed else 'wake1', frm):
                self.say('walkie_call', 'wake from %s: TALKIE opens' % who)
                self.app = 'TALKIE'
        elif pkt[15] == gate.OP_CALL:
            if self.app in ('WALKIE', 'TALKIE'):
                self.world.mgr.paired_with(self, frm)
            else:
                self.say('walkie_call', 'call from %s (ask)' % who)

    # ---- the console of the gate
    def gate_cmd(self, line):
        w = line.split()
        with self.lock:
            if not w:
                return
            if w[0] in ('quiet', 'wake'):
                return
            if w[0] == 'open':
                what = ' '.join(w[1:])
                self.app, self.link, self.paired, self.near_new = None, False, None, []
                if what == 'menu':
                    self.say('console', 'open menu: ok')
                elif what in ('pong', 'zap', 'walkie', 'talkie'):
                    self.app = what.upper()
                    self.say('console', 'open %s: ok' % what)
                    self.say('main', 'open %s' % self.app)
                else:
                    self.say('console', 'open %s: no such app' % what)
            elif w[0] == 'ltap' and len(w) == 3:
                xy = (int(w[1]), int(w[2]))
                if self.app in GAME_APPS and not self.link:
                    if gate.GAMES[self.app.lower()][3][self.kind] == xy:
                        self.link = True
                        self.say('link', 'on, channel 6')
                elif self.link and self.near_new:
                    name = self.near_new.pop(0)
                    mac = self.world.mac_of(name)
                    if mac:
                        self.friends[mac] = name
            elif w[0] == 'friends':
                if len(w) == 3 and w[1] == 'rm':
                    ks = list(self.friends)
                    i = int(w[2])
                    if 0 <= i < len(ks):
                        del self.friends[ks[i]]
                self.say('console', 'friends: this board %s, %d friends, 0 near' % (self.name, len(self.friends)))
                for i, (mac, n) in enumerate(self.friends.items()):
                    self.say('console', 'friend %d %s %s' % (i, n, mac.hex()[-4:].upper()))
                for n in self.near_new:
                    self.say('console', 'new board near: %s' % n)
            elif w[0] == 'lanonly':
                self.lanonly = len(w) > 1 and w[1] == '1'
            elif w[:2] == ['pool', 'leave']:
                self.erase()
                self.print('pool: the pool data is erased')
            elif w[:2] == ['webpass', 'off']:
                self.webpw = None
            elif w[0] == 'pool' and len(w) == 1 and self.j and self.j.get('step') == 'wait':
                self.print('POOL MANAGER M WANTS TO MANAGE THIS BOARD: SAS %s - ALLOW / NO' %
                           pool.sas_text(self.shown_sas()))
            else:
                self.console(line)

    # ---- the web of the gate
    def handle(self, method, uri, headers, body):
        path, _, query = uri.partition('?')
        if method == 'POST' and path == '/api/pool' and query == 'op=leave':
            with self.lock:
                if not self.auth_ok(method, uri, headers.get(pool.HDR)):
                    return 401, {'error': 'auth'}
                self.erase()
                return 200, {'ok': True, 'state': 'none'}
        if path not in ('/api/boards', '/api/apps'):
            return super().handle(method, uri, headers, body)
        with self.lock:
            signed = self.auth_ok(method, uri, headers.get(pool.HDR))
            if self.webpw and not signed:
                want = 'Basic ' + base64.b64encode(('admin:%s' % self.webpw).encode()).decode()
                if headers.get('Authorization') != want:
                    return 401, {'error': 'auth'}
            if path == '/api/boards':
                now = time.time()
                lan = [{'name': e[0], 'ip': '%s:%d' % e[1], 'app': e[2]} for e in self.peer.values() if now - e[3] < 10]
                return 200, {'me': {'name': self.name, 'ip': '127.0.0.1', 'app': self.app or ''}, 'lan': lan}
            op = urllib.parse.parse_qs(query).get('op', [''])[0]
            if method == 'POST' and op == 'mgr':
                if self.roster and not signed and not self.take_mgr:
                    return 200, {'ok': False, 'msg': 'not the pool manager'}
                return 200, {'ok': True, 'msg': 'the manager is set'}
            return 200, {'apps': []}


class GCon:
    """The fake USB console of a fake board (the interface of gate.Console)."""

    def __init__(self, b):
        self.b = b

    def cmd(self, c, wait=0.15):
        self.b.gate_cmd(c)
        time.sleep(min(wait, 0.05))

    def mark(self):
        return len(self.b.log)

    def since(self, m):
        return list(self.b.log[m:])

    def close(self):
        self.b.gate_cmd('open menu')


# ---------------------------------------------------------------- the world: the radio, the beacons, the manager
class World:
    def __init__(self):
        self.boards = []
        self.mgr = None
        self.run = True

    def name_of(self, mac):
        return next((b.name for b in self.boards if b.mac == mac), 'MANAGER' if mac == MGR else '?')

    def mac_of(self, name):
        return next((b.mac for b in self.boards if b.name == name), None)

    def loop(self):
        n = 0
        while self.run:
            time.sleep(TICK)
            n += 1
            for s in self.boards:
                with s.lock:
                    if not s.link or s.app not in GAME_APPS:
                        continue
                    f = s.frame()
                    copies = [e[1] for e in s.peer.values() if e[2] == s.app and time.time() - e[3] < 10] \
                        if s.lanonly else []
                for r in self.boards:
                    if r is not s and not r.lanonly:
                        r.deliver(s.mac, f)
                for addr in copies:
                    s.u.sendto(b'AD' + s.mac + f, addr)
                with s.lock:
                    if s.paired and s.app == 'ZAP' and time.time() - s.n2_t > 1:
                        s.n2_t = time.time()
                        s.say('net2', 'in 5 s 300 packets from the other board')
            if n % 5 == 0:
                self.beacons()

    def beacons(self):
        fam = self.mgr.p.pool_id
        for s in self.boards:
            r_s = s.roster
            plain = (r_s is None and s.legacy) or (r_s is not None and r_s.flags & pool.F_LEGACY_BEACON)
            if (r_s is None and s.legacy) or (r_s is not None and (r_s.pool_id == fam or plain)):
                self.mgr.seen[s.mac] = time.time()
            for r in self.boards:
                if r is s:
                    continue
                with r.lock:
                    same = r_s is not None and r.roster is not None and r.roster.pool_id == r_s.pool_id
                    if same or plain:
                        r.peer_seen(s.mac, s.name, s.uaddr, s.app or '')


# ---------------------------------------------------------------- the fake manager
class FakeManager:
    def __init__(self, d, world):
        self.world = world
        self.key = os.path.join(d, 'fam_a.pem')
        fw_sign.keygen(self.key)
        self.p = pool.Pool.create('FAMILY', pc.p256_pub(self.key), None, pool.F_LEGACY_BEACON)
        self.p.add_manager()
        self.p.sign(self.key)
        self.seen = {}
        self.joins = {}
        self.msgs = []
        self.wk = {}
        self.lock = threading.RLock()
        self.u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.u.bind(('127.0.0.1', 0))
        self.ctr = 1
        self.srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), make_mgr_handler(self))
        self.url = 'http://127.0.0.1:%d' % self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def board(self, mac_hex):
        return next((b for b in self.world.boards if b.mac.hex().upper() == mac_hex.upper()), None)

    def online(self, b):
        return b is not None and time.time() - self.seen.get(b.mac, 0) < 10

    def member(self, b):
        return any(m['mac'] == b.mac.hex() for m in self.p.members)

    def devices(self):
        now = time.time()
        return [{'mac': b.mac.hex().upper(), 'name': b.name, 'online': self.online(b), 'ip': '%s:%d' % b.uaddr,
                 'age': round(now - self.seen.get(b.mac, 0), 1), 'ver': '1.0.103'} for b in self.world.boards]

    def view(self):
        out = {'on': True, 'pool': {'id': self.p.pool_id.hex(), 'name': self.p.name, 'v': self.p.version},
               'members': [], 'joins': {k: dict(v) for k, v in self.joins.items()}, 'msgs': list(self.msgs)}
        for m in self.p.members:
            b = self.board(m['mac'])
            st = b.state() if b else {}
            out['members'].append({'mac': m['mac'].upper(), 'name': m['name'], 'state': st.get('state', ''),
                                   'same': st.get('pool') == self.p.pool_id.hex()})
        return out

    def adopt(self, mac, token=False):
        b = self.board(mac)
        if not self.online(b):
            self.msgs.append([time.time(), 'pool_join_start: PoolStop: the board is not online'])
            return {'ok': False, 'msg': 'the board is not online'}
        brd = pool_tool.Board(b.ip)
        try:
            ident = brd.call('GET', '/api/id')
            j = pool.Join(self.key)
            f = j.take_hello(brd.call('POST', '/api/pool?op=hello', j.hello_body('x' * 32 if token else None)),
                             bytes.fromhex(ident['pk']))
            brd.call('POST', '/api/pool?op=reveal', j.reveal_body())
        except (pool_tool.Refused, pool_tool.ToolError, pool.PoolError) as e:
            self.joins[mac] = {'state': 'failed', 'msg': str(e), 'sas': ''}
            return {'ok': False, 'msg': str(e)}
        self.joins[mac] = {'state': 'sas', 'msg': '', 'sas': pool.sas_text(j.sas()), 'j': j, 'f': f, 'b': b}
        return {'ok': True, 'sas': pool.sas_text(j.sas())}

    def confirm(self, mac):
        e = self.joins.get(mac)
        if not e or e['state'] != 'sas':
            return {'ok': False, 'msg': 'no JOIN waits'}
        e['state'] = 'send'

        def run():
            f, b = e['f'], e['b']
            did = f['id'].hex()
            with self.lock:
                self.p.upsert({'id': did, 'pk': f['pk'].hex(), 'mac': b.mac.hex(), 'role': pool.ROLE_MEMBER,
                               'flags': 0, 'name': b.name, 'bx': f['bx'].hex(), 'ip': b.ip})
                roster = self.p.sign(self.key)
                body = e['j'].set_body(roster, self.p.epoch, self.p.pool_k, self.p.wk(did))
            end = time.time() + 20
            while True:
                try:
                    pool_tool.Board(b.ip).call('POST', '/api/pool?op=set', body)
                    break
                except pool_tool.Refused as r:
                    if r.ans.get('error') != 'wait' or time.time() > end:
                        e['state'], e['msg'] = 'failed', str(r)
                        return
                time.sleep(0.2)
            e['state'], e['msg'] = 'done', '%s is a member' % b.name
        threading.Thread(target=run, daemon=True).start()
        return {'ok': True, 'msg': 'sent'}

    def call(self, b, op):
        """A call of the manager to a board it sees: version 2 to a member, else version 1."""
        if not self.online(b):
            return
        if self.member(b):
            self.ctr += 1
            pkt = pool.call_v2(self.p.keys(), MGR, b.mac, op, self.ctr)
        else:
            pkt = b'AC\x01' + MGR + b.mac + bytes([op])
        self.u.sendto(pkt, b.uaddr)

    def paired_with(self, b, frm):
        if frm == MGR and b.mac.hex().upper() in self.wk:
            self.wk[b.mac.hex().upper()]['paired'] = True

    def handle(self, method, path, q):
        if path == '/api/devices':
            return 200, self.devices()
        if path == '/api/pool':
            return 200, {k: v for k, v in self.view().items()}
        mac = str(q.get('mac', '')).upper()
        if path == '/api/pool/adopt':
            return 200, self.adopt(mac)
        if path == '/api/pool/confirm':
            return 200, self.confirm(mac)
        if path == '/api/pool/token':
            threading.Thread(target=self.adopt, args=(mac, True), daemon=True).start()
            return 200, {'ok': True, 'msg': 'the token JOIN runs'}
        if path == '/api/pool/cancel':
            if mac in self.joins:
                self.joins[mac]['state'] = 'cancel'
            return 200, {'ok': True}
        parts = path.strip('/').split('/')
        if len(parts) == 3 and parts[0] == 'wk':
            m, op = parts[1].upper(), parts[2]
            b = self.board(m)
            if op == 'call':
                self.wk[m] = {'paired': False}
                for _ in range(2):
                    self.call(b, gate.OP_CALL)
                    time.sleep(0.2)
            elif op == 'ptt' and q.get('on') == '1' and m in self.wk and not self.wk[m]['paired']:
                self.call(b, gate.OP_WAKE)
            elif op == 'end':
                self.wk.pop(m, None)
            return 200, dict(self.wk.get(m) or {'active': False})
        if len(parts) >= 2 and parts[0] == 'board':
            b = self.board(parts[1])
            if not self.online(b):
                return 404, {'error': 'no such board'}
            return 200, b.handle('GET', '/' + '/'.join(parts[2:]), {}, b'')[1]
        return 404, {'error': 'route'}


def make_mgr_handler(mgr):
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def go(self, method):
            n = int(self.headers.get('Content-Length') or 0)
            if n:
                self.rfile.read(n)
            u = urllib.parse.urlparse(self.path)
            q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
            code, ans = mgr.handle(method, u.path, q)
            ans = {k: v for k, v in ans.items()} if isinstance(ans, dict) else ans
            if isinstance(ans, dict) and 'joins' in ans:
                ans['joins'] = {k: {x: y for x, y in v.items() if x in ('state', 'msg', 'sas')}
                                for k, v in ans['joins'].items()}
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


# ---------------------------------------------------------------- the runs
def run_gate(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = gate.main(list(argv))
    return code, out.getvalue()


def setup(d):
    w = World()
    w.mgr = FakeManager(d, w)
    A = GBoard('OMEGA', bytes.fromhex('02000000a001'), d, w)
    B = GBoard('BRAVO', bytes.fromhex('02000000b002'), d, w)
    F = GBoard('FOXTROT', bytes.fromhex('02000000f003'), d, w, kind='cores3')
    w.boards = [A, B, F]
    threading.Thread(target=w.loop, daemon=True).start()
    # the friends of before: A and B know the manager (keyed in the family pool); B does not know A (the ADD badge)
    for b in (A, B, F):
        b.friends[MGR] = 'MANAGER'
        b.pinned[MGR] = w.mgr.p.pool_id
    A.friends[B.mac] = B.name
    cons = {'conA': A, 'conB': B, 'conF': F}
    gate.open_console = lambda port, label, log: GCon(cons[port])
    udp = {b.ip: b.uaddr for b in w.boards}
    gate.udp_addr = lambda addr: udp[addr]
    gate.TAP_HOST, gate.TAP_PORT = '127.0.0.1', 0
    gate.SLOW = 0.3
    # pool T (the TEST key, pool_tool): A and B by the console (the 6 digits), then random web passwords
    pd, key = os.path.join(d, 'poolT'), os.path.join(d, 'test_t.pem')
    tcons = {'tA': A, 'tB': B}
    pool_tool.open_console = lambda port: tpt.FakeConsole(tcons[port])
    for argv in (['keygen', key], ['--dir', pd, '--key', key, 'create', 'TEST T', '--manager'],
                 ['--dir', pd, '--key', key, 'adopt', A.ip, '--console', 'tA', '--name', A.name],
                 ['--dir', pd, '--key', key, 'adopt', B.ip, '--console', 'tB', '--name', B.name],
                 ['--dir', pd, '--key', key, 'webpass']):
        code, out = tpt.tool(*argv)
        expect(code == 0, 'pool T: %s: %s' % (argv[-1], out))
    # the family board F joins the pool of the manager (the gate's own JOIN code)
    time.sleep(0.8)
    g = gate.Gate(None)
    g.M = gate.Manager(w.mgr.url)
    tf = gate.Target('F', F.ip, GCon(F), 'cores3')
    tf.mac, tf.name = F.mac, F.name
    with contextlib.redirect_stdout(io.StringIO()):
        g.join_family(tf)
    expect(F.roster is not None and F.roster.pool_id == w.mgr.p.pool_id, 'F is a member of the family pool')
    expect(A.roster.pool_id == B.roster.pool_id != F.roster.pool_id and A.webpw and B.webpw, 'A + B in pool T')
    return w, A, B, F, pd, key


def secrets_of(pd, A, B):
    p = pool.Pool.load(pd)
    return [p.pool_k.hex(), *p.wks.values(), A.webpw or 'x' * 9, B.webpw or 'x' * 9, '127.0.0.1']


def port_checks(base):
    """The UDP port of the tap is exclusive. A port in use stops the gate at the start (exit 2) before it opens a
    console; a second socket cannot take the port of a tap (also not with SO_REUSEADDR, the way of another program on
    Windows)."""
    hold = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    hold.bind(('127.0.0.1', 0))
    opened = []
    real_open = gate.open_console
    gate.open_console = lambda port, label, log: opened.append(label) or real_open(port, label, log)
    gate.TAP_PORT = hold.getsockname()[1]
    try:
        code, out = run_gate(*base)
    finally:
        gate.TAP_PORT = 0
        gate.open_console = real_open
        hold.close()
    expect(code == 2 and 'STOP' in out and 'in use' in out and not opened,
           'a port in use: the gate stops at the start, no console opened: %s' % out[-300:])
    tap = gate.Tap()
    other = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    other.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        other.bind(tap.s.getsockname())
        took = True
    except OSError:
        took = False
    finally:
        other.close()
        tap.close()
    expect(not took, 'a second socket with SO_REUSEADDR cannot take the port of the tap')


def run(d):
    w, A, B, F, pd, key = setup(d)
    base = ['--t-dir', pd, '--t-key', key, '--a-con', 'conA', '--b-con', 'conB', '--fam', F.ip, '--fam-con', 'conF',
            '--manager', w.mgr.url, '--secs', '1.5', '--call-wait', '1', '--replay-wait', '2', '--logs',
            os.path.join(d, 'logs')]
    sec = secrets_of(pd, A, B)
    port_checks(base)

    # a family board that is a friend of a T board: the setup stops (exit 2), nothing changes
    A.friends[F.mac] = F.name
    code, out = run_gate(*base)
    expect(code == 2 and 'are friends' in out, 'friends T <-> F: the setup stops: %s' % out[-300:])
    del A.friends[F.mac]

    # G6 with a RELAXED family board: the forged call of the manager MAC rings (FAIL)
    F.rules_strict = False
    code, out = run_gate(*base, '--only', 'G6', '--no-restore')
    F.rules_strict = True
    expect(code == 1 and 'G6 FAIL' in out and 'GATE FAIL' in out, 'G6 sees a RELAXED board: %s' % out[-400:])

    # G4 with a board that takes op=mgr of anybody (FAIL)
    A.take_mgr = True
    code, out = run_gate(*base, '--only', 'G4', '--no-restore')
    A.take_mgr = False
    expect(code == 1 and 'G4 FAIL' in out and 'op=mgr: TAKEN' in out, 'G4 sees op=mgr taken: %s' % out[-400:])
    expect(A.roster.pool_id == pool.Pool.load(pd).pool_id, 'A keeps pool T after G4')

    # the full gate
    t0 = time.time()
    code, out = run_gate(*base)
    print(out)
    for g in gate.CHECKS:
        expect('%s PASS' % g in out, '%s PASS' % g)
    expect(code == 0 and 'GATE PASS' in out and 'RESTORE OK' in out, 'the full gate: exit %d' % code)
    expect('token JOIN %s: no JOIN started' % A.name in out and 'op=mgr: refused' in out, 'G4: the evidence lines')
    expect(not any(s in out for s in sec), 'no address and no key in the output')
    fam = w.mgr.p.pool_id
    expect(all(b.roster is not None and b.roster.pool_id == fam for b in (A, B, F)),
           'the restore: A, B and F in the family pool')
    expect(not A.webpw and not B.webpw and not A.lanonly and not B.lanonly, 'the restore: web passwords off, lanonly 0')
    expect(A.mac not in B.friends, 'the restore: the friend that the gate added is removed')
    expect(all(b.app is None for b in (A, B, F)), 'the restore: the menu on all boards')
    print('(the full gate: %.0f s)' % (time.time() - t0))
    w.run = False


def main():
    try:
        fw_sign.openssl()
    except fw_sign.SignError as e:
        print('POOL GATE PY FAILED: %s' % e)
        return 1
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix='tfn_pool_gate_') as d:
        run(d)
    print('POOL GATE PY %s (%d checks against fake boards, %.1f s)' % (
        'OK' if not FAILS else '%d FAILED' % len(FAILS), CHECKS[0], time.time() - t0))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
