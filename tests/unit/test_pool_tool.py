#!/usr/bin/env python3
"""The test of sdk/tools/pool_tool.py against FAKE boards (tests/unit/poolmgr_fake.py: small HTTP servers on 127.0.0.1
that speak /api/pool, and a fake USB console). No real board and no network.

The steps: keygen (admin keys A, R and a key X that is not an admin), create, adopt with the console (the tool reads
the 6 digits and sends "pool allow"), adopt with --digits (a person taps ALLOW), a wrong SAS (a device between the
tool and the board), push, the request header (no header, a wrong key, a replay, a new start of the board), forged
and old rosters, webpass, remove with a new key, the token JOIN (and its refusals), a board with a web password
(--password), show, check-backup, and the answers of devices that are not TFN boards (Board.call: an HTML page, a JSON
list, no nonce, an answer that is not HTTP). As the firmware (hdr_good of main/tfn_pool_flow.c), a fake board in a pool
takes the request header only from a MANAGER member of its roster: a pool of "create --manager" works, and a pool
with no manager member gets a WARNING at create and 401 after the first adopt. The USB console opens with no restart
of the board (DTR and RTS). All keys are TEST keys in a temporary folder.

Usage: python tests/unit/test_pool_tool.py
"""
import builtins
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import fw_sign  # noqa: E402
import pool  # noqa: E402
import pool_crypto as pc  # noqa: E402
import pool_tool  # noqa: E402
from poolmgr_fake import FakeBoard, FakeConsole, NotHttp, OtherDevice  # noqa: E402,F401

FAILS = []
CHECKS = [0]
REAL_INPUT = builtins.input


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


