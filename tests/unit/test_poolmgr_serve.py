#!/usr/bin/env python3
"""The test of "serve" and "service" of poolmgr (P6 design 2.8, 2.11, 2.13; track E).

SERVE: a pool in a temporary data folder with 3 fake boards (tests/unit/poolmgr_fake.py) as members: one with the
newest roster, one with an older roster, one after LEAVE. The test drives pm_serve.Serve step by step with a fake
clock and a fake listener (the beacons go in by the callback), and puts fakes in place of pm_pool.upkeep,
pm_store.route / set_mode / sync and pm_ota.update (the parts of other tracks). It checks: the addresses into
addrs.json, the upkeep only of the members that need it and at most once a minute, the probe of a member with no
beacon, the HTTP server of the local store (only the 3 routes, GET only, 404 else, a fault of a route = 500 and the
server goes on), the LAN address and op=mgr again, a new key = the listener again with the new keys, a new store mode
= the HTTP server stops or starts, the sync and the auto update each 6 hours, the change lock of another process, a
fault of a job, a device that is not a TFN board at the address of a member (an answer that is not HTTP, a JSON
answer with values that are not numbers: one line, serve goes on), run() with a stop, a port in use at the start with
no output (the STOP line in log/serve.log and log/poolmgr.log), the lines of a job (at once; a line of another thread
one time only).

SERVICE: the files of the 3 systems as text (systemd unit, launchd plist, Task Scheduler XML; read back with
plistlib and ElementTree), the quoting of a path with a space, "%" and "$", install and remove with a fake command
runner in a temporary home, --dry-run (nothing written), the command line. NOTHING IS INSTALLED ON THIS COMPUTER.

No real board, no network: 127.0.0.1 only, no listen on UDP 47474 or TCP 47480.

Usage: python tests/unit/test_poolmgr_serve.py
"""
import contextlib
import io
import json
import os
import plistlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PM = os.path.join(ROOT, 'poolmgr')
sys.path.insert(0, HERE)
sys.path.insert(0, PM)
import pm_board  # noqa: E402
import pm_data  # noqa: E402
import pm_ota  # noqa: E402
import pm_pool  # noqa: E402
import pm_serve  # noqa: E402
import pm_service  # noqa: E402
import pm_store  # noqa: E402
import poolmgr  # noqa: E402
from pm_shared import Failed, InputError, NotDone, fw_sign, pool  # noqa: E402
from poolmgr_fake import FakeBoard, NotHttp, OtherDevice  # noqa: E402

FAILS = []
CHECKS = [0]


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


# ---------------------------------------------------------------- the fakes of the other tracks
class FakeListener:
    """The form of pm_net.Listener; the test calls on_beacon / on_join itself."""
    made = []

    def __init__(self, on_join, on_beacon, port=47474, keys=None):
        self.on_join, self.on_beacon, self.port, self.keys = on_join, on_beacon, port, keys
        self.started = self.stopped = False
        self.sets = 0
        FakeListener.made.append(self)

    def start(self):
        self.started = True

    def set_keys(self, keys):
        self.keys = list(keys)
        self.sets += 1

    def stop(self):
        self.stopped = True


class Fakes:
    """The calls of serve to the modules of the other tracks."""

    def __init__(self):
        self.upkeep, self.modes, self.syncs, self.updates, self.routes = [], [], [], [], []
        self.upkeep_fault = self.fault_mac = None
        self.route_fault = None

    def install(self):
        self.saved = [(pm_pool, 'upkeep', pm_pool.upkeep), (pm_store, 'route', pm_store.route),
                      (pm_store, 'set_mode', pm_store.set_mode), (pm_store, 'sync', pm_store.sync),
                      (pm_ota, 'update', pm_ota.update)]
        pm_pool.upkeep = self.f_upkeep
        pm_store.route = self.f_route
        pm_store.set_mode = self.f_set_mode
        pm_store.sync = self.f_sync
        pm_ota.update = self.f_update

    def restore(self):
        for mod, name, fn in self.saved:
            setattr(mod, name, fn)

    def f_upkeep(self, data, mac, st):
        self.upkeep.append((mac, dict(st)))
        if self.upkeep_fault and mac == self.fault_mac:
            raise self.upkeep_fault
        print('a line that the upkeep prints')
        return ['the result of the upkeep of %s' % mac]

    def f_route(self, data, path, query):
        self.routes.append((path, query))
        if self.route_fault:
            raise self.route_fault
        if path == '/api/store/policy':
            return 200, 'text/plain', b'TFNP0'
        if path == '/api/store/catalog':
            return 200, 'text/plain', ('TFN1\tpool\t0\n%s\n' % query.get('kind', [''])[0]).encode()
        return 404, 'text/plain', b'no file\n'

    def f_set_mode(self, data, mode):
        self.modes.append((mode, data.cfg.get('lan_url')))
        return ['%s: ok' % mode]

    def f_sync(self, data):
        self.syncs.append(1)
        return 3

    def f_update(self, data, members, version=None, now=False):
        self.updates.append((list(members), version, now))
        return [{'member': 'T1', 'result': 'idle'}]


