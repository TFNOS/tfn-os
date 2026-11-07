#!/usr/bin/env python3
"""The test of the WiFi install and the store upload of sdk/tools/tfn_pkg.py (push() and publish()) against a FAKE
board: a small HTTP server on localhost that speaks POST /api/apps (op=put, op=commit) and POST /api/store/upload. No
real board and no network.

The rule: push() and publish() send a package only when it is complete (manifest.txt, and each file that it names
with the same SHA-256); else they stop before the first request. mk_app.py and tfn_app.py make a new package in a
stage folder (pkg_stage) and move it into the package folder only after the last step (pkg_commit).

The checks: a complete package (the order of the requests and the Content-Type of each file), a package with no
manifest.txt (the folder of a build that stopped), an app.elf or an icon.bin that does not match the manifest, a
missing icon.bin, a board that answers a wrong byte count, a board with an answer of an older firmware (no count),
publish() of a package that is not complete, and the stage folder (a build that stops keeps the old package; the
commit of a new package removes the old files that it does not have).

Usage: python tests/unit/test_pkg_push.py
"""
import contextlib
import hashlib
import http.server
import io
import json
import os
import shutil
import socket
import sys
import tempfile
import threading
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
import tfn_pkg  # noqa: E402

FAILS = []
CHECKS = [0]
APP = {'sym': 'app_tpush', 'name': 'TPUSH'}


def expect(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(what)
        print('FAIL', what)


# ---------------------------------------------------------------- a fake board
class FakeBoard:
    """The board side of POST /api/apps (op=put, op=commit) and of the store upload of a manager. count: the byte
    count that each put answer gives ('real', 'wrong' = 100 for app.elf, 'none' = an answer of an older firmware)."""

    def __init__(self):
        self.reqs = []
        self.count = 'real'
        self.folders = {}
        # (the IPv4 address of localhost: a client that tries IPv6 first waits about 2 s for each request on Windows)
        host = socket.gethostbyname('localhost')
        self.srv = http.server.ThreadingHTTPServer((host, 0), make_handler(self))
        self.ip = '%s:%d' % (host, self.srv.server_address[1])
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def reset(self, count='real'):
        self.reqs, self.count, self.folders = [], count, {}

    def put(self, q, body):
        tmp = self.folders.setdefault(q.get('place', 'board'), {})
        if q['file'] == 'manifest.txt':
            tmp.clear()
        elif 'manifest.txt' not in tmp:
            # (the board refuses a file before manifest.txt: main/tfn_web.c)
            return {'ok': False, 'msg': '%s: no manifest.txt came before it' % q['file']}
        tmp[q['file']] = body
        n = 100 if self.count == 'wrong' and q['file'] == 'app.elf' else len(body)
        msg = 'stored' if self.count == 'none' else '%s: %d bytes in 1 ms' % (q['file'], n)
        return {'ok': True, 'msg': msg}

    def commit(self, q):
        tmp = self.folders.get(q.get('place', 'board'), {})
        man = tmp.get('manifest.txt', b'').decode()
        if not man:
            return {'ok': False, 'msg': 'no manifest.txt came in /data/apps/.tmp_%s' % q['id']}
        want = dict(ln.split('=', 1) for ln in man.splitlines() if '=' in ln)
        if hashlib.sha256(tmp.get('app.elf', b'')).hexdigest() != want.get('elf_sha256'):
            return {'ok': False, 'msg': '%s: app.elf does not match the manifest' % q['id']}
        return {'ok': True, 'msg': '%s is installed' % want.get('name')}


def make_handler(board):
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_POST(self):
            u = urllib.parse.urlparse(self.path)
            q = dict(urllib.parse.parse_qsl(u.query))
            body = self.rfile.read(int(self.headers.get('Content-Length') or 0))
            board.reqs.append((u.path, q.get('op', ''), q.get('file', ''), self.headers.get('Content-Type', '')))
            if u.path == '/api/apps' and q.get('op') == 'put':
                j = board.put(q, body)
            elif u.path == '/api/apps' and q.get('op') == 'commit':
                j = board.commit(q)
            elif u.path == '/api/store/upload':
                j = {'ok': True, 'msg': 'stored'}
            else:
                j = {'ok': False, 'msg': 'unknown'}
            out = json.dumps(j).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(out)))
            self.end_headers()
            self.wfile.write(out)
    return H


