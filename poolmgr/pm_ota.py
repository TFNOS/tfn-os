"""The firmware updates of poolmgr from the public releases (design 2.10).

THE CHANNEL (config.json "channel", a copy of channel.json of the release zip):
  "pages"     the web site of tfn-os: <pages>/firmware/index.json -> the version of each kind ->
              <pages>/firmware/<kind>/manifest.json -> the file "app" (app.bin: the signed app; its SHA-256 and its
              size are in the manifest)
  "repo"      <owner>/<name> on GitHub: --version V reads tfn-os-V-<kind>.bin and SHA256SUMS of the tag vV
  "releases"  (optional) the base URL of the release files in place of https://github.com/<repo>/releases/download
  "ota_pub"   the official public OTA keys (65 bytes in hex each)
A URL can also be file:// (a local copy of the channel).

THE CHECKS OF THE PROGRAM before a byte goes to a board (decision 18): the SHA-256 and the size of the manifest (or
SHA256SUMS), then the trailer TFNOTA1 (main/ota_sig.h) with fw_sign.check(): a good signature of an official key, the
kind of the board, the version, sec_ver >= the minsv of the board. A board in ENFORCE also gives its key IDs: a file
of a key that the board does not have is not sent. The board checks the file again; a refusal comes back as
{"code": "sig", "sig": "<the code>"}.

THE ORDER: one board at a time. The program sends to a board, then reads GET /api/boards each POLL_S seconds up to
WAIT_S seconds: the new version, "pend" false and (ENFORCE) "own_sig" ok = good; a "bad" slot after a restart = a
rollback. Only then the next board. The first failure stops the run (Failed, exit 1). A board with an open app waits
for the next run, unless now.
"""
import hashlib
import json
import os
import re
import sys
import time

import pm_board
from pm_shared import Failed, InputError, channel_hint, fw_sign, pool, pool_tool

DONE = True

POLL_S = 2.0  # the time between two reads of /api/boards after a send
WAIT_S = 180.0  # the longest wait for the new firmware (the restart and the self-test)
MAX_FILE = 16 * 1024 * 1024  # the biggest firmware file
MAX_JSON = 1024 * 1024  # the biggest index, manifest or SHA256SUMS
IDLE_APPS = ('MENU', '')  # the "app" of a board at the menu
GITHUB = 'https://github.com/%s/releases/download'
VER_RE = re.compile(r'^[0-9A-Za-z][0-9A-Za-z._+-]{0,31}$')
KIND_RE = re.compile(r'^[a-z0-9]{1,15}$')

# the results of a row of update()
R_OK = 'ok'
R_SAME = 'up to date'
R_BUSY = 'busy: waits for the next run'
R_NOANS = 'no answer: waits for the next run'
R_NOCH = 'no channel for this kind'
R_STOP = 'not sent: the run stopped'


def say(text):
    """One line of progress (a person sees the run go on). (pythonw of the Windows service has no sys.stdout.)"""
    print(text)
    if sys.stdout is not None:
        sys.stdout.flush()


# ---------------------------------------------------------------- the channel
def channel(data):
    """The channel of config.json; InputError when it has no web site or no OTA key."""
    if data.pool is None and not data.cfg.get('channel'):
        data.load()
    ch = data.cfg.get('channel') or {}
    if not ch.get('pages'):
        raise InputError('no channel (no web site of the releases) in %s: %s' % (data.cfg_file, channel_hint(data)))
    return ch


def ota_keys(ch):
    """The official OTA keys of the channel: a list of 65-byte points."""
    out = []
    for h in ch.get('ota_pub') or []:
        try:
            pt = bytes.fromhex(h)
        except (TypeError, ValueError):
            pt = b''
        if len(pt) != 65 or pt[0] != 4:
            raise InputError('the channel has an OTA key that is not 65 bytes in hex that start with 04')
        out.append(pt)
    if not out:
        raise InputError('the channel has no OTA key ("ota_pub")')
    return out


def url_join(base, rel):
    return base.rstrip('/') + '/' + rel


def safe_rel(rel):
    """A relative path of the channel (no "..", no start with "/", no back slash), else Failed."""
    if not isinstance(rel, str) or not rel or rel.startswith('/') or '\\' in rel or \
            any(p in ('', '.', '..') for p in rel.split('/')):
        raise Failed('the channel names a bad path: %r' % rel)
    return rel


def get_json(url):
    try:
        return json.loads(pm_board.http_get(url, limit=MAX_JSON).decode('utf-8'))
    except (ValueError, UnicodeDecodeError):
        raise Failed('%s is not JSON' % url)