# ---------------------------------------------------------------- a pool with fake boards
def make_pool(d):
    """A data folder with a pool: key A, the manager member and 3 fake boards. (data, pool, boards, old roster)."""
    data = pm_data.Data(os.path.join(d, 'data'))
    data.make()
    pub = fw_sign.keygen(data.key_a)
    p = pool.Pool.create('TESTPOOL', pub)
    p.add_manager()
    boards = [FakeBoard('T%d' % i, bytes.fromhex('aabbcc00000%d' % i), d) for i in (1, 2, 3)]
    for b in boards:
        p.upsert({'id': b.id.hex(), 'pk': b.pk.hex(), 'mac': b.mac.hex(), 'role': pool.ROLE_MEMBER, 'flags': 0,
                  'name': b.name})
    old = p.sign(data.key_a)
    p.sign(data.key_a)
    p.save(data.pool_dir)
    data.cfg['store'] = 'local'
    data.save_cfg()
    for b, r in ((boards[0], p.roster_file), (boards[1], old)):
        b.roster, b.epoch, b.pool_k = pool.roster_parse(r), p.epoch, p.pool_k
    return data, p, boards


def small_pool(data):
    """A pool with key A and the manager member only (the service needs a pool)."""
    data.make()
    pub = fw_sign.keygen(data.key_a)
    p = pool.Pool.create('SMALL', pub)
    p.add_manager()
    p.sign(data.key_a)
    p.save(data.pool_dir)


def http(url, method='GET'):
    req = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except (urllib.error.URLError, OSError):
        return None, b''


def beacon(s, b, app='MENU', ip=None):
    s.listener.on_beacon({'mac': b.mac, 'id': b.id, 'name': b.name, 'app': app, 'flags': 0, 'ip': ip or b.ip})


def macs(calls):
    return [m for m, _ in calls]


# ---------------------------------------------------------------- serve
def test_serve(d):
    data, p, (b1, b2, b3) = make_pool(d)
    fk = Fakes()
    fk.install()
    lines = []
    s = pm_serve.Serve(data, port=0, host='127.0.0.1', listener=FakeListener, udp_port=0, out=lines.append,
                       clock=lambda: 0.0)
    try:
        run_serve(d, data, p, s, fk, lines, (b1, b2, b3))
    finally:
        s.stop()
        fk.restore()
        for b in (b1, b2, b3):
            b.srv.shutdown()
    expect(s.listener is None and s.httpd is None, 'stop: no listener, no HTTP server')


