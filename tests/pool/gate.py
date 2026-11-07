#!/usr/bin/env python3
"""The P4 gate: a second test pool cannot see or talk to the family pool (P4 stage 2 design, section 4, G1-G7).

The boards: A and B = the two boards of the test pool T (made with sdk/tools/pool_tool.py: create, adopt, webpass; both
STRICT), F = a board of the family pool near them (STRICT), and the family manager. The tool uses the USB consoles of
A, B and F (silent: "quiet 1", then "quiet 0" at the end), the web API of the boards, the API of the manager, and UDP
packets from this computer (it holds the keys of pool T: it can make the frames of a T member).

  G1  the radio: A + B pair in each game (PONG, a net2 game); F opens the same game at the same time. PASS: A and B
      pair; F pairs with nobody; no lobby shows a board of the other pool; F counts the frames of T as "other" and
      no frame as "ok"; A and B count the frames of F as "other".
  G2  the LAN: the peer lists (/api/boards "lan") of A, B, F and the DEVICES list of the manager. PASS: no board of
      the other pool in a list (a friend is the exception of the STRICT rule: the tool names it); the manager does
      not see A or B on line.
  G3  the calls and the invites: a call and an invite of version 2 of T (from A) to F; the manager calls A (CALL and
      the WAKE of TALKIE); a WAKE of version 1 with the manager MAC to A. PASS: no ring, no notice, no TALKIE; F
      counts the T packets as drops.
  G4  the pool layer: the family manager tries ADOPT and the token JOIN of A; a stand-in manager (a key that this
      tool makes, not an admin key of T) sends op=mgr, op=set, op=key, op=webpass and a request with its header.
      PASS: each one refused (401, "not the pool manager", or the manager reports the refusal); A keeps pool T.
  G5  the replay: the tool records a T frame of A (the LAN copy) and sends it again to B after --replay-wait seconds.
      PASS: B counts it as "old", none as "ok".
  G6  the forged member: a v3 frame and a call of version 1 with the MAC of A and with the MAC of the manager to F,
      and a call of version 1 with the MAC of A to B. PASS: no pairing, no ring, the MAC stays out of the peer lists.
  G7  no effect on the family: F + the manager do WALKIE and TALKIE (the manager pairs), the manager reads F (DEVICES,
      a request through the manager, the pool state). PASS: all work.

At the end (also after a fault, not with --no-restore) the boards go back: lanonly 0, the menu, the friends that the
tool added are removed, A and B leave pool T (pool_tool remove; else "pool leave" on the console), their web passwords
go back (--old-webpass, else off), the family manager adopts them again (the JOIN with the 6 digits on the console,
as board_onboard.py), and "quiet 0". The tool does not change the brightness.

The output never has an address or a key: the tool shows names, the last 4 digits of a MAC and the first 8 digits of a
pool ID. The console logs (--logs) have the addresses replaced too.

The tool must have the UDP port 47474 of this computer alone (an exclusive bind): the boards send their LAN copies to
it. When another program has the port (a manager, "poolmgr serve"), the tool stops at the start (exit 2) and changes
no board. A limit on macOS (BSD): the bind of the tool cannot stop a program that sets SO_REUSEADDR and binds the LAN
address of the computer on port 47474 (the manager and poolmgr bind all addresses: they cannot do this). That program
gets the unicast packets to the address, and the gate loses LAN copies. On macOS, stop each such program before the
gate. On Windows (SO_EXCLUSIVEADDRUSE) and on Linux the bind is exclusive.

Usage:
  python tests/pool/gate.py --t-dir <folder of pool T> --t-key <admin key of T> --a-con <port> --b-con <port>
         --fam <address of F> --fam-con <port> [--fam-kind cores3|lcd349] [--manager URL] [--games pong zap]
         [--secs 12] [--replay-wait 60] [--only G1,G5] [--old-webpass <json>] [--no-restore] [--logs <folder>]
--manager: default = the manager of the store setup (sdk/tools/mk_app.py default_manager(): TFN_MANAGER or the store setup).
--old-webpass: a JSON file {MAC: password} of the web passwords before the gate ("" = none); default: none.
Exit 0 = all checks PASS, 1 = a check failed, 2 = a fault of the setup or the tool.
"""
import argparse
import base64
import contextlib
import io
import json
import os
import re
import socket
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import pool  # noqa: E402
import pool_crypto as pc  # noqa: E402
import pool_tool  # noqa: E402

UDP_PORT = 47474            # the LAN port of the boards (main/lan.c LAN_PORT)
TAP_HOST, TAP_PORT = '', UDP_PORT   # the socket of this tool (the boards send the LAN copies to UDP_PORT)
MANAGER_MAC = pool.MANAGER_MAC
OP_CALL, OP_WAKE = 1, 5
HTTP_S = 10
SLOW = 1.0                  # the factor of the waits for the boards to settle (the test of the tool makes it small)
PAIR_S = 20
JOIN_S = 150
ONLINE_S = 120
CHECKS = ['G1', 'G2', 'G3', 'G4', 'G5', 'G6', 'G7']
LOBBY = r'link: on, channel|ESPNOW: espnow \[version.*\] init'
PAIRED = r'2 BOARDS.*(HOST|CLIENT)'
RING = r'call from|wake from|TALKIE opens|invite of'
CRASH = r"Guru Meditation|Backtrace:|panic'ed|abort\(\) was called|stack overflow|rst:0x"
SAS_RE = re.compile(r'\bSAS\b\D*(\d{3})\s?(\d{3})')
IP_RE = re.compile(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}(:\d+)?\b')
# the game: (console name, log name, net2, the middle of the 2 BOARDS button for each kind in logical points)
GAMES = {
    'pong': ('pong', 'PONG', False, {'lcd349': (130, 143), 'cores3': (240, 59)}),
    'zap': ('zap', 'ZAP', True, {'lcd349': (425, 76), 'cores3': (236, 80)}),
}


class GateStop(Exception):
    """A fault of the setup or the tool (exit 2)."""


# ---------------------------------------------------------------- no address and no key in the output
SECRETS = set()