def channel_index(data):
    """The firmware index of the channel (<pages>/firmware/index.json) as a dict."""
    ch = channel(data)
    idx = get_json(url_join(ch['pages'], 'firmware/index.json'))
    kinds = idx.get('kinds') if isinstance(idx, dict) else None
    if not isinstance(kinds, dict):
        raise Failed('the firmware index of the channel has no "kinds"')
    for kind, e in kinds.items():
        if not isinstance(e, dict) or not isinstance(e.get('version'), str) or 'manifest' not in e:
            raise Failed('the firmware index of the channel: a bad entry for %r' % kind)
        safe_rel(e['manifest'])
    return idx


def check_version(version):
    if not isinstance(version, str) or not VER_RE.match(version):
        raise InputError('not a version: %r' % version)
    return version


def cache_path(data, version, kind):
    return os.path.join(data.cache, 'fw', version, kind + '.bin')


def from_cache(data, version, kind, size, sha):
    """The cached file when it has the size and the SHA-256, else None."""
    try:
        with open(cache_path(data, version, kind), 'rb') as h:
            got = h.read(MAX_FILE + 1)
    except OSError:
        return None
    if len(got) == size and hashlib.sha256(got).hexdigest() == sha:
        return got
    return None


def to_cache(data, version, kind, payload):
    p = cache_path(data, version, kind)
    try:
        os.makedirs(os.path.dirname(p), mode=0o700, exist_ok=True)
        tmp = p + '.tmp'
        with open(tmp, 'wb') as h:
            h.write(payload)
        os.replace(tmp, p)
    except OSError:
        pass  # (the cache is only a help)


def download(data, url, version, kind, size, sha):
    """The file at url, or the cached copy, with the given size (None = any) and SHA-256; else Failed."""
    got = from_cache(data, version, kind, size, sha) if size is not None else None
    if got is not None:
        return got
    got = pm_board.http_get(url, timeout=60, limit=MAX_FILE)
    if size is not None and len(got) != size:
        raise Failed('%s: %d bytes, the channel says %d (a cut file?)' % (url, len(got), size))
    if hashlib.sha256(got).hexdigest() != sha:
        raise Failed('%s: the SHA-256 is not the one of the channel' % url)
    to_cache(data, version, kind, got)
    return got


def newest(data, kind, idx=None):
    """The version of the kind in the channel, or None (no channel for this kind)."""
    idx = idx or channel_index(data)
    e = idx['kinds'].get(kind)
    return e['version'] if e else None


def fetch_v(data, kind, version=None):
    """(the bytes, the version) of the signed app file of the kind (see fetch)."""
    if not KIND_RE.match(kind or ''):
        raise Failed('not a board kind: %r' % kind)
    ch = channel(data)
    if version is None:
        idx = channel_index(data)
        e = idx['kinds'].get(kind)
        if not e:
            raise Failed('%s: %s' % (kind, R_NOCH))
        ver = check_version(e['version'])
        base = url_join(ch['pages'], 'firmware')
        man = get_json(url_join(base, e['manifest']))
        if not isinstance(man, dict) or man.get('kind') != kind or man.get('version') != ver:
            raise Failed('the manifest of %s is not for %s %s' % (kind, kind, ver))
        app = next((f for f in man.get('files', []) if isinstance(f, dict) and f.get('name') == 'app'), None)
        if not app or not isinstance(app.get('size'), int) or not isinstance(app.get('sha256'), str):
            raise Failed('the manifest of %s %s has no file "app" with a size and a SHA-256' % (kind, ver))
        url = url_join(base, '%s/%s' % (kind, safe_rel(app['path'])))
        return download(data, url, ver, kind, app['size'], app['sha256'].lower()), ver
    ver = check_version(version)
    base = ch.get('releases') or (GITHUB % ch['repo'] if ch.get('repo') else '')
    if not base:
        raise InputError('the channel has no "repo": no GitHub release for --version')
    rel = url_join(base, 'v' + ver)
    name = 'tfn-os-%s-%s.bin' % (ver, kind)
    sums = {}
    for ln in pm_board.http_get(url_join(rel, 'SHA256SUMS'), limit=MAX_JSON).decode('utf-8', 'replace').splitlines():
        m = re.match(r'^([0-9a-fA-F]{64}) [ *](\S+)$', ln.strip())
        if m:
            sums[m.group(2)] = m.group(1).lower()
    if name not in sums:
        raise Failed('%s: %s (the release v%s has no %s)' % (kind, R_NOCH, ver, name))
    return download(data, url_join(rel, name), ver, kind, None, sums[name]), ver


def fetch(data, kind, version=None):
    """The signed app file of the kind: the newest of the channel, or version (the GitHub release). The SHA-256 (and
    the size) of the channel are checked; the file goes into cache/fw/<version>/<kind>.bin."""
    return fetch_v(data, kind, version)[0]


