"""The HTTP calls of poolmgr: to a member of the pool, and to the channel (design 2.2, 2.10).

A member = its name, device ID, MAC or address (as pool_tool.py), or the member dict of pool.json. The address of a
member comes from addrs.json (the beacons), else from pool.json (the address of the adopt). When the member does not
answer there, run() listens up to 3 s for the 'A' 'P' beacons (one time in a command), then tries again at the new
address. The request header X-TFN-Auth passes the web gate of a board (pool_tool.Board): poolmgr needs no web
password, but it sends the password of webpass.json too (an older board). The password goes only with a signed
request, after the board at the address gave the device ID of the member (board()).
"""
import base64
import http.client
import json
import time
import urllib.error
import urllib.request

import pm_net
from pm_shared import Failed, InputError, pool, pool_tool

TIMEOUT = 10
MAX_GET = 64 * 1024 * 1024
PART = 4096
LISTEN_AGAIN = 3.0
DONE = True


def http_get(url, timeout=TIMEOUT, limit=MAX_GET):
    """GET of a URL (the channel, the static store, GET /api/pool of a member): the bytes of the body. Failed for an
    HTTP error, no answer, an answer that is not HTTP (another service at the address), a bad URL, or a body over
    limit."""
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'tfn-poolmgr'})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            data = r.read(limit + 1)
    except urllib.error.HTTPError as e:
        raise Failed('%s: HTTP %d' % (url, e.code))
    except (urllib.error.URLError, OSError) as e:
        raise Failed('%s: no answer (%s)' % (url, getattr(e, 'reason', e)))
    except (http.client.HTTPException, ValueError) as e:
        raise Failed('%s: no HTTP answer (%s: %s)' % (url, type(e).__name__, e))
    if len(data) > limit:
        raise Failed('%s: more than %d bytes' % (url, limit))
    return data


# ---------------------------------------------------------------- the members
def the_pool(data):
    return data.need_pool()


def is_manager(m):
    return m['role'] == pool.ROLE_MANAGER and m['mac'] == pool.MANAGER_MAC.hex()


def boards(data):
    """The members that are boards (not the manager member), in the order of the roster."""
    return [m for m in the_pool(data).members if not is_manager(m)]


def find_member(data, what):
    """The member dict of what (a name, an ID, a MAC, an address, or the dict). InputError when the pool has no such
    board."""
    if isinstance(what, dict):
        return what
    p = the_pool(data)
    m = p.find(str(what))
    if m is None:
        w = str(what).lower()
        for mac, a in data.addrs().items():
            if (a.get('ip') or '').lower() == w:
                m = p.find(mac.lower())
                break
    if m is None:
        raise InputError('no member %s in the pool %s' % (what, p.name))
    if is_manager(m):
        raise InputError('%s is the manager, not a board' % what)
    return m


def known_address(data, m):
    """The address of addrs.json for the MAC of m, else the address of pool.json, else ''."""
    a = data.addrs().get(m['mac'].upper()) or {}
    return a.get('ip') or m.get('ip') or ''


def webpass_of(data, m):
    return pool_tool.load_webpass(data.pool_dir).get(m['mac'])


def address(data, member, wait=3.0):
    """The address ("ip" or "ip:port") of the member: addrs.json or pool.json, else a listen of wait seconds for its
    beacon. Failed when no address is known."""
    m = find_member(data, member)
    ip = known_address(data, m)
    if not ip and wait > 0:
        listen_once(data, wait)
        ip = known_address(data, m)
    if not ip:
        raise Failed('%s: no address (is it on, and on the same LAN?)' % m['name'])
    return ip


def listen_once(data, wait):
    """Listen for the beacons, one time in LISTEN_AGAIN * 3 s for each data object (many members off line do not
    each wait)."""
    if time.time() - getattr(data, 'heard_t', 0) < LISTEN_AGAIN * 3:
        return False
    pm_net.listen(data, wait)
    data.heard_t = time.time()
    return True


def board(data, member):
    """A pool_tool.Board for the member: its address, its wk, its web password (webpass.json) and its device ID. The
    read of the board nonce checks the device ID (pool_tool.Board.nonce): a signed request, and the web password with
    it, goes only to the board of the member, not to another device at an old address of the member."""
    p = the_pool(data)
    m = find_member(data, member)
    return pool_tool.Board(address(data, m), p, p.wk(m['id']), webpass_of(data, m), dev_id=m['id'])


def run(data, member, fn, wait=LISTEN_AGAIN):
    """fn(board) for the member. When the member does not answer (pool_tool.ToolError), listen for its beacon and
    try again one time at its new address."""
    m = find_member(data, member)
    b = board(data, m)
    try:
        return fn(b)
    except pool_tool.ToolError:
        if wait <= 0:
            raise
        old = b.ip
        listen_once(data, wait)
        ip = known_address(data, m)
        if not ip or ip == old:
            raise
        b.ip = ip
        b.bn = None
        return fn(b)


def signed(b, method, path, body=None):
    """A request with the request header; one more try with a new board nonce after a 401 (a new start of the
    board)."""
    try:
        return b.call(method, path, body, signed=True)
    except pool_tool.Refused as e:
        if e.code != 401:
            raise
        b.bn = None
        return b.call(method, path, body, signed=True)


def call(data, member, method, path, body=None):
    """A request with the request header to the member (run + signed): the answer as a dict."""
    return run(data, member, lambda b: signed(b, method, path, body))


# ---------------------------------------------------------------- the big POST
def post_stream(data, member, path, payload, progress=None, timeout=90):
    """A POST of a big body (the OTA file) to the member with the request header, in parts of PART bytes;
    progress(sent, total) after each part. The answer of the board as a dict (status 200). pool_tool.Refused for
    another status (e.ans has the answer of the board, for example {"code": "sig"}); one more try after a 401 (a new
    start of the board). pool_tool.ToolError when the board does not answer."""
    def send(b):
        try:
            return stream_once(b, path, payload, progress, timeout)
        except pool_tool.Refused as e:
            if e.code != 401:
                raise
            b.bn = None
            return stream_once(b, path, payload, progress, timeout)
    return run(data, member, send)


def stream_once(b, path, payload, progress, timeout):
    if b.bn is None:
        b.bn = b.nonce()
    host, _, port = b.ip.partition(':')
    c = http.client.HTTPConnection(host, int(port or 80), timeout=timeout)
    total = len(payload)
    try:
        c.putrequest('POST', path)
        c.putheader(pool.HDR, pool.web_header(b.wk, b.bn, b.p.mid(), b.ctr.next(), 'POST', path))
        if b.password:
            c.putheader('Authorization', basic(b.password))
        c.putheader('Content-Type', 'application/octet-stream')
        c.putheader('Content-Length', str(total))
        c.endheaders()
        sent = 0
        view = memoryview(payload)
        while sent < total:
            n = min(PART, total - sent)
            c.send(view[sent:sent + n])
            sent += n
            if progress:
                progress(sent, total)
        r = c.getresponse()
        text = r.read().decode('utf-8', 'replace')
    except (OSError, http.client.HTTPException) as e:
        raise pool_tool.ToolError('no answer from %s: %s' % (b.ip, e))
    finally:
        c.close()
    try:
        ans = json.loads(text or '{}')
    except ValueError:
        ans = {'error': text[:200]}
    if r.status != 200:
        raise pool_tool.Refused(r.status, ans if isinstance(ans, dict) else {'error': str(ans)})
    return ans


def basic(password):
    return 'Basic ' + base64.b64encode(('admin:%s' % password).encode()).decode()