def scrub(text):
    text = IP_RE.sub('<address>', str(text))
    for s in SECRETS:
        if s and len(s) >= 6:
            text = text.replace(s, '<secret>')
    return text


def nap(secs):
    time.sleep(secs * SLOW)


def say(text=''):
    print(scrub(text), flush=True)


def short(mac):
    return mac.hex()[-4:].upper() if isinstance(mac, (bytes, bytearray)) else str(mac)[-4:].upper()


# ---------------------------------------------------------------- the USB console
class Console:
    """The USB console of a board (no reset: DTR and RTS low). A reader thread keeps all lines with the time of this
    computer and writes them to a log (the addresses and the WiFi name replaced)."""

    def __init__(self, port, label, log):
        try:
            import serial
        except ImportError:
            raise GateStop('the consoles need the package pyserial')
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout, self.s.dtr, self.s.rts = port, 115200, 0.05, False, False
        self.s.open()
        self.label = label
        self.lines = []
        self.lock = threading.Lock()
        self.log = open(log, 'w', encoding='utf-8') if log else None
        self.run = True
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b''
        while self.run:
            try:
                d = self.s.read(65536)
            except Exception:
                break
            if not d:
                continue
            buf += d
            while b'\n' in buf:
                ln, buf = buf.split(b'\n', 1)
                txt = re.sub(r'\x1b\[[0-9;]*m', '', ln.decode('utf-8', 'replace')).rstrip()
                txt = re.sub(r'network [^,]*,', 'network <hidden>,', txt)
                txt = re.sub(r'(ssid|SSID)[ =:]+\S+', r'\1 <hidden>', txt)
                txt = scrub(txt)
                with self.lock:
                    self.lines.append(txt)
                if self.log:
                    self.log.write('%.3f %s\n' % (time.time(), txt))
                    self.log.flush()

    def cmd(self, c, wait=0.15):
        self.s.write((c + '\n').encode())
        time.sleep(wait)

    def mark(self):
        with self.lock:
            return len(self.lines)

    def since(self, m):
        with self.lock:
            return list(self.lines[m:])

    def close(self):
        try:
            self.s.write(b'open menu\n')
            time.sleep(0.8)
            self.s.write(b'quiet 0\n')
            time.sleep(0.3)
        finally:
            self.run = False
            time.sleep(0.1)
            self.s.close()
            if self.log:
                self.log.close()


def open_console(port, label, log):
    return Console(port, label, log)


def con_wait(con, pattern, timeout=5.0, m=0):
    """The first line after mark m that matches, or None after the timeout."""
    end = time.time() + timeout
    rx = re.compile(pattern)
    while True:
        for ln in con.since(m):
            if rx.search(ln):
                return ln
        if time.time() >= end:
            return None
        time.sleep(0.05)


def con_grep(con, pattern, m=0):
    rx = re.compile(pattern)
    return [ln for ln in con.since(m) if rx.search(ln)]


# ---------------------------------------------------------------- the UDP packets of this computer
def udp_addr(addr):
    """The UDP address of a board: its host and the LAN port."""
    return addr.rsplit(':', 1)[0] if addr.count(':') == 1 else addr, UDP_PORT


class Tap:
    """One UDP socket: it sends the packets of the tool and gets the LAN copies that a board sends to it.

    The bind is exclusive: no SO_REUSEADDR, and SO_EXCLUSIVEADDRUSE on Windows (there SO_REUSEADDR of another program
    can take a port that is in use). Two sockets on the port share the packets, so the gate could lose LAN copies.
    The gate uses one tap at a time. A limit on macOS (BSD): this bind of all addresses cannot stop a program that
    sets SO_REUSEADDR and binds the LAN address of the computer on the same port (it gets the unicast packets to that
    address). The manager and poolmgr bind all addresses, so they cannot do this: the module docstring tells the
    rule."""

    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            excl = getattr(socket, 'SO_EXCLUSIVEADDRUSE', None)
            if excl is not None:
                self.s.setsockopt(socket.SOL_SOCKET, excl, 1)
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self.s.bind((TAP_HOST, TAP_PORT))
        except OSError as e:
            self.s.close()
            raise GateStop('STOP: the UDP port %d of this computer is in use (%s). The gate must have this port alone: '
                           'the boards send their LAN copies to it. Stop the other program on the port (a manager, '
                           '"poolmgr serve", a second gate), then start the gate again.' % (TAP_PORT, e))
        self.s.settimeout(0.1)

    def send(self, data, addr):
        self.s.sendto(bytes(data), addr)

    def collect(self, keep, secs, every=None):
        """The packets that keep(packet) accepts in secs seconds; every = (period, function) runs during the wait."""
        out, end, nxt = [], time.time() + secs, 0.0
        while time.time() < end:
            if every and time.time() >= nxt:
                every[1]()
                nxt = time.time() + every[0]
            try:
                pkt, _ = self.s.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            if keep(pkt):
                out.append(pkt)
        return out

    def close(self):
        self.s.close()


# ---------------------------------------------------------------- the boards and the manager
def http(method, url, body=None, headers=None, timeout=HTTP_S):
    """(HTTP code, JSON answer or {}); a board with no answer = (0, {'error': ...})."""
    data = json.dumps(body).encode() if isinstance(body, (dict, list)) else body
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header('Content-Type', 'application/json')
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read().decode('utf-8', 'replace')
            code = r.status
    except urllib.error.HTTPError as e:
        raw, code = e.read().decode('utf-8', 'replace'), e.code
    except (urllib.error.URLError, OSError) as e:
        return 0, {'error': 'no answer: %s' % e}
    try:
        return code, json.loads(raw or '{}')
    except ValueError:
        return code, {'text': raw[:200]}