# ---------------------------------------------------------------- the steps
def tool(*argv):
    """pool_tool.main with the output caught: (exit code, the output)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = pool_tool.main(list(argv))
    return code, out.getvalue()


def raw_post(board, path, body, hdr=None):
    req = urllib.request.Request('http://%s%s' % (board.ip, path), data=json.dumps(body).encode(), method='POST')
    if hdr:
        req.add_header(pool.HDR, hdr)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def same_pool(p, b):
    return (b.roster is not None and b.roster.pool_id == p.pool_id and b.roster.version == p.version and
            b.pool_k == p.pool_k and b.epoch == p.epoch and b.wk == p.wk(b.id.hex()))


def run(d):
    pd = os.path.join(d, 'poolT')
    A, R, X = (os.path.join(d, 'test_%s.pem' % n) for n in ('a', 'r', 'x'))
    for k in (A, R, X):
        code, out = tool('keygen', k)
        expect(code == 0 and 'key ID' in out, 'keygen %s' % os.path.basename(k))
    expect(tool('keygen', A)[0] == 2, 'keygen: an existing file stays')
    base = ['--dir', pd, '--key', A]
    code, out = tool(*base, 'create', 'TEST T', '--manager', '--recovery', R)
    expect(code == 0 and 'WARNING' not in out, 'create: %s' % out)
    expect(tool(*base, 'create', 'TEST T')[0] == 2, 'create: a pool is never overwritten')
    expect(tool('--dir', pd, '--key', X, 'push')[0] == 2, 'a key that is not an admin key is refused')
    b1 = FakeBoard('b1', bytes.fromhex('020000000001'), d)
    b2 = FakeBoard('b2', bytes.fromhex('020000000002'), d)
    b3 = FakeBoard('b3', bytes.fromhex('020000000003'), d, legacy=True, token=pc.rand(16).hex())
    b4 = FakeBoard('b4', bytes.fromhex('020000000004'), d, legacy=False, token=pc.rand(16).hex())
    cons = {'fake1': b1, 'fake2': b2, 'fake3': b3, 'fake4': b4}
    pool_tool.open_console = lambda port: FakeConsole(cons[port])
    pool_tool.ALLOW_WAIT = 8

    # the device ID of /api/id has the dashes of the board: the tool compares it with no dashes
    expect(pool_tool.ident_id({'id': b1.id_text()}) == b1.id.hex() and '-' in b1.id_text(),
           '/api/id: the ID with dashes is the ID of the device key')
    expect(pool_tool.ident_id({'id': '9B10-4E22-7AC1-056D'}) == '9b104e227ac1056d', '/api/id: no dashes, small letters')

    # adopt with the console: the tool reads the 6 digits and sends "pool allow"
    code, out = tool(*base, 'adopt', b1.ip, '--console', 'fake1', '--name', 'ONE')
    p = pool.Pool.load(pd)
    expect(code == 0 and same_pool(p, b1), 'adopt with the console: %s' % out)
    expect(p.find('ONE')['bx'] == b1.bx_pub.hex() and p.find('ONE')['mac'] == b1.mac.hex(), 'adopt: the member data')
    expect(b1.state()['role'] == 'member', 'adopt: the role member')

    # a JOIN with no open JOIN screen is refused
    code, out = tool(*base, 'adopt', b2.ip, '--digits', '000000')
    expect(code == 1 and 'no_join' in out, 'adopt: no JOIN screen open = 409 no_join: %s' % out)
    # a device between the tool and the board: another SAS on the board, the JOIN stops before ALLOW
    b2.join_open, b2.lie_sas = True, True
    code, out = tool(*base, 'adopt', b2.ip, '--console', 'fake2')
    expect(code == 2 and 'NOT the same' in out and b2.roster is None, 'adopt: a wrong SAS stops the JOIN: %s' % out)
    expect('allowed' != (b2.j or {}).get('step'), 'adopt: no ALLOW after a wrong SAS')
    # a person reads the digits and taps ALLOW (one second later: the tool waits)
    b2.lie_sas, b2.auto_tap = False, 1.0
    b2.join_open = True
    code, out = tool(*base, 'adopt', b2.ip, '--digits', '12345x', '--name', 'TWO')
    expect(code == 2 and 'NOT the same' in out and b2.roster is None, 'adopt: digits that are not the SAS stop it')
    # the person types the digits that the board shows (input() of the tool), then taps ALLOW
    b2.join_open = True
    typed = []
    builtins.input = lambda prompt='': typed.append(1) or '%06d' % b2.shown_sas()
    try:
        code, out = tool(*base, 'adopt', b2.ip, '--name', 'TWO', '--role', 'child')
    finally:
        builtins.input = REAL_INPUT
    expect(typed == [1], 'adopt: the tool asks for the digits one time')
    p = pool.Pool.load(pd)
    expect(code == 0 and same_pool(p, b2), 'adopt with the typed digits and a tap: %s' % out)
    expect(b2.state()['role'] == 'child', 'adopt: the role child')
    expect(b1.roster.version == p.version and b1.roster.member_by_id(b2.id) >= 0,
           'adopt: the other member gets the new roster')

    # the link: frames and beacons of the boards open with the keys of the tool
    kt, k1, k2 = p.keys(), b1.keys(), b2.keys()
    f = pool.frame_seal(k1, b1.mac, 0x59, 1, 0, b'ping', 1)
    macs = [m.mac for m in b2.roster.members]
    expect(pool.frame_open(kt, b1.mac, f, macs)[0] == 'ok' and pool.frame_open(k2, b1.mac, f, macs)[0] == 'ok',
           'a v4 frame of board 1 opens on board 2 and in the tool')
    bea = pool.beacon_seal(k2, pool.beacon_plain(b2.mac, 'TWO', 'PONG', 0, b2.id))
    expect(pool.beacon_open(kt, bea)[0] == 'ok', 'the beacon of board 2 opens in the tool')

    # the request header
    code, ans = raw_post(b1, '/api/pool?op=set', {'roster': p.roster_file.hex()})
    expect(code == 401, 'op=set with no header = 401')
    hdr = pool.web_header(pc.rand(32), b1.bn, p.mid(), 10 ** 12, 'POST', '/api/pool?op=set')
    expect(raw_post(b1, '/api/pool?op=set', {'roster': p.roster_file.hex()}, hdr)[0] == 401, 'a wrong wk = 401')
    hdr = pool.web_header(p.wk(b1.id.hex()), b1.bn, p.mid(), pool_tool.COUNTER.next(), 'POST', '/api/pool?op=set')
    code, ans = raw_post(b1, '/api/pool?op=set', {'roster': p.roster_file.hex()}, hdr)
    expect(code == 200 and ans.get('roster') == 'same', 'a good header: the same roster = ok: %s' % ans)
    expect(raw_post(b1, '/api/pool?op=set', {'roster': p.roster_file.hex()}, hdr)[0] == 401,
           'the same header again (a replay) = 401')
    hdr = pool.web_header(p.wk(b1.id.hex()), b1.bn, p.mid(), pool_tool.COUNTER.next(), 'POST', '/api/pool?op=key')
    expect(raw_post(b1, '/api/pool?op=set', {'roster': p.roster_file.hex()}, hdr)[0] == 401,
           'a header for another uri = 401')
    bt = pool_tool.Board(b1.ip, p, p.wk(b1.id.hex()))
    expect(bt.signed_post('set', {'roster': p.roster_file.hex()}).get('ok'), 'Board.signed_post')
    old_bn = bt.bn
    b1.restart()
    expect(bt.signed_post('set', {'roster': p.roster_file.hex()}).get('ok') and bt.bn != old_bn,
           'a new start of the board: the tool reads bn again')
    old = bytes.fromhex(b1.state()['bn'])
    hdr = pool.web_header(p.wk(b1.id.hex()), old_bn, p.mid(), pool_tool.COUNTER.next(), 'POST', '/api/pool?op=set')
    expect(raw_post(b1, '/api/pool?op=set', {'roster': p.roster_file.hex()}, hdr)[0] == 401 and old != old_bn,
           'a header with the bn of the start before = 401')

    # forged and old rosters
    r = p.roster()
    r.version = p.version + 1
    body = pool.roster_body(r)
    forged = body + pool.sig_record(pc.p256_pub(X), pc.p256_sign(X, body))
    try:
        bt.signed_post('set', {'roster': forged.hex()})
        expect(False, 'a roster signed by a key that is not an admin key is refused')
    except pool_tool.Refused as e:
        expect(e.code == 403 and e.ans['error'] == 'sig', 'forged roster: %s' % e.ans)
    r.version = p.version - 1
    older = pool.roster_sign(r, A)
    try:
        bt.signed_post('set', {'roster': older.hex()})
        expect(False, 'an older roster is refused')
    except pool_tool.Refused as e:
        expect(e.ans['error'] == 'old', 'older roster: %s' % e.ans)
    expect(same_pool(p, b1), 'board 1 keeps its pool after the refusals')

    # push (the roster and the key; a board that missed a version gets it)
    b2.roster, b2.rv = pool.roster_parse(older), p.version - 1
    code, out = tool(*base, 'push')
    expect(code == 0 and same_pool(p, b1) and b2.roster.version == p.version, 'push: %s' % out)
    code, out = tool(*base, 'show', '--live')
    expect(code == 0 and 'ONE' in out and 'TWO' in out and 'NOT THE SAME' not in out, 'show --live: %s' % out)

    # webpass: a random web password on each member; the tool still works (header, Basic, GET /api/pool is open)
    code, out = tool(*base, 'webpass')
    wp = json.load(open(os.path.join(pd, 'webpass.json')))
    expect(code == 0 and wp.get(b1.mac.hex()) == b1.webpw and b1.webpw and b2.webpw, 'webpass: %s' % out)
    try:
        urllib.request.urlopen('http://%s/api/id' % b1.ip, timeout=5)
        expect(False, 'a web password closes /api/id')
    except urllib.error.HTTPError as e:
        expect(e.code == 401, 'a web password closes /api/id')
    expect(tool(*base, 'push')[0] == 0 and tool(*base, 'show', '--live')[0] == 0, 'push and show with web passwords')
    code, out = tool(*base, 'webpass', 'ONE', '--console', 'fake1')
    expect(code == 0 and json.load(open(os.path.join(pd, 'webpass.json')))[b1.mac.hex()] == b1.webpw,
           'webpass by the console')

    # remove: board 2 leaves, board 1 gets the new key, and keeps the previous key for its hour
    ep, k_old = p.epoch, p.pool_k
    code, out = tool(*base, 'remove', 'TWO')
    p = pool.Pool.load(pd)
    expect(code == 0 and b2.roster is None and b2.pool_k is None, 'remove: board 2 left: %s' % out)
    expect(same_pool(p, b1) and p.epoch == ep + 1 and b1.prev == (ep, k_old) and p.pool_k != k_old,
           'remove: board 1 has the new key (epoch %d)' % p.epoch)
    expect(p.find('TWO') is None and b2.rv == p.version - 1, 'remove: the member is gone; board 2 keeps its floor')
    old_f = pool.frame_seal(pool.derive(k_old, p.pool_id, ep), b2.mac, 1, 1, 0, b'x', 5)
    expect(pool.frame_open(b1.keys(), b2.mac, old_f, None)[0] in ('other', 'bad'),
           'a frame with the old key does not open with the new key')

    # the token JOIN (a family board: LEGACY build, a token, no pool): no digits, no tap
    code, out = tool(*base, 'adopt', b3.ip, '--token', b3.token, '--name', 'THREE')
    p = pool.Pool.load(pd)
    expect(code == 0 and same_pool(p, b3) and b3.token is None, 'token JOIN: %s' % out)
    code, out = tool(*base, 'adopt', b3.ip, '--token', 'x' * 32)
    expect(code == 1 and 'token' in out, 'token JOIN of a board in a pool (or a wrong token) is refused')
    code, out = tool(*base, 'adopt', b4.ip, '--token', b4.token)
    expect(code == 1 and 'token' in out and b4.roster is None, 'token JOIN of a public build is refused')
    # a JOIN with the console of the public board works
    code, out = tool(*base, 'adopt', b4.ip, '--console', 'fake4', '--name', 'FOUR')
    p = pool.Pool.load(pd)
    expect(code == 0 and same_pool(p, b4), 'adopt of a public board with the console: %s' % out)
    expect(b1.roster.version == p.version and b3.roster.version == p.version, 'all members have the newest roster')
    # a board with a web password: the web gate answers 401 "login"; the tool names --password; --password works
    b5 = FakeBoard('b5', bytes.fromhex('020000000005'), d, legacy=False)
    cons['fake5'] = b5
    b5.webpw = 'own-pass-5555'
    code, out = tool(*base, 'adopt', b5.ip, '--console', 'fake5', '--name', 'FIVE')
    expect(code == 2 and 'has a web password' in out and '--password' in out and b5.roster is None,
           'adopt of a board with a web password, no --password: %s' % out)
    code, out = tool(*base, 'adopt', b5.ip, '--console', 'fake5', '--name', 'FIVE', '--password', 'wrong-one-0000')
    expect(code == 2 and 'not right' in out and b5.roster is None, 'adopt with a wrong --password: %s' % out)
    code, out = tool(*base, 'adopt', b5.ip, '--console', 'fake5', '--name', 'FIVE', '--password', 'own-pass-5555')
    p = pool.Pool.load(pd)
    expect(code == 0 and same_pool(p, b5) and b5.webpw == 'own-pass-5555', 'adopt --password: %s' % out)
    po = io.StringIO()
    with contextlib.redirect_stdout(po), contextlib.redirect_stderr(po):
        try:
            pool_tool.main(['adopt', '--help'])
        except SystemExit:
            pass
    expect('--digits' not in po.getvalue() and '--password' in po.getvalue(), 'adopt --help: no --digits')
    b5.srv.shutdown()
    b5.srv.server_close()

    # check-backup
    expect(tool(*base, 'check-backup', A)[0] == 0, 'check-backup: A is an admin key of the pool')
    expect(tool(*base, 'check-backup', R, '--pub', R)[0] == 0, 'check-backup: R with its public key')
    expect(tool(*base, 'check-backup', X)[0] == 1, 'check-backup: X is not an admin key')
    expect(tool(*base, 'check-backup', A, '--pub', R)[0] == 1, 'check-backup: A with the public key of R')
    # the files of the pool: the secrets are not in pool.json
    pj = open(os.path.join(pd, 'pool.json')).read()
    expect(p.pool_k.hex() not in pj and all(w not in pj for w in p.wks.values()), 'pool.json has no secret')
    if os.name == 'posix':
        expect(os.stat(os.path.join(pd, 'secrets.json')).st_mode & 0o077 == 0, 'secrets.json has mode 0600')
    # a pool of a manager: the manager is a member (role MANAGER, its radio MAC), the first roster is signed at once
    pm = os.path.join(d, 'poolM')
    code, out = tool('--dir', pm, '--key', A, 'create', 'FAMILY', '--manager', '--legacy-beacon', '--lan',
                     'http://198.51.100.7:8080')
    r = pool.roster_parse(pool.Pool.load(pm).roster_file)
    m0 = r.members[0]
    expect(code == 0 and m0.role == pool.ROLE_MANAGER and m0.mac == pool.MANAGER_MAC and m0.id == pc.device_id(
        pc.p256_pub(A)) and r.flags == pool.F_LEGACY_BEACON and r.lan == 'http://198.51.100.7:8080' and r.vpn == '' and
        pool.roster_signer(r) == pc.p256_pub(A), 'create --manager: %s' % out)
    not_board_part(pool.Pool.load(pd))
    for b in (b1, b2, b3, b4):
        b.srv.shutdown()
        b.srv.server_close()


def raised(fn):
    """The exception of fn(), or None."""
    try:
        fn()
    except Exception as e:  # the test looks at the exception
        return e
    return None


def not_board_part(p):
    """Board.call and the devices that are not TFN boards (for example at an old address of a board): an HTML page and
    a JSON list give NotBoard (a ToolError); a signed request stops at the read of the nonce, which has no web
    password; an HTTP error with a JSON list gives Refused with {"error": "HTTP <code>"}; an answer that is not HTTP
    and a bad address give ToolError (not NotBoard)."""
    page, arr = OtherDevice(), OtherDevice(ctype='application/json', body=b'[1, 2, 3]')
    nobn = OtherDevice(ctype='application/json', body=b'{"state": "ok"}')
    err, raw = OtherDevice(status=404, ctype='application/json', body=b'["no route"]'), NotHttp()
    try:
        for dev, what in ((page, 'an HTML page'), (arr, 'a JSON list')):
            e = raised(lambda: pool_tool.Board(dev.ip).call('GET', '/api/pool'))
            expect(isinstance(e, pool_tool.NotBoard) and isinstance(e, pool_tool.ToolError) and
                   'not a TFN board' in str(e), 'Board.call of %s: NotBoard (%r)' % (what, e))
        for dev, what in ((page, 'an HTML page'), (nobn, 'a JSON object with no nonce')):
            del dev.seen[:]
            b = pool_tool.Board(dev.ip, p, os.urandom(32), 'a-web-password')
            e = raised(lambda: b.call('POST', '/api/pool?op=key', {'x': 1}, signed=True))
            expect(isinstance(e, pool_tool.NotBoard) and dev.seen == [('GET', '/api/pool', None)],
                   'a signed request to %s: NotBoard at the nonce, no web password goes out (%r %r)' % (
                       what, e, dev.seen))
        e = raised(lambda: pool_tool.Board(err.ip).call('GET', '/api/pool'))
        expect(isinstance(e, pool_tool.Refused) and e.code == 404 and e.ans == {'error': 'HTTP 404'},
               'an HTTP error with a JSON list: Refused, {"error": "HTTP 404"} (%r)' % e)
        for ip, what in ((raw.ip, 'an answer that is not HTTP'), ('127.0.0.1:1 x', 'a bad address')):
            e = raised(lambda: pool_tool.Board(ip).call('GET', '/api/pool'))
            expect(isinstance(e, pool_tool.ToolError) and not isinstance(e, pool_tool.NotBoard) and
                   'no answer from' in str(e), '%s: ToolError (%r)' % (what, e))
    finally:
        for dev in (page, arr, nobn, err, raw):
            dev.close()


def run_no_manager(d, key):
    """A pool with no MANAGER member (create with no --manager): create gives a WARNING; the first adopt works, but a
    board in the pool refuses the request header of the tool (its ID is not a MANAGER member of the roster), so the
    roster of the second adopt (a note, exit 1) and the push (exit 1) do not reach the first board."""
    pd = os.path.join(d, 'poolN')
    base = ['--dir', pd, '--key', key]
    code, out = tool(*base, 'create', 'TEST N')
    expect(code == 0 and 'WARNING' in out and '--manager' in out, 'create with no --manager: a WARNING: %s' % out)
    n1 = FakeBoard('n1', bytes.fromhex('020000000011'), d)
    n2 = FakeBoard('n2', bytes.fromhex('020000000012'), d)
    pool_tool.open_console = lambda port: FakeConsole({'n1': n1, 'n2': n2}[port])
    try:
        code, out = tool(*base, 'adopt', n1.ip, '--console', 'n1', '--name', 'N1')
        expect(code == 0 and same_pool(pool.Pool.load(pd), n1), 'no manager: the first adopt works: %s' % out)
        code, out = tool(*base, 'adopt', n2.ip, '--console', 'n2', '--name', 'N2')
        p = pool.Pool.load(pd)
        expect(code == 1 and same_pool(p, n2) and n1.roster.version < p.version and '401' in out and
               'NOT ALL members' in out,
               'no manager: the roster of the second adopt does not reach the first board (401, exit 1): %s' % out)
        code, out = tool(*base, 'push')
        expect(code == 1 and n1.roster.version < p.version, 'no manager: push gets 401: %s' % out)
    finally:
        for b in (n1, n2):
            b.srv.shutdown()
            b.srv.server_close()


class FakeSerial:
    """serial.Serial of the test: it keeps each state of the DTR and RTS lines from the open on (DTR low with RTS high
    restarts an ESP32-S3). FakeSerial.posix: the open sets both lines high, as Linux and macOS do."""
    SerialException = OSError
    posix = False
    last = None

    def __init__(self, *args, **kw):
        self.args, self.is_open, self.states = args, False, []
        self.port, self._dtr, self._rts = kw.get('port'), True, True
        FakeSerial.last = self

    def _set_line(self, name, value):
        setattr(self, name, value)
        if self.is_open:
            self.states.append((self._dtr, self._rts))

    dtr = property(lambda self: self._dtr, lambda self, v: self._set_line('_dtr', v))
    rts = property(lambda self: self._rts, lambda self, v: self._set_line('_rts', v))

    def open(self):
        if FakeSerial.posix:
            self._dtr, self._rts = True, True
        self.is_open = True
        self.states.append((self._dtr, self._rts))


def test_serial_console():
    """SerialConsole opens the port with no restart of the board: the lines never have DTR low with RTS high, and both
    are low at the end. Windows (posix False): both are low at the open. Linux and macOS (posix True): the open sets
    both high, then RTS goes low first."""
    fake = type(sys)('serial')
    fake.Serial, fake.SerialException = FakeSerial, OSError
    old = sys.modules.get('serial')
    sys.modules['serial'] = fake
    try:
        for posix in (False, True):
            FakeSerial.posix = posix
            pool_tool.SerialConsole('/dev/ttyACM9', posix=posix)
            s = FakeSerial.last
            expect(s.args == () and s.port == '/dev/ttyACM9' and s.baudrate == 115200 and
                   s.states[-1] == (False, False) and (False, True) not in s.states and
                   (posix or s.states[0] == (False, False)),
                   'SerialConsole (posix %s): no restart at the open, both lines low: %s' % (posix, s.states))
    finally:
        if old is None:
            sys.modules.pop('serial', None)
        else:
            sys.modules['serial'] = old


def main():
    try:
        fw_sign.openssl()
    except fw_sign.SignError as e:
        print('POOL TOOL PY FAILED: %s' % e)
        return 1
    d = tempfile.mkdtemp(prefix='tfn_pool_tool_')
    t0 = time.time()
    try:
        test_serial_console()
        run(d)
        run_no_manager(d, os.path.join(d, 'test_a.pem'))
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print('POOL TOOL PY %s (%d checks against fake boards, %.1f s)' % (
        'OK' if not FAILS else '%d FAILED' % len(FAILS), CHECKS[0], time.time() - t0))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
