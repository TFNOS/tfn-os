"""The network of poolmgr (design 2.2, 2.4): the UDP listener of the board packets and the LAN address of this
computer.

The packets on UDP 47474 (listen only; poolmgr sends no beacon):
  'A' 'J'  a board with an open JOIN screen (MAC, ID, name), to the LAN broadcast;
  'A' 'P'  the beacon of a pool member each 2 s (encrypted with the pool key: MAC, name, open app, flags, ID;
           pool.py has the format).

A packet gives the host of the board, not its web port. A board serves its web on port 80, so the address is the
host. When addrs.json has an address "host:port" for the same MAC, the new address keeps that port (the fake boards of
the tests use other ports; a real board has no port in its address).

When the port is in use (for example "serve" runs), listen() does not open it: it waits and reads addrs.json again
(serve writes the beacons there). Then it sees no 'A' 'J' packet.
"""
import os
import socket
import threading
import time

from pm_shared import pool

UDP_PORT = 47474
PREV_KEEP = 3600     # the previous pool key opens beacons for 1 hour after a change of the key
DONE = True


def open_socket(port):
    """A UDP socket on all addresses at the port, with broadcast receive; OSError when the port is in use."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.bind(('0.0.0.0', port))
    except OSError:
        s.close()
        raise
    return s


def pool_keys(p, now=None, changed=None):
    """The beacon keys of the pool p: the current epoch, and the previous epoch for PREV_KEEP s after the change of
    the key (the rule of the boards: a board that missed the new key still sends with the old one; it gets the new
    key at once). The time of the change: "t" of the newest "prev" entry of secrets.json (pm_pool.rekey), else
    changed (the time of the file secrets.json). The older entries are never used."""
    if p is None:
        return []
    ks = [pool.derive(p.pool_k, p.pool_id, p.epoch)]
    prev = (p.prev or [None])[0]
    now = time.time() if now is None else now
    try:
        t = prev.get('t') or changed or 0
        if now - t < PREV_KEEP and prev['epoch'] == p.epoch - 1:
            ks.append(pool.derive(bytes.fromhex(prev['k']), p.pool_id, prev['epoch']))
    except (AttributeError, KeyError, ValueError, TypeError):
        pass
    return ks


def data_keys(data):
    """pool_keys of the pool of the data folder, with the time of its secrets.json."""
    if data.pool is None and data.has_pool():
        data.load()
    try:
        changed = os.path.getmtime(os.path.join(data.pool_dir, 'secrets.json'))
    except OSError:
        changed = None
    return pool_keys(data.pool, changed=changed)


def parse(pkt, host, keys):
    """('join' | 'beacon', info) of one packet, or (None, None) for any other packet."""
    j = pool.join_pkt_parse(pkt)
    if j is not None:
        return 'join', {'mac': j['mac'].hex().upper(), 'id': j['id'].hex(), 'name': j['name'], 'ip': host}
    for k in keys:
        code, plain = pool.beacon_open(k, pkt)
        if code == pool.DROP_OK:
            f = pool.beacon_fields(plain)
            return 'beacon', {'mac': f['mac'].hex().upper(), 'id': f['id'].hex(), 'name': f['name'],
                              'app': f['app'], 'flags': f['flags'], 'ip': host, 'epoch': k.epoch}
    return None, None


def with_port(host, known):
    """The address of a board from the host of a packet: the host, with the port of the known address when it has
    one."""
    port = (known or '').partition(':')[2]
    return '%s:%s' % (host, port) if port else host


class Listener:
    """The UDP listener: Listener(on_join, on_beacon, port=None, keys=None); start(), stop().

    on_join(info) gets a dict {'mac', 'id', 'name', 'ip'} for each 'A' 'J' packet. on_beacon(info) gets a dict
    {'mac', 'id', 'name', 'app', 'flags', 'ip', 'epoch'} for each 'A' 'P' packet that opens with keys (a list of the
    pool keys of pool.derive; None or [] = no beacon). The MAC is in upper hex, the ID in lower hex. port None = the
    value of UDP_PORT at the start. start() raises OSError when the port is in use."""

    def __init__(self, on_join, on_beacon, port=None, keys=None):
        self.on_join, self.on_beacon, self.port = on_join, on_beacon, port
        self.keys = list(keys or ())
        self.sock = None
        self.thread = None
        self.run = False

    def set_keys(self, keys):
        """New beacon keys (serve: after a change of the pool, and each minute for the end of the previous key)."""
        self.keys = list(keys or ())

    def start(self):
        self.sock = open_socket(UDP_PORT if self.port is None else self.port)
        self.sock.settimeout(0.2)
        self.run = True
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()
        return self

    def loop(self):
        while self.run:
            try:
                pkt, src = self.sock.recvfrom(512)
            except socket.timeout:
                continue
            except OSError:
                if not self.run:
                    break
                time.sleep(0.05)
                continue
            what, info = parse(pkt, src[0], self.keys)
            fn = self.on_join if what == 'join' else self.on_beacon if what == 'beacon' else None
            if fn is not None:
                try:
                    fn(info)
                except Exception as e:  # a fault of a callback must not stop the listener
                    print('the listener: %s: %s' % (type(e).__name__, e))

    def stop(self):
        self.run = False
        if self.thread is not None:
            self.thread.join(2.0)
        if self.sock is not None:
            self.sock.close()
        self.sock = self.thread = None


def listen(data, secs):
    """Listen secs seconds: (joins, beacons), two lists of the dicts of Listener (one dict for each MAC, the last
    packet). The beacons also go into data.set_addr(). When the port is in use, the lists come from addrs.json (the
    entries that changed in the time of the wait), and joins is empty."""
    known = data.addrs()
    joins, beacons = {}, {}
    lock = threading.Lock()

    def on_join(info):
        info['ip'] = with_port(info['ip'], (known.get(info['mac']) or {}).get('ip'))
        with lock:
            joins[info['mac']] = info

    def on_beacon(info):
        info['ip'] = with_port(info['ip'], (known.get(info['mac']) or {}).get('ip'))
        with lock:
            beacons[info['mac']] = info

    lst = Listener(on_join, on_beacon, keys=data_keys(data))
    try:
        lst.start()
    except OSError:
        return [], wait_addrs(data, secs, known)
    try:
        time.sleep(max(0.0, secs))
    finally:
        lst.stop()
    for b in beacons.values():
        data.set_addr(b['mac'], b['ip'], b['app'], b['id'])
    return list(joins.values()), list(beacons.values())


def wait_addrs(data, secs, before):
    """The port is in use: wait secs, then the entries of addrs.json that are new or newer than before."""
    t0 = time.time()
    time.sleep(max(0.0, secs))
    out = []
    for mac, a in data.addrs().items():
        if a.get('t', 0) >= int(t0) - 1 and a != before.get(mac):
            out.append({'mac': mac, 'id': a.get('id', ''), 'name': '', 'app': a.get('app', ''), 'flags': 0,
                        'ip': a.get('ip', '')})
    return out


def lan_ip_toward(ip):
    """The address of this computer toward ip (a UDP socket connect: no packet goes out)."""
    host = ip.split(':')[0]
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, UDP_PORT))
        return s.getsockname()[0]
    finally:
        s.close()
