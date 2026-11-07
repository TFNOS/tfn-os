"""The web password on a real board (USB console + its web server on the same network as this PC).
    python tests/web_auth/board_auth.py <COM port> <board ip>      -> BOARD AUTH OK
It sets a test password with the console, checks the gate (pages, API, Basic, the login, the WebSocket, the OTA
upload, the wait after 5 wrong passwords, a new password, logout), and at the end turns the password off again (the
board had none before: the test stops when the board already has one, so it never removes the user's password).
"""
import base64
import http.client
import os
import re
import socket
import sys
import time

import serial

PORT, IP = sys.argv[1], sys.argv[2]
PW, PW2 = 'Test pass 41', 'second-pass'
fails = 0


def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what, flush=True)
    if not ok:
        fails += 1


def console(cmd, wait=3.0):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout, s.dtr, s.rts = PORT, 115200, 0.3, False, False
    s.open()
    s.reset_input_buffer()
    s.write((cmd + '\r\n').encode())
    out, end = [], time.time() + wait
    while time.time() < end:
        l = s.readline()
        if l:
            out.append(l.decode('utf-8', 'replace').rstrip())
    s.close()
    return out


def req(method, path, headers=None, body=None):
    c = http.client.HTTPConnection(IP, 80, timeout=15)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse()
    data = r.read()
    c.close()
    return r.status, {k.lower(): v for k, v in r.getheaders()}, data


def basic(pw, user=''):
    return {'Authorization': 'Basic ' + base64.b64encode((user + ':' + pw).encode()).decode()}


def login(pw, nxt='/'):
    body = 'p=%s&next=%s' % (pw.replace(' ', '+'), nxt.replace('/', '%2F'))
    st, h, d = req('POST', '/login', {'Content-Type': 'application/x-www-form-urlencoded'}, body)
    m = re.search(r'tfn_s=([0-9a-f]{32})', h.get('set-cookie', ''))
    return st, h, d, (m.group(1) if m else None)


