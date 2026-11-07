#!/usr/bin/env python3
"""The test of tests/hil/hil.py with fakes: a fake serial board (the console lines of TFN OS) and a fake HTTP board and
manager (/api/devices, /api/boards, /api/id, /api/ota/queue). No real port, no board, no network.

Usage: python tests/hil/test_hil.py   (no pyserial is necessary; the last line is "HIL TEST: OK" or "HIL TEST: FAULT")
"""
import contextlib
import http.server
import io
import json
import os
import re
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hil  # noqa: E402

hil.SCALE = 0.02


# ---------------------------------------------------------------- the fake serial board

class FakePort:
    def __init__(self, device, serial_number, description='USB JTAG/serial debug unit (Interface 0)'):
        self.device, self.serial_number, self.description = device, serial_number, description


class Air:
    """The radio between the fake boards: two boards in the lobby of the same game that are friends pair."""

    def __init__(self):
        self.boards = []

    def check(self):
        lob = [b for b in self.boards if b.lobby and not b.paired]
        if len(lob) == 2:
            a, b = lob
            if a.lobby == b.lobby and b.name in a.friends and a.name in b.friends:
                for x, role in ((a, 'HOST'), (b, 'CLIENT')):
                    x.paired = True
                    x.say('I (9) %s: 2 BOARDS: %s, the other board 00:00:01, channel 6' % (x.app.lower(), role))


class FakeBoard:
    GAME_XY = {'PONG': (130, 143), 'ZAP': (425, 76), 'TENNIS': (578, 58)}

    def __init__(self, name, mac, port, apps=(), faults=None, mem=57000, air=None, friends=()):
        self.name, self.mac, self.port = name, mac, port
        self.apps = {a.upper() for a in apps}
        self.faults = faults or {}
        self.mem = mem
        self.air = air
        self.friends = list(friends)
        self.cmds = []
        self.out = []
        self.quiet = 0
        self.app = 'MENU'
        self.lobby = None
        self.paired = False
        self.sent = 100
        self.ser = None
        if air:
            air.boards.append(self)

    def say(self, line):
        if self.ser:
            self.ser.feed((line + '\n').encode())

    def others(self):
        return [b for b in (self.air.boards if self.air else []) if b is not self]

    def handle(self, c):
        self.cmds.append(c)
        w = c.split()
        if not w:
            return
        if w[0] == 'quiet':
            self.quiet = int(w[1])
        elif w[0] == 'open':
            name = c[5:].strip()
            if name.upper() == 'MENU':
                self.app, self.lobby, self.paired = 'MENU', None, False
                self.say('I (1) console: open %s: ok' % name)
            elif name.upper() in self.apps:
                self.app, self.lobby, self.paired = name.upper(), None, False
                self.say('I (1) console: open %s: ok' % name)
                self.say('I (1) main: open %s' % name.upper())
                if name.upper() in self.faults:
                    self.say('E (2) tfn_app: %s' % self.faults[name.upper()])
            else:
                self.say('I (1) console: open %s: no such app' % name)
        elif w[0] == 'mem':
            self.say('I (3) console: mem: internal %d free (%d min, 30000 block), PSRAM 6500000 free' %
                     (self.mem, self.mem - 9000))
        elif w[0] == 'friends':
            if len(w) == 3 and w[1] == 'rm':
                self.friends.pop(int(w[2]))
            near = [b.name for b in self.others() if b.lobby and b.name not in self.friends]
            self.say('I (4) console: friends: this board %s, %d friends, %d near' % (self.name, len(self.friends),
                                                                                      len(near)))
            for i, f in enumerate(self.friends):
                self.say('I (4) console: friend %d %s 5C48' % (i, f))
            for n in near:
                self.say('I (4) console: new board near: %s' % n)
        elif w[0] == 'ltap':
            x, y = int(w[1]), int(w[2])
            if self.app in self.GAME_XY and self.GAME_XY[self.app] == (x, y) and not self.lobby:
                self.lobby = self.app
                self.say('I (5) link: on, channel 6')
                self.air.check()
            elif self.lobby and (x, y) == hil.badge_xy(0, 1):
                near = [b.name for b in self.others() if b.lobby and b.name not in self.friends]
                if near:
                    self.friends.append(near[0])
                    self.air.check()
        elif w[0] == 'wifistat':
            if self.paired:
                self.sent += 300
                if self.app in ('ZAP', 'TENNIS'):
                    self.say('I (6) net2: 2 BOARDS (H): in 5 s 240 packets from the other board (the longest gap '
                             '40 ms), 250 sent, 0 refused')
            self.say('W (6) link: diag (console): link %s, channel 6, sent %d, refused 0, ps 0, AP rssi -50, '
                     'internal RAM 50000 free (low 40000, block 30000)' % ('on' if self.lobby else 'off', self.sent))