class Target:
    """One board of the gate: its web API (with its web password) and its console."""

    def __init__(self, label, addr, con, kind='lcd349', password=None):
        self.label, self.addr, self.con, self.kind, self.password = label, addr, con, kind, password
        self.mac = b''
        self.dev_id = b''
        self.name = label
        self.friends = []       # (name, the last 4 digits of the MAC)

    def api(self, method, path, body=None, auth=True, headers=None):
        h = dict(headers or {})
        if auth and self.password:
            h['Authorization'] = 'Basic ' + base64.b64encode(('admin:%s' % self.password).encode()).decode()
        return http(method, 'http://%s%s' % (self.addr, path), body, h)

    def pool(self):
        code, st = self.api('GET', '/api/pool', auth=False)
        if code != 200:
            raise GateStop('%s gives no /api/pool (%s)' % (self.name, st.get('error', code)))
        return st

    def counts(self):
        return dict(self.pool().get('v4') or {})

    def peers(self):
        """[(name, address)] of /api/boards "lan" (the address stays in the tool)."""
        code, b = self.api('GET', '/api/boards')
        if code != 200:
            raise GateStop('%s gives no /api/boards (HTTP %s)' % (self.name, code))
        return [(p.get('name', ''), p.get('ip', '')) for p in b.get('lan', [])]


def delta(c0, c1):
    return {k: int(c1.get(k, 0)) - int(c0.get(k, 0)) for k in ('ok', 'other', 'who', 'bad', 'old')}


def dtext(d):
    return ' '.join('%s %+d' % (k, v) for k, v in d.items() if v) or 'no change'


class Manager:
    def __init__(self, url):
        self.url = url.rstrip('/')

    def get(self, path):
        return http('GET', self.url + path)

    def post(self, path, body=b''):
        return http('POST', self.url + path, body)

    def devices(self):
        code, d = self.get('/api/devices')
        if code != 200:
            raise GateStop('the manager gives no /api/devices (%s)' % d.get('error', code))
        return d if isinstance(d, list) else d.get('devices', [])

    def device(self, mac):
        m = mac.hex().upper()
        return next((d for d in self.devices() if str(d.get('mac', '')).upper() == m), None)

    def pool(self):
        code, p = self.get('/api/pool')
        return p if code == 200 else {}


def default_manager():
    sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
    try:
        import mk_app  # noqa: E402
        return mk_app.default_manager()
    except Exception:
        return ''


# ---------------------------------------------------------------- the console helpers (the lobbies, the friends)
def friends(t):
    """(the name of the board, [(friend name, the last 4 MAC digits)], [the names of the new boards near])."""
    m = t.con.mark()
    t.con.cmd('friends', 0.1)
    if not con_wait(t.con, r'friends: this board', 3, m):
        return None, [], []
    time.sleep(0.4)
    me, fr, new = None, [], []
    for ln in t.con.since(m):
        r = re.search(r'friends: this board (.+?), \d+ friends', ln)
        if r:
            me = r.group(1)
        r = re.search(r'friend (\d+) (.+) ([0-9A-F]{4})$', ln)
        if r:
            fr.append((r.group(2), r.group(3)))
        r = re.search(r'new board near: (.+)$', ln)
        if r:
            new.append(r.group(1))
    return me, fr, new


def badge_xy(i, k):
    """The "+ ADD <name>" badge i of k in the lobby of the 3.49 (main/net2.c lobby_badge(), no friend button)."""
    w = (640 - 40 - (k - 1) * 8) // k
    return 20 + i * (w + 8) + w // 2, 172 // 2 - 44 + 70 + 6 + 16


def tap_add(t, other, added):
    """Tap the ADD badge of the other board in the lobby of t until it is a friend (the real user flow)."""
    for _ in range(20):
        _, fr, new = friends(t)
        if other.name in [n for n, _ in fr]:
            return True
        if other.name in new:
            x, y = badge_xy(new.index(other.name), min(len(new), 3))
            t.con.cmd('ltap %d %d' % (x, y), 0.8)
            _, fr, _ = friends(t)
            if other.name in [n for n, _ in fr]:
                added.append((t, other.name))
                say('   %s: %s added as a friend (the ADD badge)' % (t.name, other.name))
                return True
        time.sleep(0.5)
    return False


def open_game(ts, game):
    """Open the game on the boards ts; False when one has no such app."""
    app = GAMES[game][0]
    marks = [t.con.mark() for t in ts]
    for t in ts:
        t.con.cmd('wake', 0.1)
        t.con.cmd('open ' + app, 0.1)
    for t, m in zip(ts, marks):
        ln = con_wait(t.con, r'open %s: (ok|no such app)' % app, 4, m)
        if not ln or 'no such' in ln:
            return False
    nap(2.0)     # (the title is up: the first tap after an open can get lost)
    return True


def open_lobby(t, game):
    xy = GAMES[game][3].get(t.kind, GAMES[game][3]['lcd349'])
    for _ in range(3):
        m = t.con.mark()
        t.con.cmd('ltap %d %d' % xy, 0.1)
        if con_wait(t.con, LOBBY, 2.5, m):
            return True
        time.sleep(0.5)
    return False


def to_menu(ts, wait=1.5):
    for t in ts:
        t.con.cmd('open menu', 0.3)
    nap(wait)


