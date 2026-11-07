#!/usr/bin/env python3
"""The hardware-in-the-loop (HIL) runner of TFN OS: the board checks after a release, on the PC that has the boards.

  python tests/hil/hil.py ports                       the boards of the local file and the USB ports (opens no port)
  python tests/hil/hil.py versions [--want VER] [--minsv N]
                                                      the version and the signed-OTA fields of each board: the manager
                                                      /api/devices and /api/boards of each board (ver, sig, minsv,
                                                      own_sig, pend, self)
  python tests/hil/hil.py mem [--board NAME] [--minutes N] [--min-kb K]
                                                      the free internal RAM at the menu after N minutes (console "mem")
  python tests/hil/hil.py open-all [--board NAME] [APP ...]
                                                      each app of main/apps.json that the board has opens with no fault
                                                      (MIC is skipped: it takes the USB for its sound device)
  python tests/hil/hil.py pair [--a NAME --b NAME] [GAME ...] [--secs N] [--keep-friends] [--shots]
                                                      the 2 BOARDS link test (games: pong zap tennis; default pong zap)
  python tests/hil/hil.py old-to-new --board NAME --from VER --to VER [--minutes N] [--expect-sig MODE]
                                     [--minsv N] [--go]
                                                      a board that runs the old version gets the new version through
                                                      the update queue of the manager; the tool watches it to the end
  python tests/hil/hil.py all [--want VER] [--minutes N]
                                                      versions, then mem and open-all on each "console" board (the
                                                      boards at the same time), then pair on the two "pair" boards

The boards: tests/hil/boards.local.json (git ignores it). tests/hil/boards.example.json shows the form. --boards gives
another file. The logs and the pictures go into tests/host/hil/<time>/ (git ignores it), or into --out.

The safety rules (always on):
  - Before the tool opens a port, it reads the USB serial number of the port from the system (for the USB of an ESP32
    chip, the serial number is the MAC). When it is not the MAC of the board in the local file, the tool stops.
  - The tool never opens a port of the list "deny_ports" of the local file.
  - The tool opens a port with DTR and RTS low: no reset, no download mode.
  - Each console gets "quiet 1" at the start. At the end (also after a fault) it gets the menu and "quiet 0".
  - No flash, no erase, no upload of an image to a board. old-to-new needs a board that already runs the old version.
    It only puts the update into the queue of the manager (as the release flow does), and only with --go.

The exit code: 0 = all checks OK, 1 = a fault, 2 = a stop (a safety rule, a bad option, a bad local file).
"""
import argparse
import base64
import contextlib
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
LOCAL_FILE = os.path.join(HERE, 'boards.local.json')
ROLES = ('console', 'pair', 'net')

# The tests make the pauses short. Each pause and each wait of the tool goes through pause() and Console.wait().
SCALE = 1.0


def pause(secs):
    time.sleep(secs * SCALE)


def _comports():
    from serial.tools import list_ports
    return list(list_ports.comports())


def _serial():
    import serial
    return serial.Serial()


# The tests put fakes here: COMPORTS() gives the ports of the system, SERIAL() gives a closed port object.
COMPORTS = _comports
SERIAL = _serial

