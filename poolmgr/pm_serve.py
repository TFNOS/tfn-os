"""serve (design 2.13): one process with the UDP listener (the addresses into addrs.json), the HTTP server of the
local store (only in the mode "local"; only the 3 store routes of pm_store.route()), the upkeep of the members
(pm_pool.upkeep each 30 s), the store sync and the auto update (each 6 hours). No admin route.

The parts:
  - THE LISTENER (pm_net.Listener): the 'A' 'P' beacons of the members give their addresses (addrs.json) and mark
    them as "seen". The 'A' 'J' packets of a board with an open JOIN screen give one line with the adopt command
    (serve never adopts a board by itself).
  - THE UPKEEP (design 2.8), each 30 s: GET /api/pool (a public route) of each member whose beacon came, and of each
    other member with a known address each 2 minutes (a member with an older key sends beacons that the new key
    cannot open, and a member after LEAVE sends no beacon). pm_pool.upkeep() gets the answer only when the member
    needs work (an older roster or key, "damaged", or "none" with its own ID), and at most once a minute for each
    member (no push storm).
  - THE LOCAL STORE (design 2.11), only in the mode "local": the HTTP server with the routes /api/store/policy,
    /api/store/catalog and /api/store/file (GET only). Any other path or method = 404. The LAN address of this
    computer toward the boards is checked each minute: a new address (DHCP) = op=mgr again (pm_store.set_mode).
  - THE STORE SYNC (pm_store.sync) in the mode "local" and THE AUTO UPDATE (pm_ota.update, only when config.json
    has "auto_update": true) each 6 hours.
  - THE FILES: serve reads config.json and the pool folder again when they change (the CLI changes them). Each change
    of the pool takes the change lock (data.change()); a busy lock = the work waits for the next round.
  - THE BEACON KEYS (pm_net.data_keys): the current pool key, and the previous key for 1 hour after a change of the
    key (a member that missed the new key still sends with the old key; the upkeep gives it the new key). A new key
    list goes to the listener that runs (Listener.set_keys); serve checks the list each minute.
  - THE LINES: each line of serve goes to its output and into log/serve.log of the data folder (the service of
    Windows runs pythonw, which has no output). At SERVE_LOG_MAX bytes the file becomes serve.log.1 (the one before
    goes): the two files stay small. The lines that a job prints (the upkeep, the store, the update) go the same way,
    each line at once (LineOut). A fault that stops serve (a port in use at the start, a fault of the disk) gives the
    line "STOP: <fault>" in log/serve.log and log/poolmgr.log before poolmgr.main prints it (stopped_by()).

All work runs in the main thread, one job at a time (the change lock of pm_data counts in the process: two threads
of one process must not change the pool at the same time). The listener thread only puts the packets in a queue; the
HTTP threads only read. A fault of one job prints one line and serve goes on.
"""
import collections
import contextlib
import http.server
import io
import json
import os
import signal
import socketserver
import sys
import threading
import time
import urllib.parse

import pm_board
import pm_net
import pm_ota
import pm_pool
import pm_store
from pm_shared import Failed, InputError, NotDone, pool, pool_tool

DONE = True
PORT = 47480

TICK = 1.0                  # the main loop: the check of the files and the queue
UPKEEP_S = 30.0             # the upkeep round
QUIET_S = 120.0             # a member with no new beacon: GET /api/pool at most this often
FAIL_S = 300.0              # a member that did not answer: the next try
PUSH_GAP = 60.0             # pm_pool.upkeep at most once a minute for each member
LAN_S = 60.0                # the check of the LAN address of this computer (mode "local")
KEYS_S = 60.0               # the check of the beacon keys (the end of the hour of the previous key)
LAN_RETRY = 600.0           # after a failed op=mgr
SYNC_FIRST = 60.0           # the first store sync after the start
SYNC_S = 6 * 3600.0
UPDATE_FIRST = 600.0        # the first auto update after the start
UPDATE_S = 6 * 3600.0
STATUS_S = 600.0            # one status line
ADDR_WRITE_S = 300.0        # addrs.json: a write for a new address, else at most this often
JOIN_NOTE_S = 600.0         # one line for each JOIN screen in this time
PROBE_TIMEOUT = 3.0
LOCK_WAIT = 5.0
HTTP_TIMEOUT = 30.0         # a store client that sends nothing for this long is closed
ROUTES = ('/api/store/policy', '/api/store/catalog', '/api/store/file')
SERVE_LOG = 'serve.log'     # in log/ of the data folder
SERVE_LOG_MAX = 256 * 1024  # then serve.log becomes serve.log.1