class FakeSerial:
    """A closed port object as serial.Serial(); open() finds the fake board of the port."""
    boards = {}
    opened = []

    def __init__(self):
        self.port = None
        self.baudrate = 0
        self.timeout = 0.05
        self.dtr = True
        self.rts = True
        self.buf = b''
        self.cv = threading.Condition()
        self.is_open = False
        self.line = b''

    def open(self):
        FakeSerial.opened.append((self.port, self.dtr, self.rts))
        self.board = FakeSerial.boards[self.port]
        self.board.ser = self
        self.is_open = True

    def feed(self, data):
        with self.cv:
            self.buf += data
            self.cv.notify_all()

    def read(self, n):
        with self.cv:
            if not self.buf:
                self.cv.wait(self.timeout)
            d, self.buf = self.buf[:n], self.buf[n:]
            return d

    def write(self, data):
        if not self.is_open:
            raise OSError('closed')
        self.line += data
        while b'\n' in self.line:
            c, self.line = self.line.split(b'\n', 1)
            self.board.handle(c.decode())
        return len(data)

    def close(self):
        self.is_open = False


# ---------------------------------------------------------------- the fake HTTP board and manager

class Web:
    """One HTTP server: the manager at /api/devices and /api/ota/queue, each fake board at /b/<mac>/api/..."""

    def __init__(self):
        self.devs = {}       # mac -> manager row
        self.state = {}      # mac -> the "me" fields of /api/boards
        self.queued = []
        self.update = {}     # mac -> the steps of a running update
        web = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def answer(self, code, obj):
                b = json.dumps(obj).encode()
                self.send_response(code)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(b)))
                self.end_headers()
                self.wfile.write(b)

            def do_GET(self):
                if self.path == '/api/devices':
                    return self.answer(200, list(web.devs.values()))
                m = re.match(r'/b/([0-9A-F]{12})/api/(boards|id)$', self.path)
                if not m:
                    return self.answer(404, {})
                mac = m.group(1)
                if m.group(2) == 'id':
                    return self.answer(200, {'id': 'TFN-ID-%s' % mac[-4:], 'alg': 'p256'})
                steps = web.update.get(mac)
                if steps:
                    st = steps.pop(0)
                    if st is None:
                        return self.answer(503, {})
                    web.state[mac].update(st)
                    web.devs[mac]['ver'] = web.state[mac]['ver']
                return self.answer(200, {'me': dict(web.state[mac], name=web.devs[mac]['name']), 'lan': []})

            def do_POST(self):
                n = int(self.headers.get('Content-Length') or 0)
                body = json.loads(self.rfile.read(n) or b'{}')
                if self.path == '/api/ota/queue':
                    web.queued.append(body)
                    for mac in body['macs']:
                        web.update[mac] = [None, dict(ver=body['version'], pend=True, self='wait', up=5),
                                           dict(pend=False, self='pass', sig='enforce', own_sig='ok', minsv=1, up=40),
                                           dict(up=60), dict(up=80)]
                    return self.answer(200, {'ok': True, 'added': [{'mac': m, 'ok': True} for m in body['macs']]})
                return self.answer(404, {})

        self.srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
        self.url = 'http://127.0.0.1:%d' % self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def add(web, name, mac, ver, online=True, **me):  # (web, not self: "self" is a field of /api/boards)
        web.devs[mac] = {'mac': mac, 'name': name, 'ver': ver, 'online': online,
                         'ip': '127.0.0.1:%d/b/%s' % (web.srv.server_address[1], mac)}
        f = dict(ver=ver, slot='ota_0', sig='enforce', minsv=1, own_sv=1, own_key='7b9a', own_sig='ok', pend=False,
                 bad='', self='pass', up=500)
        f.update(me)
        web.state[mac] = f

    def close(self):
        self.srv.shutdown()
        self.srv.server_close()


# ---------------------------------------------------------------- the tests

MAC_A, MAC_B, MAC_C = '02AA00000001', '02AA00000002', '02AA00000003'


class HilTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        FakeSerial.boards = {}
        FakeSerial.opened = []
        self.ports = []
        hil.COMPORTS = lambda: list(self.ports)
        hil.SERIAL = FakeSerial
        self.web = Web()

    def tearDown(self):
        self.web.close()
        self.tmp.cleanup()

    def cfg(self, boards, deny=()):
        p = os.path.join(self.tmp.name, 'boards.local.json')
        with open(p, 'w', encoding='utf-8') as h:
            json.dump({'manager': self.web.url, 'deny_ports': list(deny), 'boards': boards}, h)
        return p

    def plug(self, board, serial=None, desc=None):
        FakeSerial.boards[board.port] = board
        args = [board.port, serial if serial is not None else ':'.join(board.mac[i:i + 2] for i in range(0, 12, 2))]
        self.ports.append(FakePort(*(args + ([desc] if desc else []))))

    def run_main(self, path, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = hil.main(['--boards', path, '--out', os.path.join(self.tmp.name, 'out')] + list(argv))
        return code, out.getvalue()

    def summary(self):
        with open(os.path.join(self.tmp.name, 'out', 'summary.json'), encoding='utf-8') as h:
            return json.load(h)

    def check_quiet(self, board):
        self.assertEqual(board.cmds[0], 'quiet 1', 'the first command must be quiet 1')
        self.assertEqual(board.cmds[-1], 'quiet 0', 'the last command must be quiet 0')
        self.assertEqual(board.quiet, 0)

    # -- the safety rules

    def test_wrong_mac_stops_before_open(self):
        a = FakeBoard('A', MAC_A, 'COM_A', apps=['DICE'])
        self.plug(a, serial='02:AA:00:00:00:99')
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}])
        code, out = self.run_main(path, 'open-all', 'DICE')
        self.assertEqual(code, 2)
        self.assertIn('STOP', out)
        self.assertEqual(FakeSerial.opened, [], 'the port must stay closed')

    def test_no_serial_number_stops(self):
        a = FakeBoard('A', MAC_A, 'COM_A')
        self.plug(a, serial='')
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}])
        self.assertEqual(self.run_main(path, 'mem', '--minutes', '0.01')[0], 2)
        self.assertEqual(FakeSerial.opened, [])

    def test_jtag_part_stops(self):
        a = FakeBoard('A', MAC_A, 'COM_A')
        self.plug(a, desc='USB JTAG/serial debug unit (Interface 2)')
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}])
        self.assertEqual(self.run_main(path, 'mem', '--minutes', '0.01')[0], 2)
        self.assertEqual(FakeSerial.opened, [])

    def test_port_not_on_usb_stops(self):
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}])
        self.assertEqual(self.run_main(path, 'mem', '--minutes', '0.01')[0], 2)

    def test_deny_list(self):
        a = FakeBoard('A', MAC_A, 'COM_A')
        self.plug(a)
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}],
                        deny=['com_a'])
        code, out = self.run_main(path, 'mem', '--minutes', '0.01')
        self.assertEqual(code, 2)
        self.assertIn('deny_ports', out)
        self.assertEqual(FakeSerial.opened, [])
        # the check of the port also refuses a denied port (a board object that did not come from the file)
        cfg = hil.Config(self.cfg([{'name': 'B', 'mac': MAC_B, 'kind': 'lcd349', 'role': 'net'}], deny=['COM_A']))
        b = hil.BoardCfg({'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349'}, 0)
        with self.assertRaises(hil.Stop):
            hil.check_port(cfg, b)
        self.assertEqual(FakeSerial.opened, [])

    def test_ports_lists_and_opens_nothing(self):
        a = FakeBoard('A', MAC_A, 'COM_A')
        self.plug(a, serial='02AA00000099')
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}],
                        deny=['COM_Z'])
        code, out = self.run_main(path, 'ports')
        self.assertEqual(code, 0)
        self.assertIn('MAC DIFFERS', out)
        self.assertIn('COM_Z', out)
        self.assertEqual(FakeSerial.opened, [])

    def test_no_flash_in_the_tool(self):
        with open(os.path.join(HERE, 'hil.py'), encoding='utf-8') as h:
            src = h.read()
        code = '\n'.join(ln for ln in src.splitlines() if not ln.lstrip().startswith('#'))
        code = re.sub(r'""".*?"""', '', code, flags=re.S)
        for word in ('esptool', 'write_flash', 'erase_flash', 'erase_region', "'/api/ota'", "/api/ota/upload",
                     "/api/ota/part", 'subprocess'):
            self.assertNotIn(word, code)

    def test_bad_local_file(self):
        p = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': '12', 'kind': 'lcd349'}])
        self.assertEqual(self.run_main(p, 'ports')[0], 2)
        self.assertEqual(self.run_main(os.path.join(self.tmp.name, 'none.json'), 'ports')[0], 2)

    # -- (2) mem

    def test_mem(self):
        a = FakeBoard('A', MAC_A, 'COM_A', mem=57000)
        c = FakeBoard('C', MAC_C, 'COM_C', mem=30000)
        self.plug(a)
        self.plug(c)
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'},
                         {'name': 'C', 'port': 'COM_C', 'mac': MAC_C, 'kind': 'cores3', 'role': 'console'}])
        code, out = self.run_main(path, 'mem', '--minutes', '0.2')
        self.assertEqual(code, 1)
        res = {r['board']: r for r in self.summary()}
        self.assertEqual(res['A']['status'], 'OK')
        self.assertIn('internal 57000 B free', res['A']['note'])
        self.assertEqual(res['C']['status'], 'FAULT')
        for port, dtr, rts in FakeSerial.opened:
            self.assertEqual((dtr, rts), (False, False), 'DTR and RTS must be low before the open')
        self.check_quiet(a)
        self.check_quiet(c)

    # -- (3) open-all

    def test_open_all(self):
        a = FakeBoard('A', MAC_A, 'COM_A', apps=['DICE', 'PONG', 'MIC', 'CLOCK'],
                      faults={'CLOCK': 'CLOCK NEEDS A NEWER FIRMWARE'})
        self.plug(a)
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'console'}])
        code, out = self.run_main(path, 'open-all', 'DICE', 'PONG', 'MIC', 'CLOCK', 'WIKI')
        self.assertEqual(code, 1)
        r = self.summary()[0]
        self.assertEqual(r['status'], 'FAULT')
        self.assertIn('2 opened with no fault, 1 faults, 1 not on the board', r['note'])
        self.assertIn('CLOCK', r['note'])
        self.assertNotIn('open MIC', a.cmds)
        self.check_quiet(a)

    def test_open_all_from_apps_json(self):
        # (the private tree and the public tree have other app lists: compare with main/apps.json of this tree)
        names = hil.app_names()
        root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        with open(os.path.join(root, 'main', 'apps.json'), encoding='utf-8') as f:
            want = {a['name'] for a in json.load(f)['apps']}
        self.assertTrue(names)
        self.assertTrue(set(names) <= want, sorted(set(names) - want))

    # -- (4) pair

    def test_pair(self):
        air = Air()
        a = FakeBoard('A', MAC_A, 'COM_A', apps=['PONG', 'ZAP'], air=air, friends=['B'])
        b = FakeBoard('B', MAC_B, 'COM_B', apps=['PONG', 'ZAP'], air=air, friends=[])
        self.plug(a)
        self.plug(b)
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'pair'},
                         {'name': 'B', 'port': 'COM_B', 'mac': MAC_B, 'kind': 'lcd349', 'role': 'pair'}])
        code, out = self.run_main(path, 'pair', 'pong', 'zap', 'tennis', '--secs', '5')
        res = {r['board']: r for r in self.summary()}
        self.assertEqual(res['pong']['status'], 'OK', out)
        self.assertEqual(res['zap']['status'], 'OK', out)
        self.assertIn('from the other board', res['zap']['note'])
        self.assertEqual(res['tennis']['status'], 'SKIP')
        self.assertEqual(code, 0)
        self.assertIn('restore: A removed from the friends of B (ok)', out)
        self.assertEqual(b.friends, [], 'the friend that the tool added must go again')
        self.assertEqual(a.friends, ['B'], 'an old friend stays')
        self.check_quiet(a)
        self.check_quiet(b)

    def test_pair_no_link(self):
        air = Air()
        a = FakeBoard('A', MAC_A, 'COM_A', apps=['PONG'], air=air, friends=['B'])
        b = FakeBoard('B', MAC_B, 'COM_B', apps=['PONG'], air=air, friends=['A'])
        self.plug(a)
        self.plug(b)
        air.check = lambda: None  # (no radio: no pairing)
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'pair'},
                         {'name': 'B', 'port': 'COM_B', 'mac': MAC_B, 'kind': 'lcd349', 'role': 'pair'}])
        code, out = self.run_main(path, 'pair', 'pong')
        self.assertEqual(code, 1)
        self.assertIn('NO PAIRING', self.summary()[0]['note'])
        self.check_quiet(a)
        self.check_quiet(b)

    # -- (1) versions

    def test_versions(self):
        self.web.add('A', MAC_A, '1.0.102')
        self.web.add('B', MAC_B, '1.0.102', pend=True, self='wait')
        self.web.add('C', MAC_C, '1.0.98', online=False)
        path = self.cfg([{'name': n, 'mac': m, 'kind': 'lcd349', 'role': 'net'}
                         for n, m in (('A', MAC_A), ('B', MAC_B), ('C', MAC_C))])
        code, out = self.run_main(path, 'versions', '--want', '1.0.102', '--minsv', '1')
        self.assertEqual(code, 1)
        res = {r['board']: r for r in self.summary()}
        self.assertEqual(res['A']['status'], 'OK')
        self.assertIn('ver 1.0.102 sig enforce minsv 1 own_sig ok pend no self pass', res['A']['note'])
        self.assertEqual(res['B']['status'], 'FAULT')
        self.assertIn('pend', res['B']['note'])
        self.assertEqual(res['C']['status'], 'OFFLINE')
        self.assertNotIn('127.0.0.1', out, 'no address in the output')

    def test_versions_own_sig_fault(self):
        self.web.add('A', MAC_A, '1.0.102', own_sig='sig_bad')
        path = self.cfg([{'name': 'A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'net'}])
        self.assertEqual(self.run_main(path, 'versions')[0], 1)
        self.assertIn('own_sig', self.summary()[0]['note'])

    # -- (5) old-to-new

    def test_old_to_new(self):
        self.web.add('A', MAC_A, '1.0.60', sig='off', minsv=0, own_sig='', self='')
        path = self.cfg([{'name': 'A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'net'}])
        code, out = self.run_main(path, 'old-to-new', '--board', 'A', '--from', '1.0.60', '--to', '1.0.102')
        self.assertEqual(code, 0)
        self.assertEqual(self.summary()[0]['status'], 'SKIP')
        self.assertEqual(self.web.queued, [], 'a dry run puts nothing into the queue')
        code, out = self.run_main(path, 'old-to-new', '--board', 'A', '--from', '1.0.60', '--to', '1.0.102',
                                  '--expect-sig', 'enforce', '--minsv', '1', '--minutes', '10', '--go')
        self.assertEqual(code, 0, out)
        self.assertEqual(self.web.queued, [{'version': '1.0.102', 'macs': [MAC_A], 'mode': 'now'}])
        r = self.summary()[0]
        self.assertEqual(r['status'], 'OK')
        self.assertIn('1.0.60 -> 1.0.102', r['note'])
        self.assertIn('ID TFN-ID-0001', r['note'])
        self.assertIn('restarts seen', r['note'])
        self.assertIn('no answer', out)

    def test_old_to_new_wrong_start(self):
        self.web.add('A', MAC_A, '1.0.101')
        path = self.cfg([{'name': 'A', 'mac': MAC_A, 'kind': 'lcd349', 'role': 'net'}])
        code, out = self.run_main(path, 'old-to-new', '--board', 'A', '--from', '1.0.60', '--to', '1.0.102', '--go')
        self.assertEqual(code, 2)
        self.assertIn('does not flash', out)
        self.assertEqual(self.web.queued, [])

    # -- all

    def test_all(self):
        air = Air()
        a = FakeBoard('A', MAC_A, 'COM_A', apps=['DICE', 'PONG', 'ZAP', 'TENNIS'], air=air, friends=['B'])
        b = FakeBoard('B', MAC_B, 'COM_B', apps=['PONG', 'ZAP', 'TENNIS'], air=air, friends=['A'])
        self.plug(a)
        self.plug(b)
        self.web.add('A', MAC_A, '1.0.102')
        self.web.add('B', MAC_B, '1.0.102')
        path = self.cfg([{'name': 'A', 'port': 'COM_A', 'mac': MAC_A, 'kind': 'lcd349', 'role': ['console', 'pair']},
                         {'name': 'B', 'port': 'COM_B', 'mac': MAC_B, 'kind': 'lcd349', 'role': ['pair']}])
        hil_names = hil.app_names
        hil.app_names = lambda: ['DICE', 'PONG', 'MIC']
        try:
            code, out = self.run_main(path, 'all', '--want', '1.0.102', '--minutes', '0.1', '--secs', '5')
        finally:
            hil.app_names = hil_names
        self.assertEqual(code, 0, out)
        checks = [(r['check'], r['board'], r['status']) for r in self.summary()]
        self.assertEqual(checks, [('versions', 'A', 'OK'), ('versions', 'B', 'OK'), ('mem', 'A', 'OK'),
                                  ('open-all', 'A', 'OK'), ('pair', 'pong', 'OK'), ('pair', 'zap', 'OK'),
                                  ('pair', 'tennis', 'OK')])
        self.assertIn('HIL: OK', out)
        self.assertEqual((a.quiet, b.quiet), (0, 0))


if __name__ == '__main__':
    t0 = time.time()
    r = unittest.main(exit=False, verbosity=1).result
    ok = r.wasSuccessful()
    print('HIL TEST: %s (%d tests, %.1f s)' % ('OK' if ok else 'FAULT', r.testsRun, time.time() - t0))
    sys.exit(0 if ok else 1)