def check_file(data, payload, kind, version, minsv):
    """The checks of the program before a send (design 2.10.2): (code, key_id, sec_ver); code 'ok' or the fault
    (the codes of the board: sig_none, sig_form, sig_hash, sig_kind, sig_key, sig_bad, sig_old)."""
    return fw_sign.check(payload, ota_keys(channel(data)), kind, version, int(minsv or 0))


# ---------------------------------------------------------------- the members
def load_pool(data):
    return data.need_pool()


def is_board(m):
    return not (m['role'] == pool.ROLE_MANAGER and m['mac'] == pool.MANAGER_MAC.hex())


def members_of(data, members):
    """The member dicts of the names (all boards when none is given), in the order of the pool."""
    p = load_pool(data)
    if not members:
        return [m for m in p.members if is_board(m)]
    out = []
    for w in members:
        m = p.find(w)
        if m is None:
            raise InputError('no member %s in the pool %s' % (w, p.name))
        if not is_board(m):
            raise InputError('%s is the manager, not a board' % w)
        if m not in out:
            out.append(m)
    return out


def call(b, method, path):
    """A signed request to a board; one more try with a new board nonce after a 401 (a restart)."""
    try:
        return b.call(method, path, signed=True)
    except pool_tool.Refused as e:
        if e.code != 401:
            raise
        b.bn = None
        return b.call(method, path, signed=True)


def state(data, m):
    """The "me" part of GET /api/boards of a member, or None when it does not answer."""
    try:
        me = call(pm_board.board(data, m['name']), 'GET', '/api/boards').get('me')
    except (pool_tool.Refused, pool_tool.ToolError, Failed, OSError, ValueError):
        return None
    return me if isinstance(me, dict) else None


def ver_key(v):
    """The order of two versions: the numbers of "1.0.104" (a version with other letters: None)."""
    parts = (v or '').split('.')
    return tuple(int(x) for x in parts) if all(x.isdigit() for x in parts) else None


def needs(now_ver, new_ver, pinned):
    """True: the board with now_ver gets new_ver (a pinned version: when it is another one; the channel: when it is
    newer)."""
    if now_ver == new_ver:
        return False
    if pinned:
        return True
    a, b = ver_key(now_ver), ver_key(new_ver)
    return a is None or b is None or b > a


# ---------------------------------------------------------------- update check
def plan(data, members, version=None):
    """"update check": one row for each member (the kind, the version now, the newest version, or "no channel for
    this kind"); a row is a line of text or a dict."""
    ms = members_of(data, members)
    idx = None if version else channel_index(data)
    if version:
        check_version(version)
    rows = []
    for m in ms:
        me = state(data, m)
        if me is None:
            rows.append({'member': m['name'], 'kind': '?', 'now': '?', 'newest': '?', 'state': R_NOANS})
            continue
        kind, now_ver = me.get('kind', ''), me.get('ver', '')
        new_ver = version or newest(data, kind, idx)
        if new_ver is None:
            st = R_NOCH
        elif not needs(now_ver, new_ver, bool(version)):
            st = R_SAME
        elif me.get('app', '') not in IDLE_APPS:
            st = 'update (%s is open: waits, or --now)' % me.get('app')
        else:
            st = 'update'
        rows.append({'member': m['name'], 'kind': kind, 'now': now_ver, 'newest': new_ver or '-', 'state': st,
                     'sig': me.get('sig', ''), 'minsv': me.get('minsv', 0)})
    return rows


# ---------------------------------------------------------------- update
def board_keys(me):
    """The key IDs of a board in ENFORCE (a list of 16 hex digits), or None (the board takes any file)."""
    if me.get('sig') != 'enforce':
        return None
    keys = [k for k in me.get('keys') or [] if isinstance(k, str)]
    return keys or None


def send(data, m, payload):
    """POST /api/ota: the answer of the board (a dict), or None when the answer did not come."""
    try:
        ans = pm_board.post_stream(data, m['name'], '/api/ota', payload, None, 90)
    except pool_tool.Refused as e:
        return dict(e.ans, ok=False, http=e.code)
    except (pool_tool.ToolError, Failed, OSError):
        return None
    return ans if isinstance(ans, dict) else None


def refusal(ans, me):
    """The code of a refused update: the "sig" code of the board, else its "code"."""
    if ans and ans.get('sig'):
        return ans['sig']
    if me and me.get('last_sig') and not me['last_sig'].startswith('ok'):
        return me['last_sig']
    if ans and ans.get('code'):
        return ans['code']
    return (me or {}).get('ota_code') or 'refused'