def run_serve(d, data, p, s, fk, lines, boards):
    b1, b2, b3 = boards
    m1, m2, m3 = (b.mac.hex().upper() for b in boards)
    s.start()
    lst = s.listener
    expect(lst is not None and lst.started and [k.hint for k in lst.keys] == [p.keys().hint],
           'the listener has the pool keys')
    expect(s.httpd is not None and s.bound, 'the HTTP server runs in the mode local')
    expect(any('SERVE: pool TESTPOOL, 3 members, store local' in ln for ln in lines), 'the start line: %r' % lines)
    base = 'http://127.0.0.1:%d' % s.bound

    # the routes of the local store (GET only), 404 for all else
    code, body = http(base + '/api/store/policy')
    expect(code == 200 and body == b'TFNP0', 'GET policy: %r %r' % (code, body))
    code, body = http(base + '/api/store/catalog?kind=lcd349&abi=23&rev=1')
    expect(code == 200 and b'lcd349' in body, 'GET catalog with the query')
    expect(fk.routes[-1][1].get('abi') == ['23'], 'the query goes to route() as parse_qs')
    for path in ('/api/pool', '/', '/api/store', '/api/store/policy/x', '/admin'):
        expect(http(base + path)[0] == 404, 'GET %s = 404' % path)
    for method in ('POST', 'PUT', 'DELETE'):
        expect(http(base + '/api/store/policy', method)[0] == 404, '%s policy = 404' % method)
    fk.route_fault = ValueError('a test fault')
    expect(http(base + '/api/store/policy')[0] == 500, 'a fault of route() = 500')
    fk.route_fault = NotDone('NOT DONE: route')
    expect(http(base + '/api/store/policy')[0] == 503, 'route() not done = 503')
    fk.route_fault = None
    expect(http(base + '/api/store/policy')[0] == 200, 'the HTTP server goes on after a fault')

    # beacons of 2 members (one with an older roster), one of a board that is not a member; member 3 (LEAVE) sends
    # no beacon, but its address is known from the CLI
    beacon(s, b1)
    beacon(s, b2, app='PONG')
    s.listener.on_beacon({'mac': b'\x01\x02\x03\x04\x05\x06', 'id': b'\0' * 8, 'name': 'X', 'app': '', 'flags': 0,
                          'ip': '127.0.0.1:9'})
    data.set_addr(m3, b3.ip, '', b3.id.hex())
    s.tick(0.0)
    a = data.addrs()
    expect(a.get(m1, {}).get('ip') == b1.ip and a.get(m2, {}).get('app') == 'PONG', 'the beacons into addrs.json')
    expect('010203040506' not in a, 'a board that is not a member: not in addrs.json')
    expect(sorted(macs(fk.upkeep)) == sorted([m2, m3]), 'the upkeep: the old roster and the LEAVE: %r' %
           macs(fk.upkeep))
    st2 = dict(fk.upkeep)[m2]
    expect(st2.get('v') == p.version - 1 and st2.get('ip') == b2.ip, 'the upkeep gets the answer of /api/pool')
    expect(dict(fk.upkeep)[m3].get('state') == 'none', 'the LEAVE member: state none')
    expect(any('UPKEEP T2: old roster' in ln for ln in lines), 'the upkeep line of T2')
    expect(any(ln.endswith('  the result of the upkeep of %s' % m2) for ln in lines) and
           any(ln.endswith(' a line that the upkeep prints') for ln in lines), 'serve prints the lines of the upkeep')
    slog = os.path.join(data.root, 'log', 'serve.log')
    with open(slog, 'rb') as h:
        text = h.read().decode('utf-8')
    expect('SERVE: pool TESTPOOL' in text and 'the result of the upkeep of %s' % m2 in text,
           'the lines of serve go into log/serve.log too')
    with open(data.log_file, 'rb') as h:
        expect(b'serve upkeep T2' in h.read(), 'the upkeep goes into the log')

    # the LAN address: op=mgr again with the address of this computer
    url = 'http://127.0.0.1:%d' % s.bound
    expect(fk.modes == [('local', url)], 'op=mgr with the LAN address: %r' % fk.modes)
    d2 = pm_data.Data(data.root)
    d2.load()
    expect(d2.cfg['lan_url'] == url and d2.cfg['port'] == s.bound, 'config.json has lan_url and the port')

    # 30 s: a new round, but at most one upkeep a minute for each member; no beacon of 3 = no new probe
    n = len(fk.upkeep)
    beacon(s, b2)
    s.tick(10.0)
    expect(len(fk.upkeep) == n and not fk.syncs, 'no round before 30 s, no sync before 60 s')
    s.tick(31.0)
    expect(len(fk.upkeep) == n, 'no upkeep of the same member in 60 s (no push storm)')
    beacon(s, b2)
    s.tick(62.0)
    expect(macs(fk.upkeep[n:]) == [m2], 'the upkeep again after 60 s, only the member with a beacon: %r' %
           macs(fk.upkeep[n:]))
    s.tick(125.0)
    expect(macs(fk.upkeep[n + 1:]) == [m3], 'a member with no beacon: a probe each 2 min: %r' % macs(fk.upkeep[n:]))
    expect(fk.modes == [('local', url)], 'the same LAN address: no op=mgr again')

    # a member that does not answer: no fault, the next try in 5 min
    b3.srv.shutdown()
    b3.srv.server_close()
    t = 260.0
    s.tick(t)
    expect(s.next_probe.get(m3, 0) >= t + pm_serve.FAIL_S - 1, 'a member with no answer waits 5 min')

    # a new key of the pool (the CLI): serve reads it, the listener starts again with the new keys
    p2 = pool.Pool.load(data.pool_dir)
    p2.new_key()
    p2.sign(data.key_a)
    time.sleep(0.05)
    p2.save(data.pool_dir)
    old = s.listener
    s.tick(t + 1)
    expect(not old.stopped and s.listener is old and old.sets == 1 and
           [k.epoch for k in old.keys] == [1, 0] and old.keys[0].hint == p2.keys().hint,
           'a new key: set_keys with the new key and the previous key (its hour)')
    expect(any('READ the pool again' in ln and 'key epoch 1' in ln for ln in lines), 'the line of the new pool')
    # b1 has the old key now: the upkeep at the next round
    beacon(s, b1)
    s.tick(t + 31)
    expect(m1 in macs(fk.upkeep) and any('UPKEEP T1: old roster' in ln for ln in lines),
           'a member with the old roster and key: the upkeep')
    # the end of the hour of the previous key: the check of the keys (each minute) gives the current key only
    keep, pm_serve.pm_net.PREV_KEEP = pm_serve.pm_net.PREV_KEEP, 0
    try:
        s.t_keys = 0
        s.tick(t + 32)
    finally:
        pm_serve.pm_net.PREV_KEEP = keep
    expect(s.listener is old and old.sets == 2 and [k.epoch for k in old.keys] == [1],
           'the end of the hour of the previous key: set_keys with the current key only (%r)' % (
               [k.epoch for k in old.keys],))

    # the sync of the mirror (the first one at 60 s) and the auto update
    expect(len(fk.syncs) == 1 and any('STORE SYNC: 3 packages' in ln for ln in lines), 'the first sync')
    s.tick(t + 50)
    expect(len(fk.syncs) == 1, 'the next sync after 6 hours only')
    s.tick(pm_serve.UPDATE_FIRST + 1)
    expect(not fk.updates, 'auto_update false: no update')
    cfg_set(data.root, auto_update=True)
    s.tick(pm_serve.UPDATE_FIRST + 2)
    expect(fk.updates == [([], None, False)], 'auto_update: all members, only idle ones: %r' % fk.updates)
    expect(any('AUTO UPDATE: member=T1' in ln for ln in lines), 'the line of the auto update')
    s.tick(pm_serve.UPDATE_FIRST + 3)
    expect(len(fk.updates) == 1, 'the next update after 6 hours only')
    s.tick(pm_serve.UPDATE_FIRST + pm_serve.UPDATE_S + 3)
    expect(len(fk.updates) == 2 and len(fk.syncs) == 2, 'after 6 hours: the update and the sync again')

    # the store mode "official" (the CLI): the HTTP server stops; "local" again: it starts on the same port
    port = s.bound
    cfg_set(data.root, store='official')
    tt = pm_serve.UPDATE_FIRST + pm_serve.UPDATE_S + 10
    s.tick(tt)
    expect(s.httpd is None and http(base + '/api/store/policy')[0] is None, 'official: no HTTP server')
    cfg_set(data.root, store='local')
    s.tick(tt + 1)
    expect(s.httpd is not None and s.bound == port and http(base + '/api/store/policy')[0] == 200,
           'local again: the HTTP server on the same port')

    # a new LAN address (DHCP): op=mgr again
    s.lan_sent = 'http://192.0.2.99:%d' % port
    s.t_lan = 0
    s.tick(tt + 2)
    expect(len(fk.modes) == 2 and fk.modes[-1] == ('local', url), 'a new LAN address: op=mgr again: %r' % fk.modes)

    # the change lock of another process: the upkeep waits for the next round; then it runs
    hold = subprocess.Popen([sys.executable, '-c', 'import sys, time; sys.path.insert(0, %r); import pm_data; '
                             'd = pm_data.Data(%r)\nwith d.change():\n    print("HELD", flush=True)\n'
                             '    time.sleep(4)' % (PM, data.root)], stdout=subprocess.PIPE, text=True)
    expect(hold.stdout.readline().strip() == 'HELD', 'the other process holds the lock')
    wait, pm_serve.LOCK_WAIT = pm_serve.LOCK_WAIT, 0.3
    n = macs(fk.upkeep).count(m2)
    beacon(s, b2)
    tb = tt + 200
    s.tick(tb)
    expect(macs(fk.upkeep).count(m2) == n and any('BUSY' in ln for ln in lines), 'a busy lock: no upkeep, a BUSY line')
    hold.wait(20)
    beacon(s, b2)
    s.tick(tb + 31)
    expect(macs(fk.upkeep).count(m2) == n + 1, 'the upkeep in the next round after the lock is free')
    pm_serve.LOCK_WAIT = wait

    # a fault of the upkeep: one line, serve goes on
    fk.fault_mac = m2
    fk.upkeep_fault = Failed('the board refused')
    beacon(s, b2)
    s.tick(tb + 100)
    expect(any('FAILED upkeep T2: the board refused' in ln for ln in lines), 'a failed upkeep: one line')
    fk.upkeep_fault = RuntimeError('a test fault')
    beacon(s, b2)
    s.tick(tb + 200)
    expect(any('FAULT upkeep T2: RuntimeError' in ln for ln in lines), 'a fault of the upkeep: one line')
    fk.upkeep_fault = NotDone('NOT DONE: upkeep (track A)')
    for i in range(2):
        beacon(s, b2)
        s.tick(tb + 300 + 100 * i)
    expect(sum('NOT DONE: upkeep' in ln for ln in lines) == 1, 'NOT DONE: one line only')
    fk.upkeep_fault = None

    # a JOIN screen: one line with the adopt command, not again in 10 min
    s.listener.on_join({'mac': b'\xaa\xbb\xcc\x00\x00\x09', 'id': b'\1' * 8, 'name': 'NEWBOARD', 'ip': '127.0.0.1'})
    s.listener.on_join({'mac': b'\xaa\xbb\xcc\x00\x00\x09', 'id': b'\1' * 8, 'name': 'NEWBOARD', 'ip': '127.0.0.1'})
    s.tick(tb + 600)
    expect(sum('JOIN SCREEN OPEN: NEWBOARD' in ln and 'poolmgr adopt 127.0.0.1' in ln for ln in lines) == 1,
           'one JOIN line with the adopt command')
    expect(any(ln.split(' ', 2)[2].startswith('STATUS: 3 members') for ln in lines), 'the status line')

    # a friend entry that waits for its removal on a member: the upkeep of that member
    pl = pool.Pool.load(data.pool_dir)
    with b1.lock:
        b1.roster, b1.epoch, b1.pool_k = pool.roster_parse(pl.roster_file), pl.epoch, pl.pool_k
    with open(os.path.join(data.pool_dir, 'friends_out.json'), 'w') as h:
        json.dump({'format': 1, 'pending': [{'on': m1, 'mac': 'AABBCC0000FF', 'name': 'GONE'}]}, h)
    n = macs(fk.upkeep).count(m1)
    beacon(s, b1)
    s.tick(tb + 700)
    expect(macs(fk.upkeep).count(m1) == n + 1 and any('UPKEEP T1: a friend entry waits for its removal' in ln
                                                      for ln in lines), 'a friend entry that waits: the upkeep')
    os.remove(os.path.join(data.pool_dir, 'friends_out.json'))
    other_devices(s, data, p, b3, lines, tb + 800)

    # the size limit of log/serve.log: serve.log.1 and a new serve.log
    keep, pm_serve.SERVE_LOG_MAX = pm_serve.SERVE_LOG_MAX, 400
    try:
        for i in range(12):
            s.out('a long line %d %s' % (i, 'x' * 60))
    finally:
        pm_serve.SERVE_LOG_MAX = keep
    expect(os.path.isfile(slog + '.1') and os.path.getsize(slog) < 400 + 100 and os.path.getsize(slog + '.1') < 400 + 100,
           'log/serve.log: two small files (%d, %d)' % (os.path.getsize(slog), os.path.getsize(slog + '.1')))

    # no secret in the output
    sec = json.load(open(os.path.join(data.pool_dir, 'secrets.json')))
    text = '\n'.join(lines)
    expect(sec['pool_k'] not in text and not any(w in text for w in sec['wk'].values()), 'no secret in the output')


