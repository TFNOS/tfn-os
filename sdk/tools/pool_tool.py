#!/usr/bin/env python3
"""The pool tool: a pool with no manager (the test pool of the P4 gate), the work with the admin keys, the recovery.

It speaks /api/pool of the boards (design 2.6-2.7 and the "api" notes of tests/unit/vectors/pool_v1.json). The pool
data is in a folder (--dir, default manager/pool next to this file; git ignores it): pool.json, secrets.json (mode
0600), roster.bin, webpass.json (mode 0600). The admin key is a PEM file (--key, or the variable TFN_POOL_KEY); it never
goes into the data folder, a log or a web request.

Commands:
  keygen <out.pem>                       make a P-256 admin key (mode 0600; an existing file stays); prints the public
                                         key and its key ID
  create <name> [--recovery <pem|hex>] [--legacy-beacon] [--manager] [--lan URL] [--vpn URL]
                                         make a new pool in --dir (a new pool ID and pool key); --manager adds the
                                         manager as a member (role MANAGER) and signs the first roster. A board in
                                         a pool takes the request header of this tool only from a MANAGER member:
                                         with no --manager, create gives a WARNING (the requests get 401)
  adopt <ip> [--console PORT] [--token TOKEN] [--password [PW]] [--name NAME] [--role member|child]
                                         JOIN of a board: op=hello, op=reveal, the 6 digits, op=set. --console: the USB
                                         console of the board opens its JOIN screen, the tool reads the 6 digits there
                                         and sends "pool allow <digits>". --token: the token JOIN of a board of an old network (no
                                         digits). With neither, the tool asks for the digits on the screen of the board
                                         (a person taps ALLOW). --password: the web password of a board that has one
                                         (SETUP > WEB PASSWORD); give it with no value: the tool asks for it (a value
                                         goes into the history of the shell and into the process list). When the
                                         board is a member but another member does not take the new roster, exit 1
  push [<member> ...]                    send the newest roster and the pool key (op=key) to the members (all when
                                         no member is given)
  remove <member>                        a new roster without the member and a new pool key: the member leaves the
                                         pool, the other members get the new key
  webpass [<member> ...] [--console PORT]
                                         a random web password on each member (op=webpass with the request header,
                                         or "webpass set" on the console); kept in webpass.json
  show [--live]                          the pool and the members; --live also reads GET /api/pool of each member
  check-backup <pem> [--pub <pem|hex>]   sign a test text with the private key and verify it with the public key (the
                                         check of a restored copy); with no --pub, the key must be an admin of the pool
A member: its name, device ID, MAC or address. An address can have a port ("host:8080").
Exit 0 = ok, 1 = a refusal of a board or a check that failed, 2 = a fault of the input or the tool.
"""
import argparse
import base64
import getpass
import http.client
import json
import os
import re
import secrets
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fw_sign  # noqa: E402
import pool  # noqa: E402
import pool_crypto as pc  # noqa: E402

DEFAULT_DIR = os.path.join(HERE, 'pool')
TIMEOUT = 10
ALLOW_WAIT = 120
SAS_RE = re.compile(r'\bSAS\b\D*(\d{3})\s?(\d{3})', re.I)


class ToolError(Exception):
    pass


class Refused(Exception):
    """A board refused a request (an HTTP error with its answer)."""

    def __init__(self, code, ans):
        super().__init__('the board answered %d: %s' % (code, ans.get('error', ans)))
        self.code, self.ans = code, ans


class NotBoard(ToolError):
    """A device answers, but not as the TFN board that the tool wants: an answer that is not a JSON object, no board
    nonce (for example the web page of a router or a printer at an old address of a board), or another device ID."""


# ---------------------------------------------------------------- the HTTP calls
def error_answer(e):
    """The JSON object of an HTTP error of a board, else {"error": "HTTP <code>"}."""
    try:
        ans = json.loads(e.read().decode('utf-8', 'replace'))
    except (ValueError, OSError, http.client.HTTPException):
        ans = None
    return ans if isinstance(ans, dict) else {'error': 'HTTP %d' % e.code}