# ---------------------------------------------------------------- the gate
class Gate:
    def __init__(self, a):
        self.a = a
        self.res = {}
        self.added = []
        self.captured = None
        self.capture_t = 0.0
        self.tap = None
        self.cons = {}
        self.ready = False      # (the setup is complete: the restore can run)

    def result(self, g, ok, lines):
        self.res[g] = ok
        say('%s %s' % (g, 'PASS' if ok else 'FAIL'))
        for ln in lines:
            say('   ' + ln)

    def want(self, g):
        return g in self.a.only

    # ---- the setup
    def setup(self):
        a = self.a
        p = pool_tool.load_pool(a.t_dir)
        self.tp = p
        self.keys = p.keys()
        SECRETS.update({p.pool_k.hex(), *p.wks.values()})
        ms = pool_tool.boards(p)
        if len(ms) != 2:
            raise GateStop('pool T must have 2 boards (it has %d)' % len(ms))
        wp = pool_tool.load_webpass(a.t_dir)
        SECRETS.update(v for v in wp.values() if v)
        self.old_wp = {}
        if a.old_webpass:
            with open(a.old_webpass, 'rb') as h:
                self.old_wp = {k.lower().replace(':', ''): v for k, v in json.loads(h.read().decode('utf-8')).items()}
            SECRETS.update(v for v in self.old_wp.values() if v)
        os.makedirs(a.logs, exist_ok=True)
        cons = self.cons
        for label, port in (('A', a.a_con), ('B', a.b_con), ('F', a.fam_con)):
            cons[label] = open_console(port, label, os.path.join(a.logs, 'gate_%s.log' % label))
        self.A = Target('A', ms[0]['ip'], cons['A'], 'lcd349', wp.get(ms[0]['mac']))
        self.B = Target('B', ms[1]['ip'], cons['B'], 'lcd349', wp.get(ms[1]['mac']))
        self.F = Target('F', a.fam, cons['F'], a.fam_kind, a.fam_pass)
        self.T = [self.A, self.B]
        self.all = [self.A, self.B, self.F]
        for t in self.all:
            t.con.cmd('quiet 1', 0.2)
            t.con.cmd('wake', 0.2)
            t.con.cmd('open menu', 0.3)
        nap(1.5)
        tid = p.pool_id.hex()
        self.mp = self.M.pool()
        fid = (self.mp.get('pool') or {}).get('id', '')
        if not fid:
            raise GateStop('the manager has no pool (POOL > CREATE), or its pool is OFF')
        for t in self.all:
            st = t.pool()
            t.mac, t.dev_id = bytes.fromhex(st.get('mac', '')), bytes.fromhex(st.get('id', '') or '')
            me, t.friends, _ = friends(t)
            if not me:
                raise GateStop('the console of %s does not answer' % t.label)
            t.name = me
            want = tid if t in self.T else fid
            if st.get('state') != 'ok' or st.get('pool') != want or st.get('strict') is not True:
                raise GateStop('%s (%s): state %s, pool %s, strict %s: the gate needs a STRICT member of pool %s' % (
                    t.label, t.name, st.get('state'), str(st.get('pool'))[:8], st.get('strict'),
                    'T' if t in self.T else 'of the manager'))
            say('%s = %s (%s), pool %s..., role %s, %d friends' % (t.label, t.name, short(t.mac), want[:8],
                                                                     st.get('role'), len(t.friends)))
        for t in self.T:
            if short(self.F.mac) in [s for _, s in t.friends] or short(t.mac) in [s for _, s in self.F.friends]:
                raise GateStop('%s and %s are friends: remove the friendship first (FRIENDS on the manager, or '
                               '"friends rm N" on both consoles)' % (t.name, self.F.name))

    # ---- G1 the radio (and the capture of G5)
    def g1(self):
        lines, ok = [], True
        for g in self.a.games:
            app, logname, n2, _ = GAMES[g]
            ms = {t.label: t.con.mark() for t in self.all}
            if not open_game(self.all, g):
                lines.append('%s: not on all 3 boards (SKIP)' % logname)
                to_menu(self.all)
                continue
            c0 = {t.label: t.counts() for t in self.all}
            for t in self.all:
                if not open_lobby(t, g):
                    raise GateStop('%s: the lobby of %s did not start' % (t.name, logname))
            if not (tap_add(self.A, self.B, self.added) and tap_add(self.B, self.A, self.added)):
                ok = False
                lines.append('%s: A and B are not friends (the ADD badge did not add)' % logname)
            pa = con_wait(self.A.con, PAIRED, PAIR_S, ms['A'])
            pb = con_wait(self.B.con, PAIRED, PAIR_S, ms['B'])
            t0 = time.time()
            near = {}
            while time.time() - t0 < self.a.secs:
                for t in self.all:
                    near.setdefault(t.label, set()).update(friends(t)[2])
                time.sleep(0.3)
            c1 = {t.label: t.counts() for t in self.all}
            d = {k: delta(c0[k], c1[k]) for k in c0}
            fp = con_grep(self.F.con, PAIRED, ms['F'])
            ended = con_grep(self.A.con, r'link ended|THE OTHER BOARD IS GONE|' + CRASH, ms['A']) + \
                con_grep(self.B.con, r'link ended|' + CRASH, ms['B'])
            n2a = con_grep(self.A.con, r'in 5 s [1-9]\d* packets from the other board', ms['A']) if n2 else ['-']
            n2b = con_grep(self.B.con, r'in 5 s [1-9]\d* packets from the other board', ms['B']) if n2 else ['-']
            seen_t = sorted(near.get('F', set()) & {self.A.name, self.B.name})
            seen_f = sorted((near.get('A', set()) | near.get('B', set())) & {self.F.name})
            why = []
            if not pa or not pb:
                why.append('A and B did not pair (A %s, B %s)' % (bool(pa), bool(pb)))
            if ended:
                why.append('the link of A and B ended: %s' % ended[0][-60:])
            if n2 and (not n2a or not n2b):
                why.append('no net2 line "packets from the other board" on A or B')
            if fp:
                why.append('F paired: %s' % fp[0][-60:])
            if seen_t or seen_f:
                why.append('a lobby shows the other pool: F sees %s, A/B see %s' % (seen_t, seen_f))
            # (another pool = "other"; its tag byte can be ours by chance (1 of 256): then the SipHash says "bad")
            far = {k: v['other'] + v['bad'] for k, v in d.items()}
            if far['F'] <= 0 or d['F']['ok'] != 0:
                why.append('F: other %+d (must grow), ok %+d (must stay)' % (far['F'], d['F']['ok']))
            if far['A'] <= 0 or far['B'] <= 0:
                why.append('A or B counted no frame of F as other (A %+d, B %+d)' % (far['A'], far['B']))
            ok = ok and not why
            lines.append('%s: A+B %s, %d s; F paired with nobody: %s; F sees no T board: %s' % (
                logname, 'paired' if pa and pb else 'NOT paired', self.a.secs, not fp, not seen_t))
            lines.append('%s counters: A [%s] B [%s] F [%s]' % (logname, dtext(d['A']), dtext(d['B']), dtext(d['F'])))
            lines += ['%s: %s' % (logname, w) for w in why]
            if g == 'pong' and self.want('G5') and pa and pb:
                self.capture()
                lines.append('PONG: the record for G5: %s' % ('one T frame of %s' % self.A.name if self.captured
                                                               else 'NO T frame came'))
            to_menu(self.all)
        return ok, lines

    def capture(self):
        """G5, part 1: record a T frame of A. Both boards use the LAN path ("lanonly 1"), the tool sends the T beacon
        of B (B's MAC, the PONG app) to A, so A sends its LAN copies to this computer for a moment."""
        A, B = self.A, self.B
        for t in self.T:
            t.con.cmd('lanonly 1', 0.1)
        nap(0.6)
        bea = lambda: self.tap.send(pool.beacon_seal(self.keys, pool.beacon_plain(B.mac, B.name, 'PONG', 0,
                                                                                    B.dev_id)), udp_addr(A.addr))

        def keep(pkt):
            if len(pkt) <= 8 or pkt[:2] != b'AD' or pkt[2:8] != A.mac:
                return False
            return pool.frame_open(self.keys, A.mac, pkt[8:], None)[0] == pool.DROP_OK
        got = self.tap.collect(keep, 2.0, (0.3, bea))
        for t in self.T:
            t.con.cmd('lanonly 0', 0.1)
        if got:
            # the copy goes on to B (as the network gives it): B sees this counter one time now
            self.captured, self.capture_t = got[0], time.time()
            self.tap.send(self.captured, udp_addr(B.addr))
            nap(1.0)

    # ---- G2 the LAN
    def g2(self):
        devs = self.M.devices()
        tm = {t.mac.hex().upper() for t in self.T}
        fam = [d for d in devs if d.get('online') and str(d.get('mac', '')).upper() not in tm]
        fam_names = {d.get('name', '') for d in fam} | {self.F.name}
        fam_ips = {d.get('ip', '') for d in fam if d.get('ip')} | {self.F.addr.split(':')[0]}
        fam_sfx = {str(d.get('mac', ''))[-4:].upper(): d.get('name', '') for d in fam}
        t_names = {t.name for t in self.T}
        t_ips = {t.addr.split(':')[0] for t in self.T}
        lines, why = [], []
        for t in self.T:
            fr_names = {fam_sfx[s] for _, s in t.friends if s in fam_sfx}
            bad = sorted({n for n, ip in t.peers() if (n in fam_names or ip in fam_ips) and n not in fr_names})
            friend = sorted({n for n, ip in t.peers() if n in fr_names})
            lines.append('%s: %d family boards on line; in the peer list: %s%s' % (
                t.name, len(fam), bad or 'none', ('; friends (the STRICT rule keeps a friend): %s' % friend)
                if friend else ''))
            if bad:
                why.append('%s lists %s' % (t.name, bad))
        fbad = sorted({n for n, ip in self.F.peers() if n in t_names or ip in t_ips})
        lines.append('%s: T boards in the peer list: %s' % (self.F.name, fbad or 'none'))
        if fbad:
            why.append('%s lists %s' % (self.F.name, fbad))
        for t in self.T:
            d = self.M.device(t.mac)
            on = bool(d and d.get('online'))
            lines.append('the manager: %s %s' % (t.name, 'ON LINE' if on else 'not on line (%s)' % (
                'last seen %s s ago' % int(d.get('age', 0)) if d else 'not in the list')))
            if on:
                why.append('the manager sees %s on line' % t.name)
        return not why, lines + why

    # ---- G3 the calls and the invites
    def g3(self):
        A, F = self.A, self.F
        lines, why = [], []
        ma, mf = A.con.mark(), F.con.mark()
        c0 = F.counts()
        ctr = int(time.time() * 1000) & 0x7FFFFFFF
        for i in range(3):
            self.tap.send(pool.call_v2(self.keys, A.mac, F.mac, OP_CALL, ctr + i), udp_addr(F.addr))
            self.tap.send(pool.invite_v2(self.keys, A.mac, 'PONG', ctr + 10 + i), udp_addr(F.addr))
            time.sleep(0.3)
        nap(1.5)
        d = delta(c0, F.counts())
        drops = d['other'] + d['bad'] + d['who']
        lines.append('a T call + invite (version 2) from %s to %s, 3 each: %s counters [%s]' % (A.name, F.name, F.name,
                                                                                           dtext(d)))
        if drops < 6 or d['ok'] or con_grep(F.con, RING, mf):
            why.append('%s: %d drops of 6, ok %+d, ring lines %s' % (F.name, drops, d['ok'],
                                                                     con_grep(F.con, RING, mf)[:1]))
        # the family manager calls A (CALL, then the WAKE of TALKIE)
        mac = A.mac.hex().upper()
        self.M.post('/wk/%s/call' % mac)
        self.M.post('/wk/%s/ptt?on=1' % mac)
        time.sleep(self.a.call_wait)
        self.M.post('/wk/%s/ptt?on=0' % mac)
        _, st = self.M.get('/wk/%s/state' % mac)
        self.M.post('/wk/%s/end' % mac)
        # a WAKE of version 1 with the MAC of the manager
        for _ in range(3):
            self.tap.send(b'AC\x01' + MANAGER_MAC + A.mac + bytes([OP_WAKE]), udp_addr(A.addr))
            time.sleep(0.3)
        nap(1.5)
        ring = con_grep(A.con, RING, ma)
        lines.append('the manager called %s for %d s (CALL + WAKE), and 3 WAKE v1 with the manager MAC: paired %s, '
                     'ring/TALKIE lines on %s: %d' % (A.name, self.a.call_wait, bool(st.get('paired')), A.name,
                                                       len(ring)))
        if st.get('paired') or ring:
            why.append('%s answered: %s' % (A.name, ring[:1] or 'the manager paired'))
        return not why, lines + why

    # ---- G4 the pool layer
    def g4(self):
        A = self.A
        lines, why = [], []
        st0 = A.pool()
        mac = A.mac.hex().upper()
        # the family manager: ADOPT (the JOIN screen of A is closed) and the token JOIN
        code, r = self.M.post('/api/pool/adopt?mac=%s' % mac)
        lines.append('the manager ADOPT %s: %s' % (A.name, 'refused: %s' % r.get('msg') if not r.get('ok')
                                                  else 'STARTED'))
        if r.get('ok'):
            why.append('the manager started a JOIN of %s' % A.name)
            self.M.post('/api/pool/cancel?mac=%s' % mac)
        code, r = self.M.post('/api/pool/token?mac=%s' % mac)
        j, mp = {}, {}
        end = time.time() + 10
        while time.time() < end:
            mp = self.M.pool()
            j = (mp.get('joins') or {}).get(mac) or {}
            if not r.get('ok') or j.get('state') in ('failed', 'cancel', 'done', 'expired'):
                break
            time.sleep(0.5)
        if not r.get('ok'):
            tok = 'refused: %s' % r.get('msg')
        elif j:
            tok = 'the JOIN %s: %s' % (j.get('state', '?'), j.get('msg'))
        else:
            last = (mp.get('msgs') or [[0, 'none']])[-1]
            tok = 'no JOIN started (the last note of the manager: %s)' % (last[-1] if isinstance(last, list) else last)
        lines.append('the manager token JOIN %s: %s' % (A.name, tok))
        if r.get('ok') and j.get('state') == 'done':
            why.append('the token JOIN of the manager worked')
        # a stand-in manager: a P-256 key of this tool (not an admin of T)
        key = os.path.join(self.a.logs, 'standin_%d.pem' % int(time.time() * 1000))
        with contextlib.redirect_stdout(io.StringIO()):
            pc.p256_keygen(key)
        fp = pool.Pool.create('STAND-IN', pc.p256_pub(key))
        fp.upsert({'id': A.dev_id.hex(), 'pk': self.tp.find(A.mac.hex())['pk'], 'mac': A.mac.hex(),
                   'role': pool.ROLE_MEMBER, 'flags': 0, 'name': 'A'})
        st = A.pool()
        fp.version = int(st.get('v', 0)) + 5
        roster = fp.sign(key)
        same_id = pool.Pool.load(self.a.t_dir)
        same_id.version += 5
        body = pool.roster_body(same_id.roster())      # (pool T, a higher version, a key that is not an admin)
        forged = body + pool.sig_record(pc.p256_pub(key), pc.p256_sign(key, body))
        fhdr = lambda method, uri: {pool.HDR: pool.web_header(pc.rand(32), bytes.fromhex(A.pool()['bn']), fp.mid(),
                                                               int(time.time() * 1000), method, uri)}
        bx = bytes.fromhex(st.get('bx', '') or '00' * 32)
        tries = [
            ('op=mgr', 'POST', '/api/apps?op=mgr&lan=http%3A%2F%2F192.0.2.1%3A1&vpn=&newtok=' + pc.rand(8).hex() +
             '&auth=' + pc.rand(8).hex(), None, False),
            ('op=set (a roster of another pool)', 'POST', '/api/pool?op=set', {'roster': roster.hex()}, True),
            ('op=set (pool T, a key that is not an admin)', 'POST', '/api/pool?op=set', {'roster': forged.hex()}, True),
            ('op=key', 'POST', '/api/pool?op=key', pool.key_body(bx, A.dev_id, roster, 1, pc.rand(32), pc.rand(32)),
             True),
            ('op=webpass', 'POST', '/api/pool?op=webpass', {'pw': pc.rand(9).hex()}, True),
            ('a request with its header', 'GET', '/api/apps', None, True),
        ]
        for what, method, uri, body, hdr in tries:
            for pw in (True, False):
                code, ans = A.api(method, uri, body, auth=pw, headers=fhdr(method, uri) if hdr else None)
                refused = code in (401, 403) or (code == 200 and ans.get('ok') is False and
                                                 'not the pool manager' in str(ans.get('msg', '')))
                if what == 'a request with its header' and pw:
                    continue        # (the web password alone opens GET /api/apps: only the header is the test)
                lines.append('%s%s: %s' % (what, '' if pw else ' (no web password)', 'refused (%s%s)' % (
                    code, ', %s' % (ans.get('error') or ans.get('msg')) if (ans.get('error') or ans.get('msg'))
                    else '') if refused else 'TAKEN (%s)' % code))
                if not refused:
                    why.append('%s was not refused' % what)
        st1 = A.pool()
        keep = all(st1.get(k) == st0.get(k) for k in ('state', 'pool', 'v', 'epoch'))
        lines.append('%s after: pool %s..., version %s, epoch %s: %s' % (A.name, str(st1.get('pool'))[:8], st1.get('v'),
                                                                         st1.get('epoch'), 'the same' if keep else
                                                                         'CHANGED'))
        if not keep:
            why.append('the pool of %s changed' % A.name)
        return not why, lines + why

    # ---- G5 the replay
    def g5(self):
        B = self.B
        if not self.captured:
            return False, ['no T frame of %s came to this computer (G1 PONG must pair first)' % self.A.name]
        wait = self.capture_t + self.a.replay_wait - time.time()
        if wait > 0:
            say('   (G5 waits %d s: the replay comes %d s after the record)' % (wait, self.a.replay_wait))
            time.sleep(wait)
        m = B.con.mark()
        if not open_game([B], 'pong') or not open_lobby(B, 'pong'):
            to_menu([B])
            raise GateStop('%s: the lobby of PONG did not start' % B.name)
        c0 = B.counts()
        for _ in range(3):
            self.tap.send(self.captured, udp_addr(B.addr))
            time.sleep(0.3)
        nap(1.5)
        d = delta(c0, B.counts())
        paired = con_grep(B.con, PAIRED, m)
        to_menu([B])
        ok = d['old'] >= 3 and d['ok'] == 0 and not paired
        return ok, ['a T frame of %s (the LAN copy) sent again to %s 3 times, %d s after the record: counters [%s]' % (
            self.A.name, B.name, int(time.time() - self.capture_t), dtext(d))] + (
            [] if ok else ['%s took the replay (old %+d of 3, ok %+d)' % (B.name, d['old'], d['ok'])])

    # ---- G6 the forged member
    def g6(self):
        A, B, F = self.A, self.B, self.F
        lines, why = [], []
        mf, mb = F.con.mark(), B.con.mark()
        if not open_game([F], 'pong') or not open_lobby(F, 'pong'):
            to_menu([F])
            raise GateStop('%s: the lobby of PONG did not start' % F.name)
        v3 = lambda mac, i: b'AD' + mac + b'AL\x03' + bytes([ord('P'), 1, 4, 0, i & 255, i >> 8]) + b'\x00' * 4
        for i in range(3):
            for mac in (A.mac, MANAGER_MAC):
                self.tap.send(v3(mac, 100 + i), udp_addr(F.addr))
                self.tap.send(b'AC\x01' + mac + F.mac + bytes([OP_CALL]), udp_addr(F.addr))
            self.tap.send(b'AC\x01' + A.mac + B.mac + bytes([OP_CALL]), udp_addr(B.addr))
            time.sleep(0.3)
        nap(3.0)
        fl = con_grep(F.con, PAIRED + '|' + RING, mf)
        bl = con_grep(B.con, RING, mb)
        fpeers = {n for n, _ in F.peers()}
        to_menu([F])
        lines.append('a v3 frame + a call v1 with the MAC of %s and of the manager to %s (3 each): pairing/ring lines '
                     '%d, %s in its peer list: %s' % (A.name, F.name, len(fl), A.name, A.name in fpeers))
        lines.append('a call v1 with the MAC of %s to %s (3): ring lines %d' % (A.name, B.name, len(bl)))
        if fl or A.name in fpeers:
            why.append('%s took a forged packet: %s' % (F.name, fl[:1] or 'the peer list'))
        if bl:
            why.append('%s rang for a forged member: %s' % (B.name, bl[0][-60:]))
        return not why, lines + why

    # ---- G7 no effect on the family
    def g7(self):
        F = self.F
        lines, why = [], []
        mac = F.mac.hex().upper()
        for app in ('walkie', 'talkie'):
            m = F.con.mark()
            F.con.cmd('wake', 0.1)
            F.con.cmd('open %s' % app, 0.1)
            if not con_wait(F.con, r'open %s: ok' % app, 4, m):
                why.append('%s: no %s' % (F.name, app.upper()))
                continue
            nap(2.0)
            self.M.post('/wk/%s/call' % mac)
            st, end = {}, time.time() + PAIR_S
            while time.time() < end:
                _, st = self.M.get('/wk/%s/state' % mac)
                if st.get('paired'):
                    break
                time.sleep(0.5)
            self.M.post('/wk/%s/end' % mac)
            to_menu([F], 1.0)
            lines.append('%s: the manager %s with %s' % (app.upper(), 'PAIRED' if st.get('paired') else
                                                         'did NOT pair', F.name))
            if not st.get('paired'):
                why.append('%s: the manager did not pair with %s' % (app.upper(), F.name))
        d = self.M.device(F.mac) or {}
        code, b = self.M.get('/board/%s/api/boards' % mac)
        mem = next((m for m in self.M.pool().get('members', []) if str(m.get('mac', '')).upper() == mac), {})
        ver = d.get('ver') or d.get('fw') or ''
        lines.append('the manager: %s on line %s, firmware %s, a request through the manager %s, pool state %s%s' % (
            F.name, bool(d.get('online')), ver or '?', 'OK' if code == 200 and b.get('me') else 'FAILED (%s)' % code,
            mem.get('state') or '?', ' (the same pool)' if mem.get('same') else ''))
        if not d.get('online') or not ver or code != 200 or mem.get('state') != 'ok' or not mem.get('same'):
            why.append('the manager does not reach %s as before' % F.name)
        return not why, lines + why

    # ---- the restore
    def restore(self):
        say('RESTORE')
        ok = True
        for t in self.T:
            t.con.cmd('lanonly 0', 0.1)
        to_menu(self.all, 1.0)
        for t, name in self.added:
            _, fr, _ = friends(t)
            names = [n for n, _ in fr]
            if name in names:
                t.con.cmd('friends rm %d' % names.index(name), 0.5)
                say('   %s: the friend %s that the gate added is removed' % (t.name, name))
        for t in self.T:
            st = t.pool()
            if st.get('pool') == self.tp.pool_id.hex() and st.get('state') == 'ok':
                how = self.leave_t(t)
                st = t.pool()
                say('   %s left pool T (%s): state %s' % (t.name, how, st.get('state')))
                if st.get('state') != 'none':
                    ok = False
            pw = self.old_wp.get(t.mac.hex(), '')
            t.con.cmd('webpass set %s' % pw if pw else 'webpass off', 0.5)
            t.password = pw or None
            say('   %s: the web password %s' % (t.name, 'as before' if pw else 'off'))
        for t in self.T:
            try:
                self.join_family(t)
            except GateStop as e:
                ok = False
                say('   %s: NOT back in the family pool: %s' % (t.name, e))
        say('RESTORE %s' % ('OK' if ok else 'FAILED'))
        return ok

    def leave_t(self, t):
        """The board leaves pool T: pool_tool remove (the other member gets a new key); the last member: op=leave with
        the request header of T (a roster needs one member); else "pool leave" on the console."""
        p = pool_tool.load_pool(self.a.t_dir)
        if len(pool_tool.boards(p)) > 1:
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                code = pool_tool.main(['--dir', self.a.t_dir, '--key', self.a.t_key, 'remove', t.mac.hex()])
            if code == 0 and t.pool().get('state') == 'none':
                return 'pool_tool remove'
        else:
            m = p.find(t.mac.hex())
            try:
                pool_tool.board_for(p, m, self.a.t_dir).signed_post('leave', {})
                if t.pool().get('state') == 'none':
                    return 'op=leave of the pool manager T'
            except (pool_tool.Refused, pool_tool.ToolError, TypeError):
                pass
        t.con.cmd('pool leave', 1.0)
        return 'pool leave on the console'

    def join_family(self, t):
        """The family manager adopts the board again (as board_onboard.py step pool)."""
        mac = t.mac.hex().upper()
        fid = (self.M.pool().get('pool') or {}).get('id', '')
        end = time.time() + ONLINE_S
        while True:
            mem = next((m for m in self.M.pool().get('members', []) if str(m.get('mac', '')).upper() == mac), {})
            if mem.get('state') == 'ok' and mem.get('same') and t.pool().get('pool') == fid:
                say('   %s is in the family pool again (AUTO JOIN)' % t.name)
                return
            d = self.M.device(t.mac)
            if d and d.get('online'):
                break
            if time.time() > end:
                raise GateStop('the manager does not see it in %d s' % ONLINE_S)
            time.sleep(2)
        m = t.con.mark()
        t.con.cmd('pool join', 1.0)
        code, r = self.M.post('/api/pool/adopt?mac=%s' % mac)
        if not r.get('ok'):
            raise GateStop('the manager did not start the JOIN: %s' % r.get('msg'))
        want = str(r.get('sas', ''))
        t.con.cmd('pool', 0.5)
        ln = con_wait(t.con, SAS_RE.pattern, 6, m)
        got = SAS_RE.search(ln or '')
        if not got or '%s %s' % (got.group(1), got.group(2)) != want:
            self.M.post('/api/pool/cancel?mac=%s' % mac)
            raise GateStop('the board shows %s, the manager %s: NOT the same' % (
                '%s %s' % (got.group(1), got.group(2)) if got else 'no digits', want))
        t.con.cmd('pool allow ' + want.replace(' ', ''), 1.0)
        code, r = self.M.post('/api/pool/confirm?mac=%s' % mac)
        if not r.get('ok'):
            raise GateStop('CONFIRM: %s' % r.get('msg'))
        end = time.time() + JOIN_S
        while time.time() < end:
            time.sleep(1)
            j = (self.M.pool().get('joins') or {}).get(mac) or {}
            if j.get('state') == 'done':
                st = t.pool()
                if st.get('pool') != fid or st.get('state') != 'ok':
                    raise GateStop('the JOIN is done, but the board says %s' % st.get('state'))
                say('   %s is in the family pool again (the 6 digits %s, roster version %s)' % (t.name, want,
                                                                                               st.get('v')))
                return
            if j.get('state') in ('failed', 'cancel', 'expired'):
                raise GateStop('the JOIN %s: %s' % (j['state'], j.get('msg')))
        raise GateStop('no result of the JOIN in %d s' % JOIN_S)

    # ---- the run
    def run(self):
        a = self.a
        url = a.manager or default_manager()
        if not url:
            raise GateStop('no manager address: give --manager, or set TFN_MANAGER')
        SECRETS.add(url)
        self.M = Manager(url)
        say('THE P4 GATE: the test pool T against the family pool (the manager of the store setup%s)' % (
            '' if not a.manager else ', given'))
        steps = [('G1', self.g1), ('G2', self.g2), ('G3', self.g3), ('G4', self.g4), ('G6', self.g6),
                 ('G7', self.g7), ('G5', self.g5)]
        restored = True
        try:
            self.tap = Tap()        # (first: a port in use stops the gate before it changes a board)
            self.setup()
            self.ready = True
            for g, fn in steps:
                if not self.want(g):
                    continue
                try:
                    ok, lines = fn()
                except GateStop as e:
                    ok, lines = False, ['the check stopped: %s' % e]
                self.result(g, ok, lines)
        finally:
            if self.tap:
                self.tap.close()
            try:
                if self.ready and not a.no_restore:
                    restored = self.restore()
            except (GateStop, pool.PoolError, OSError) as e:
                restored = False
                say('RESTORE FAILED: %s' % e)
            finally:
                for c in self.cons.values():
                    c.close()
        done = [g for g in CHECKS if g in self.res]
        good = all(self.res[g] for g in done)
        say('GATE %s: %s%s' % ('PASS' if good and restored else 'FAIL',
                               ' '.join('%s=%s' % (g, 'PASS' if self.res[g] else 'FAIL') for g in done),
                               '' if restored else ' (THE RESTORE FAILED)'))
        return 0 if good and restored else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--t-dir', required=True, help='the folder of pool T (pool_tool.py --dir)')
    ap.add_argument('--t-key', required=True, help='the admin key of pool T (a TEST key)')
    ap.add_argument('--a-con', required=True)
    ap.add_argument('--b-con', required=True)
    ap.add_argument('--fam', required=True, help='the address of the family board F')
    ap.add_argument('--fam-con', required=True)
    ap.add_argument('--fam-kind', default='cores3', choices=('cores3', 'lcd349'))
    ap.add_argument('--fam-pass', default=os.environ.get('TFN_BOARD_PASSWORD') or None)
    ap.add_argument('--manager', default='')
    ap.add_argument('--games', nargs='+', default=['pong', 'zap'], choices=sorted(GAMES))
    ap.add_argument('--secs', type=float, default=12)
    ap.add_argument('--call-wait', type=float, default=8)
    ap.add_argument('--replay-wait', type=float, default=60)
    ap.add_argument('--only', default=','.join(CHECKS))
    ap.add_argument('--old-webpass', default='')
    ap.add_argument('--no-restore', action='store_true')
    ap.add_argument('--logs', default=os.path.join(ROOT, 'tests', 'host', 'pool_gate'))
    a = ap.parse_args(argv)
    a.only = [g.strip().upper() for g in a.only.split(',') if g.strip()]
    if 'G5' in a.only and 'pong' not in a.games:
        a.games = ['pong'] + a.games
    if 'G5' in a.only and 'G1' not in a.only:
        a.only.append('G1')     # (the record of G5 comes from the PONG game of G1)
    try:
        return Gate(a).run()
    except (GateStop, pool.PoolError, pool_tool.ToolError, OSError) as e:
        say('gate: %s' % e)
        return 2


if __name__ == '__main__':
    sys.exit(main())