def other_devices(s, data, p, b3, lines, t):
    """A device that is not a TFN board at the address of the member T3 (it is off): bytes that are not HTTP, then a
    JSON answer of the pool with a version that is not a number. Each gives no trace and no upkeep: one line, the next
    try after FAIL_S, and serve goes on."""
    m3 = b3.mac.hex().upper()
    nh = NotHttp()
    page = OtherDevice(ctype='application/json', body=json.dumps({'state': 'ok', 'pool': p.pool_id.hex(), 'v': 'x',
                                                                  'epoch': [1]}).encode())
    try:
        for i, (dev, what) in enumerate(((nh, 'an answer that is not HTTP'), (page, 'a version that is not a number'))):
            n = s.counts['no answer']
            fixes = len([ln for ln in lines if 'UPKEEP T3' in ln])
            beacon(s, b3, ip=dev.ip)
            try:
                s.tick(t + 100 * i)
                ok = True
            except Exception as e:  # the fault of the test: serve must not stop
                ok = False
                print('serve stopped: %s: %s' % (type(e).__name__, e))
            expect(ok and s.next_probe.get(m3, 0) >= t + 100 * i + pm_serve.QUIET_S - 1 and
                   len([ln for ln in lines if 'UPKEEP T3' in ln]) == fixes, 'serve with %s at the address of a member: '
                   'no trace, no upkeep (%r)' % (what, lines[-3:]))
            if dev is nh:
                expect(s.counts['no answer'] == n + 1, 'an answer that is not HTTP counts as no answer')
        expect(page.seen and page.seen[0][:2] == ('GET', '/api/pool') and all(a is None for _, _, a in page.seen),
               'the device gets only GET /api/pool with no password (%r)' % page.seen)
        # a fault of the probe of one member: one line, the next try after FAIL_S, serve goes on
        real = pm_serve.needs_upkeep

        def broken(*a):
            raise RuntimeError('a test fault of the probe')
        pm_serve.needs_upkeep = broken
        beacon(s, b3, ip=page.ip)
        try:
            s.tick(t + 250)
            ok = True
        except RuntimeError:
            ok = False
        finally:
            pm_serve.needs_upkeep = real
        expect(ok and any('FAULT probe T3: RuntimeError' in ln for ln in lines) and
               s.next_probe.get(m3, 0) >= t + 250 + pm_serve.FAIL_S - 1, 'a fault of the probe of one member: one '
               'line, serve goes on (%r)' % lines[-3:])
    finally:
        nh.close()
        page.close()
    try:
        pm_board.http_get('http://%s/api/pool' % NotHttp().ip, timeout=2)
        expect(False, 'http_get of an answer that is not HTTP: Failed')
    except Failed as e:
        expect('no HTTP answer' in str(e), 'http_get of an answer that is not HTTP: Failed (%s)' % e)
    for url in ('http://127.0.0.1:99999/x', 'http://[::1/x'):
        try:
            pm_board.http_get(url, timeout=2)
            expect(False, 'http_get of a bad URL %s: Failed' % url)
        except Failed:
            expect(True, 'http_get of a bad URL')