class Board:
    """The HTTP calls to one board. The request header (X-TFN-Auth) needs the wk of the board and its nonce bn: the
    tool reads bn again with GET /api/pool before the first signed request and after a refusal (a new start of the
    board makes a new bn). dev_id (hex): the device ID that the board at ip must give in GET /api/pool before the
    first signed request (another board at an old address of a member gets no signed request and no password)."""

    def __init__(self, ip, p=None, wk=None, password=None, dev_id=None):
        self.ip, self.p, self.wk, self.password = ip, p, wk, password
        self.dev_id = dev_id.lower() if dev_id else None
        self.bn = None
        self.ctr = COUNTER

    def call(self, method, path, body=None, signed=False):
        """The answer of the board (a dict). Refused for an HTTP error, NotBoard for an answer that a TFN board does
        not give, ToolError when nothing answers (also for a bad address or an answer that is not HTTP)."""
        url = 'http://%s%s' % (self.ip, path)
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        if data is not None:
            req.add_header('Content-Type', 'application/json')
        if self.password:
            tok = base64.b64encode(('admin:%s' % self.password).encode()).decode()
            req.add_header('Authorization', 'Basic ' + tok)
        if signed:
            if self.bn is None:
                self.bn = self.nonce()
            req.add_header(pool.HDR, pool.web_header(self.wk, self.bn, self.p.mid(), self.ctr.next(), method, path))
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                text = r.read().decode('utf-8', 'replace')
        except urllib.error.HTTPError as e:
            raise Refused(e.code, error_answer(e))
        except (urllib.error.URLError, OSError, http.client.HTTPException, ValueError) as e:
            raise ToolError('no answer from %s: %s' % (self.ip, e))
        try:
            ans = json.loads(text or '{}')
        except ValueError:
            ans = None
        if not isinstance(ans, dict):
            raise NotBoard('%s answers %s %s with no JSON object: it is not a TFN board' % (self.ip, method, path))
        return ans

    def nonce(self):
        """The board nonce bn of GET /api/pool (the request header needs it). The request has no web password (GET
        /api/pool needs none): a device at an old address of the board does not get the password before the check of
        its answer. NotBoard when the answer has no nonce, or (with dev_id) another device ID."""
        st = Board(self.ip).call('GET', '/api/pool')
        try:
            bn = bytes.fromhex(st['bn'])
        except (KeyError, TypeError, ValueError):
            bn = b''
        if not bn:
            raise NotBoard('%s gives no board nonce in GET /api/pool: it is not a TFN board with the pool code' %
                           self.ip)
        got = str(st.get('id') or '').lower()
        if self.dev_id and got != self.dev_id:
            raise NotBoard('another board answers at %s (device ID %s, not %s): no signed request goes to it' %
                           (self.ip, got[:16] or 'none', self.dev_id))
        return bn

    def signed_post(self, op, body):
        """A POST /api/pool?op=<op> with the request header; one more try with a new bn after a 401."""
        path = '/api/pool?op=%s' % op
        try:
            return self.call('POST', path, body, signed=True)
        except Refused as e:
            if e.code != 401:
                raise
            self.bn = None
            return self.call('POST', path, body, signed=True)


COUNTER = pool.Counter()