PRINT_LOCK = threading.Lock()
IP_RX = re.compile(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b')


def mask(text):
    """The text with no IP address (the output can go into a report)."""
    return IP_RX.sub('<ip>', str(text))


def say(text):
    with PRINT_LOCK:
        print(mask(text).encode('ascii', 'backslashreplace').decode('ascii'), flush=True)


def norm_mac(text):
    return re.sub(r'[^0-9A-Fa-f]', '', text or '').upper()


class Stop(Exception):
    """A safety rule or a bad setup: the tool stops (exit 2)."""


class ConsoleGone(Exception):
    """The serial port of a board stopped (the board restarted, or the cable is out)."""


# ---------------------------------------------------------------- the local file

class BoardCfg:
    def __init__(self, d, i):
        self.name = str(d.get('name') or '').strip()
        self.mac = norm_mac(d.get('mac'))
        self.port = str(d.get('port') or '').strip()
        self.kind = str(d.get('kind') or '').strip()
        role = d.get('role') or []
        self.roles = [role] if isinstance(role, str) else list(role)
        self.min_internal = int(d.get('min_internal') or 0)
        self.password = str(d.get('web_password') or '')
        if not self.name:
            raise Stop('board %d of the local file has no "name"' % i)
        if len(self.mac) != 12:
            raise Stop('board %s: "mac" must have 12 hex digits' % self.name)
        if not self.kind:
            raise Stop('board %s has no "kind"' % self.name)
        bad = [r for r in self.roles if r not in ROLES]
        if bad:
            raise Stop('board %s: unknown role %s (the roles: %s)' % (self.name, bad, ', '.join(ROLES)))
        if ('console' in self.roles or 'pair' in self.roles) and not self.port:
            raise Stop('board %s: a "console" or "pair" board needs a "port"' % self.name)


class Config:
    def __init__(self, path):
        if not os.path.exists(path):
            raise Stop('no local file %s: copy tests/hil/boards.example.json to it and put your boards in it' % path)
        try:
            with open(path, encoding='utf-8-sig') as h:
                d = json.load(h)
        except ValueError as e:
            raise Stop('%s is not JSON: %s' % (path, e))
        self.path = path
        self.manager = str(d.get('manager') or '').rstrip('/')
        self.deny = {str(p).strip().upper() for p in d.get('deny_ports') or []}
        self.boards = [BoardCfg(b, i) for i, b in enumerate(d.get('boards') or [])]
        if not self.boards:
            raise Stop('%s has no boards' % path)
        names = [b.name.upper() for b in self.boards]
        if len(set(names)) != len(names):
            raise Stop('two boards of %s have the same name' % path)
        for b in self.boards:
            if b.port and b.port.upper() in self.deny:
                raise Stop('board %s: its port is in "deny_ports"' % b.name)

    def find(self, name):
        key = name.upper()
        for b in self.boards:
            if b.name.upper() == key or b.mac == norm_mac(name):
                return b
        raise Stop('no board %s in %s' % (name, self.path))

    def with_role(self, role):
        return [b for b in self.boards if role in b.roles]


def manager_url(opt, cfg):
    """The URL of the manager: --manager, the local file, TFN_MANAGER, the store setup."""
    for u in (opt, cfg.manager, os.environ.get('TFN_MANAGER', '')):
        if u:
            return u.rstrip('/')
    try:
        sys.path.insert(0, os.path.join(ROOT, 'sdk', 'tools'))
        import store_conf
        u = store_conf.manager_url()
        if u:
            return u.rstrip('/')
    except Exception:
        pass
    return ''


# ---------------------------------------------------------------- the serial console

def check_port(cfg, b):
    """The safety check before the port of board b opens: not denied, on USB now, the console part, the right MAC."""
    if not b.port:
        raise Stop('%s: no port in the local file' % b.name)
    if b.port.upper() in cfg.deny:
        raise Stop('%s: the port is in "deny_ports": the tool never opens it' % b.name)
    p = next((p for p in COMPORTS() if (p.device or '').upper() == b.port.upper()), None)
    if p is None:
        raise Stop('%s: port %s is not on USB now' % (b.name, b.port))
    if 'Interface 2' in (p.description or ''):
        raise Stop('%s: port %s is the JTAG part of the USB, not the console' % (b.name, b.port))
    got = norm_mac(p.serial_number)
    if got != b.mac:
        raise Stop('%s: port %s has the USB serial number "%s", not the MAC %s of the local file: stop' %
                   (b.name, b.port, got or 'none', b.mac))


class Console:
    """The console of one board over its USB port. A reader thread keeps all lines (with the PC time) and writes them to
    a log file. The WiFi name is never kept."""

    def __init__(self, cfg, b, log_path):
        check_port(cfg, b)
        self.b, self.name, self.port = b, b.name, b.port
        s = SERIAL()
        s.port, s.baudrate, s.timeout = b.port, 115200, 0.05
        s.dtr, s.rts = False, False  # (set before open: no reset, no download mode)
        s.open()
        self.s = s
        self.lines = []          # (pc time, text)
        self.shotbuf = []        # the base64 lines of a picture (not in lines, not in the log)
        self.lock = threading.Lock()
        self.log = open(log_path, 'w', encoding='utf-8')
        self.run = True
        self.gone = False
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()

    def _reader(self):
        buf = b''
        while self.run:
            try:
                d = self.s.read(65536)
            except Exception:
                self.gone = True
                break
            if not d:
                continue
            buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                if line.startswith(b'#S'):
                    self.shotbuf.append(line.decode('ascii', 'replace').rstrip())
                    continue
                txt = re.sub(r'\x1b\[[0-9;]*m', '', line.decode('utf-8', 'replace')).rstrip()
                txt = re.sub(r'network [^,]*,', 'network <hidden>,', txt)
                txt = re.sub(r'(ssid|SSID)[ =:]+\S+', r'\1 <hidden>', txt)
                t = time.time()
                with self.lock:
                    self.lines.append((t, txt))
                self.log.write('%.3f %s\n' % (t, txt))
                self.log.flush()

    def cmd(self, c, wait=0.15):
        try:
            self.s.write((c + '\n').encode())
        except Exception as e:
            self.gone = True
            raise ConsoleGone('%s: the console stopped (%s)' % (self.name, e))
        pause(wait)

    def mark(self):
        with self.lock:
            return len(self.lines)

    def since(self, mark):
        with self.lock:
            return [ln for _, ln in self.lines[mark:]]

    def wait(self, pattern, timeout=5.0, mark=0):
        """The first line after mark that matches; None after the timeout."""
        end = time.time() + max(timeout * SCALE, min(timeout, 0.5))  # (a short SCALE keeps 0.5 s for the reader)
        rx = re.compile(pattern)
        while True:
            for ln in self.since(mark):
                if rx.search(ln):
                    return ln
            if time.time() >= end:
                return None
            time.sleep(0.02)

    def grep(self, pattern, mark=0):
        rx = re.compile(pattern)
        return [ln for ln in self.since(mark) if rx.search(ln)]

    def first_time(self, pattern, mark):
        rx = re.compile(pattern)
        with self.lock:
            for t, ln in self.lines[mark:]:
                if rx.search(ln):
                    return t
        return None

    def shot(self, path, timeout=8.0):
        """A picture of the screen (the console "shot": base64 lines of the RGB565 frame); needs Pillow."""
        for _ in range(3):
            try:
                if self._shot(path, timeout):
                    return True
            except ConsoleGone:
                raise
            except Exception:
                pass
            pause(0.2)
        return False

    def _shot(self, path, timeout):
        from PIL import Image
        m = self.mark()
        self.shotbuf = []
        self.cmd('shot', 0.05)
        if not self.wait(r'^SHOT (END|FAILED)', timeout, m):
            return False
        head = [ln for ln in self.since(m) if ln.startswith('SHOT ') and ln[5:6].isdigit()]
        if not head:
            return False
        w, h = map(int, head[0].split()[1:3])
        body = [ln[2:] for ln in self.shotbuf]
        self.shotbuf = []
        if any(len(x) != 1024 for x in body[:-1]) or sum(len(x) for x in body) != (w * h * 2 + 2) // 3 * 4:
            return False
        data = base64.b64decode(''.join(body))
        rgb = bytearray(w * h * 3)
        for i in range(w * h):
            v = (data[2 * i] << 8) | data[2 * i + 1]
            rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2] = ((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3
        Image.frombytes('RGB', (w, h), bytes(rgb)).save(path)
        return True

    def close(self):
        """The menu and "quiet 0", then the port closes."""
        try:
            if not self.gone:
                self.s.write(b'open menu\n')
                pause(0.8)
                self.s.write(b'quiet 0\n')
                pause(0.3)
        except Exception:
            say('%s: WARNING: "quiet 0" did not go to the board: send it by hand' % self.name)
        finally:
            self.run = False
            time.sleep(0.1)
            try:
                self.s.close()
            finally:
                self.log.close()


class Run:
    """The state of one run: the local file, the manager, the output folder and the results."""

    def __init__(self, cfg, mgr, out):
        self.cfg, self.mgr, self.out = cfg, mgr, out
        self.results = []        # (check, board, status, note)
        self.lock = threading.Lock()
        os.makedirs(out, exist_ok=True)

    def result(self, check, board, status, note=''):
        with self.lock:
            self.results.append((check, board, status, note))
        say('%-10s %-12s %-7s %s' % (check, board, status, note))

    @contextlib.contextmanager
    def console(self, b, tag):
        con = Console(self.cfg, b, os.path.join(self.out, '%s_%s.log' % (tag, safe(b.name))))
        try:
            con.cmd('quiet 1', 0.3)
            con.cmd('wake', 0.3)
            yield con
        finally:
            con.close()


def safe(name):
    return re.sub(r'[^A-Za-z0-9_.-]', '_', name)


# ---------------------------------------------------------------- the manager and the board web

KEYS = ('ver', 'slot', 'sig', 'minsv', 'own_sv', 'own_key', 'own_sig', 'pend', 'bad', 'self', 'up', 'name')


def http(url, body=None, timeout=10, password=''):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method='POST' if data is not None else 'GET')
    if data is not None:
        req.add_header('Content-Type', 'application/json')
    if password:
        req.add_header('Authorization', 'Basic ' + base64.b64encode((':' + password).encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read().decode('utf-8', 'replace')
    except urllib.error.HTTPError as e:
        e.close()  # (the answer of an error holds the socket)
        raise


def board_fields(text):
    """The fields of /api/boards ("me"): pend is a bool, minsv/up are numbers, the others text."""
    try:
        d = json.loads(text)
        me = d.get('me', d) if isinstance(d, dict) else {}
    except ValueError:
        me = {}
        for k in KEYS:
            m = re.search(r'"%s":("[^"]*"|-?\d+|true|false)' % k, text)
            if m:
                v = m.group(1)
                me[k] = v.strip('"') if v.startswith('"') else (v == 'true') if v in ('true', 'false') else int(v)
    out = {k: me.get(k) for k in KEYS}
    if isinstance(out['pend'], str):
        out['pend'] = out['pend'] == 'true'
    for k in ('minsv', 'up', 'own_sv'):
        try:
            out[k] = int(out[k]) if out[k] is not None else None
        except (TypeError, ValueError):
            out[k] = None
    return out


class Manager:
    def __init__(self, url):
        self.url = url

    def need(self):
        if not self.url:
            raise Stop('no manager URL: give --manager, "manager" in the local file, or TFN_MANAGER')

    def devices(self):
        self.need()
        d = json.loads(http(self.url + '/api/devices', timeout=15))
        d = d if isinstance(d, list) else d.get('devices', [])
        return list(d.values()) if isinstance(d, dict) else d

    def device(self, mac):
        return next((x for x in self.devices() if isinstance(x, dict) and norm_mac(x.get('mac')) == mac), None)

    def board(self, b, dev=None):
        """(the /api/boards fields of board b, the /api/id answer or None); None when no address answers."""
        dev = dev if dev is not None else self.device(b.mac)
        if not dev:
            return None, None
        for k in ('ip', 'lan_ip', 'vpn_ip'):
            addr = dev.get(k)
            if not addr:
                continue
            try:
                f = board_fields(http('http://%s/api/boards' % addr, timeout=8, password=b.password))
            except (OSError, urllib.error.URLError, ValueError):
                continue
            try:
                idt = json.loads(http('http://%s/api/id' % addr, timeout=8, password=b.password))
            except (OSError, urllib.error.URLError, ValueError):
                idt = None
            return f, idt
        return None, None

    def queue(self, mac, ver):
        """Put an update of one board into the queue of the manager (as fw_workflow.py update --mode now)."""
        self.need()
        return json.loads(http(self.url + '/api/ota/queue', {'version': ver, 'macs': [mac], 'mode': 'now'},
                               timeout=30))


def sig_faults(f, want=None, minsv=None, expect_sig=None):
    """The faults of the signed-OTA fields of one board (an empty list = OK)."""
    why = []
    if want and f['ver'] != want:
        why.append('ver %s, not %s' % (f['ver'], want))
    if f['pend']:
        why.append('pend: the firmware did not mark itself good')
    if f['self'] in ('back', 'kept'):
        why.append('the self-test failed (%s)' % f['self'])
    elif f['self'] == 'wait':
        why.append('the self-test still waits')
    if f['bad']:
        why.append('bad slot %s (a rollback)' % f['bad'])
    if f['sig'] == 'enforce' and f['own_sig'] != 'ok':
        why.append('own_sig "%s" in ENFORCE' % f['own_sig'])
    if expect_sig and f['sig'] != expect_sig:
        why.append('sig %s, not %s' % (f['sig'], expect_sig))
    if minsv is not None and (f['minsv'] is None or f['minsv'] < minsv):
        why.append('minsv %s, under %d' % (f['minsv'], minsv))
    return why


def fmt_fields(f):
    return 'ver %s sig %s minsv %s own_sig %s pend %s self %s' % (
        f['ver'], f['sig'], f['minsv'], f['own_sig'] or '-', 'yes' if f['pend'] else 'no', f['self'] or '-')


# ---------------------------------------------------------------- (1) versions

def step_versions(run, boards, want=None, minsv=None):
    try:
        devs = run.mgr.devices()
    except (OSError, urllib.error.URLError, ValueError) as e:
        run.result('versions', 'manager', 'FAULT', 'no answer from /api/devices (%s)' % e)
        return
    for b in boards:
        dev = next((x for x in devs if isinstance(x, dict) and norm_mac(x.get('mac')) == b.mac), None)
        if not dev:
            run.result('versions', b.name, 'FAULT', 'the manager does not know this MAC')
            continue
        if not dev.get('online'):
            run.result('versions', b.name, 'OFFLINE', 'the manager has %s (offline: skipped)' % dev.get('ver'))
            continue
        f, _ = run.mgr.board(b, dev)
        if f is None:
            run.result('versions', b.name, 'FAULT', 'online at the manager, but /api/boards gives no answer')
            continue
        why = sig_faults(f, want, minsv)
        if dev.get('ver') and dev.get('ver') != f['ver']:
            why.append('the manager has %s' % dev.get('ver'))
        run.result('versions', b.name, 'FAULT' if why else 'OK', fmt_fields(f) + ('  ' + '; '.join(why) if why else ''))


# ---------------------------------------------------------------- (2) mem

MEM_RX = r'mem: internal (\d+) free \((\d+) min, (\d+) block\), PSRAM (\d+) free'


def read_mem(con):
    m = con.mark()
    con.cmd('mem', 0.2)
    ln = con.wait(MEM_RX, 4, m)
    return tuple(int(x) for x in re.search(MEM_RX, ln).groups()) if ln else None


def step_mem(run, b, minutes, min_kb):
    limit = b.min_internal or int(min_kb * 1024)
    try:
        with run.console(b, 'mem') as con:
            con.cmd('open MENU', 2.0)
            first = read_mem(con)
            n = max(1, int(minutes + 0.999))
            samples = []
            for _ in range(n):
                pause(minutes * 60.0 / n)
                con.cmd('wake', 0.2)
                con.cmd('open MENU', 1.0)
                s = read_mem(con)
                if s:
                    samples.append(s)
    except ConsoleGone as e:
        run.result('mem', b.name, 'FAULT', str(e))
        return
    if not samples:
        run.result('mem', b.name, 'FAULT', 'no answer to "mem"')
        return
    last = samples[-1]
    note = 'internal %d B free at the menu after %g min (start %s, low %d, block %d), PSRAM %d; limit %d' % (
        last[0], minutes, first[0] if first else '?', last[1], last[2], last[3], limit)
    run.result('mem', b.name, 'OK' if last[0] >= limit else 'FAULT', note)


# ---------------------------------------------------------------- (3) open-all

# not opened: the private or local apps, the menu and SETUP, and MIC (it takes the USB for its sound device: the
# console stops)
OPEN_SKIP = {'WALKING HOUSES', 'SETUP', 'MENU', 'MIC'}
OPEN_BAD = re.compile(r"NEEDS A NEWER|refused|Can't find|missing name|DAMAGED|abort|panic|Guru|rst:|assert|"
                      r"NO MEMORY|stack overflow", re.I)
CRASH = r"Guru Meditation|Backtrace:|panic'ed|abort\(\) was called|assert failed|Rebooting\.\.\.|rst:0x|" \
        r"stack overflow|ESP-ROM:"


def app_names():
    with open(os.path.join(ROOT, 'main', 'apps.json'), encoding='utf-8') as h:
        d = json.load(h)
    return [a['name'] for a in (d['apps'] if isinstance(d, dict) else d) if a.get('name')]


def step_open_all(run, b, only=None):
    names = only or app_names()
    n_ok = n_none = 0
    faults = []
    try:
        with run.console(b, 'open') as con:
            con.cmd('open MENU', 2.0)
            for name in names:
                if name.upper() in OPEN_SKIP:
                    continue
                m = con.mark()
                con.cmd('wake', 0.2)
                con.cmd('open ' + name, 0.1)
                ans = con.wait(r'open %s: (ok|no such app)' % re.escape(name), 5, m)
                if ans and 'no such app' in ans:
                    n_none += 1
                    continue
                pause(4.0)
                bad = [ln.strip()[-120:] for ln in con.since(m) if OPEN_BAD.search(ln)]
                if ans and not bad:
                    n_ok += 1
                else:
                    why = ' | '.join(bad[:3]) or 'no answer to "open"'
                    faults.append('%s: %s' % (name, why))
                    say('   %s %s: FAULT %s' % (b.name, name, why))
                if any(re.search(CRASH, x) for x in bad):
                    pause(10.0)  # (the board starts again)
                    con.cmd('quiet 1', 0.3)
                con.cmd('open MENU', 2.0)
    except ConsoleGone as e:
        faults.append(str(e))
    note = '%d opened with no fault, %d faults, %d not on the board' % (n_ok, len(faults), n_none)
    if faults:
        note += ': ' + '; '.join(faults[:5]) + (' ...' if len(faults) > 5 else '')
    run.result('open-all', b.name, 'FAULT' if faults else 'OK', note)


# ---------------------------------------------------------------- (4) pair

# the game: (console name, log name, the middle of the 2 BOARDS button in logical points of the 3.49, net2 module)
GAMES = {
    'pong': ('pong', 'PONG', (130, 143), False),  # apps/pong/app_pong.c NET_X/Y/W/H
    'zap': ('zap', 'ZAP', (425, 76), True),       # apps/zap/app_zap.c BNET_X/W, B2P_Y/H (BOARD_BAR_SCREEN)
    'tennis': ('tennis', 'TENNIS', (578, 58), True),  # apps/tennis/app_tennis.c L[B_NET] (landscape)
}
# the 2 BOARDS point when the title is in portrait (172 x 640): TENNIS turns with the board
PORTRAIT = {'tennis': (86, 586)}
LOBBY = r'link: on, channel|ESPNOW: espnow \[version.*\] init'
PAIRED = r'2 BOARDS.*(HOST|CLIENT)'


def friends(con):
    """(the name of this board, [its friends], [the new boards near])"""
    m = con.mark()
    con.cmd('friends', 0.1)
    if not con.wait(r'console: friends: this board', 3, m):
        return None, [], []
    pause(0.4)
    me, fr, new = None, [], []
    for ln in con.since(m):
        r = re.search(r'friends: this board (.+?), \d+ friends', ln)
        if r:
            me = r.group(1)
        r = re.search(r'console: friend (\d+) (.+) [0-9A-F]{4}$', ln)
        if r:
            fr.append(r.group(2))
        r = re.search(r'console: new board near: (.+)$', ln)
        if r:
            new.append(r.group(1))
    return me, fr, new


def badge_xy(i, k):
    """The "+ ADD" badge i of k in the lobby of main/net2.c lobby_badge() on the 3.49 (640 x 172, no friend button)."""
    w = (640 - 40 - (k - 1) * 8) // k
    return 20 + i * (w + 8) + w // 2, 172 // 2 - 44 + 70 + 6 + 16


def tap_add(con, other):
    """Tap the "+ ADD <other>" badge of the lobby (the real user flow); True when other is a friend after it."""
    for _ in range(20):
        _, fr, new = friends(con)
        if other in fr:
            return True
        if other in new:
            x, y = badge_xy(new.index(other), min(len(new), 3))
            say('   %s: tap + ADD %s at %d,%d' % (con.name, other, x, y))
            con.cmd('ltap %d %d' % (x, y), 0.8)
            _, fr, _ = friends(con)
            if other in fr:
                return True
        pause(0.5)
    return False


def open_lobby(run, con, game, xy):
    """The game is open: tap 2 BOARDS until the lobby starts."""
    if game in PORTRAIT:
        pic = os.path.join(run.out, '%s_title_%s.png' % (game, safe(con.name)))
        try:
            from PIL import Image
            if con.shot(pic):
                with Image.open(pic) as im:
                    if im.size[0] < im.size[1]:
                        xy = PORTRAIT[game]
        except ImportError:
            pass
    for t in range(3):
        m = con.mark()
        con.cmd('ltap %d %d' % xy, 0.1)
        if con.wait(LOBBY, 2.5, m):
            return True
        say('   %s: no lobby after tap %d of 2 BOARDS' % (con.name, t + 1))
        pause(0.5)
    return False


def sent_count(con):
    m = con.mark()
    con.cmd('wifistat', 0.1)
    ln = con.wait(r'diag \(console\): link .*sent (\d+)', 3, m)
    return int(re.search(r'sent (\d+)', ln).group(1)) if ln else None


def pair_game(run, A, B, na, nb, lists, g, secs, shots, added):
    app, logname, xy, is_n2 = GAMES[g]
    ma, mb = A.mark(), B.mark()
    for c in (A, B):
        c.cmd('wake', 0.1)
        c.cmd('open ' + app, 0.1)
    oa = A.wait(r'open %s: (ok|no such app)' % app, 4, ma)
    ob = B.wait(r'open %s: (ok|no such app)' % app, 4, mb)
    if not oa or not ob or 'no such' in oa or 'no such' in ob:
        return 'SKIP', 'not installed on %s' % (na if (not oa or 'no such' in oa) else nb)
    A.wait(r'main: open %s' % logname, 4, ma)
    B.wait(r'main: open %s' % logname, 4, mb)
    pause(2.0)  # (the title is up: the first ltap after an open can get lost)
    sa0, sb0 = sent_count(A), sent_count(B)
    if not open_lobby(run, A, g, xy) or not open_lobby(run, B, g, xy):
        return 'FAULT', 'the lobby did not start (the tap did not reach 2 BOARDS)'
    t_lobby = time.time()
    for c, other, lst in ((A, nb, lists[0]), (B, na, lists[1])):
        if other not in lst:
            if not tap_add(c, other):
                return 'FAULT', 'the ADD badge did not add the friend'
            added.append((c, other))
            lst.append(other)
            say('   %s: %s added as a friend' % (c.name, other))
    pa = A.wait(PAIRED, 20, ma)
    pb = B.wait(PAIRED, 20, mb)
    if not pa or not pb:
        if shots:
            A.shot(os.path.join(run.out, '%s_%s.png' % (g, safe(A.name))))
            B.shot(os.path.join(run.out, '%s_%s.png' % (g, safe(B.name))))
        return 'FAULT', 'NO PAIRING (A: %s, B: %s)' % (bool(pa), bool(pb))
    tp = max(0.0, max(A.first_time(PAIRED, ma), B.first_time(PAIRED, mb)) - t_lobby)
    ra, rb = ('HOST' if 'HOST' in pa else 'CLIENT'), ('HOST' if 'HOST' in pb else 'CLIENT')
    ka, kb = A.mark(), B.mark()
    t0 = time.time()
    if shots:
        pause(2)
        A.shot(os.path.join(run.out, '%s_%s.png' % (g, safe(A.name))))
        B.shot(os.path.join(run.out, '%s_%s.png' % (g, safe(B.name))))
    while time.time() - t0 < secs * SCALE:
        time.sleep(0.05)
    sa1, sb1 = sent_count(A), sent_count(B)
    ended = A.grep(r'link ended|THE OTHER BOARD IS GONE', ma) + B.grep(r'link ended|THE OTHER BOARD IS GONE', mb)
    crash = A.grep(CRASH, ma) + B.grep(CRASH, mb)
    st_a = [int(x) for ln in A.grep(r'in 5 s \d+ packets from the other board', ka)
            for x in re.findall(r'in 5 s (\d+) packets', ln)]
    st_b = [int(x) for ln in B.grep(r'in 5 s \d+ packets from the other board', kb)
            for x in re.findall(r'in 5 s (\d+) packets', ln)]
    dsa = (sa1 - sa0) if sa0 is not None and sa1 is not None else None
    dsb = (sb1 - sb0) if sb0 is not None and sb1 is not None else None
    why = []
    if sorted([ra, rb]) != ['CLIENT', 'HOST']:
        why.append('roles A %s, B %s' % (ra, rb))
    if ended:
        why.append('link ended: ' + ended[0][-80:])
    if crash:
        why.append('crash: ' + crash[0][-80:])
    if not dsa or not dsb:
        why.append('no packets sent (A %s, B %s)' % (dsa, dsb))
    if is_n2 and (not st_a or not st_b or min(st_a + st_b) <= 0):
        why.append('net2 5 s lines: A %s, B %s' % (st_a, st_b))
    if why:
        return 'FAULT', '; '.join(why)
    return 'OK', 'paired in %.1f s (A %s, B %s), %g s link, sent A %d B %d%s' % (
        tp, ra, rb, secs, dsa, dsb, (', from the other board each 5 s: A %s B %s' % (st_a, st_b)) if is_n2 else '')


def step_pair(run, ba, bb, games, secs, keep, shots):
    if ba.mac == bb.mac:
        raise Stop('pair needs two different boards')
    for g in games:
        if g not in GAMES:
            raise Stop('unknown game %s (the games: %s)' % (g, ' '.join(GAMES)))
    added = []
    with run.console(ba, 'pair') as A, run.console(bb, 'pair') as B:
        try:
            for c in (A, B):
                c.cmd('open menu', 0.5)
            pause(1.5)
            na, fa, _ = friends(A)
            nb, fb, _ = friends(B)
            if not na or not nb:
                run.result('pair', '%s+%s' % (ba.name, bb.name), 'FAULT',
                           'no answer to "friends" from %s' % (ba.name if not na else bb.name))
                return
            lists = (fa, fb)
            for g in games:
                say('== pair %s' % g)
                try:
                    st, note = pair_game(run, A, B, na, nb, lists, g, secs, shots, added)
                except ConsoleGone as e:
                    st, note = 'FAULT', str(e)
                run.result('pair', g, st, note)
                for c in (A, B):
                    if not c.gone:
                        c.cmd('open menu', 0.8)
                pause(2.0)
        finally:
            if added and not keep:
                for c, other in added:
                    if c.gone:
                        continue
                    c.cmd('open menu', 0.5)
                    _, fr, _ = friends(c)
                    if other in fr:
                        c.cmd('friends rm %d' % fr.index(other), 0.5)
                        _, fr, _ = friends(c)
                        say('restore: %s removed from the friends of %s (%s)' %
                            (other, c.name, 'ok' if other not in fr else 'NOT REMOVED'))


# ---------------------------------------------------------------- (5) old-to-new

WATCH = ('ver', 'slot', 'sig', 'minsv', 'own_sig', 'own_key', 'pend', 'bad', 'self')


def step_old_to_new(run, b, v_from, v_to, minutes, go, expect_sig=None, minsv=None):
    dev = run.mgr.device(b.mac)
    if not dev or not dev.get('online'):
        run.result('old-to-new', b.name, 'FAULT', 'the board is not online at the manager')
        return
    f0, _ = run.mgr.board(b, dev)
    if f0 is None:
        run.result('old-to-new', b.name, 'FAULT', 'no answer from /api/boards')
        return
    if f0['ver'] != v_from:
        raise Stop('%s runs %s, not %s. The tool does not flash: put the old image on the board first' %
                   (b.name, f0['ver'], v_from))
    name0 = dev.get('name')
    say('start: %s on %s (%s)' % (b.name, f0['ver'], fmt_fields(f0)))
    if not go:
        run.result('old-to-new', b.name, 'SKIP', 'a dry run: give --go to put %s into the queue of the manager' % v_to)
        return
    try:
        ans = run.mgr.queue(b.mac, v_to)
    except (OSError, urllib.error.URLError, ValueError) as e:
        run.result('old-to-new', b.name, 'FAULT', 'the manager did not take the update (%s)' % e)
        return
    if not ans.get('ok') or any(not x.get('ok') for x in ans.get('added') or []):
        run.result('old-to-new', b.name, 'FAULT', 'the manager did not take the update: %s' % json.dumps(ans)[:160])
        return
    say('queued: %s -> %s' % (v_from, v_to))
    end = time.time() + minutes * 60 * SCALE
    last, prev_up, restarts, good, f = None, None, 0, 0, None
    while time.time() < end:
        pause(15)
        try:
            f, idt = run.mgr.board(b)
        except (OSError, urllib.error.URLError, ValueError):
            f = None
        if f is None:
            if last != 'no answer':
                say('%s %s: no answer' % (time.strftime('%H:%M:%S'), b.name))
                last = 'no answer'
            good = 0
            continue
        if prev_up is not None and f['up'] is not None and f['up'] < prev_up:
            restarts += 1
        prev_up = f['up'] if f['up'] is not None else prev_up
        s = {k: f[k] for k in WATCH}
        if s != last:
            say('%s %s: %s' % (time.strftime('%H:%M:%S'), b.name, fmt_fields(f)))
            last = s
        done = f['ver'] == v_to and not f['pend'] and f['self'] != 'wait'
        good = good + 1 if done else 0
        if good >= 2:
            break
    if good < 2:
        run.result('old-to-new', b.name, 'FAULT', 'not on %s with the self-test done after %g min (last: %s)' % (
            v_to, minutes, fmt_fields(f) if f else 'no answer'))
        return
    why = sig_faults(f, v_to, minsv, expect_sig)
    dev = run.mgr.device(b.mac) or {}
    if dev.get('name') != name0:
        why.append('the name changed (%s -> %s)' % (name0, dev.get('name')))
    board_id = (idt or {}).get('id') if isinstance(idt, dict) else None
    note = '%s -> %s, %d restarts seen, ID %s; %s' % (v_from, v_to, restarts, board_id or 'none', fmt_fields(f))
    run.result('old-to-new', b.name, 'FAULT' if why else 'OK', note + ('; ' + '; '.join(why) if why else ''))


# ---------------------------------------------------------------- ports, all, main

def step_ports(cfg):
    ports = {(p.device or '').upper(): p for p in COMPORTS()}
    for b in cfg.boards:
        if not b.port:
            say('%-12s %-8s %-12s (no USB: web only)' % (b.name, b.kind, b.mac))
            continue
        p = ports.get(b.port.upper())
        if p is None:
            st = 'not on USB now'
        elif norm_mac(p.serial_number) != b.mac:
            st = 'MAC DIFFERS (%s): the tool will not open it' % (norm_mac(p.serial_number) or 'none')
        else:
            st = 'ok'
        say('%-12s %-8s %-12s %-8s %s' % (b.name, b.kind, b.mac, b.port, st))
    for dp in sorted(cfg.deny):
        say('deny       %s (never opened)' % dp)


def in_threads(jobs):
    """Run the jobs (functions) at the same time; a Stop of one job stops the run after all jobs end."""
    errs = []

    def wrap(fn):
        try:
            fn()
        except Stop as e:
            errs.append(e)
    ts = [threading.Thread(target=wrap, args=(fn,)) for fn in jobs]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    if errs:
        raise errs[0]


def pick(cfg, names, role):
    return [cfg.find(n) for n in names] if names else cfg.with_role(role)


def main(argv=None):
    ap = argparse.ArgumentParser(description='The HIL runner of TFN OS (see the top of this file).')
    ap.add_argument('--boards', default=LOCAL_FILE, help='the local file of the boards')
    ap.add_argument('--manager', default='', help='the URL of the manager')
    ap.add_argument('--out', default='', help='the folder of the logs (default tests/host/hil/<time>)')
    sub = ap.add_subparsers(dest='step', required=True)
    sub.add_parser('ports')
    p = sub.add_parser('versions')
    p.add_argument('--want', default='')
    p.add_argument('--minsv', type=int, default=None)
    p.add_argument('--board', action='append', default=[])
    p = sub.add_parser('mem')
    p.add_argument('--board', action='append', default=[])
    p.add_argument('--minutes', type=float, default=3.0)
    p.add_argument('--min-kb', type=float, default=45.0)
    p = sub.add_parser('open-all')
    p.add_argument('--board', action='append', default=[])
    p.add_argument('apps', nargs='*')
    p = sub.add_parser('pair')
    p.add_argument('--a', default='')
    p.add_argument('--b', default='')
    p.add_argument('games', nargs='*')
    p.add_argument('--secs', type=float, default=12.0)
    p.add_argument('--keep-friends', action='store_true')
    p.add_argument('--shots', action='store_true')
    p = sub.add_parser('old-to-new')
    p.add_argument('--board', required=True)
    p.add_argument('--from', dest='v_from', required=True)
    p.add_argument('--to', dest='v_to', required=True)
    p.add_argument('--minutes', type=float, default=25.0)
    p.add_argument('--expect-sig', default=None, choices=('off', 'report', 'enforce'))
    p.add_argument('--minsv', type=int, default=None)
    p.add_argument('--go', action='store_true', help='put the update into the queue (else a dry run)')
    p = sub.add_parser('all')
    p.add_argument('--want', default='')
    p.add_argument('--minsv', type=int, default=None)
    p.add_argument('--minutes', type=float, default=3.0)
    p.add_argument('--min-kb', type=float, default=45.0)
    p.add_argument('--secs', type=float, default=12.0)
    a = ap.parse_args(argv)
    try:
        cfg = Config(a.boards)
        if a.step == 'ports':
            step_ports(cfg)
            return 0
        out = a.out or os.path.join(ROOT, 'tests', 'host', 'hil', time.strftime('%Y%m%d-%H%M%S'))
        run = Run(cfg, Manager(manager_url(a.manager, cfg)), out)
        if a.step == 'versions':
            step_versions(run, [cfg.find(n) for n in a.board] or cfg.boards, a.want or None, a.minsv)
        elif a.step == 'mem':
            in_threads([lambda b=b: step_mem(run, b, a.minutes, a.min_kb) for b in pick(cfg, a.board, 'console')])
        elif a.step == 'open-all':
            in_threads([lambda b=b: step_open_all(run, b, a.apps) for b in pick(cfg, a.board, 'console')])
        elif a.step == 'pair':
            two = [cfg.find(a.a), cfg.find(a.b)] if a.a and a.b else cfg.with_role('pair')
            if len(two) != 2:
                raise Stop('pair needs two boards: --a and --b, or two boards with the role "pair"')
            step_pair(run, two[0], two[1], [g.lower() for g in a.games] or ['pong', 'zap'], a.secs,
                      a.keep_friends, a.shots)
        elif a.step == 'old-to-new':
            step_old_to_new(run, cfg.find(a.board), a.v_from, a.v_to, a.minutes, a.go, a.expect_sig, a.minsv)
        elif a.step == 'all':
            step_versions(run, cfg.boards, a.want or None, a.minsv)

            def one(b):
                step_mem(run, b, a.minutes, a.min_kb)
                step_open_all(run, b)
            in_threads([lambda b=b: one(b) for b in cfg.with_role('console')])
            two = cfg.with_role('pair')
            if len(two) == 2:
                step_pair(run, two[0], two[1], ['pong', 'zap', 'tennis'], a.secs, False, False)
            else:
                run.result('pair', '-', 'SKIP', 'the local file has %d boards with the role "pair" (2 needed)' % len(two))
    except Stop as e:
        say('STOP: %s' % e)
        return 2
    with open(os.path.join(out, 'summary.json'), 'w', encoding='utf-8') as h:
        json.dump([{'check': c, 'board': b, 'status': s, 'note': mask(n)} for c, b, s, n in run.results], h, indent=1)
    bad = ['%s %s' % (c, b) for c, b, s, _ in run.results if s == 'FAULT']
    say('HIL: %s (%d checks%s); logs in %s' % ('FAULT' if bad else 'OK', len(run.results),
                                              ': ' + ', '.join(bad) if bad else '', os.path.relpath(out, ROOT)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
