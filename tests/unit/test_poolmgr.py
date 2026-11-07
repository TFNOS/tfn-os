#!/usr/bin/env python3
"""The test of the pool commands of poolmgr (P6 design 2.5-2.9, 3.1; poolmgr/pm_pool.py, pm_net.py, pm_board.py,
pm_data.py) against FAKE boards (tests/unit/poolmgr_fake.py and the subclass PmBoard of this file). No real board and
no network: a temporary data folder, the addresses 127.0.0.1 and 127.0.0.2, a free UDP port in place of 47474.

The steps: init (the official keys, keys A and R, no key R in clear on the disk, a pool is never overwritten),
the faults of init (the two pass phrases come first and name their files; a fault between key A and the pool leaves
nothing; the empty parts of an older poolmgr; a key with no pool stops init; a pool name with no ASCII letter), the
channel command (show; set only with the same official keys), a damaged pool/recover.json (recover stops, the file
stays), the USB console (DTR and RTS low at the open), the UDP listener ('A' 'J' and 'A' 'P' packets, a packet of
another pool, a port in use), adopt of 4 boards (the USB console, --digits with a tap, wrong digits = stop, --all with
the consoles of two boards and the names of their 'A' 'J' packets), the JOIN with no ALLOW and with NO, boards with a
web password (401 "login": a question, --password, the password that the pool gave before; the board keeps its own
password), the web passwords, the friends of all pairs and the friends limit (16: exit 1), friends --check, push,
list and list --live (the store), pool_tool.py show --live on the data folder, a new address of a board (the beacon),
the big POST (post_stream), remove in the order of design 2.8 (the web password of the pool off, kept, or kept by a
board that does not answer; the friend entries that a member did not take out wait and go out at push and friends
--check), the upkeep of serve (an older roster, "damaged", LEAVE on a board with the lines of the friend faults, the
friend entries that wait, the limit of once a minute), and a second pool (step_stale): remove of a member whose last
address has another board now (no request with the web password goes there), a board that answers op=webpass but
keeps the password (remove and webpass new: the entry stays; with no answer to the read after it, webpass new keeps
the new password), the kept password only for an address that the person gives, a kept password that does not open
the board, a web page that is not a TFN board at the address of a member (push and remove: a line, no trace, a new
pool key), another TFN board at an old address of a member (push and list --live: no signed request, no web
password), a channel host that answers bytes that are not HTTP (doctor: a WARN line), adopt --all with an InputError
of one board, with a device key of /api/id that is not hex or not text, and with a stop after a JOIN, the questions
(an empty answer is a try; no terminal and the end of the input stop with exit 2).

The test uses the real modules pm_keys.py (keys A and R, the backup) and pm_store.py (op=mgr of remove). All keys are
TEST keys in the temporary folder.

Usage: python tests/unit/test_poolmgr.py
"""
import base64
import builtins
import contextlib
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
PM = os.path.join(ROOT, 'poolmgr')
sys.path.insert(0, HERE)
sys.path.insert(0, PM)
import pm_board  # noqa: E402
import pm_data  # noqa: E402
import pm_net  # noqa: E402
import pm_ota  # noqa: E402
import pm_pool  # noqa: E402
import poolmgr  # noqa: E402
from pm_shared import InputError, fw_sign, pool, pool_tool  # noqa: E402
from pm_shared import pool_crypto as pc  # noqa: E402
import poolmgr_fake  # noqa: E402
from poolmgr_fake import FakeBoard, FakeConsole  # noqa: E402

FAILS = []
CHECKS = [0]
SEQ = []            # (board name, method, uri) of each request to a fake board, in order
CONS = {}           # the console port names of the test -> the fake board
REAL_INPUT = builtins.input
ASK_SECRET = pm_pool.ask_secret


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


def cli(*argv):
    """poolmgr.main with the output caught: (exit code, the output)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            code = poolmgr.main(list(argv))
        except SystemExit as e:
            code = e.code
    return code, out.getvalue()


def quiet(fn, *a, **k):
    """fn(*a, **k) with the output caught: (the result or the exception, the output)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        try:
            r = fn(*a, **k)
        except Exception as e:  # the test looks at the exception
            r = e
    return r, out.getvalue()