def cfg_set(root, **kv):
    """A change of config.json by "another process" (a new Data object), with a new time stamp."""
    d = pm_data.Data(root)
    d.load()
    d.cfg.update(kv)
    time.sleep(0.05)
    d.save_cfg()


def test_run(d):
    """run(): no pool = InputError; with a pool it runs until a stop and gives 0."""
    empty = pm_data.Data(os.path.join(d, 'empty'))
    try:
        pm_serve.run(empty, 0)
        expect(False, 'run with no pool must stop')
    except InputError as e:
        expect('no pool' in str(e), 'run with no pool: %s' % e)
    code, out = cli('--data', empty.root, 'serve', '--port', '0')
    expect(code == 2 and 'no pool' in out, 'poolmgr serve with no pool: exit 2')

    d2 = os.path.join(d, 'run')
    os.makedirs(d2)
    data, p, boards = make_pool(d2)
    data.cfg['store'] = 'official'
    data.save_cfg()
    old = pm_serve.pm_net.Listener
    pm_serve.pm_net.Listener = FakeListener
    res = []
    out = io.StringIO()

    def go():
        with contextlib.redirect_stdout(out):
            res.append(pm_serve.run(pm_data.Data(data.root), 0))
    th = threading.Thread(target=go)
    try:
        th.start()
        end = time.time() + 10
        while pm_serve.ACTIVE is None and time.time() < end:
            time.sleep(0.05)
        expect(pm_serve.ACTIVE is not None and pm_serve.ACTIVE.httpd is None, 'run: serve runs, no HTTP (official)')
        if pm_serve.ACTIVE is not None:
            pm_serve.ACTIVE.stop()
        th.join(10)
    finally:
        pm_serve.pm_net.Listener = old
        for b in boards:
            b.srv.shutdown()
    expect(res == [0] and 'SERVE STOP' in out.getvalue(), 'run: a stop gives 0: %r %r' % (res, out.getvalue()[-300:]))


def read(path):
    try:
        with open(path, 'rb') as h:
            return h.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def main_no_output(argv):
    """poolmgr.main with no output (pythonw of the service of Windows: sys.stdout is None): the exit code."""
    keep = sys.stdout
    sys.stdout = None
    try:
        return poolmgr.main(argv)
    finally:
        sys.stdout = keep