# ---------------------------------------------------------------- the USB console of a board
class SerialConsole:
    """The USB console of a board (the pyserial package). The tests give a fake console in its place. The USB serial
    of an ESP32-S3 restarts the chip when DTR is low and RTS is high, so the lines never go through that state, and
    they are both low at the end (no restart at the open):
    - Windows: pyserial sets the two levels of the settings in one step at the open: both low.
    - Linux and macOS: the system sets both high at the open of the port, and pyserial then sets DTR before RTS. So
      the port opens with both high, then RTS goes low first and DTR after it.
    posix: the second way (None = the way of this computer)."""

    def __init__(self, port, posix=None):
        try:
            import serial
        except ImportError:
            raise ToolError('--console needs the package pyserial')
        if posix is None:
            posix = os.name == 'posix'
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.write_timeout = port, 115200, 0.2, 2
        if not posix:
            s.dtr = False
            s.rts = False
        try:
            s.open()
            if posix:
                s.rts = False
                s.dtr = False
        except (serial.SerialException, OSError) as e:
            raise ToolError('the console %s does not open: %s' % (port, e))
        self.s = s
        self.buf = b''

    def write_line(self, text):
        self.s.write(text.encode('utf-8') + b'\r\n')

    def read_line(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if b'\n' in self.buf:
                ln, self.buf = self.buf.split(b'\n', 1)
                return ln.decode('utf-8', 'replace').strip()
            self.buf += self.s.read(256)
        return None

    def close(self):
        self.s.close()


def open_console(port):
    return SerialConsole(port)


def console_sas(con, timeout=20):
    """The 6 digits that the board shows, read from its console (a line with "SAS" and 3 + 3 digits). The tool sends
    "pool" one time to make the board print its state again."""
    end = time.time() + timeout
    asked = False
    while time.time() < end:
        ln = con.read_line(min(2.0, max(0.1, end - time.time())))
        if ln is None:
            if not asked:
                con.write_line('pool')
                asked = True
            continue
        m = SAS_RE.search(ln)
        if m:
            return int(m.group(1) + m.group(2))
    raise ToolError('the console of the board showed no SAS digits in %d s' % timeout)


# ---------------------------------------------------------------- the pool folder and the keys
def load_pool(d):
    if not os.path.exists(os.path.join(d, 'pool.json')):
        raise ToolError('no pool in %s (use "create")' % d)
    return pool.Pool.load(d)


def admin_key(a, p=None):
    path = a.key or os.environ.get('TFN_POOL_KEY')
    if not path:
        raise ToolError('give the admin key (--key <pem> or TFN_POOL_KEY)')
    if not os.path.exists(path):
        raise ToolError('no admin key file %s' % path)
    if p is not None and pc.p256_pub(path) not in p.admins:
        raise ToolError('the key %s is not an admin key of the pool %s' % (path, p.name))
    return path


def load_webpass(d):
    """The web passwords of webpass.json in the folder d (MAC -> password; {} with no file). ToolError for a damaged
    file (the program stops with its name, and it does not write over it)."""
    path = os.path.join(d, 'webpass.json')
    try:
        with open(path, 'rb') as h:
            wp = json.loads(h.read().decode('utf-8'))
    except FileNotFoundError:
        return {}
    except ValueError as e:
        wp, why = None, str(e)
    else:
        why = 'no JSON object'
    if not isinstance(wp, dict):
        raise ToolError('%s is damaged (%s): repair it (a JSON object, MAC -> web password)' % (path, why))
    return wp


def board_for(p, m, d):
    wp = load_webpass(d).get(m['mac'])
    return Board(m['ip'], p, p.wk(m['id']), wp)


def member_arg(p, what):
    m = p.find(what)
    if m is None:
        raise ToolError('no member %s in the pool %s' % (what, p.name))
    if m['role'] == pool.ROLE_MANAGER and m['mac'] == pool.MANAGER_MAC.hex():
        raise ToolError('%s is the manager, not a board' % what)
    return m


def boards(p):
    return [m for m in p.members if not (m['role'] == pool.ROLE_MANAGER and m['mac'] == pool.MANAGER_MAC.hex())]


# ---------------------------------------------------------------- the commands
def cmd_keygen(a):
    pt = pc.p256_keygen(a.out)
    print('admin key %s: key ID %s' % (a.out, pc.key_id(pt).hex()))
    print('public %s' % pt.hex())
    return 0


def cmd_create(a):
    if os.path.exists(os.path.join(a.dir, 'pool.json')):
        raise ToolError('%s has a pool already: a pool is never overwritten' % a.dir)
    key = admin_key(a)
    rec = fw_sign.parse_pub(a.recovery) if a.recovery else None
    p = pool.Pool.create(a.name, pc.p256_pub(key), rec, pool.F_LEGACY_BEACON if a.legacy_beacon else 0)
    if a.lan or a.vpn:
        p.lan, p.vpn = a.lan or '', a.vpn or ''
    if a.manager:
        p.add_manager()
        p.sign(key)
    p.save(a.dir)
    print('pool %s created: ID %s, %d admin key(s), epoch %d, roster version %d' % (p.name, p.pool_id.hex(),
                                                                                   len(p.admins), p.epoch, p.version))
    if not a.manager:
        print('WARNING: the pool has no MANAGER member (no --manager). A board in a pool takes the request header of '
              'this tool only from a MANAGER member: after the first adopt, push, remove, webpass and the roster of '
              'the next adopt get 401 "auth". Make the pool again with --manager.')
    return 0


def send_roster(p, m, d, with_key=True):
    """The newest roster (and the key, op=key) to one member. Returns the answer of the board."""
    b = board_for(p, m, d)
    if with_key:
        body = pool.key_body(bytes.fromhex(m['bx']), bytes.fromhex(m['id']), p.roster_file, p.epoch, p.pool_k,
                             p.wk(m['id']))
        return b.signed_post('key', body)
    return b.signed_post('set', {'roster': p.roster_file.hex()})


def push_all(p, d, members, with_key=True):
    bad = 0
    for m in members:
        try:
            ans = send_roster(p, m, d, with_key)
            print('  %s (%s): %s' % (m['name'], m['ip'], ans.get('state', 'ok')))
        except (Refused, ToolError) as e:
            bad += 1
            print('  %s (%s): FAILED: %s' % (m['name'], m['ip'], e))
    return bad


def open_id(ip, password):
    """GET /api/id of the board (the first request of the JOIN): (the Board, the answer). The web gate of a board with
    a web password answers 401: then the password of --password ('' = a question)."""
    b = Board(ip)
    try:
        return b, b.call('GET', '/api/id')
    except Refused as e:
        if e.code != 401:
            raise
    if password is None:
        raise ToolError('the board %s has a web password (SETUP > WEB PASSWORD on the board): give adopt %s '
                        '--password' % (ip, ip))
    b.password = password or getpass.getpass('The web password of the board %s (SETUP > WEB PASSWORD): ' % ip)
    try:
        return b, b.call('GET', '/api/id')
    except Refused as e:
        if e.code != 401:
            raise
    raise ToolError('%s: the web password is not right (SETUP > WEB PASSWORD on the board)' % ip)


def cmd_adopt(a):
    p = load_pool(a.dir)
    key = admin_key(a, p)
    b, ident = open_id(a.ip, a.password)
    pin = bytes.fromhex(ident.get('pk', ''))
    if len(pin) != 65 or pc.device_id(pin).hex() != ident_id(ident):
        raise ToolError('/api/id of %s gives no good device key' % a.ip)
    con = open_console(a.console) if a.console else None
    try:
        if con:
            con.write_line('pool join')
        j = pool.Join(key)
        f = j.take_hello(b.call('POST', '/api/pool?op=hello', j.hello_body(a.token)), pin)
        print('board %s: device ID %s, its JOIN signature and box key verify' % (a.ip, f['id'].hex()))
        b.call('POST', '/api/pool?op=reveal', j.reveal_body())
        if not a.token:
            want = j.sas()
            print('the 6 digits: %s' % pool.sas_text(want))
            if con:
                got = console_sas(con)
            elif a.digits:
                got = int(re.sub(r'\D', '', a.digits) or -1)
            else:
                got = int(re.sub(r'\D', '', input('the 6 digits that the board shows: ')) or -1)
            if got != want:
                raise ToolError('the board shows %s, the tool %s: NOT the same (a device between them?): the '
                                'JOIN stops' % (pool.sas_text(got) if got >= 0 else '?', pool.sas_text(want)))
            if con:
                con.write_line('pool allow %06d' % want)
            else:
                print('tap ALLOW on the board')
    finally:
        if con:
            con.close()
    did = f['id'].hex()
    name = a.name or 'TFN-%s' % did[:4].upper()
    p.upsert({'id': did, 'pk': f['pk'].hex(), 'mac': ident_mac(ident, b), 'role': pool.ROLES.index(a.role),
              'flags': 0, 'name': name, 'bx': f['bx'].hex(), 'ip': a.ip})
    roster = p.sign(key)
    body = j.set_body(roster, p.epoch, p.pool_k, p.wk(did))
    end = time.time() + ALLOW_WAIT
    while True:
        try:
            ans = b.call('POST', '/api/pool?op=set', body)
            break
        except Refused as e:
            if e.ans.get('error') != 'wait' or time.time() > end:
                raise
            time.sleep(0.5)
    p.save(a.dir)
    st = Board(a.ip, p, p.wk(did)).call('GET', '/api/pool')
    if st.get('state') != 'ok' or st.get('pool') != p.pool_id.hex() or st.get('v') != p.version:
        raise ToolError('the board answered %s, but its state is %s (pool %s, version %s)' % (
            ans, st.get('state'), st.get('pool'), st.get('v')))
    print('%s is a member of the pool %s (roster version %d)' % (name, p.name, p.version))
    others = [m for m in boards(p) if m['id'] != did]
    if others:
        print('the new roster to the other members:')
        # (the new member is in the pool: a member that is off or refuses gets the roster with "push" later)
        if push_all(p, a.dir, others, with_key=False):
            # (a script must see it: exit 1, although the new board is a member)
            print('NOT ALL members have the new roster: "push" sends it again')
            return 1
    return 0


def ident_id(ident):
    """The device ID of an answer of /api/id as 16 hex digits in small letters (the board gives XXXX-XXXX-XXXX-XXXX)."""
    return str(ident.get('id', '')).replace('-', '').lower()


def ident_mac(ident, b):
    """The MAC of the board: /api/id has none, /api/pool can have "mac", else the hint of /api/boards."""
    for path, field in (('/api/pool', 'mac'), ('/api/boards', 'mac')):
        try:
            v = b.call('GET', path).get(field, '')
        except (Refused, ToolError):
            continue
        v = re.sub(r'[^0-9a-fA-F]', '', v or '').lower()
        if len(v) == 12:
            return v
    raise ToolError('the board gives no MAC (GET /api/pool "mac")')


def cmd_push(a):
    p = load_pool(a.dir)
    key = admin_key(a, p)
    if not p.roster_file or pool.roster_parse(p.roster_file).version != p.version:
        p.sign(key)
        p.save(a.dir)
    ms = [member_arg(p, w) for w in a.members] if a.members else boards(p)
    print('roster version %d, epoch %d to %d member(s):' % (p.version, p.epoch, len(ms)))
    return 1 if push_all(p, a.dir, ms) else 0


def cmd_remove(a):
    p = load_pool(a.dir)
    key = admin_key(a, p)
    m = member_arg(p, a.member)
    gone = Board(m['ip'], p, p.wk(m['id']), load_webpass(a.dir).get(m['mac']))
    p.remove(m['id'])
    p.new_key()
    roster = p.sign(key)
    bad = 0
    try:
        ans = gone.signed_post('set', {'roster': roster.hex()})
        print('%s left the pool: %s' % (m['name'], ans.get('state', 'ok')))
    except (Refused, ToolError) as e:
        bad += 1
        print('%s did not answer (%s): it loses the link with the new key' % (m['name'], e))
    p.save(a.dir)
    print('the new key (epoch %d) to the other members:' % p.epoch)
    bad += push_all(p, a.dir, boards(p))
    return 1 if bad else 0


def cmd_webpass(a):
    p = load_pool(a.dir)
    ms = [member_arg(p, w) for w in a.members] if a.members else boards(p)
    wp = load_webpass(a.dir)
    bad = 0
    for m in ms:
        pw = secrets.token_urlsafe(12)
        try:
            if a.console:
                con = open_console(a.console)
                try:
                    con.write_line('webpass set %s' % pw)
                finally:
                    con.close()
            else:
                board_for(p, m, a.dir).signed_post('webpass', {'pw': pw})
            wp[m['mac']] = pw
            print('  %s: web password set' % m['name'])
        except (Refused, ToolError) as e:
            bad += 1
            print('  %s: FAILED: %s' % (m['name'], e))
    pool.write_file(os.path.join(a.dir, 'webpass.json'), (json.dumps(wp, indent=1) + '\n').encode(), 0o600)
    return 1 if bad else 0


def cmd_show(a):
    p = load_pool(a.dir)
    print('pool %s  ID %s  roster version %d  epoch %d  flags %d' % (p.name, p.pool_id.hex(), p.version, p.epoch,
                                                                     p.flags))
    for i, k in enumerate(p.admins):
        print('  admin key %s: %s' % ('AR'[i] if i < 2 else i, pc.key_id(k).hex()))
    bad = 0
    for m in p.members:
        line = '  %-16s %s  %s  %-7s %s' % (m['name'], m['id'], m['mac'], pool.ROLES[m['role']], m.get('ip', ''))
        if a.live and m in boards(p):
            try:
                st = board_for(p, m, a.dir).call('GET', '/api/pool')
                same = st.get('pool') == p.pool_id.hex() and st.get('v') == p.version
                line += '  [%s, version %s, epoch %s%s]' % (st.get('state'), st.get('v'), st.get('epoch'),
                                                            '' if same else ', NOT THE SAME')
                bad += 0 if same and st.get('state') == 'ok' else 1
            except (Refused, ToolError) as e:
                line += '  [no answer: %s]' % e
                bad += 1
        print(line)
    return 1 if bad else 0


def cmd_check_backup(a):
    if a.pub:
        pub = fw_sign.parse_pub(a.pub)
    else:
        p = load_pool(a.dir)
        pub = pc.p256_pub(a.pem)
        if pub not in p.admins:
            print('KEY CHECK FAILED: %s is not an admin key of the pool %s' % (a.pem, p.name))
            return 1
    ok = fw_sign.check_backup(a.pem, pub)
    print('KEY CHECK %s (key ID %s)' % ('OK' if ok else 'FAILED', pc.key_id(pub).hex()))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dir', default=os.environ.get('TFN_POOL_DIR', DEFAULT_DIR))
    ap.add_argument('--key', help='the PEM file of the admin key')
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('keygen')
    s.add_argument('out')
    s = sub.add_parser('create')
    s.add_argument('name')
    s.add_argument('--recovery')
    s.add_argument('--legacy-beacon', action='store_true')
    s.add_argument('--manager', action='store_true')
    s.add_argument('--lan')
    s.add_argument('--vpn')
    s = sub.add_parser('adopt')
    s.add_argument('ip')
    s.add_argument('--console')
    s.add_argument('--digits', help=argparse.SUPPRESS)   # (the tests: the digits of a fake board)
    s.add_argument('--token')
    s.add_argument('--password', nargs='?', const='', help='the web password of the board; give it with no value: '
                   'the tool asks for it (a value goes into the history of the shell and into the process list)')
    s.add_argument('--name')
    s.add_argument('--role', choices=('member', 'child'), default='member')
    s = sub.add_parser('push')
    s.add_argument('members', nargs='*')
    s = sub.add_parser('remove')
    s.add_argument('member')
    s = sub.add_parser('webpass')
    s.add_argument('members', nargs='*')
    s.add_argument('--console')
    s = sub.add_parser('show')
    s.add_argument('--live', action='store_true')
    s = sub.add_parser('check-backup')
    s.add_argument('pem')
    s.add_argument('--pub')
    a = ap.parse_args(argv)
    fn = {'keygen': cmd_keygen, 'create': cmd_create, 'adopt': cmd_adopt, 'push': cmd_push, 'remove': cmd_remove,
          'webpass': cmd_webpass, 'show': cmd_show, 'check-backup': cmd_check_backup}[a.cmd]
    try:
        return fn(a)
    except Refused as e:
        print('pool_tool: %s' % e, file=sys.stderr)
        return 1
    except (ToolError, pool.PoolError, pc.CryptoError, fw_sign.SignError, OSError) as e:
        print('pool_tool: %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