def has_address(host):
    """True when this computer can bind the address host (Linux and Windows: all of 127.0.0.0/8; macOS: only 127.0.0.1
    when no alias is set)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind((host, 0))
        return True
    except OSError:
        return False
    finally:
        s.close()


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('0.0.0.0', 0))
    p = s.getsockname()[1]
    s.close()
    return p


# ---------------------------------------------------------------- the fake board with the routes of poolmgr
class PmBoard(FakeBoard):
    """FakeBoard + /api/friends (16 at most), POST /api/apps?op=mgr (only the pool manager), GET /api/boards, POST
    /api/ota (the body length), the 'A' 'J' and 'A' 'P' packets each 0.2 s to the UDP port of pm_net, the log SEQ of
    the requests, a move to another address, a fixed eb/nb for op=hello (a known SAS), the requests with a Basic
    password (basic), and op=webpass that answers but keeps the password (webpass_keep: the answer of the firmware
    comes before the change)."""

    def __init__(self, name, mac, d):
        self.friends = []          # [mac upper hex, name]
        self.friends_down = False  # /api/friends does not answer (503)
        self.mgr = None
        self.ota_len = None
        self.ota_refuse = False
        self.fixed = None          # (eb_priv, nb) for op=hello
        self.host = '127.0.0.1'
        self.beacons = True
        self.basic = []            # the requests that came with an Authorization header (the URIs)
        self.webpass_keep = False
        self.bad_pk = None         # the "pk" of /api/id when it is not None (a device with a bad answer)
        super().__init__(name, mac, d, legacy=False)
        self.stop_ev = threading.Event()
        self.udp = None
        self.bind_udp()
        threading.Thread(target=self.beat, daemon=True).start()

    def bind_udp(self):
        if self.udp is not None:
            self.udp.close()
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind((self.host, 0))

    def beat(self):
        while not self.stop_ev.wait(0.2):
            pkt = None
            with self.lock:
                if self.beacons and self.join_open and not self.roster:
                    pkt = pool.join_pkt(self.mac, self.id, self.name)
                elif self.beacons and self.roster and self.pool_k:
                    pkt = pool.beacon_seal(self.keys(), pool.beacon_plain(self.mac, self.name, '', 0, self.id))
                udp = self.udp
            if pkt:
                try:
                    udp.sendto(pkt, ('127.0.0.1', pm_net.UDP_PORT))
                except OSError:
                    pass

    def close(self):
        self.stop_ev.set()
        self.srv.shutdown()
        self.srv.server_close()

    def move(self, host):
        """The board gets a new address (a new DHCP lease): the same web port on another host."""
        port = self.srv.server_address[1]
        self.srv.shutdown()
        self.srv.server_close()
        with self.lock:
            self.host = host
            self.bind_udp()
        import http.server
        self.srv = http.server.ThreadingHTTPServer((host, port), poolmgr_fake.make_handler(self))
        self.ip = '%s:%d' % (host, port)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def op_hello(self, b, signed):
        if not self.fixed:
            return super().op_hello(b, signed)
        if not self.join_open:
            return 409, {'error': 'no_join'}
        eb_priv, nb = self.fixed
        eb_pub = pc.x25519_pub(eb_priv)
        c = bytes.fromhex(b['c'])
        self.j = {'step': 'hello', 'admin': bytes.fromhex(b['admin']), 'c': c, 'eb_priv': eb_priv, 'eb': eb_pub,
                  'nb': nb, 'token': False}
        sig = fw_sign.sign_raw(self.pem, pool.msg_b(c, eb_pub, nb))
        return 200, {'eb': eb_pub.hex(), 'nb': nb.hex(), 'pk': self.pk.hex(), 'id': self.id.hex(),
                     'bx': self.bx_pub.hex(), 'bxs': self.bxs.hex(), 'sig': sig.hex()}

    def op_webpass(self, b, signed):
        if self.webpass_keep and signed:
            return 200, {'ok': True}
        return super().op_webpass(b, signed)

    def handle(self, method, uri, headers, body):
        with self.lock:
            SEQ.append((self.name, method, uri))
            if headers.get('Authorization'):
                self.basic.append(uri)
            path, _, query = uri.partition('?')
            if method == 'GET' and path == '/api/id' and self.bad_pk is not None:
                return 200, {'id': self.id_text(), 'pk': self.bad_pk, 'x25519': '', 'xs': '', 'alg': 'p256'}
            if path not in ('/api/friends', '/api/apps', '/api/boards', '/api/ota'):
                return super().handle(method, uri, headers, body)
            signed = self.auth_ok(method, uri, headers.get(pool.HDR))
            deny = self.gate(headers, signed)
            if deny:
                return deny
            q = urllib.parse.parse_qs(query, keep_blank_values=True)
            if path == '/api/friends' and self.friends_down:
                return 503, {'error': 'busy'}
            if path == '/api/apps' and method == 'GET':
                return 200, {'kind': 'lcd349', 'mgr': bool(self.mgr and self.mgr.get('lan')), 'apps': []}
            if path == '/api/friends' and method == 'GET':
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
            if path == '/api/apps' and method == 'POST' and q.get('op') == ['mgr']:
                if not (self.roster and signed):
                    return 200, {'ok': False, 'msg': 'not the pool manager'}
                self.mgr = {k: (q.get(k) or [''])[0] for k in ('lan', 'vpn', 'newtok', 'auth')}
                return 200, {'ok': True, 'msg': 'the manager store: %s' % (self.mgr['lan'] or 'none')}
            if path == '/api/boards' and method == 'GET':
                return 200, {'me': {'name': self.name, 'ip': self.host, 'app': '', 'ver': '1.0.104',
                                    'kind': 'lcd349', 'sig': 'enforce', 'minsv': 1}, 'lan': []}
            if path == '/api/ota' and method == 'POST':
                if not signed:
                    return 401, {'error': 'auth'}
                if self.ota_refuse:
                    return 400, {'ok': False, 'code': 'sig', 'msg': 'the signature is not good'}
                self.ota_len = len(body)
                return 200, {'ok': True, 'msg': 'the update is in'}
            return 404, {'error': 'route'}


def same_pool(p, b):
    return (b.roster is not None and b.roster.pool_id == p.pool_id and b.roster.version == p.version and
            b.pool_k == p.pool_k and b.epoch == p.epoch and b.wk == p.wk(b.id.hex()))


def fmac(b):
    return b.mac.hex().upper()


def test_keys(d):
    """A tfn-keys.json of a release with TEST keys (the form of sdk/tools/release_pack.py)."""
    pubs = [fw_sign.keygen(os.path.join(d, 'test_%s.pem' % n)) for n in ('store', 'ota', 'ota2')]
    j = {'tfn_keys': 1, 'release': '9.9.9', 'store': {'pubkey': pubs[0].hex(), 'key_id': ''},
         'ota': {'pubkey': pubs[1].hex(), 'key_id': ''}, 'ota_backup': {'pubkey': pubs[2].hex(), 'key_id': ''}}
    path = os.path.join(d, 'tfn-keys.json')
    with open(path, 'w') as h:
        json.dump(j, h)
    return path, pubs


# ---------------------------------------------------------------- the steps
def step_init(d, root):
    keys, pubs = test_keys(d)
    code, out = cli('--data', root, 'init', 'HOME')
    expect(code == 2 and '--keys <channel.json>' in out,
           'init with no channel.json and no --keys: exit 2, the hint names --keys (%r)' % out[-300:])
    code, out = cli('--data', os.path.join(d, 'other'), 'init', 'X' * 25, '--keys', keys)
    expect(code == 2 and 'pool name' in out, 'init: a pool name over 24 bytes: exit 2')
    code, out = cli('--data', root, 'init', 'HOME', '--keys', keys)
    expect(code == 0, 'init: %s' % out[-600:])
    expect('has only the keys' in out and '--keys <channel.json>' in out and 'poolmgr channel <channel.json>' in out,
           'init with a tfn-keys.json: the WARN line')
    data = pm_data.Data(root)
    p = data.load()
    expect(p is not None and p.name == 'HOME' and len(p.admins) == 2 and p.flags == 0, 'init: the pool, A and R')
    expect(pm_data.rights(root) == ([], ''), 'init: only this user (and SYSTEM, the administrators) has rights on '
           'the data folder: %r' % (pm_data.rights(root),))
    expect(not [n for n in os.listdir(root) if n.startswith('.init-')], 'init: no temporary folder stays')
    try:
        pm_ota.channel(data)
        expect(False, 'channel(): the channel of a tfn-keys.json has no web site')
    except InputError as e:
        expect('poolmgr channel <channel.json' in str(e) and '<tfn-keys.json>' not in str(e),
               'channel(): the hint of a pool that exists: %s' % e)
    expect(p is not None and p.admins[0] == pc.p256_pub(data.key_a), 'init: key A = admin 1')
    mgr = [m for m in (p.members if p else []) if m['role'] == pool.ROLE_MANAGER]
    expect(len(mgr) == 1 and mgr[0]['mac'] == pool.MANAGER_MAC.hex() and mgr[0]['id'] == pc.device_id(
        p.admins[0]).hex(), 'init: the manager member (ID of key A, the MAC hint)')
    expect(p is not None and p.version == 1 and pool.roster_parse(p.roster_file).version == 1, 'init: roster v1')
    ch = data.cfg.get('channel') or {}
    expect(ch.get('store_pub') == pubs[0].hex() and ch.get('ota_pub') == [pubs[1].hex(), pubs[2].hex()],
           'init: the official keys in config.json')
    expect(pc.key_id(pubs[1]).hex() in out and pc.key_id(pubs[0]).hex() in out, 'init prints the key IDs')
    expect(os.path.isfile(os.path.join(d, 'HOME-recovery.tfnkey')) and os.path.isfile(
        os.path.join(d, 'HOME-backup.tfnbak')), 'init: the recovery key file and the first backup')
    expect('poolmgr adopt --all' in out and 'SETUP > POOL > JOIN' in out, 'init: the next steps')
    # no key R in clear: each file with a private key holds key A only
    r_pub = p.admins[1] if p else b''
    clear = []
    for top in (root, d):
        for dp, _, fs in os.walk(top):
            for f in fs:
                fp = os.path.join(dp, f)
                with open(fp, 'rb') as h:
                    if b'PRIVATE KEY' not in h.read():
                        continue
                if os.path.dirname(fp) == d and f.startswith('test_'):
                    continue
                if pc.p256_pub(fp) == r_pub:
                    clear.append(fp)
    expect(not clear, 'init: no key R in clear on the disk %s' % clear)
    for f in ('HOME-recovery.tfnkey', 'HOME-backup.tfnbak'):
        code, out = cli('--data', root, 'check-backup', os.path.join(d, f))
        expect(code == 0 and 'CHECK OK' in out, 'init: check-backup %s: %s' % (f, out[-300:]))
    code, out = cli('--data', root, 'init', 'HOME2', '--keys', keys)
    expect(code == 2 and 'never overwritten' in out, 'init: a pool is never overwritten')
    return data, keys, pubs


def made_files(d, base):
    """The files <base>-* of init in the folder d."""
    return sorted(n for n in os.listdir(d) if n.startswith(base + '-'))


def step_init_faults(d, keys):
    """The faults of init: nothing is made before the two pass phrases (and the questions name the files); a fault
    between key A and the pool (also Ctrl+C) leaves nothing, and a new init works; the empty parts of an older
    poolmgr; a key with no pool stops init and stays; a pool name with no ASCII letter or digit."""
    r1 = os.path.join(d, 'f1')
    phrase = os.environ.pop(pm_pool.PHRASE_ENV)
    asked, answers = [], []
    real_ask = pm_pool.ask_secret

    def fake_ask(prompt=''):
        asked.append(prompt)
        return answers.pop(0)
    pm_pool.ask_secret = fake_ask
    try:
        answers[:] = ['phrase number one', 'phrase number two']
        code, out = cli('--data', r1, 'init', 'FAULT', '--keys', keys)
        expect(code == 2 and not os.path.exists(r1) and not made_files(d, 'FAULT'), 'init: two pass phrases of key R '
               'that are not the same: nothing is made (%r)' % out[-200:])
        expect(len(asked) == 2 and 'FAULT-recovery.tfnkey' in asked[0] and 'key R' in asked[0],
               'init: the question names the key file: %r' % asked)
        del asked[:]
        answers[:] = ['phrase number one', 'phrase number one', 'short', 'short']
        code, out = cli('--data', r1, 'init', 'FAULT', '--keys', keys)
        expect(code == 2 and not os.path.exists(r1) and not made_files(d, 'FAULT') and len(asked) == 4 and
               'FAULT-backup.tfnbak' in asked[2], 'init: the pass phrase of the backup comes before the keys: nothing '
               'is made (%r %r)' % (asked, out[-200:]))
    finally:
        pm_pool.ask_secret = real_ask
        os.environ[pm_pool.PHRASE_ENV] = phrase
    # a fault between key A and pool.json (Ctrl+C at the backup; a fault of the disk at the save of the pool)
    import pm_keys
    real_backup, real_save = pm_keys.backup, pool.Pool.save

    def stop(*a):
        raise KeyboardInterrupt()

    def bad_save(self, folder):
        raise OSError('a test fault of the disk')
    for name, patch in (('Ctrl+C at the backup', ('backup', stop)), ('a fault of the disk', ('save', bad_save))):
        if patch[0] == 'backup':
            pm_keys.backup = patch[1]
        else:
            pool.Pool.save = patch[1]
        try:
            code, out = cli('--data', r1, 'init', 'FAULT', '--keys', keys)
        finally:
            pm_keys.backup, pool.Pool.save = real_backup, real_save
        expect(code == 2 and not os.path.exists(r1) and not made_files(d, 'FAULT'), 'init: %s between key A and the '
               'pool: nothing stays (%r %r)' % (name, out[-200:], os.listdir(r1) if os.path.isdir(r1) else None))
    code, out = cli('--data', r1, 'init', 'FAULT', '--keys', keys)
    expect(code == 0 and pm_data.Data(r1).load() is not None and len(made_files(d, 'FAULT')) == 2,
           'init again after the faults: %s' % out[-300:])
    # the empty parts of an older poolmgr (a command that stopped with "no pool") and its lock: init takes them
    r2 = os.path.join(d, 'f2')
    for part in ('keys', 'pool', 'log', os.path.join('cache', 'fw'), os.path.join('cache', 'store'), 'service'):
        os.makedirs(os.path.join(r2, part))
    with open(os.path.join(r2, 'lock'), 'wb') as h:
        h.write(b'4242\n')
    code, out = cli('--data', r2, 'init', 'PARTS', '--keys', keys)
    expect(code == 0 and pm_data.Data(r2).load() is not None, 'init into the empty parts of an older poolmgr: %s' %
           out[-300:])
    # a key with no pool (an init of an older poolmgr that stopped): init stops, the key stays
    r3 = os.path.join(d, 'f3')
    os.makedirs(os.path.join(r3, 'keys'))
    fw_sign.keygen(os.path.join(r3, 'keys', 'admin_a.pem'))
    before = open(os.path.join(r3, 'keys', 'admin_a.pem'), 'rb').read()
    code, out = cli('--data', r3, 'init', 'LEFT', '--keys', keys)
    expect(code == 2 and 'key files but no pool' in out and 'keys/admin_a.pem' in out and
           open(os.path.join(r3, 'keys', 'admin_a.pem'), 'rb').read() == before and not made_files(d, 'LEFT'),
           'init: a key with no pool stops init, the key stays: %s' % out[-300:])
    code, out = cli('--data', r3, 'doctor')
    expect('WARN the data folder has keys but no pool' in out, 'doctor: the parts of a data folder with no pool')
    # a pool name with no ASCII letter or digit: "pool-" and 8 hex digits of the pool ID
    pid = bytes.fromhex('a1b2c3d4' + '00' * 12)
    expect(pm_pool.file_name('בית', pid) == 'pool-a1b2c3d4' and pm_pool.file_name('OUR HOME', pid) ==
           'OUR_HOME' and pm_pool.file_name('-!-', pid) == 'pool-a1b2c3d4', 'file_name')
    r4 = os.path.join(d, 'f4')
    code, out = cli('--data', r4, 'init', 'בית', '--keys', keys)
    p4 = pm_data.Data(r4).load()
    base = 'pool-%s' % (p4.pool_id.hex()[:8] if p4 else '?')
    expect(code == 0 and made_files(d, base) == [base + '-backup.tfnbak', base + '-recovery.tfnkey'],
           'init of a Hebrew pool name: the files %s-*: %s' % (base, out[-300:]))


def free_tcp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def write_channel(d, name, store, ota):
    """A channel.json of a release (the form of sdk/tools/release_pack.py) with TEST keys."""
    path = os.path.join(d, name)
    with open(path, 'w') as h:
        json.dump({'format': 1, 'pages': 'https://example.org/tfn-os/', 'repo': 'example/tfn-os',
                   'store_pub': store.hex(), 'ota_pub': [k.hex() for k in ota]}, h)
    return path


def step_channel(d, root, keys, pubs):
    """poolmgr channel: with no file, the channel of the pool; with a file, the web site of a pool that exists, only
    with the same official keys; doctor with the channel of the pool."""
    import pm_serve
    pm_serve.PORT = free_tcp_port()
    code, out = cli('--data', root, 'channel')
    expect(code == 0 and 'none: update and store sync do not work' in out and 'poolmgr channel' in out and
           '--keys' not in out, 'channel: a pool with no web site: %s' % out[-300:])
    code, out = cli('--data', root, 'doctor')
    warn = [ln for ln in out.splitlines() if ln.startswith('WARN') and 'the channel of the pool' in ln]
    expect(warn and 'poolmgr channel <channel.json' in warn[0], 'doctor: the channel of the pool (%r)' % warn)
    expect('OK   key A: key ID' in out, 'doctor: key A is the admin key A of the pool')
    good = write_channel(d, 'channel.json', pubs[0], pubs[1:])
    other = write_channel(d, 'other.json', pubs[0], [pubs[1], fw_sign.keygen(os.path.join(d, 'test_other.pem'))])
    first = pm_data.Data(root)
    first.load()
    before = dict(first.cfg['channel'])
    code, out = cli('--data', root, 'channel', os.path.join(d, 'none.json'))
    expect(code == 2 and 'no file' in out and 'none.json' in out, 'channel with a file that is not there: exit 2 '
           '(%r)' % out[-300:])
    code, out = cli('--data', root, 'channel', keys)
    expect(code == 2 and 'no web site' in out and 'has only the keys' in out,
           'channel with a tfn-keys.json: exit 2 (%r)' % out[-300:])
    code, out = cli('--data', root, 'channel', other)
    expect(code == 2 and 'other official keys' in out, 'channel with another OTA key: exit 2 (%r)' % out[-300:])
    code, out = cli('--data', os.path.join(d, 'nopool'), 'channel', good)
    expect(code == 2 and 'no pool in' in out and 'poolmgr restore <file>' in out and
           not os.path.exists(os.path.join(d, 'nopool')), 'channel with no pool: exit 2, no folder made (%r)' %
           out[-300:])
    data = pm_data.Data(root)
    data.load()
    expect(data.cfg['channel'] == before, 'a refused channel does not change config.json')
    code, out = cli('--data', root, 'channel', good)
    expect(code == 0 and 'https://example.org/tfn-os/' in out and pc.key_id(pubs[1]).hex() in out and
           'BACKUP IS OLD' in out, 'channel: %s' % out[-400:])
    data = pm_data.Data(root)
    data.load()
    ch = pm_ota.channel(data)
    expect(ch['pages'] == 'https://example.org/tfn-os/' and ch['repo'] == 'example/tfn-os' and
           ch['store_pub'] == pubs[0].hex() and ch['ota_pub'] == [pubs[1].hex(), pubs[2].hex()],
           'channel: config.json has the web site and the same keys')
    code, out = cli('--data', root, 'channel')
    expect(code == 0 and 'https://example.org/tfn-os/' in out and 'example/tfn-os' in out,
           'channel with no file shows the channel of the pool: %s' % out[-300:])
    # a web site with no HTTP service (the next steps run with it: no download)
    local = write_channel(d, 'channel_local.json', pubs[0], pubs[1:])
    with open(local, 'rb') as h:
        j = json.loads(h.read().decode())
    j['pages'], j['repo'] = 'http://127.0.0.1:9', 'test/tfn-os'
    with open(local, 'w') as h:
        json.dump(j, h)
    code, out = cli('--data', root, 'channel', local)
    data = pm_data.Data(root)
    data.load()
    expect(code == 0 and data.cfg['channel']['pages'] == 'http://127.0.0.1:9' and
           pm_ota.channel(data)['repo'] == 'test/tfn-os', 'channel <channel.json>: the channel of the pool: %s' %
           out[-300:])
    # the web site of the channel answers with bytes that are not HTTP (another service at that address): doctor
    # gives a WARN line, no trace
    nh = poolmgr_fake.NotHttp()
    keep = data.cfg['channel']['pages']
    try:
        data.cfg['channel']['pages'] = 'http://%s' % nh.ip
        data.save_cfg()
        code, out = cli('--data', root, 'doctor')
    finally:
        nh.close()
        data.cfg['channel']['pages'] = keep
        data.save_cfg()
    warn = [ln for ln in out.splitlines() if ln.startswith('WARN') and 'the channel of the pool' in ln]
    expect(code in (0, 1) and warn and 'no HTTP answer' in warn[0] and 'Traceback' not in out,
           'doctor with a channel host that is not HTTP: a WARN line (%r)' % (warn or out[-300:]))


class FakeSerialError(Exception):
    pass


class FakeSerialPort:
    """The serial.Serial of a fake pyserial module. It keeps the DTR and RTS levels at the open and each change of
    the levels after the open (the lines of an open port)."""
    opened = []

    def __init__(self, *a, **k):
        self.port, self.baudrate, self.timeout, self.write_timeout = None, 9600, None, None
        self.is_open = False
        self._dtr = self._rts = True   # (the defaults of pyserial)
        self.levels = []
        self.lines = []
        if a or k:
            self.port = a[0] if a else k.get('port')
            self.open()

    @property
    def dtr(self):
        return self._dtr

    @dtr.setter
    def dtr(self, v):
        self._dtr = v
        if self.is_open:
            self.levels.append((self._dtr, self._rts))

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, v):
        self._rts = v
        if self.is_open:
            self.levels.append((self._dtr, self._rts))

    def open(self):
        if self.port == 'COM_GONE':
            raise FakeSerialError('could not open port %s' % self.port)
        self.is_open = True
        self.levels.append((self._dtr, self._rts))
        FakeSerialPort.opened.append((self.port, self.baudrate, self.timeout, self.write_timeout, self.levels))

    def write(self, data):
        self.lines.append(data)

    def read(self, n):
        return b''

    def close(self):
        pass


def step_serial():
    """The USB console of adopt --console and store trust --console (pool_tool.SerialConsole): DTR and RTS low at the
    end, and never DTR low with RTS high (the restart state of an ESP32-S3) on the way: Windows opens with both low,
    Linux and macOS open with both high and set RTS low first; 115200 baud, timeouts; a port that does not open gives
    a STOP line of the tool (ToolError), not a trace."""
    import types
    fake = types.ModuleType('serial')
    fake.Serial, fake.SerialException = FakeSerialPort, FakeSerialError
    real = sys.modules.get('serial')
    sys.modules['serial'] = fake
    try:
        import pm_store
        for con in (pool_tool.SerialConsole('COM_A', posix=False), pm_store.open_console('COM_B'),
                    pool_tool.SerialConsole('COM_C', posix=True)):
            con.write_line('pool join')
            con.close()
        try:
            pool_tool.open_console('COM_GONE')
            gone = ''
        except pool_tool.ToolError as e:
            gone = str(e)
    finally:
        if real is None:
            sys.modules.pop('serial', None)
        else:
            sys.modules['serial'] = real
    here = [(False, False)] if os.name != 'posix' else [(True, True), (True, False), (False, False)]
    expect(FakeSerialPort.opened == [('COM_A', 115200, 0.2, 2, [(False, False)]), ('COM_B', 115200, 0.2, 2, here),
                                     ('COM_C', 115200, 0.2, 2, [(True, True), (True, False), (False, False)])],
           'the USB console: DTR and RTS low at the end, never DTR low with RTS high (Windows: low at the open; '
           'Linux and macOS: RTS low first) (%r)' % FakeSerialPort.opened)
    expect('COM_GONE' in gone and 'does not open' in gone, 'a console that does not open: ToolError (%r)' % gone)


def step_recover_damaged(d, root):
    """A damaged pool/recover.json (a cut file, or boards that wait with no old_mid or old_wk) stops recover with
    exit 2: the file keeps its bytes (the only copy of the old request keys), key A and the pool do not change."""
    data = pm_data.Data(root)
    state = os.path.join(data.pool_dir, 'recover.json')
    rec = os.path.join(d, 'HOME-recovery.tfnkey')
    with open(data.key_a, 'rb') as h:
        key_a = h.read()
    with open(os.path.join(data.pool_dir, 'pool.json'), 'rb') as h:
        pool_json = h.read()
    full = json.dumps({'format': 1, 'old_mid': '00' * 16, 'new_mid': '11' * 16, 'pending': ['22' * 16],
                       'old_wk': {'22' * 16: '33' * 32}}).encode()
    no_wk = json.dumps({'format': 1, 'old_mid': '00' * 16, 'new_mid': '11' * 16, 'pending': ['22' * 16]}).encode()
    bad_hex = full.replace(b'"old_mid": "' + b'00' * 16, b'"old_mid": "zz')
    for what, blob in (('a cut file', full[:40]), ('no old_wk', no_wk), ('an old_mid that is not hex', bad_hex),
                       ('a list', b'[1, 2]\n')):
        with open(state, 'wb') as h:
            h.write(blob)
        code, out = cli('--data', root, 'recover', '--recovery', rec)
        with open(state, 'rb') as h:
            after = h.read()
        with open(data.key_a, 'rb') as h:
            key_after = h.read()
        with open(os.path.join(data.pool_dir, 'pool.json'), 'rb') as h:
            pool_after = h.read()
        expect(code == 2 and 'is damaged' in out and 'recover.json' in out and 'Traceback' not in out and
               after == blob and key_after == key_a and pool_after == pool_json,
               'recover with %s in pool/recover.json: exit 2, nothing changes (%r)' % (what, out[-300:]))
    os.remove(state)


def step_listener(d, data, b1):
    """'A' 'J' + 'A' 'P' packets, the port rule, a packet of another pool and garbage, a port in use."""
    b1.join_open = True
    data.set_addr(fmac(b1), b1.ip, '', b1.id.hex())
    joins, beacons = pm_net.listen(data, 1.0)
    j = [x for x in joins if x['mac'] == fmac(b1)]
    expect(len(j) == 1 and j[0]['ip'] == b1.ip and j[0]['name'] == 'T1' and j[0]['id'] == b1.id.hex(),
           'listen: the A J packet of a JOIN screen (%r)' % joins)
    expect(pm_net.with_port('192.0.2.3', '127.0.0.1:8080') == '192.0.2.3:8080' and
           pm_net.with_port('192.0.2.3', '192.0.2.4') == '192.0.2.3', 'with_port')
    other = pool.derive(os.urandom(32), os.urandom(16), 0)
    k = pool_keys = pm_net.pool_keys(data.pool)
    expect(len(k) == 1, 'pool_keys: one epoch (no change of the key yet)')
    fake_beacon = pool.beacon_seal(other, pool.beacon_plain(b'\x01' * 6, 'X', '', 0, b'\x02' * 8))
    expect(pm_net.parse(fake_beacon, '127.0.0.1', pool_keys) == (None, None), 'a beacon of another pool: no')
    expect(pm_net.parse(b'garbage', '127.0.0.1', pool_keys) == (None, None), 'garbage: no')
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('0.0.0.0', pm_net.UDP_PORT))
    try:
        t0 = time.time()
        joins, beacons = pm_net.listen(data, 0.3)
        expect(joins == [] and time.time() - t0 < 2, 'listen: the port in use: no JOIN, no fault')
        lst = pm_net.Listener(lambda i: None, lambda i: None)
        try:
            lst.start()
            expect(False, 'Listener.start: the port in use = OSError')
        except OSError:
            expect(True, 'Listener.start: OSError')
    finally:
        s.close()
    b1.join_open = False


def step_adopt(d, root, data, bs):
    b1, b2, b3, b4 = bs
    # (1) the USB console: the program opens the JOIN screen, reads the 6 digits, sends "pool allow"
    code, out = cli('--data', root, 'adopt', b1.ip, '--console', 'CON1')
    p = pm_data.Data(root).load()
    expect(code == 0 and same_pool(p, b1), 'adopt --console: %s' % out[-800:])
    m1 = p.find(b1.id.hex()) if p else None
    expect(m1 is not None and m1['name'] == 'TFN-%s' % b1.id.hex()[:4].upper() and m1['role'] == 0,
           'adopt: the name TFN-<4 hex> with no A J packet')
    wp = pool_tool.load_webpass(os.path.join(root, 'pool'))
    expect(b1.webpw and wp.get(b1.mac.hex()) == b1.webpw and len(b1.webpw) == 16, 'adopt: a random web password')
    expect('BACKUP IS OLD' in out, 'adopt: BACKUP IS OLD')
    ids = [i for i, s in enumerate(SEQ) if s[0] == 'T1']
    first = [SEQ[i][2] for i in ids][:2]
    expect(first and first[0] == '/api/id', 'adopt reads /api/id first (%r)' % first)

    # (2) wrong digits = the JOIN stops; then --digits with a tap of a person
    em, nm, eb, nb = os.urandom(32), os.urandom(16), os.urandom(32), os.urandom(16)
    want = pool.sas(pc.x25519_pub(em), pc.x25519_pub(eb), nm, nb)
    real_join = pool.Join
    pool.Join = lambda pem, em_priv=None, nm_=None: real_join(pem, em, nm)
    try:
        b2.fixed, b2.join_open = (eb, nb), True
        code, out = cli('--data', root, 'adopt', b2.ip, '--digits', '%06d' % ((want + 1) % 1000000))
        expect(code == 1 and 'NOT the same' in out and b2.roster is None, 'adopt: wrong digits stop (%r)' % out[-300:])
        expect(pm_data.Data(root).load().find(b2.id.hex()) is None, 'adopt: wrong digits: no member')
        b2.auto_tap = 0.5
        code, out = cli('--data', root, 'adopt', b2.ip, '--digits', pool.sas_text(want), '--name', 'TWO',
                        '--role', 'child')
    finally:
        pool.Join = real_join
    p = pm_data.Data(root).load()
    expect(code == 0 and same_pool(p, b2) and same_pool(p, b1), 'adopt --digits + a tap: %s' % out[-600:])
    m2 = p.find(b2.id.hex())
    expect(m2 and m2['name'] == 'TWO' and m2['role'] == pool.ROLE_CHILD, 'adopt --name --role child')
    expect([fmac(b2)] == [f[0] for f in b1.friends] and [fmac(b1)] == [f[0] for f in b2.friends] and
           b1.friends[0][1] == 'TWO', 'adopt: the pair are friends (with the member name)')
    code, out = cli('--data', root, 'adopt', b2.ip, '--name', 'X' * 17)
    expect(code == 2, 'adopt --name over 16 bytes: exit 2')

    # (3) --all with two consoles: the program finds the console that shows the digits of each board
    for i in range(15):
        b4.friends.append(['0200000000%02X' % i, 'OWN%d' % i])
    data.set_addr(fmac(b3), b3.ip, '', b3.id.hex())
    data.set_addr(fmac(b4), b4.ip, '', b4.id.hex())
    code, out = cli('--data', root, 'adopt', '--all', '--console', 'CON4,CON3')
    p = pm_data.Data(root).load()
    expect(code == 0 and all(same_pool(p, b) for b in bs), 'adopt --all --console: %s' % out[-1200:])
    expect(p.find(b3.id.hex())['name'] == 'T3' and p.find(b4.id.hex())['name'] == 'T4',
           'adopt --all: the names of the A J packets')
    expect('FRIENDS FULL ON T4' in out, 'the friends limit: FRIENDS FULL ON T4')
    expect(len(b4.friends) == 16 and fmac(b3) in [f[0] for f in b4.friends], 'the friends limit: 16 on T4')
    for b in (b1, b2, b3):
        others = {fmac(o) for o in bs if o is not b}
        expect(others <= {f[0] for f in b.friends}, '%s: a friend of each other member' % b.name)
    expect(all(b.webpw for b in bs), 'adopt --all: web passwords')
    # --digits is for the tests only: no --help shows it
    code, out = cli('adopt', '--help')
    po = io.StringIO()
    with contextlib.redirect_stdout(po), contextlib.redirect_stderr(po):
        try:
            pool_tool.main(['adopt', '--help'])
        except SystemExit:
            pass
    expect('--digits' not in out and '--password' in out and '--digits' not in po.getvalue() and
           '--password' in po.getvalue() and '--digits' not in (pool_tool.__doc__ or ''),
           'adopt --help of poolmgr and of pool_tool.py: no --digits, a --password')
    return p


def step_join_stops(d, root):
    """The JOIN with no ALLOW in its time, and with NO on the board: the line tells why and the next step."""
    b = PmBoard('T9', bytes.fromhex('aabbcc000009'), d)
    old_wait, old_input = pm_pool.ALLOW_WAIT, builtins.input
    pm_pool.ALLOW_WAIT = 1.0
    builtins.input = lambda prompt='': pool.sas_text(b.shown_sas())
    try:
        b.join_open = True
        code, out = cli('--data', root, 'adopt', b.ip)
        expect(code == 1 and 'nobody tapped ALLOW on the board' in out and 'SETUP > POOL > JOIN again' in out and
               b.roster is None, 'adopt with no tap on ALLOW: %s' % out[-300:])
        b.join_open, b.auto_no = True, 0.3
        code, out = cli('--data', root, 'adopt', b.ip)
        expect(code == 1 and 'the board ended the JOIN (NO on the board' in out and b.roster is None,
               'adopt with NO on the board: %s' % out[-300:])
    finally:
        pm_pool.ALLOW_WAIT, builtins.input = old_wait, old_input
        b.close()


def step_upkeep_cmds(d, root, data, bs):
    b1, b2, b3, b4 = bs
    # friends --check repairs a missing entry; the full friend list of T4 gives exit 1
    b1.friends = [f for f in b1.friends if f[0] != fmac(b3)]
    code, out = cli('--data', root, 'friends', '--check')
    expect(code == 1 and fmac(b3) in [f[0] for f in b1.friends] and 'FULL ON T4' in out and
           'friends: 1 new friend entries, 0 were there' in out, 'friends --check repairs the entry: %s' % out[-400:])
    code, out = cli('--data', root, 'friends')
    expect(code == 1 and 'friends: 0 new friend entries, 10 were there' in out and 'FULL ON T4' in out,
           'friends: the count of the entries that were there (%s)' % out[-300:])
    # push
    code, out = cli('--data', root, 'push')
    p = pm_data.Data(root).load()
    expect(code == 0 and 'PUSH OK: 4' in out and all(same_pool(p, b) for b in bs), 'push: %s' % out[-300:])
    code, out = cli('--data', root, 'push', 'NOBODY')
    expect(code == 2, 'push of an unknown member: exit 2')
    # webpass
    code, out = cli('--data', root, 'webpass', 'show', 'T3')
    expect(code == 0 and b3.webpw in out, 'webpass show')
    old = b3.webpw
    code, out = cli('--data', root, 'webpass', 'new', 'T3')
    code2, out2 = cli('--data', root, 'webpass', 'show', b3.id.hex())
    expect(code == 0 and b3.webpw != old and b3.webpw in out2, 'webpass new: %s' % out[-300:])
    # list, list --live, the pool_tool form of the folder
    code, out = cli('--data', root, 'list')
    expect(code == 0 and all(b.ip in out for b in bs) and 'roster version %d' % p.version in out, 'list')
    code, out = cli('--data', root, 'list', '--live')
    expect(code == 0 and out.count('[ok v%d e0]' % p.version) == 4 and 'kind lcd349' in out and
           out.count('bad 0 store official') == 4 and 'NOT THE STORE' not in out, 'list --live: %s' % out[-600:])
    b1.mgr = {'lan': 'http://192.0.2.9:47480'}
    code, out = cli('--data', root, 'list', '--live')
    b1.mgr = None
    expect(code == 0 and out.count('store local (NOT THE STORE OF THE POOL: poolmgr store official)') == 1,
           'list --live: the store of a board that is not the store mode of the pool: %s' % out[-600:])
    po = io.StringIO()
    with contextlib.redirect_stdout(po), contextlib.redirect_stderr(po):
        code = pool_tool.main(['--dir', data.pool_dir, '--key', data.key_a, 'show', '--live'])
    expect(code == 0, 'pool_tool.py show --live works on the pool folder of poolmgr: %s' % po.getvalue()[-300:])
    # a member that has a new address: run() listens for its beacon and tries again there
    if has_address('127.0.0.2'):
        b1.move('127.0.0.2')
        code, out = cli('--data', root, 'push', b1.id.hex())
        a = pm_data.Data(root).addrs().get(fmac(b1), {})
        expect(code == 0 and a.get('ip') == b1.ip, 'a new address from the beacon: %s %r' % (out[-300:], a))
    else:
        print('(no address 127.0.0.2 on this computer: the check of a new address is not done)')
    # the big POST with the header (a new start of the board in the middle = a 401, then a new nonce)
    d2 = pm_data.Data(root)
    d2.load()
    seen = []
    ans = pm_board.post_stream(d2, 'TWO', '/api/ota', b'\xa5' * 100000, lambda s, t: seen.append((s, t)))
    expect(ans.get('ok') and b2.ota_len == 100000 and seen[-1] == (100000, 100000) and len(seen) > 10,
           'post_stream: the body in parts')
    b2.restart()
    ans = pm_board.post_stream(d2, 'TWO', '/api/ota', b'\x01' * 5000)
    expect(ans.get('ok') and b2.ota_len == 5000, 'post_stream after a new start of the board (a new bn)')
    b2.ota_refuse = True
    try:
        pm_board.post_stream(d2, 'TWO', '/api/ota', b'\x01' * 10)
        expect(False, 'post_stream: a refusal')
    except pool_tool.Refused as e:
        expect(e.code == 400 and e.ans.get('code') == 'sig', 'post_stream: the refusal with the code of the board')
    b2.ota_refuse = False


def step_remove(d, root, data, bs):
    b1, b2, b3, b4 = bs
    for b in bs:
        b.mgr = {'lan': 'http://192.0.2.9:47480'}
    SEQ.clear()
    # step (1) goes through the real pm_store.set_member (it adds the store token): the test records the call
    import pm_store
    calls = []
    real = pm_store.set_member

    def set_member(data_, m, mode):
        calls.append((m['name'], mode))
        return real(data_, m, mode)
    pm_store.set_member = set_member
    pw3 = b3.webpw
    b1.friends_down = True   # (T1 does not take the removal of T3 from its friends: the entry waits)
    try:
        code, out = cli('--data', root, 'remove', 'T3')
    finally:
        pm_store.set_member = real
        b1.friends_down = False
    expect(calls == [('T3', 'official')], 'remove (1): pm_store.set_member(official) (%r)' % calls)
    p = pm_data.Data(root).load()
    expect(code == 0 and p.find(b3.id.hex()) is None and b3.roster is None, 'remove: T3 left (%s)' % out[-600:])
    expect(b3.mgr and b3.mgr['lan'] == '' and b3.mgr['vpn'] == '', 'remove (1): the official store on T3 (%r)' % b3.mgr)
    expect(all(same_pool(p, b) for b in (b1, b2, b4)) and p.epoch == 1, 'remove (4): a new key for the others')
    expect(pw3 and not b3.webpw and b3.mac.hex() not in pool_tool.load_webpass(data.pool_dir) and
           '(3) T3 has no web password now' in out and 'SETUP > WEB PASSWORD' in out,
           'remove (3): the web password of the pool is off on T3 and out of webpass.json')
    waits = pm_pool.out_load(pm_data.Data(root))
    n1 = 'TFN-%s' % b1.id.hex()[:4].upper()
    expect('%s: T3 not out' % n1 in out and 'poolmgr tries again' in out and
           [(x['on'], x['mac']) for x in waits] == [(fmac(b1), fmac(b3))] and fmac(b3) in [f[0] for f in b1.friends],
           'remove (2): T1 did not take it out: the entry waits (%r)' % waits)
    code, out2 = cli('--data', root, 'push')
    expect(code == 0 and '%s: T3 out of its friends' % n1 in out2 and not pm_pool.out_load(pm_data.Data(root)),
           'push: the friend entry that waits goes out: %s' % out2[-300:])
    expect(all(fmac(b3) not in [f[0] for f in b.friends] for b in (b1, b2, b4)) and not any(
        fmac(o) in [f[0] for f in b3.friends] for o in (b1, b2, b4)), 'remove (2): the pool friends out')
    # the previous key opens the beacons of a member for its hour after the change of the key (the rule of the boards)
    prev = p.prev[0]
    old_keys = pool.derive(bytes.fromhex(prev['k']), p.pool_id, prev['epoch'])
    old = pool.beacon_seal(old_keys, pool.beacon_plain(b1.mac, 'T1', '', 0, b1.id))
    what, info = pm_net.parse(old, '127.0.0.1', pm_net.pool_keys(p))
    expect(prev.get('t') and abs(prev['t'] - time.time()) < 60 and what == 'beacon' and info['epoch'] == 0 and
           info['mac'] == fmac(b1), 'a beacon of the previous key opens after the change (%r %r)' % (prev, info))
    expect(len(pm_net.pool_keys(p, now=prev['t'] + pm_net.PREV_KEEP + 1)) == 1 and
           pm_net.parse(old, '127.0.0.1', pm_net.pool_keys(p, now=prev['t'] + pm_net.PREV_KEEP + 1))[0] is None,
           'the previous key: not after its hour')
    lst = pm_net.Listener(None, None, keys=[])
    lst.set_keys(pm_net.data_keys(pm_data.Data(root)))
    expect(len(lst.keys) == 2, 'data_keys: the current and the previous key; Listener.set_keys')

    def first(fn):
        return next((i for i, s in enumerate(SEQ) if fn(s)), -1)
    i_mgr = first(lambda s: s[0] == 'T3' and 'op=mgr' in s[2])
    i_rm = first(lambda s: s[2].startswith('/api/friends?rm='))
    i_web = first(lambda s: s[0] == 'T3' and s[2] == '/api/pool?op=webpass')
    i_set = first(lambda s: s[0] == 'T3' and s[2] == '/api/pool?op=set' and s[1] == 'POST')
    i_key = first(lambda s: s[0] != 'T3' and s[2] == '/api/pool?op=key')
    expect(0 <= i_mgr < i_rm < i_web < i_set < i_key, 'remove: the order of design 2.8 (%d %d %d %d %d)' % (
        i_mgr, i_rm, i_web, i_set, i_key))
    expect('BACKUP IS OLD' in out, 'remove: BACKUP IS OLD')
    # a member that does not answer still leaves: the new key cuts it off; it keeps the web password of the pool
    b4.close()
    pw4 = b4.webpw
    b2.friends_down = True
    try:
        code, out = cli('--data', root, 'remove', 'T4')
    finally:
        b2.friends_down = False
    p = pm_data.Data(root).load()
    expect(code == 0 and p.find(b4.id.hex()) is None and 'did not answer' in out, 'remove of a member that does '
           'not answer: %s' % out[-600:])
    expect(all(same_pool(p, b) for b in (b1, b2)) and p.epoch == 2 and b4.epoch == 1, 'remove: epoch + 1 for the '
           'others, not for T4')
    expect('(3) T4 did not answer' in out and 'keeps the web password of the pool' in out and
           'poolmgr webpass show %s' % fmac(b4) in out and
           pool_tool.load_webpass(data.pool_dir).get(b4.mac.hex()) == pw4, 'remove (3): T4 keeps the web password '
           'of the pool, and webpass.json keeps it')
    code, out = cli('--data', root, 'webpass', 'show', fmac(b4))
    expect(code == 0 and pw4 in out and 'not a member of the pool now' in out, 'webpass show <MAC> of a board that '
           'left: %s' % out[-200:])
    code, out = cli('--data', root, 'friends', '--check')
    expect(code == 0 and 'TWO: T4 out of its friends' in out and not pm_pool.out_load(pm_data.Data(root)),
           'friends --check: the friend entry that waits goes out: %s' % out[-300:])
    expect(all(fmac(b4) not in [f[0] for f in b.friends] for b in (b1, b2)), 'remove: T4 out of the friends')
    code, out = cli('--data', root, 'remove', 'MANAGER')
    expect(code == 2 and 'the manager' in out, 'remove of the manager member: exit 2')


def step_serve_upkeep(d, root, bs):
    b1, b2 = bs[0], bs[1]
    data = pm_data.Data(root)
    p = data.load()
    # an older roster (a board that was off line): the newest roster and key, at most once in UPKEEP_S
    p.sign(data.key_a)
    p.save(data.pool_dir)
    st = pool_tool.Board(b1.ip).call('GET', '/api/pool')
    lines, _ = quiet(pm_pool.upkeep, data, fmac(b1), st)
    expect(isinstance(lines, list) and lines and same_pool(p, b1), 'upkeep: an older roster: %r' % (lines,))
    p.sign(data.key_a)
    p.save(data.pool_dir)
    lines, _ = quiet(pm_pool.upkeep, data, fmac(b1), pool_tool.Board(b1.ip).call('GET', '/api/pool'))
    expect(lines == [] and not same_pool(p, b1), 'upkeep: once in UPKEEP_S at most')
    pm_pool.UPKEEP_S = 0
    lines, _ = quiet(pm_pool.upkeep, data, fmac(b1), {'mac': fmac(b1)})
    expect(lines and same_pool(p, b1), 'upkeep: a beacon only (the program reads GET /api/pool)')
    lines, _ = quiet(pm_pool.upkeep, data, fmac(b2), dict(pool_tool.Board(b2.ip).call('GET', '/api/pool'),
                                                         state='damaged'))
    expect(lines and same_pool(p, b2), 'upkeep: damaged')
    lines, _ = quiet(pm_pool.upkeep, data, fmac(b1), pool_tool.Board(b1.ip).call('GET', '/api/pool'))
    expect(lines == [], 'upkeep: a member with the newest roster: nothing')
    lines, _ = quiet(pm_pool.upkeep, data, '0200000000AA', {'state': 'none'})
    expect(lines == [], 'upkeep: not a member: nothing')
    # LEAVE on the screen of a board: removed with a new key for the others; T1 does not take the friend removal
    with b2.lock:
        b2.erase()
    b1.friends_down = True
    try:
        lines, _ = quiet(pm_pool.upkeep, data, fmac(b2), pool_tool.Board(b2.ip).call('GET', '/api/pool'))
    finally:
        b1.friends_down = False
    p = pm_data.Data(root).load()
    text = '\n'.join(lines) if isinstance(lines, list) else str(lines)
    expect(p.find(b2.id.hex()) is None and p.epoch == 3 and same_pool(p, b1),
           'upkeep: LEAVE on a board = removed, a new key for the others: %r' % (lines,))
    n1 = 'TFN-%s' % b1.id.hex()[:4].upper()
    expect('%s: TWO is not out of its friends' % n1 in text and 'keeps the web password of the pool' in text and
           'BACKUP IS OLD' in text and [x['mac'] for x in pm_pool.out_load(data)] == [fmac(b2)],
           'upkeep: LEAVE: a line for the friend fault, the entry waits (%r)' % (lines,))
    lines, _ = quiet(pm_pool.upkeep, data, fmac(b1), pool_tool.Board(b1.ip).call('GET', '/api/pool'))
    expect(lines and '%s: TWO out of its friends' % n1 in lines[0] and fmac(b2) not in [f[0] for f in b1.friends] and
           not pm_pool.out_load(data), 'upkeep: the friend entry that waits goes out: %r' % (lines,))


def step_password(d, root):
    """Boards with a web password at the JOIN (the web gate answers 401 "login"): no password and no terminal = a
    stop that names the web password; a wrong --password; the right --password (the board keeps its own web
    password); a question after the 401; a board that waits after many wrong passwords (429); the web password of the
    pool that a removed board kept (--keep-webpass, webpass show <MAC>) and its use at a new adopt."""
    b5, b6, b7, b8 = (PmBoard('T%d' % i, bytes.fromhex('aabbcc00000%d' % i), d) for i in (5, 6, 7, 8))
    CONS.update(CON5=b5, CON6=b6, CON8=b8)
    can, ask = pm_pool.can_ask, pm_pool.ask_secret
    pm_pool.can_ask = lambda: False
    try:
        b5.webpw = 'own-pass-5555'
        code, out = cli('--data', root, 'adopt', b5.ip, '--console', 'CON5', '--name', 'FIVE')
        expect(code == 1 and 'has a web password' in out and 'SETUP > WEB PASSWORD' in out and '--password' in out
               and b5.roster is None, 'adopt of a board with a web password, no --password: %s' % out[-300:])
        code, out = cli('--data', root, 'adopt', b5.ip, '--console', 'CON5', '--name', 'FIVE', '--password',
                        'not-the-password')
        expect(code == 1 and 'the web password is not right' in out and b5.roster is None,
               'adopt with a wrong --password: %s' % out[-300:])
        code, out = cli('--data', root, 'adopt', b5.ip, '--console', 'CON5', '--name', 'FIVE', '--password',
                        'own-pass-5555')
        p = pm_data.Data(root).load()
        expect(code == 0 and same_pool(p, b5) and b5.webpw == 'own-pass-5555' and 'keeps its own web password' in out
               and b5.mac.hex() not in pool_tool.load_webpass(os.path.join(root, 'pool')),
               'adopt --password: the board joins and keeps its own web password: %s' % out[-400:])
        b6.webpw = 'own-pass-6666'
        answers, prompts = ['wrong-one-0000', 'own-pass-6666'], []
        pm_pool.can_ask = lambda: True
        pm_pool.ask_secret = lambda prompt='': (prompts.append(prompt), answers.pop(0))[1]
        code, out = cli('--data', root, 'adopt', b6.ip, '--console', 'CON6', '--name', 'SIX')
        p = pm_data.Data(root).load()
        expect(code == 0 and same_pool(p, b6) and len(prompts) == 2 and 'SETUP > WEB PASSWORD' in prompts[0] and
               b6.webpw == 'own-pass-6666', 'adopt: a question after the 401, two tries: %r %s' % (prompts,
                                                                                                  out[-300:]))
        b7.webpw, b7.wrong_pw = 'x' * 12, poolmgr_fake.WRONG_MAX + 1
        code, out = cli('--data', root, 'adopt', b7.ip, '--password', 'y' * 12)
        expect(code == 1 and 'refuses web passwords' in out, 'adopt: too many wrong web passwords (429): %s' %
               out[-300:])
    finally:
        pm_pool.can_ask, pm_pool.ask_secret = can, ask
    # the web password of the pool stays on a removed board (--keep-webpass); a new adopt of the board uses it
    code, out = cli('--data', root, 'adopt', b8.ip, '--console', 'CON8', '--name', 'EIGHT')
    pw8 = b8.webpw
    code, out = cli('--data', root, 'remove', 'EIGHT', '--keep-webpass')
    expect(code == 0 and pw8 and b8.webpw == pw8 and b8.roster is None and '(3) EIGHT keeps the web password of the '
           'pool' in out, 'remove --keep-webpass: %s' % out[-400:])
    code, out = cli('--data', root, 'webpass', 'show', fmac(b8))
    expect(code == 0 and pw8 in out, 'webpass show <MAC> after remove --keep-webpass')
    code, out = cli('--data', root, 'adopt', b8.ip, '--console', 'CON8', '--name', 'EIGHT')
    p = pm_data.Data(root).load()
    wp = pool_tool.load_webpass(os.path.join(root, 'pool'))
    expect(code == 0 and same_pool(p, b8) and 'gave it before' in out and b8.webpw and b8.webpw != pw8 and
           wp.get(b8.mac.hex()) == b8.webpw, 'adopt of a board with the web password of the pool: the program uses '
           'it, then a new random password: %s' % out[-400:])
    # a person set another web password on the board: remove keeps it
    b8.webpw = 'person-own-8888'
    code, out = cli('--data', root, 'remove', 'EIGHT')
    expect(code == 0 and b8.roster is None and b8.webpw == 'person-own-8888' and 'another web password now' in out and
           b8.mac.hex() not in pool_tool.load_webpass(os.path.join(root, 'pool')),
           'remove: the board keeps a web password that a person set: %s' % out[-400:])
    for b in (b5, b6, b7, b8):
        b.close()


def step_stale(d, keys):
    """A pool of its own (STALE): remove of a member whose last address has another board now (no request with the
    web password goes there, the data folder keeps the password); remove of a board that answers op=webpass but keeps
    its password (the entry stays, also at webpass new); the kept password of a board that left goes only to an
    address that the person gives (not to the board of a JOIN packet); a kept password that does not open the board
    goes out after the JOIN; a web page at the address of a member (stale_html); adopt --all with an InputError and a
    stop (stale_all); the questions: an empty answer is a try, no terminal and the end of the input stop with exit 2."""
    root = os.path.join(d, 'stale')
    code, out = cli('--data', root, 'init', 'STALE', '--keys', keys)
    expect(code == 0, 'init STALE: %s' % out[-300:])
    b9, b10, b11, bx = (PmBoard(n, bytes.fromhex(m), d) for n, m in (('S9', 'aabbcc0000a9'), ('S10', 'aabbcc0000aa'),
                                                                       ('S11', 'aabbcc0000ab'), ('SX', 'aabbcc0000af')))
    more = [PmBoard('S%d' % i, bytes.fromhex('aabbcc0001%02x' % i), d) for i in range(12, 18)]
    CONS.update(CON9=b9, CON10=b10, CON11=b11)
    CONS.update({'CON%s' % b.name[1:]: b for b in more})
    can, ask = pm_pool.can_ask, pm_pool.ask_secret
    try:
        stale_remove(root, b9, b10, bx)
        stale_adopt(root, b10)
        stale_html(root, b10, more[0], more[1])
        stale_all(root, b10, more[2], more[3], more[4])
        stale_bad_pk(root, more[4], more[5])
        stale_questions(root, b11)
    finally:
        pm_pool.can_ask, pm_pool.ask_secret = can, ask
        builtins.input = REAL_INPUT
        for b in [b9, b10, b11, bx] + more:
            try:
                b.close()
            except OSError:
                pass


def kept_pw(root, b):
    """The web password of the board b in webpass.json of the data folder root, or None."""
    return pool_tool.load_webpass(os.path.join(root, 'pool')).get(b.mac.hex())


def stale_remove(root, b9, b10, bx):
    code, out = cli('--data', root, 'adopt', b9.ip, '--console', 'CON9', '--name', 'NINE')
    pw9 = b9.webpw
    expect(code == 0 and pw9 and kept_pw(root, b9) == pw9, 'adopt NINE: %s' % out[-300:])
    # NINE is off, and its last address has the board TX now (another device got the address): TX has no web password
    b9.close()
    data = pm_data.Data(root)
    data.load()
    data.set_addr(fmac(b9), bx.ip, '', b9.id.hex())
    # push and list --live: the board TX gives another device ID in GET /api/pool, so it gets no signed request and
    # no web password; the line of NINE names the other board
    SEQ.clear()
    bx.basic.clear()
    code, out = cli('--data', root, 'push', 'NINE')
    expect(code == 1 and 'NINE: FAILED' in out and 'another board answers at %s' % bx.ip in out,
           'push with another board at the old address of a member: a FAILED line (%s)' % out[-400:])
    code, out = cli('--data', root, 'list', '--live')
    tx = [s[1:] for s in SEQ if s[0] == 'SX']
    expect(code == 1 and 'another board answers at %s' % bx.ip in out, 'list --live with another board at the old '
           'address of a member: the line names it (%s)' % out[-400:])
    expect(tx and all(t == ('GET', '/api/pool') for t in tx) and not bx.basic and bx.webpw is None,
           'push and list --live: the other board gets only GET /api/pool, with no password (%r %r)' % (tx, bx.basic))
    SEQ.clear()
    code, out = cli('--data', root, 'remove', 'NINE')
    p = pm_data.Data(root).load()
    tx = [s[1:] for s in SEQ if s[0] == 'SX']
    expect(code == 0 and p.find(b9.id.hex()) is None, 'remove of a member at an old address: it leaves (%s)' %
           out[-600:])
    expect('NINE is not at its address (another board answers at %s' % bx.ip in out and '(3) NINE is not at its '
           'address' in out and 'keeps the web password of the pool' in out and kept_pw(root, b9) == pw9,
           'remove at an old address: no web password change, the data folder keeps the password (%s)' % out[-600:])
    expect(tx == [('GET', '/api/pool')] and not bx.basic and bx.webpw is None and b9.webpw == pw9,
           'remove at an old address: the other board gets only GET /api/pool, with no password (%r %r)' % (
               tx, bx.basic))
    # a board that answers op=webpass but keeps the password: the program reads it again, and the entry stays
    code, out = cli('--data', root, 'adopt', b10.ip, '--console', 'CON10', '--name', 'TEN')
    pw10 = b10.webpw
    b10.webpass_keep = True
    # webpass new: the board answers, but it keeps its password: the program reads it again, and the entry stays
    code, out = cli('--data', root, 'webpass', 'new', 'TEN')
    expect(code == 1 and 'TEN: FAILED: the board answered, but the new web password does not open it' in out and
           kept_pw(root, b10) == pw10 and b10.webpw == pw10, 'webpass new: the board does not take the new password: '
           'the entry stays (%s)' % out[-400:])
    # webpass new: the board takes the password, but it does not answer the read after it: the new password stays
    b10.webpass_keep = False
    real_now = pm_pool.web_now

    def no_answer(*a, **k):
        raise pool_tool.ToolError('no answer from %s: timed out' % a[0])
    pm_pool.web_now = no_answer
    try:
        code, out = cli('--data', root, 'webpass', 'new', 'TEN')
    finally:
        pm_pool.web_now = real_now
    expect(code == 1 and 'TEN: NOT CHECKED' in out and b10.webpw != pw10 and kept_pw(root, b10) == b10.webpw and
           'no new web password that the program checked' in out, 'webpass new with no answer to the read: the data '
           'folder keeps the new password (%s)' % out[-400:])
    pw10 = b10.webpw
    b10.webpass_keep = True
    code, out = cli('--data', root, 'remove', 'TEN')
    b10.webpass_keep = False
    expect(code == 0 and b10.roster is None and b10.webpw == pw10 and kept_pw(root, b10) == pw10 and
           '(3) TEN did not turn off the web password' in out and 'poolmgr webpass show %s' % fmac(b10) in out,
           'remove: the board did not take op=webpass: the entry stays (%s)' % out[-500:])


def stale_adopt(root, b10):
    """TEN left with the web password of the pool (webpass.json keeps it)."""
    pw10 = b10.webpw
    # the board of a JOIN packet (no address given): the kept password does not go to it
    pm_pool.can_ask = lambda: False
    b10.basic.clear()
    code, out = cli('--data', root, 'adopt', '--console', 'CON10', '--name', 'TEN')
    expect(code == 1 and b10.roster is None and not b10.basic and
           'poolmgr sends it only to an address that you give: poolmgr adopt %s' % b10.ip in out and
           'THE JOIN STOPPED' in out, 'adopt of a JOIN packet: the kept password does not go out: %s %r' % (
               out[-400:], b10.basic))
    # a person set an own password on the board: the kept password is wrong, the person types the own one; the kept
    # entry goes out (webpass show must not print a password that does not open the board)
    b10.webpw = 'person-own-1010'
    answers, prompts = ['person-own-1010'], []
    pm_pool.can_ask = lambda: True
    pm_pool.ask_secret = lambda prompt='': (prompts.append(prompt), answers.pop(0))[1]
    code, out = cli('--data', root, 'adopt', b10.ip, '--console', 'CON10', '--name', 'TEN')
    p = pm_data.Data(root).load()
    expect(code == 0 and same_pool(p, b10) and 'gave it before' in out and len(prompts) == 1 and
           'keeps its own web password' in out and 'does not open it now' in out and
           b10.webpw == 'person-own-1010' and pw10 and kept_pw(root, b10) is None,
           'adopt: the kept password does not open the board: the own password, the entry goes out: %s %r' % (
               out[-500:], prompts))


def stale_html(root, b10, b12, b13):
    """A device that is not a TFN board (a web page) at the last address of a member: push and remove give a line for
    that member, with no trace; remove sends it only GET /api/pool with no password, and saves a roster with no member
    and a new pool key. TEN (b10) is a member that answers."""
    page = poolmgr_fake.OtherDevice()
    try:
        for b, n in ((b12, 'TWELVE'), (b13, 'THIRTEEN')):
            code, out = cli('--data', root, 'adopt', b.ip, '--console', 'CON%s' % b.name[1:], '--name', n)
            expect(code == 0, 'adopt %s: %s' % (n, out[-300:]))
        pw12, pw13 = kept_pw(root, b12), kept_pw(root, b13)
        data = pm_data.Data(root)
        data.load()
        # THIRTEEN is off, and the web page has its last address: push gives a FAILED line for it
        b13.close()
        data.set_addr(fmac(b13), page.ip, '', b13.id.hex())
        code, out = cli('--data', root, 'push')
        expect(code == 1 and 'THIRTEEN: FAILED' in out and 'not a TFN board' in out and
               same_pool(pm_data.Data(root).load(), b12), 'push with a web page at the address of a member: a FAILED '
               'line for it (%s)' % out[-500:])
        # TWELVE is off too, and the web page has its last address
        b12.close()
        data.set_addr(fmac(b12), page.ip, '', b12.id.hex())
        before = pm_data.Data(root).load()
        del page.seen[:]
        code, out = cli('--data', root, 'remove', 'TWELVE')
        p = pm_data.Data(root).load()
        expect(code == 1 and p.find(b12.id.hex()) is None and p.epoch == before.epoch + 1 and
               p.version > before.version and same_pool(p, b10), 'remove of a member at the address of a web page: it '
               'leaves, a new pool key for TEN (%s)' % out[-700:])
        auth12 = 'Basic ' + base64.b64encode(('admin:%s' % pw12).encode()).decode()
        expect('TWELVE is not at its address (another device answers at %s' % page.ip in out and
               'not a TFN board' in out and kept_pw(root, b12) == pw12 and page.seen[:1] == [('GET', '/api/pool', None)]
               and not [s for s in page.seen if s[2] == auth12], 'remove at the address of a web page: no request with '
               'the web password of the member, the data folder keeps it (%s %r)' % (out[-700:], page.seen))
        # THIRTEEN, the last member at the web page: remove sends the web page only GET /api/pool
        del page.seen[:]
        code, out = cli('--data', root, 'remove', 'THIRTEEN')
        p2 = pm_data.Data(root).load()
        expect(code == 0 and p2.find(b13.id.hex()) is None and p2.epoch == p.epoch + 1 and same_pool(p2, b10) and
               page.seen == [('GET', '/api/pool', None)] and kept_pw(root, b13) == pw13, 'remove of a member at the '
               'address of a web page: it leaves, the web page gets only GET /api/pool (%s %r)' % (out[-500:],
                                                                                                  page.seen))
    finally:
        page.close()


def stale_all(root, b10, b14, b15, b16):
    """adopt --all: an InputError of one board (a web password, and no terminal for its question) stops only the JOIN
    of that board: the next board joins and gets its web password and its friends. A stop on the way (Ctrl+C) after
    a JOIN names the boards that joined and prints BACKUP IS OLD."""
    data = pm_data.Data(root)
    data.load()
    for b in (b14, b15, b16):
        data.set_addr(fmac(b), b.ip, '', b.id.hex())   # (the web port of a fake board for its JOIN packets)
    b14.webpw = 'own-pass-1414'
    pm_pool.ask_secret, pm_pool.can_ask = ASK_SECRET, lambda: False
    code, out = cli('--data', root, 'adopt', '--all', '--console', 'CON14,CON15', '--password')
    p = pm_data.Data(root).load()
    expect(code == 1 and b14.roster is None and same_pool(p, b15) and 'THE JOIN STOPPED' in out and
           'no terminal for the question' in out and '1 of 2 boards did not join' in out and 'BACKUP IS OLD' in out,
           'adopt --all: an InputError of one board stops only its JOIN (%s)' % out[-700:])
    expect(b15.webpw and kept_pw(root, b15) == b15.webpw and fmac(b10) in [f[0] for f in b15.friends] and
           fmac(b15) in [f[0] for f in b10.friends], 'adopt --all: the board after the InputError gets its web '
           'password and its friends')
    b14.webpw = None
    real, calls = pm_pool.join_one, []

    def join_once(*a, **k):
        calls.append(a[1]['ip'])
        if len(calls) == 2:
            raise KeyboardInterrupt()
        return real(*a, **k)
    pm_pool.join_one = join_once
    try:
        code, out = cli('--data', root, 'adopt', '--all', '--console', 'CON14,CON16')
    finally:
        pm_pool.join_one = real
        b16.join_open = False
    p = pm_data.Data(root).load()
    expect(code == 2 and 'STOP: interrupted' in out and same_pool(p, b14) and b16.roster is None and
           'NOTE: 1 board(s) joined before the stop: S14' in out and 'friends --check' in out and
           'BACKUP IS OLD' in out, 'adopt --all with a stop after one JOIN: the NOTE and BACKUP IS OLD (%s)' %
           out[-500:])


def stale_bad_pk(root, b16, b17):
    """adopt --all: a device whose /api/id gives a "pk" that is not hex, or that is not text, stops only its own JOIN
    with a STOP line (no trace): the other board joins."""
    data = pm_data.Data(root)
    data.load()
    for b in (b16, b17):
        data.set_addr(fmac(b), b.ip, '', b.id.hex())
    b17.bad_pk = 'zz' * 65
    code, out = cli('--data', root, 'adopt', '--all', '--console', 'CON16,CON17')
    p = pm_data.Data(root).load()
    expect(code == 1 and same_pool(p, b16) and b17.roster is None and 'gives no good device key' in out and
           '1 of 2 boards did not join' in out and 'Traceback' not in out,
           'adopt --all with a device key that is not hex: only that JOIN stops (%s)' % out[-700:])
    b17.bad_pk = 12345
    code, out = cli('--data', root, 'adopt', '--all', '--console', 'CON17')
    expect(code == 1 and b17.roster is None and 'gives no good device key' in out and 'THE JOIN STOPPED' in out and
           'Traceback' not in out, 'adopt --all with a device key that is not text: a STOP line (%s)' % out[-500:])
    b17.bad_pk = None
    # the check after the JOIN does not answer one time: the board is in the roster, the steps after the JOIN run
    real, seen = b17.handle, []

    def handle(method, uri, headers, body):
        if method == 'GET' and uri == '/api/pool' and b17.roster is not None and not seen:
            seen.append(uri)
            return 503, {'error': 'busy'}
        return real(method, uri, headers, body)
    b17.handle = handle
    try:
        code, out = cli('--data', root, 'adopt', '--all', '--console', 'CON17')
    finally:
        b17.handle = real
    p = pm_data.Data(root).load()
    expect(code == 0 and seen and same_pool(p, b17) and 'WARN: S17 is in the roster' in out and
           'THE JOIN STOPPED' not in out and b17.webpw and kept_pw(root, b17) == b17.webpw and
           fmac(b16) in [f[0] for f in b17.friends], 'adopt --all: a check after the JOIN that does not answer gives '
           'a WARN line, and the web password and the friends follow (%s)' % out[-700:])


def stale_questions(root, b11):
    """The questions of the web password: an empty answer is a try; --password with no value and no terminal stops
    (exit 2); the real ask_secret with no terminal stops; the end of the input stops with exit 2."""
    b11.webpw = 'own-pass-1111'
    prompts = []
    pm_pool.can_ask = lambda: True
    pm_pool.ask_secret = lambda prompt='': (prompts.append(prompt), '')[1]
    code, out = cli('--data', root, 'adopt', b11.ip, '--console', 'CON11')
    expect(code == 1 and len(prompts) == pm_pool.WEB_TRIES and 'the web password is not right' in out and
           b11.roster is None, 'adopt: empty answers are tries: %r %s' % (prompts, out[-300:]))
    del prompts[:]
    code, out = cli('--data', root, 'adopt', b11.ip, '--console', 'CON11', '--password')
    expect(code == 1 and len(prompts) == pm_pool.WEB_TRIES and 'SETUP > WEB PASSWORD' in prompts[0],
           'adopt --password with no value: the questions, empty answers are tries: %r %s' % (prompts, out[-300:]))
    pm_pool.ask_secret = ASK_SECRET
    pm_pool.can_ask = lambda: False
    code, out = cli('--data', root, 'adopt', b11.ip, '--console', 'CON11', '--password')
    expect(code == 2 and 'no terminal for the question' in out and b11.roster is None,
           'adopt --password with no terminal: exit 2: %s' % out[-300:])
    os.environ.pop(pm_pool.PHRASE_ENV)
    try:
        code, out = cli('--data', root, 'backup', 'STALE-again.tfnbak')
    finally:
        os.environ[pm_pool.PHRASE_ENV] = 'a test pass phrase'
    expect(code == 2 and 'no terminal for the question' in out and pm_pool.PHRASE_ENV in out and
           not os.path.exists('STALE-again.tfnbak'), 'backup: no terminal for the pass phrase: exit 2: %s' %
           out[-300:])
    # the end of the input at the question of the 6 digits: STOP, exit 2
    b11.webpw = None
    b11.join_open = True

    def eof(prompt=''):
        raise EOFError()
    builtins.input = eof
    code, out = cli('--data', root, 'adopt', b11.ip)
    expect(code == 2 and 'STOP: the input ended' in out and b11.roster is None, 'adopt: the end of the input: exit 2: '
           '%s' % out[-300:])


def run(d):
    root = os.path.join(d, 'data')
    os.environ[pm_pool.PHRASE_ENV] = 'a test pass phrase'
    pm_net.UDP_PORT = free_udp_port()
    pm_pool.JOIN_LISTEN = 1.5
    pm_pool.open_console = lambda port: FakeConsole(CONS[port])
    data, keys, pubs = step_init(d, root)
    step_init_faults(d, keys)
    step_channel(d, root, keys, pubs)
    step_recover_damaged(d, root)
    step_serial()
    bs = [PmBoard('T%d' % i, bytes.fromhex('aabbcc00000%d' % i), d) for i in range(1, 5)]
    try:
        for i, b in enumerate(bs):
            CONS['CON%d' % (i + 1)] = b
        step_listener(d, data, bs[0])
        step_adopt(d, root, data, bs)
        step_join_stops(d, root)
        step_upkeep_cmds(d, root, data, bs)
        step_remove(d, root, data, bs)
        step_serve_upkeep(d, root, bs)
        step_password(d, root)
        step_stale(d, keys)
    finally:
        for b in bs:
            try:
                b.close()
            except OSError:
                pass


def main():
    t0 = time.time()
    cwd = os.getcwd()
    with tempfile.TemporaryDirectory(prefix='poolmgr_a_') as d:
        os.chdir(d)
        try:
            run(d)
        finally:
            os.chdir(cwd)
            builtins.input = REAL_INPUT
    if FAILS:
        print('POOLMGR PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('POOLMGR PY OK (%d checks: init and its faults, the channel, the listener, adopt of 4 boards, the JOIN '
          'stops, the friends, push, webpass, list, a new address, post_stream, remove, the upkeep, the web passwords '
          'at the JOIN, remove at an old address, a web page at an old address, adopt --all with a fault, the '
          'questions; %.1f s)' % (CHECKS[0], time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