def wait_new(data, m, before, new_ver, answered):
    """The wait after a send: ('ok', me) or (the fault, me). before = the "me" before the send."""
    end = time.time() + WAIT_S
    up0 = before.get('up', 0)
    restarted = False
    me = None
    while time.time() < end:
        time.sleep(POLL_S)
        cur = state(data, m)
        if cur is None:
            restarted = True  # (the board does not answer during its restart)
            continue
        me = cur
        if me.get('up', 0) < up0:
            restarted = True
        if me.get('ver') == new_ver:
            if me.get('pend') is False and (me.get('sig') != 'enforce' or me.get('own_sig') == 'ok'):
                return R_OK, me
            continue
        if restarted and me.get('bad'):
            return 'rollback: the new firmware did not start well; the board runs %s again' % me.get('ver'), me
        if not answered and not restarted and (me.get('ota_code'), me.get('ota_fail')) != \
                (before.get('ota_code'), before.get('ota_fail')):
            return 'refused by the board: %s' % refusal(None, me), me
    if me and me.get('ver') == new_ver:
        return 'the new firmware did not mark itself good in %d s (pend %s, own_sig %s)' % (
            WAIT_S, me.get('pend'), me.get('own_sig')), me
    return 'the board did not come back with %s in %d s' % (new_ver, WAIT_S), me


def update_one(data, m, files, version, now):
    """One member: (the row, True when the run must stop)."""
    row = {'member': m['name']}
    me = state(data, m)
    if me is None:
        row.update(result=R_NOANS)
        return row, False
    kind, now_ver = me.get('kind', ''), me.get('ver', '')
    row.update(kind=kind, now=now_ver)
    if kind not in files:
        try:
            files[kind] = fetch_v(data, kind, version)
        except Failed as e:
            if R_NOCH in str(e):
                files[kind] = None
            else:
                row.update(result='no file: %s' % e)
                return row, True
    if files[kind] is None:
        row.update(result=R_NOCH)
        return row, False
    payload, new_ver = files[kind]
    row.update(to=new_ver)
    if not needs(now_ver, new_ver, bool(version)):
        row.update(result=R_SAME)
        return row, False
    if not now and me.get('app', '') not in IDLE_APPS:
        row.update(result='%s (%s is open)' % (R_BUSY, me.get('app')))
        return row, False
    minsv = int(me.get('minsv') or 0)
    code, kid, sv = check_file(data, payload, kind, new_ver, minsv)
    row.update(sig=code, key=kid or '', sec_ver=sv if sv is not None else '-', minsv=minsv)
    if code != 'ok':
        row.update(result='not sent: the check of the program says %s' % code)
        return row, True
    keys = board_keys(me)
    if keys is not None and kid not in keys:
        row.update(sig='sig_key', result='not sent: the board does not have the key %s' % kid)
        return row, True
    say('%s: send %s %s (%d KB, key %s, sec_ver %d)' % (m['name'], kind, new_ver, len(payload) // 1024, kid, sv))
    ans = send(data, m, payload)
    if ans is not None and not ans.get('ok'):
        row.update(sig=refusal(ans, None), result='refused by the board: %s' % (ans.get('msg') or ans.get('code')
                                                                              or 'no reason'))
        return row, True
    say('%s: wait for the restart and the self-test (up to %d s)' % (m['name'], WAIT_S))
    res, after = wait_new(data, m, me, new_ver, ans is not None)
    if after:
        row.update(now=after.get('ver', now_ver), board_sig=after.get('own_sig', ''),
                   board_minsv=after.get('minsv', ''))
    if res != R_OK:
        if res.startswith('refused'):
            row.update(sig=refusal(None, after))
        row.update(result=res)
        return row, True
    row.update(result=R_OK)
    return row, False


def row_text(r):
    return '  '.join('%s=%s' % (k, r[k]) for k in r)


def update(data, members, version=None, now=False):
    """The update of the members, one board at a time (design 2.10.4-6): the result rows. A failure stops the run:
    Failed (its "rows" = the rows; its text has them too)."""
    if version is not None:
        check_version(version)
    ms = members_of(data, members)
    channel(data)
    rows, files, stop = [], {}, None
    with data.change():
        for m in ms:
            if stop is not None:
                rows.append({'member': m['name'], 'result': R_STOP})
                continue
            row, bad = update_one(data, m, files, version, now)
            rows.append(row)
            data.log('update %s %s %s -> %s: %s%s' % (m['name'], row.get('kind', '?'), row.get('now', '?'),
                                                       row.get('to', '-'), row['result'],
                                                       ' (%s)' % row['sig'] if row.get('sig') else ''))
            if bad:
                stop = row
    if stop is not None:
        e = Failed('the update of %s stopped the run: %s\n%s' % (stop['member'], stop['result'],
                                                                 '\n'.join(row_text(r) for r in rows)))
        e.rows = rows
        raise e
    return rows