def test_stop(d):
    """A fault that stops serve at the start, with no output: a UDP port in use (another serve or another manager),
    then a TCP port in use (the local store). The STOP line goes into log/serve.log and log/poolmgr.log, exit 2. A
    data folder with no pool gets no file. The lines of a job go out at once, and a line of another thread in the time
    of a job goes out one time only."""
    d2 = os.path.join(d, 'stop')
    os.makedirs(d2)
    data, p, boards = make_pool(d2)
    for b in boards:
        b.srv.shutdown()
    slog = os.path.join(data.root, 'log', 'serve.log')
    busy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    busy.bind(('', 0))
    udp = busy.getsockname()[1]
    keep = pm_serve.pm_net.UDP_PORT
    pm_serve.pm_net.UDP_PORT = udp
    try:
        code = main_no_output(['--data', data.root, 'serve', '--port', '0'])
    finally:
        pm_serve.pm_net.UDP_PORT = keep
        busy.close()
    want = 'the UDP port %d does not open' % udp
    expect(code == 2 and 'STOP: ' + want in read(slog) and 'serve stop: ' + want in read(data.log_file),
           'serve with a UDP port in use and no output: the STOP line in the files (%r)' % read(slog)[-300:])
    busy = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    busy.bind(('', 0))
    busy.listen(1)
    tcp = busy.getsockname()[1]
    keep = pm_serve.pm_net.Listener
    pm_serve.pm_net.Listener = FakeListener
    try:
        code = main_no_output(['--data', data.root, 'serve', '--port', str(tcp)])
    finally:
        pm_serve.pm_net.Listener = keep
        busy.close()
    want = 'the TCP port %d does not open' % tcp
    expect(code == 2 and 'STOP: ' + want in read(slog) and FakeListener.made[-1].stopped,
           'serve with a TCP port in use: the STOP line in log/serve.log, the listener stops (%r)' % read(slog)[-300:])
    empty = os.path.join(d, 'stop_nopool')
    code = main_no_output(['--data', empty, 'serve', '--port', '0'])
    expect(code == 2 and not os.path.exists(empty), 'serve with no pool and no output: exit 2, no file')

    # the lines of a job: each line at once; a line of another thread in the time of the job goes out one time
    real = io.StringIO()
    with contextlib.redirect_stdout(real):
        s = pm_serve.Serve(data, port=0, host='127.0.0.1', listener=FakeListener, udp_port=0)
    mid = []

    def job():
        print('a line of the job')
        th = threading.Thread(target=s.note, args=('k', 'a line of another thread'))
        th.start()
        th.join()
        mid.append(read(slog))
        print('the last part of the job', end='')
        return 'the result'
    r = s.lines_of(job)
    text = read(slog)
    expect(r == 'the result' and mid and 'a line of the job' in mid[0], 'a line of a job goes into the file at once')
    expect(text.count('a line of another thread') == 1 and real.getvalue().count('a line of another thread') == 1,
           'a line of another thread in the time of a job: one time (%d in the file)' % text.count(
               'a line of another thread'))
    expect('the last part of the job' in text and 'the last part of the job' in real.getvalue(),
           'the last part of a job with no line end')


def test_needs():
    p = pool.Pool.create('P', b'\4' + b'\1' * 64)
    p.version, p.epoch = 5, 2
    m = {'id': '11' * 8}
    pid = p.pool_id.hex()
    rows = [({'state': 'ok', 'pool': pid, 'v': 5, 'epoch': 2}, ''),
            ({'state': 'ok', 'pool': pid, 'v': 6, 'epoch': 2}, ''),
            ({'state': 'ok', 'pool': pid, 'v': 4, 'epoch': 2}, 'old roster'),
            ({'state': 'ok', 'pool': pid, 'v': 5, 'epoch': 1}, 'old key'),
            ({'state': 'ok', 'pool': '22' * 16, 'v': 1, 'epoch': 0}, ''),
            ({'state': 'damaged'}, 'damaged'),
            ({'state': 'none', 'id': '11' * 8}, 'LEAVE'),
            ({'state': 'none', 'id': '33' * 8}, ''),
            ({'state': 'join', 'id': '11' * 8}, ''),
            ({'state': 'ok', 'pool': pid, 'v': 'x', 'epoch': 2}, ''),
            ({'state': 'ok', 'pool': pid, 'v': 4, 'epoch': [1]}, ''),
            ({'state': 'ok', 'pool': pid, 'v': True, 'epoch': 2}, ''),
            ({'state': 'ok', 'pool': pid, 'v': '4', 'epoch': 2}, 'old roster'),
            ({'state': 'ok', 'pool': pid, 'v': None, 'epoch': 2}, 'old roster')]
    for st, want in rows:
        got = pm_serve.needs_upkeep(p, m, st)
        expect((want in got) if want else got == '', 'needs_upkeep %r: %r' % (st, got))


# ---------------------------------------------------------------- service
class FakeRun:
    def __init__(self, fail=()):
        self.argv, self.fail = [], fail

    def __call__(self, step):
        self.argv.append(step.argv)
        code = 1 if any(f in step.argv for f in self.fail) else 0
        return subprocess.CompletedProcess(step.argv, code, 'out', 'err' if code else '')