# ---------------------------------------------------------------- the packages
def make_pkg(d, name, files=('manifest.txt', 'app.elf', 'icon.bin'), elf=b'\x7fELF' + b'A' * 5000,
             icon=b'I' * 700, man_elf=None, man_icon=None):
    """A package folder like the output of mk_app.py; man_elf / man_icon: the bytes that the manifest names (default:
    the bytes of the files)."""
    out = os.path.join(d, name)
    os.makedirs(out)
    me = man_elf if man_elf is not None else elf
    mi = man_icon if man_icon is not None else icon
    man = ('id=tpush\nname=TPUSH\nver=1.0.0\nkind=lcd349\nbytes=%d\nelf_sha256=%s\nicon_sha256=%s\n' %
           (len(me), hashlib.sha256(me).hexdigest(), hashlib.sha256(mi).hexdigest())).encode()
    data = {'manifest.txt': man, 'app.elf': elf, 'icon.bin': icon}
    for f in files:
        with open(os.path.join(out, f), 'wb') as fh:
            fh.write(data[f])
    return out


def run(fn, *args):
    """fn(*args) with its output kept: (the exit code or None, the output)."""
    buf = io.StringIO()
    code = None
    with contextlib.redirect_stdout(buf):
        try:
            fn(*args)
        except SystemExit as e:
            code = e.code if e.code is not None else 0
    return code, buf.getvalue()


def check_stage(d, board):
    """8. the stage folder of mk_app.py / tfn_app.py: a build that stops keeps the old package, and the commit of a new
    package puts its files in place and removes the old files that it does not have."""
    old = make_pkg(d, 'stage', files=('manifest.txt', 'app.elf', 'icon.bin'))
    work = tfn_pkg.pkg_stage(old)
    with open(os.path.join(work, 'app.elf'), 'wb') as fh:
        fh.write(b'\x7fELF' + b'N' * 4000)   # (a build that stopped after app.elf)
    board.reset()
    code, out = run(tfn_pkg.push, board.ip, old, APP, 'board')
    expect(code is None and len(board.reqs) == 4, 'a build that stopped keeps the old package (exit %s, %s)' %
           (code, board.reqs))
    work = tfn_pkg.pkg_stage(old)
    expect(not os.listdir(work), 'a new stage starts empty (%s)' % os.listdir(work))
    elf = b'\x7fELF' + b'M' * 3000   # (a new package with no icon)
    with open(os.path.join(work, 'app.elf'), 'wb') as fh:
        fh.write(elf)
    man = 'id=tpush\nname=TPUSH\nver=1.0.1\nkind=lcd349\nbytes=%d\nelf_sha256=%s\n' % (len(elf),
                                                                                     hashlib.sha256(elf).hexdigest())
    with open(os.path.join(work, 'manifest.txt'), 'wb') as fh:
        fh.write(man.encode())
    tfn_pkg.pkg_commit(work, old)
    with open(os.path.join(old, 'app.elf'), 'rb') as fh:
        expect(fh.read() == elf, 'the commit puts the new app.elf in place')
    expect(not os.path.exists(os.path.join(old, 'icon.bin')), 'the commit removes the old icon.bin')
    expect(not os.path.exists(work), 'the stage folder goes after the commit')
    code, out = run(tfn_pkg.package_files, old)
    expect(code is None, 'the new package is complete (exit %s: %s)' % (code, out.strip()))