ACTIVE = None               # the Serve object of run() (the tests stop it with ACTIVE.stop())


# ---------------------------------------------------------------- the small parts
def mac_hex(v):
    """A MAC of a packet (bytes or text) as upper hex with no colons."""
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).hex().upper()
    return str(v or '').replace(':', '').replace('-', '').upper()


def id_hex(v):
    """A device ID of a packet (bytes or text) as lower hex."""
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).hex()
    return str(v or '').replace('-', '').lower()


def number(v):
    """The integer of a field of an answer, or None (not an integer: a device that is not a TFN board)."""
    if isinstance(v, bool):
        return None
    if v is None:
        return 0
    try:
        return int(v)
    except (TypeError, ValueError, OverflowError):
        return None


def needs_upkeep(p, m, st):
    """The reason why the member m needs the upkeep (the rules of design 2.8), or '' when it needs nothing.
    p = the pool (pool.Pool), st = the answer of GET /api/pool of the board. A version or an epoch that is not a
    number: '' (a device that is not a TFN board gets no upkeep)."""
    state = st.get('state')
    if state == 'none' and id_hex(st.get('id')) == m['id']:
        return 'LEAVE on the board'
    if state == 'damaged':
        return 'the pool of the board is damaged'
    if state == 'ok' and str(st.get('pool') or '').lower() == p.pool_id.hex():
        v, ep = number(st.get('v')), number(st.get('epoch'))
        if v is None or ep is None:
            return ''
        if v < p.version:
            return 'old roster v%d (the newest v%d)' % (v, p.version)
        if ep < p.epoch:
            return 'old key epoch %d (the newest %d)' % (ep, p.epoch)
    return ''


def file_stamp(path):
    try:
        s = os.stat(path)
        return (s.st_mtime_ns, s.st_size)
    except OSError:
        return None


def stamp():
    return time.strftime('%Y-%m-%d %H:%M:%S')


class LineOut(io.TextIOBase):
    """The output of a job of serve (Serve.lines_of): each complete line goes to fn at once (a stop of the service in
    a long job keeps the lines before it)."""

    def __init__(self, fn):
        super().__init__()
        self.fn, self.part = fn, ''
        self.lock = threading.RLock()

    def writable(self):
        return True

    def write(self, s):
        with self.lock:
            self.part += s
            while '\n' in self.part:
                ln, self.part = self.part.split('\n', 1)
                if ln.strip():
                    self.fn(ln.rstrip())
        return len(s)

    def end(self):
        """The last part with no line end."""
        with self.lock:
            if self.part.strip():
                self.fn(self.part.rstrip())
            self.part = ''


# ---------------------------------------------------------------- the HTTP server of the local store
class StoreHandler(http.server.BaseHTTPRequestHandler):
    """GET of the 3 store routes (pm_store.route). Each other path or method = 404. No secret in an answer."""
    server_version = 'tfn-poolmgr'
    sys_version = ''
    protocol_version = 'HTTP/1.0'
    timeout = HTTP_TIMEOUT

    def log_message(self, *a):
        pass

    def answer(self, code, ctype, body):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(body)

    def not_found(self):
        self.answer(404, 'text/plain', b'not found\n')

    def do_GET(self):
        srv = self.server
        path, _, q = self.path.partition('?')
        if path not in ROUTES:
            return self.not_found()
        query = urllib.parse.parse_qs(q, keep_blank_values=True)
        try:
            code, ctype, body = pm_store.route(srv.data, path, query)
            srv.requests += 1
        except NotDone as e:
            srv.note('route', str(e))
            code, ctype, body = 503, 'text/plain', b'the local store is not ready\n'
        except Exception as e:  # a fault of one request must not stop the server
            srv.note('route', 'the store route %s: %s: %s' % (path, type(e).__name__, e))
            code, ctype, body = 500, 'text/plain', b'fault\n'
        self.answer(code, ctype, body if isinstance(body, bytes) else str(body).encode('utf-8'))

    def do_other(self):
        self.not_found()

    do_POST = do_PUT = do_DELETE = do_PATCH = do_HEAD = do_OPTIONS = do_other


class StoreServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    # a new bind of the same port after a stop (the mode "official", then "local"); on Windows the option lets another
    # program take the port, so it stays off there
    allow_reuse_address = os.name != 'nt'

    def __init__(self, addr, data, note):
        self.data, self.note, self.requests = data, note, 0
        http.server.HTTPServer.__init__(self, addr, StoreHandler)


# ---------------------------------------------------------------- serve
class Serve:
    """The parts of serve. Serve(data, port): start(), tick(now) (one step of the main loop), stop().

    host = the address of the HTTP server ('' = all, as "serve"; the tests use 127.0.0.1); listener = the class of
    the UDP listener (pm_net.Listener); udp_port = its port; out = the function for a line of output (default: the
    output of the process when serve starts, self.stream: a job changes sys.stdout for a time, and the lines of the
    other threads must not go into the job); clock = the time in seconds for tick()."""

    def __init__(self, data, port=PORT, host='', listener=None, udp_port=None, out=None, clock=time.monotonic):
        self.data, self.port, self.host = data, port, host
        self.listener_cls = listener
        self.udp_port = pm_net.UDP_PORT if udp_port is None else udp_port
        self.out_fn = out
        self.stream = sys.stdout
        self.clock = clock
        self.queue = collections.deque()
        self.stop_event = threading.Event()
        self.listener = None
        self.listen_keys = None
        self.httpd = None
        self.bound = None
        self.stamps = {}
        self.seen = {}           # MAC -> the time of the last beacon
        self.addr = {}           # MAC -> (ip, app, id, the time of the write)
        self.next_probe = {}     # MAC -> the time of the next GET /api/pool with no new beacon
        self.t_fix = {}          # MAC -> the time of the last upkeep
        self.joins = {}          # MAC -> the time of the last JOIN line
        self.noted = set()
        self.lan_sent = None
        self.t_round = None
        self.t_prev_round = -1e18
        self.t_lan = self.t_sync = self.t_update = self.t_status = self.t_keys = 0.0
        self.counts = collections.Counter()

    # ---- the output
    def out(self, text):
        line = '%s %s' % (stamp(), text)
        if self.out_fn:
            self.out_fn(line)
        elif self.stream is not None:
            try:
                self.stream.write(line + '\n')
                self.stream.flush()
            except (OSError, ValueError):   # (a closed output: the line still goes into the file)
                pass
        self.file_line(line)

    def file_line(self, line):
        """The line into log/serve.log; at SERVE_LOG_MAX bytes the file becomes serve.log.1. A fault of the file
        does not stop serve."""
        path = os.path.join(os.path.dirname(self.data.log_file), SERVE_LOG)
        try:
            if os.path.getsize(path) >= SERVE_LOG_MAX:
                os.replace(path, path + '.1')
        except OSError:
            pass
        try:
            os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
            with open(path, 'ab') as h:
                h.write((line + '\n').encode('utf-8', 'replace'))
        except OSError:
            pass

    def lines_of(self, fn, *a):
        """fn(*a) with each line that it prints going to out() at once (the service of Windows has no output): its
        result. out() writes to self.stream, not to sys.stdout: so a line of another thread in this time (note() of
        an HTTP thread) goes out one time only."""
        w = LineOut(self.out)
        try:
            with contextlib.redirect_stdout(w):
                return fn(*a)
        finally:
            w.end()

    def stopped_by(self, e):
        """The line "STOP: <fault>" of a fault that stops serve, into log/serve.log and log/poolmgr.log only:
        poolmgr.main prints the STOP line to the output (the service of Windows runs pythonw, which has no output). A
        data folder with no pool gets no file (a command on a folder with no pool changes nothing there)."""
        if not self.data.has_pool():
            return
        known = (InputError, OSError, pool.PoolError, pool_tool.ToolError)
        text = str(e) if isinstance(e, known) else '%s: %s' % (type(e).__name__, e)
        self.file_line('%s STOP: %s' % (stamp(), text))
        self.log('serve stop: %s' % text)

    def note(self, key, text):
        """One line for a fault that can come again (NOT DONE, a missing part): only the first time."""
        if key in self.noted:
            return
        self.noted.add(key)
        self.out(text)

    def guard(self, name, fn, *a):
        """Run one job; a fault prints one line, serve goes on. True when the job ended with no fault."""
        try:
            fn(*a)
            return True
        except NotDone as e:
            self.note('nd:' + name, '%s (serve goes on without it)' % e)
        except (Failed, InputError, pool_tool.Refused, pool_tool.ToolError, pool.PoolError, OSError) as e:
            self.out('FAILED %s: %s' % (name, e))
            self.log('serve %s failed: %s' % (name, e))
        except Exception as e:  # serve must not stop for the fault of one job
            self.out('FAULT %s: %s: %s' % (name, type(e).__name__, e))
            self.log('serve %s fault: %s' % (name, type(e).__name__))
        return False

    def log(self, text):
        try:
            self.data.log(text)
        except OSError:
            pass

    # ---- the files
    def files(self):
        d = self.data
        return {'cfg': d.cfg_file, 'pool': os.path.join(d.pool_dir, 'pool.json'),
                'secrets': os.path.join(d.pool_dir, 'secrets.json')}

    def take_stamps(self):
        self.stamps = {k: file_stamp(v) for k, v in self.files().items()}

    def reload(self):
        """Read config.json and the pool again when a file changed, then the listener (a new key) and the HTTP
        server (a new store mode). True when they changed."""
        now = {k: file_stamp(v) for k, v in self.files().items()}
        if now == self.stamps:
            return False
        try:
            self.data.load()
        except (OSError, ValueError, KeyError, InputError) as e:
            self.note('load:%r' % (now,), 'FAILED to read the data folder again: %s (the old values stay)' % e)
            return False
        self.stamps = now
        p = self.data.pool
        if p is not None:
            self.out('READ the pool again: %d members, roster v%d, key epoch %d, store %s' %
                     (len(self.members()), p.version, p.epoch, self.data.cfg.get('store')))
        self.guard('listener', self.listen)
        self.guard('local store', self.store_server)
        return True

    def members(self):
        p = self.data.pool
        if p is None:
            return []
        return [m for m in p.members if m.get('role') != pool.ROLE_MANAGER]

    def member_by_mac(self, mac):
        for m in self.members():
            if m['mac'].upper() == mac:
                return m
        return None

    # ---- the listener
    def keys(self):
        """The beacon keys of the pool (a list): the current key, and the previous key in its hour."""
        if self.data.pool is None:
            return []
        return pm_net.data_keys(self.data)

    def listen(self):
        """Start the listener with the keys of the pool now. A listener that runs gets a new key list with
        set_keys()."""
        ks = self.keys()
        sig = tuple((k.epoch, k.hint, k.beacon) for k in ks)
        if self.listener is not None:
            if sig != self.listen_keys:
                self.listener.set_keys(ks)
                self.listen_keys = sig
            return
        cls = self.listener_cls or pm_net.Listener
        lst = cls(self.on_join, self.on_beacon, port=self.udp_port, keys=ks)
        try:
            lst.start()
        except NotDone as e:
            self.note('nd:listener', '%s (no beacons: the upkeep reads the known addresses only)' % e)
            return
        except OSError as e:
            raise InputError('the UDP port %d does not open (%s): does another manager run on this computer?' %
                             (self.udp_port, e))
        self.listener, self.listen_keys = lst, sig

    def stop_listener(self):
        if self.listener is not None:
            try:
                self.listener.stop()
            except (NotDone, OSError):
                pass
        self.listener = self.listen_keys = None

    def on_join(self, info):
        self.queue.append(('join', dict(info)))

    def on_beacon(self, info):
        self.queue.append(('beacon', dict(info)))

    def drain(self, now):
        """The packets of the listener thread."""
        while self.queue:
            kind, info = self.queue.popleft()
            if kind == 'beacon':
                self.beacon(now, info)
            else:
                self.join(now, info)

    def beacon(self, now, info):
        mac = mac_hex(info.get('mac'))
        m = self.member_by_mac(mac)
        ip = str(info.get('ip') or '')
        if ip and ':' not in ip:
            ip = pm_net.with_port(ip, self.address(mac))  # (the rule of pm_net: a known "host:port" keeps its port)
        if m is None or not ip:
            self.counts['other beacons'] += 1
            return
        self.counts['beacons'] += 1
        self.seen[mac] = now
        app, dev = str(info.get('app') or ''), id_hex(info.get('id'))
        old = self.addr.get(mac)
        if old is None or old[:3] != (ip, app, dev) or now - old[3] >= ADDR_WRITE_S:
            if old is not None and old[0] != ip:
                self.out('ADDRESS %s: %s (was %s)' % (m['name'], ip, old[0]))
                self.t_lan = min(self.t_lan, now)
            self.data.set_addr(mac, ip, app, dev)
            self.addr[mac] = (ip, app, dev, now)

    def join(self, now, info):
        mac = mac_hex(info.get('mac'))
        if now - self.joins.get(mac, -1e18) < JOIN_NOTE_S:
            return
        self.joins[mac] = now
        ip = info.get('ip') or ''
        self.out('JOIN SCREEN OPEN: %s (MAC ...%s) %s: to adopt it: poolmgr adopt %s' %
                 (info.get('name') or '?', mac[-4:], ip, ip))

    # ---- the upkeep
    def address(self, mac):
        """The address of a member: its last beacon, addrs.json, else the address of the pool folder (the JOIN)."""
        a = self.addr.get(mac)
        if a:
            return a[0]
        ip = (self.data.addrs().get(mac) or {}).get('ip')
        if ip:
            return ip
        m = self.member_by_mac(mac)
        return (m or {}).get('ip') or ''

    def upkeep_round(self, now):
        """The probe and the upkeep of each member. A fault of one member (also a device that is not a TFN board at
        its address) gives one line, and the next try of that member is after FAIL_S: serve goes on."""
        if self.data.pool is None:
            return
        prev, self.t_prev_round = self.t_prev_round, now
        for m in self.members():
            mac = m['mac'].upper()
            try:
                self.upkeep_member(m, mac, now, prev)
            except Exception as e:  # one member must not stop serve
                self.next_probe[mac] = now + FAIL_S
                self.note('probe:%s:%s' % (mac, type(e).__name__), 'FAULT probe %s: %s: %s' % (
                    m['name'], type(e).__name__, e))
                self.log('serve probe %s fault: %s' % (m['name'], type(e).__name__))
            if self.data.pool is None:
                return

    def upkeep_member(self, m, mac, now, prev):
        """The probe of one member (GET /api/pool), then its upkeep when it needs one."""
        ip = self.address(mac)
        if not ip:
            return
        fresh = self.seen.get(mac, -1e18) > prev
        if not fresh and now < self.next_probe.get(mac, -1e18):
            return
        try:
            st = json.loads(pm_board.http_get('http://%s/api/pool' % ip, timeout=PROBE_TIMEOUT).decode('utf-8'))
        except (Failed, ValueError):
            self.next_probe[mac] = now + FAIL_S
            self.counts['no answer'] += 1
            return
        self.next_probe[mac] = now + QUIET_S
        if not isinstance(st, dict):
            return
        why = needs_upkeep(self.data.pool, m, st) or self.friends_why(mac)
        if not why or now - self.t_fix.get(mac, -1e18) < PUSH_GAP:
            return
        self.t_fix[mac] = now
        st.setdefault('ip', ip)
        if self.fix(m, mac, st, why) == 'busy':
            self.t_fix.pop(mac, None)
        self.reload()

    def friends_why(self, mac):
        """The reason of an upkeep for a friend entry that waits for its removal on the member, or ''."""
        try:
            return 'a friend entry waits for its removal' if pm_pool.out_waits(self.data, mac) else ''
        except (OSError, ValueError, InputError) as e:
            self.note('out:' + mac, 'FAILED friends: %s' % e)
            return ''

    def fix(self, m, mac, st, why):
        """pm_pool.upkeep of one member under the change lock, with its lines: 'ok', 'busy' (the lock of another
        process: the next round tries again) or 'fault'."""
        self.out('UPKEEP %s: %s' % (m['name'], why))
        self.log('serve upkeep %s: %s' % (m['name'], why))
        res = ['ok']

        def job():
            entered = False
            try:
                with self.data.change(wait=LOCK_WAIT):
                    entered = True
                    lines = self.lines_of(pm_pool.upkeep, self.data, mac, st)
            except InputError:
                if entered:
                    raise
                res[0] = 'busy'
                self.out('BUSY: another poolmgr changes the pool; the upkeep of %s waits' % m['name'])
                return
            for ln in lines or ():
                self.out('  %s' % ln)
        if not self.guard('upkeep %s' % m['name'], job):
            return 'fault'
        return res[0]

    # ---- the local store
    def local(self):
        return self.data.cfg.get('store') == 'local'

    def store_server(self):
        """The HTTP server runs only in the mode "local"."""
        if self.local() and self.httpd is None:
            port = self.bound if self.bound is not None else self.port
            try:
                self.httpd = StoreServer((self.host, port), self.data, self.note)
            except OSError as e:
                raise InputError('the TCP port %d does not open (%s): is "poolmgr serve" running already?' %
                                 (port, e))
            self.bound = self.httpd.server_address[1]
            threading.Thread(target=self.httpd.serve_forever, kwargs={'poll_interval': 0.5}, daemon=True).start()
            self.out('LOCAL STORE: TCP %d (GET %s)' % (self.bound, ', '.join(ROUTES)))
        elif not self.local() and self.httpd is not None:
            self.stop_store()
            self.out('LOCAL STORE: stopped (the store mode is %s)' % self.data.cfg.get('store'))

    def stop_store(self):
        if self.httpd is not None:
            self.httpd.shutdown()
            self.httpd.server_close()
            self.httpd = None

    def lan_check(self, now):
        """The LAN address of this computer toward the boards; a new one = op=mgr again (design decision 29)."""
        self.t_lan = now + LAN_S
        own = None
        for m in self.members():
            ip = self.address(m['mac'].upper())
            if not ip:
                continue
            try:
                own = pm_net.lan_ip_toward(ip)
                break
            except OSError:
                continue
        if not own or self.bound is None:
            return
        url = 'http://%s:%d' % (own, self.bound)
        if url == self.lan_sent:
            return
        self.out('LAN ADDRESS: %s (was %s): the boards get it (op=mgr)' % (url, self.lan_sent or 'none'))
        self.t_lan = now + LAN_RETRY

        def job():
            with self.data.change(wait=LOCK_WAIT):
                self.data.cfg['lan_url'] = url
                self.data.cfg['port'] = self.bound
                self.data.save_cfg()
                self.take_stamps()
                rows = self.lines_of(pm_store.set_mode, self.data, 'local')
            for r in rows or ():
                self.out('  %s' % (r if not isinstance(r, dict) else '  '.join('%s=%s' % (k, r[k]) for k in r)))
            self.log('serve: the LAN address %s sent to the members' % url)
        if self.guard('store local', job):
            self.lan_sent = url
            self.t_lan = now + LAN_S
            self.take_stamps()

    def sync(self, now):
        self.t_sync = now + SYNC_S

        def job():
            n = pm_store.sync(self.data)
            self.out('STORE SYNC: %s packages in the mirror' % n)
        self.guard('store sync', job)

    def auto_update(self, now):
        self.t_update = now + UPDATE_S

        def job():
            with self.data.change(wait=LOCK_WAIT):
                rows = self.lines_of(pm_ota.update, self.data, [], None, False)
            for r in rows or ():
                self.out('AUTO UPDATE: %s' % (r if not isinstance(r, dict) else
                                              '  '.join('%s=%s' % (k, r[k]) for k in r)))
        self.guard('auto update', job)

    # ---- the loop
    def start(self):
        """Load the data folder, start the listener and (mode "local") the HTTP server."""
        global ACTIVE
        self.data.pool = None
        self.data.need_pool()
        self.take_stamps()
        self.lan_sent = self.data.cfg.get('lan_url') or None
        now = self.clock()
        self.t_round = now
        self.t_lan = now
        self.t_sync = now + SYNC_FIRST
        self.t_update = now + UPDATE_FIRST
        self.t_status = now + STATUS_S
        self.t_keys = now + KEYS_S
        self.listen()
        try:
            self.store_server()
        except Exception:
            self.stop_listener()
            raise
        p = self.data.pool
        self.out('SERVE: pool %s, %d members, store %s, UDP %d (listen only)%s, auto update %s' %
                 (p.name, len(self.members()), self.data.cfg.get('store'), self.udp_port,
                  ', TCP %d' % self.bound if self.httpd else '', 'on' if self.data.cfg.get('auto_update') else 'off'))
        self.log('serve start')
        ACTIVE = self
        return self

    def tick(self, now):
        """One step of the main loop."""
        self.reload()
        if now >= self.t_keys:
            self.t_keys = now + KEYS_S
            self.guard('listener', self.listen)
        self.drain(now)
        if now >= self.t_round:
            self.t_round = now + UPKEEP_S
            self.upkeep_round(now)
        if self.local() and self.httpd is not None and now >= self.t_lan:
            self.lan_check(now)
        if self.local() and now >= self.t_sync:
            self.sync(now)
        if self.data.cfg.get('auto_update') and now >= self.t_update:
            self.auto_update(now)
        if now >= self.t_status:
            self.t_status = now + STATUS_S
            seen = sum(1 for t in self.seen.values() if now - t < STATUS_S)
            self.out('STATUS: %d members, %d with a beacon in the last %d min, %d store requests' %
                     (len(self.members()), seen, STATUS_S // 60, self.httpd.requests if self.httpd else 0))

    def stop(self):
        global ACTIVE
        self.stop_event.set()
        self.stop_listener()
        self.stop_store()
        if ACTIVE is self:
            ACTIVE = None

    def loop(self):
        """tick() each second until stop() (or Ctrl+C, or SIGTERM)."""
        while not self.stop_event.is_set():
            self.tick(self.clock())
            self.stop_event.wait(TICK)


def run(data, port=PORT):
    """Run until a stop (Ctrl+C, SIGTERM of the service): the exit code. A fault that stops serve (also at the start:
    a port in use) goes into the files first (Serve.stopped_by), then up to poolmgr.main."""
    s = Serve(data, port)
    try:
        s.start()
    except Exception as e:
        s.stopped_by(e)
        raise
    old = None
    if threading.current_thread() is threading.main_thread() and hasattr(signal, 'SIGTERM'):
        old = signal.signal(signal.SIGTERM, lambda *a: s.stop_event.set())
    try:
        s.loop()
    except KeyboardInterrupt:
        pass
    except Exception as e:
        s.stopped_by(e)
        raise
    finally:
        s.stop()
        if old is not None:
            signal.signal(signal.SIGTERM, old)
        s.out('SERVE STOP')
        s.log('serve stop')
    return 0