def test_service(d):
    data = pm_data.Data(os.path.join(d, 'my data %x $HOME'))
    prog = os.path.join(d, 'tfn os', 'poolmgr', 'poolmgr.py')
    py = os.path.join(d, 'py thon', 'python3')
    home = os.path.join(d, 'home')

    # linux: a systemd user unit
    pl = pm_service.plan(data, prog, 'linux', home, python=py)
    body = pl.body.decode()
    expect(pl.path == os.path.join(home, '.config', 'systemd', 'user', 'tfn-poolmgr.service'), 'unit path')
    exec_line = [ln for ln in body.splitlines() if ln.startswith('ExecStart=')]
    expect(len(exec_line) == 1, 'one ExecStart')
    words = systemd_words(exec_line[0][len('ExecStart='):])
    expect(words == [py, prog, '--data', data.root, 'serve'], 'ExecStart quoting: %r' % words)
    for k in ('[Unit]', '[Service]', '[Install]', 'Restart=on-failure', 'WantedBy=default.target'):
        expect(k in body, 'unit: %s' % k)
    expect([s.argv for s in pl.install] == [['systemctl', '--user', 'daemon-reload'],
                                            ['systemctl', '--user', 'enable', '--now', 'tfn-poolmgr.service']],
           'unit install steps')
    expect(any('enable-linger' in n for n in pl.notes), 'unit: the linger note')
    pl = pm_service.plan(data, prog, 'linux', home, python=py, port=47481)
    expect(pl.argv[-2:] == ['--port', '47481'], 'another port goes on the command line')

    # macOS: a launchd agent
    pl = pm_service.plan(data, prog, 'macos', home, python=py)
    pq = plistlib.loads(pl.body)
    expect(pl.path == os.path.join(home, 'Library', 'LaunchAgents', 'tfn-os.poolmgr.plist'), 'plist path')
    expect(pq['ProgramArguments'] == [py, prog, '--data', data.root, 'serve'] and pq['RunAtLoad'] and
           pq['Label'] == 'tfn-os.poolmgr' and pq['KeepAlive'] == {'SuccessfulExit': False}, 'plist values')
    expect(pq['StandardOutPath'] == '/dev/null' and pq['StandardErrorPath'].startswith(data.root),
           'plist: serve writes log/serve.log itself; a fault of Python goes into the data folder')
    expect(pl.install[0].may_fail and pl.install[-1].argv[:2] == ['launchctl', 'bootstrap'], 'plist steps')

    # windows: a task of the Task Scheduler
    pl = pm_service.plan(data, prog, 'windows', home, python=py)
    xml = pl.body.decode('utf-16')
    expect(pl.body[:2] in (b'\xff\xfe', b'\xfe\xff'), 'task XML: UTF-16 with a BOM')
    cmd, args = pm_service.task_command(pl.body)
    expect(cmd == py and args == subprocess.list2cmdline([prog, '--data', data.root, 'serve']), 'task: %r %r' %
           (cmd, args))
    for k in ('<LogonTrigger>', 'LeastPrivilege', '<ExecutionTimeLimit>PT0S', '<RestartOnFailure>',
              '<StopIfGoingOnBatteries>false'):
        expect(k in xml, 'task: %s' % k)
    expect(pl.path == os.path.join(data.root, 'service', 'tfn-poolmgr-task.xml'), 'task file in the data folder')
    expect(pl.install[0].argv[:4] == ['schtasks', '/Create', '/TN', 'TFN poolmgr'] and '/XML' in pl.install[0].argv,
           'task: schtasks /Create /XML')
    # the variable OPENSSL: the Windows task gets it as "serve --openssl" (a task has no variables), the unit as a
    # variable
    want = os.path.join(d, 'open ssl', 'openssl.exe')
    keep = os.environ.get('OPENSSL')
    os.environ['OPENSSL'] = want
    try:
        pl = pm_service.plan(data, prog, 'windows', home, python=py)
        cmd, args = pm_service.task_command(pl.body)
        lin = pm_service.plan(data, prog, 'linux', home, python=py).body.decode()
    finally:
        if keep is None:
            os.environ.pop('OPENSSL')
        else:
            os.environ['OPENSSL'] = keep
    expect(pl.argv[-2:] == ['--openssl', want] and args.endswith(subprocess.list2cmdline(['--openssl', want])),
           'task: the variable OPENSSL as serve --openssl: %r' % args)
    expect('Environment="OPENSSL=' in lin and '--openssl' not in lin, 'the unit: the variable OPENSSL')
    a = poolmgr.parser().parse_args(['serve', '--openssl', 'x'])
    expect(a.openssl == 'x', 'serve --openssl')

    # text() for this system: (the file, where and how)
    body, where = pm_service.text(data, prog)
    expect(isinstance(body, str) and 'serve' in body and 'THE FILE:' in where and 'install' in where, 'text()')

    for system in pm_service.SYSTEMS:
        h = os.path.join(d, 'h_' + system)
        dd = pm_data.Data(os.path.join(d, 'data_' + system))
        # --dry-run: nothing is written, no command runs
        lines, run = [], FakeRun()
        pm_service.install(dd, prog, dry_run=True, system=system, home=h, run=run, out=lines.append)
        expect(not os.path.exists(h) and not os.path.exists(dd.root) and not run.argv, '%s dry run: nothing written'
               % system)
        expect(any(ln.startswith('WRITE ') for ln in lines) and any(ln.startswith('RUN ') for ln in lines) and
               'DRY RUN' in lines[-1] and any('has no pool' in ln for ln in lines), '%s dry run: the lines' % system)
        # install with no pool: it stops, nothing is written (a service of serve with no pool stops at each start)
        try:
            pm_service.install(dd, prog, system=system, home=h, run=run, out=lines.append)
            expect(False, '%s install with no pool must stop' % system)
        except InputError as e:
            expect('no pool' in str(e) and not os.path.exists(h) and not os.path.exists(dd.root) and not run.argv,
                   '%s install with no pool: nothing written (%s)' % (system, e))
        small_pool(dd)
        # install with a fake runner
        lines, run = [], FakeRun(fail=('bootout',))
        code = pm_service.install(dd, prog, system=system, home=h, run=run, out=lines.append)
        pl = pm_service.plan(dd, prog, system, h)
        expect(code == 0 and os.path.isfile(pl.path) and open(pl.path, 'rb').read() == pl.body and
               'at each logon of your user' in lines[-1], '%s install: the file' % system)
        if os.name == 'posix' and system == 'windows':
            expect(os.stat(os.path.dirname(pl.path)).st_mode & 0o077 == 0, 'the folder service/: mode 0700')
        expect(run.argv == [s.argv for s in pl.install], '%s install: the steps in order' % system)
        expect(lines.index('WRITE %s' % pl.path) < [i for i, ln in enumerate(lines) if ln.startswith('RUN')][0],
               '%s install: the line before each step' % system)
        # a step that fails stops the install
        try:
            pm_service.install(dd, prog, system=system, home=h, run=FakeRun(fail=(pl.install[-1].argv[1],)),
                               out=lambda s: None)
            expect(False, '%s: a failed step must stop' % system)
        except Failed as e:
            expect('exit code 1' in str(e), '%s: a failed step: %s' % (system, e))
        # remove
        lines, run = [], FakeRun(fail=('/End', 'disable'))
        pm_service.remove(dd, prog, dry_run=True, system=system, home=h, run=run, out=lines.append)
        expect(os.path.isfile(pl.path) and not run.argv, '%s remove dry run: the file stays' % system)
        pm_service.remove(dd, prog, system=system, home=h, run=run, out=lines.append)
        expect(not os.path.exists(pl.path) and run.argv[0] == pl.remove[0].argv and lines[-1] == 'SERVICE REMOVED',
               '%s remove: the file is gone, the steps ran' % system)

    # another system with no --dry-run and no runner: stop (nothing on this computer)
    other = [s for s in pm_service.SYSTEMS if s != pm_service.this_system()][0]
    try:
        pm_service.install(data, prog, system=other, home=home, out=lambda s: None)
        expect(False, 'the service of another system must stop')
    except InputError:
        expect(not os.path.exists(home), 'another system: nothing written')

    # the command lines: poolmgr service, pm_service.py --dry-run
    code, out = cli('--data', data.root, 'service')
    expect(code == 0 and 'serve' in out and 'THE FILE:' in out, 'poolmgr service: %r' % out[-300:])
    r = subprocess.run([sys.executable, os.path.join(PM, 'pm_service.py'), '--data', data.root, 'install',
                        '--dry-run'], capture_output=True, encoding='utf-8', errors='replace')
    expect(r.returncode == 0 and 'DRY RUN' in r.stdout and not os.path.exists(data.root),
           'pm_service.py install --dry-run: %r' % (r.stdout + r.stderr)[-300:])
    for system in pm_service.SYSTEMS:
        r = subprocess.run([sys.executable, os.path.join(PM, 'pm_service.py'), '--data', data.root, 'show',
                            '--system', system], capture_output=True, encoding='utf-8', errors='replace')
        expect(r.returncode == 0 and 'THE FILE:' in r.stdout, 'pm_service.py show --system %s' % system)
    r = subprocess.run([sys.executable, os.path.join(PM, 'pm_service.py'), '--data', data.root, 'install', '--system',
                        other], capture_output=True, encoding='utf-8', errors='replace')
    expect(r.returncode == 2 and 'STOP' in r.stdout, 'pm_service.py install of another system: exit 2')


def systemd_words(line):
    """The words of an ExecStart line (the systemd quoting: "..." with \\ escapes, %% and $$)."""
    words, cur, q, i, has = [], '', False, 0, False
    while i < len(line):
        c = line[i]
        if q and c == '\\':
            cur += line[i + 1]
            i += 2
            continue
        if c == '"':
            q, has = not q, True
        elif c == ' ' and not q:
            if cur or has:
                words.append(cur)
            cur, has = '', False
        else:
            cur += c
        i += 1
    if cur or has:
        words.append(cur)
    return [w.replace('%%', '%').replace('$$', '$') for w in words]


def cli(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            code = poolmgr.main(list(argv))
        except SystemExit as e:
            code = e.code
    return code, out.getvalue()


def main():
    t0 = time.time()
    expect(pm_serve.DONE and pm_service.DONE, 'DONE = True')
    test_needs()
    with tempfile.TemporaryDirectory(prefix='pm_serve_') as d:
        test_serve(os.path.join(d))
        test_run(d)
        test_stop(d)
        test_service(d)
    if FAILS:
        print('POOLMGR SERVE PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('POOLMGR SERVE PY OK (%d checks, %.1f s)' % (CHECKS[0], time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