def main():
    board = FakeBoard()
    d = tempfile.mkdtemp(prefix='tfn_pkg_push_')
    try:
        # 1. a complete package: manifest.txt first, each file as application/octet-stream, then the commit
        good = make_pkg(d, 'good')
        board.reset()
        code, out = run(tfn_pkg.push, board.ip, good, APP, 'card')
        expect(code is None, 'a complete package installs (exit %s: %s)' % (code, out.strip()))
        expect([r[1:3] for r in board.reqs] == [('put', 'manifest.txt'), ('put', 'app.elf'), ('put', 'icon.bin'),
                                                ('commit', '')], 'the order of the requests: %s' % board.reqs)
        types = set(r[3] for r in board.reqs if r[1] == 'put')
        expect(types == {'application/octet-stream'}, 'each file goes as application/octet-stream, not %s' % types)
        # 2. the folder of a build that stopped after app.elf (no manifest.txt, no icon.bin): nothing goes
        part = make_pkg(d, 'partial', files=('app.elf',))
        board.reset()
        code, out = run(tfn_pkg.push, board.ip, part, APP, 'card')
        expect(code not in (None, 0), 'a package with app.elf only stops (exit %s)' % code)
        expect(not board.reqs, 'a package with app.elf only sends no request (sent %s)' % board.reqs)
        expect('manifest.txt' in out and 'not complete' in out, 'the message names manifest.txt: %s' % out.strip())
        # 3. an app.elf of another build than the manifest
        stale = make_pkg(d, 'stale', man_elf=b'\x7fELF' + b'B' * 5000)
        board.reset()
        code, out = run(tfn_pkg.push, board.ip, stale, APP, 'board')
        expect(code not in (None, 0) and not board.reqs, 'a stale app.elf sends nothing (exit %s, %s)' %
               (code, board.reqs))
        expect('app.elf' in out and 'manifest' in out, 'the message names app.elf: %s' % out.strip())
        # 4. an icon.bin that the manifest names but that is not there, or that is another one
        noicon = make_pkg(d, 'noicon', files=('manifest.txt', 'app.elf'))
        board.reset()
        code, out = run(tfn_pkg.push, board.ip, noicon, APP, 'board')
        expect(code not in (None, 0) and not board.reqs, 'a missing icon.bin sends nothing (exit %s)' % code)
        expect('icon.bin' in out, 'the message names icon.bin: %s' % out.strip())
        badicon = make_pkg(d, 'badicon', man_icon=b'J' * 700)
        board.reset()
        code, out = run(tfn_pkg.push, board.ip, badicon, APP, 'board')
        expect(code not in (None, 0) and not board.reqs, 'another icon.bin sends nothing (exit %s)' % code)
        # 5. a board that kept fewer bytes than it got: no commit
        board.reset('wrong')
        code, out = run(tfn_pkg.push, board.ip, good, APP, 'board')
        expect(code not in (None, 0), 'a wrong byte count stops the push (exit %s)' % code)
        expect(not [r for r in board.reqs if r[1] == 'commit'], 'no commit after a wrong byte count')
        expect('app.elf' in out and '100' in out, 'the message names the file and the count: %s' % out.strip())
        # 6. the answer of an older firmware (no byte count): the push goes on
        board.reset('none')
        code, out = run(tfn_pkg.push, board.ip, good, APP, 'board')
        expect(code is None, 'an answer with no byte count is not a fault (exit %s: %s)' % (code, out.strip()))
        # 7. publish(): a package that is not complete never goes to the store
        board.reset()
        code, out = run(tfn_pkg.publish, 'http://' + board.ip, part, 'tpush', 'lcd349')
        expect(code not in (None, 0) and not board.reqs, 'publish of app.elf only sends nothing (exit %s, %s)' %
               (code, board.reqs))
        board.reset()
        code, out = run(tfn_pkg.publish, 'http://' + board.ip, good, 'tpush', 'lcd349')
        expect(code is None and len(board.reqs) == 3, 'publish of a complete package sends 3 files (%s)' % board.reqs)
        check_stage(d, board)
    finally:
        board.srv.shutdown()
        shutil.rmtree(d, ignore_errors=True)
    if FAILS:
        print('PKG PUSH PY FAILED: %d of %d checks' % (len(FAILS), CHECKS[0]))
        return 1
    print('PKG PUSH PY OK (%d checks)' % CHECKS[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