def ws_handshake(cookie=None):
    s = socket.create_connection((IP, 80), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    hdr = ('GET /ws/audio HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
           'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n' % (IP, key))
    if cookie:
        hdr += 'Cookie: tfn_s=%s\r\n' % cookie
    s.sendall((hdr + '\r\n').encode())
    try:
        resp = s.recv(512).decode('latin1')
    except OSError:
        resp = ''
    s.close()
    return resp.split('\r\n', 1)[0]


state = ' '.join(console('webpass'))
if 'webpass: on' in state:
    sys.exit('STOP: the board has a web password already (the test does not touch it)')

# 1. no password: open as before
st, h, _ = req('GET', '/api/boards')
check(st == 200, 'no password: /api/boards is open (%d)' % st)
st, h, _ = req('GET', '/login')
check(st == 303 and h.get('location') == '/', 'no password: /login goes to / (%d %s)' % (st, h.get('location')))

# 2. a password with a space (the console takes the rest of the line)
out = ' '.join(console('webpass set ' + PW))
check('webpass: on' in out, 'console: webpass set -> on')
st, h, d = req('GET', '/api/boards')
check(st == 401 and b'"login"' in d, 'API with no login: 401 JSON (%d)' % st)
check('www-authenticate' not in h, 'no WWW-Authenticate header (no Basic prompt in a browser)')
st, h, _ = req('GET', '/walkie', {'Accept': 'text/html'})
check(st == 303 and h.get('location', '') in ('/login?next=/walkie', '/login?next=%2Fwalkie'), 'a page goes to /login (%s)' % h.get('location'))
st, h, d = req('GET', '/login?next=%2Fwalkie')
check(st == 200 and b'type=password' in d and b'value="/walkie"' in d, 'the login page with next=/walkie')
st, h, d = req('GET', '/api/screen')
check(st == 401, 'the screen picture: 401 (%d)' % st)
st, h, d = req('POST', '/api/ota', {'Content-Type': 'application/octet-stream'}, b'\0' * 64)
check(st == 401, 'the firmware upload: 401 (%d)' % st)
st, h, d = req('GET', '/api/ctl?tap=10,10')
check(st == 401, 'a virtual tap: 401 (%d)' % st)
check(ws_handshake() != 'HTTP/1.1 101 Switching Protocols', 'the WebSocket /ws/audio with no login: no handshake')

# 3. Basic
st, _, _ = req('GET', '/api/boards', basic(PW))
check(st == 200, 'Basic with the right password: 200 (%d)' % st)
def timed(hdr):
    t0 = time.time()
    st, _, _ = req('GET', '/api/ctl', hdr)
    return st, (time.time() - t0) * 1000
cold = [timed(basic(PW, 'u%d' % i)) for i in range(3)]         # a new header each time: the PBKDF2 of the password
warm = [timed(basic(PW)) for _ in range(3)]                      # the same header: from the cache
mc, mw = sorted(x[1] for x in cold)[1], sorted(x[1] for x in warm)[1]
check(all(x[0] == 200 for x in cold + warm), 'Basic: a new header %.0f ms, a known header %.0f ms (the cache)' % (mc, mw))
check(mw < mc, 'the cache of a known header is faster than the hash')
console('wake', 1.0)                                            # (a screen that is off gives no picture)
time.sleep(1)
st, h, d = req('GET', '/api/screen', basic(PW))
check(st == 200 and len(d) > 1000, 'the screen with Basic: 200, %d bytes' % len(d))

# 4. the login
st, h, d, ck = login('wrong one')
check(st == 401 and not ck and b'Wrong password' in d, 'login with a wrong password: 401, no cookie')
st, h, d, ck = login(PW, '/walkie')
check(st == 303 and h.get('location') == '/walkie' and ck, 'login: 303 to /walkie with a cookie')
sc = h.get('set-cookie', '')
check('HttpOnly' in sc and 'SameSite=Strict' in sc, 'the cookie is HttpOnly and SameSite=Strict')
st, _, _ = req('GET', '/api/boards', {'Cookie': 'tfn_s=' + ck})
check(st == 200, 'API with the cookie: 200 (%d)' % st)
bad = ck[:-1] + ('0' if ck[-1] != '0' else '1')
st, _, _ = req('GET', '/api/boards', {'Cookie': 'tfn_s=' + bad})
check(st == 401, 'a changed cookie: 401 (%d)' % st)
check(ws_handshake(ck) == 'HTTP/1.1 101 Switching Protocols', 'the WebSocket with the cookie: 101')
st, h, d, _ = login(PW, '//evil.example/x')
check(st == 303 and h.get('location') == '/', 'next=//evil... goes to / (%s)' % h.get('location'))

# 5. the wait after 5 wrong passwords (Basic and the login share the count of this address)
for i in range(5):
    req('GET', '/api/boards', basic('nope%d' % i))
st, h, d = req('GET', '/api/boards', basic(PW, 'other'))
check(st == 429 and h.get('retry-after'), '6th try (the right password, a new header): 429, Retry-After %s' % h.get('retry-after'))
st, _, _ = req('GET', '/api/boards', basic(PW))
check(st == 200, 'a Basic header that passed before (in the cache) goes on in the wait: 200')
st, h, d, ck2 = login(PW)
check(st == 429 and not ck2, 'the login waits too: 429')
st, _, _ = req('GET', '/api/boards', {'Cookie': 'tfn_s=' + ck})
check(st == 200, 'a logged-in browser goes on in the wait: 200')
time.sleep(int(h.get('retry-after', '30')) + 1)
st, _, _ = req('GET', '/api/boards', basic(PW, 'other'))
check(st == 200, 'after the wait the right password passes: 200 (%d)' % st)

# 6. logout, a new password
st, h, _ = req('GET', '/logout', {'Cookie': 'tfn_s=' + ck})
check(st == 303 and h.get('location') == '/login' and 'Max-Age=0' in h.get('set-cookie', ''), 'logout: 303, the cookie goes')
st, _, _ = req('GET', '/api/boards', {'Cookie': 'tfn_s=' + ck})
check(st == 401, 'the old cookie after logout: 401')
_, _, _, ck = login(PW)
out = ' '.join(console('webpass set ' + PW2))
check('webpass: on' in out, 'console: a new password')
st, _, _ = req('GET', '/api/boards', {'Cookie': 'tfn_s=' + ck})
check(st == 401, 'a session of the old password: 401')
st, _, _ = req('GET', '/api/boards', basic(PW))
check(st == 401, 'Basic with the old password (it was in the cache): 401')
st, _, _ = req('GET', '/api/boards', basic(PW2))
check(st == 200, 'Basic with the new password: 200')
out = ' '.join(console('webpass set abc'))
check('NEEDS 4 TO 63' in out and 'webpass: on' in out, 'a password of 3 characters is refused (the old one stays)')

# 7. off again: open as before
out = ' '.join(console('webpass off'))
check('webpass: off' in out, 'console: webpass off')
st, _, _ = req('GET', '/api/boards')
check(st == 200, 'no password again: /api/boards is open')
print('BOARD AUTH OK' if not fails else 'BOARD AUTH: %d FAILED' % fails)
sys.exit(1 if fails else 0)
