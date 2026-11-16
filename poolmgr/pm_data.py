"""The data folder of one pool (design 2.3): config.json, the keys, the pool folder, the addresses, the change lock,
the log, the rights of the folder.

  <root>/                  mode 0700; on Windows only this user, SYSTEM and the administrators (protect())
    config.json            {"format", "store", "port", "auto_update", "channel", "lan_url"}
    keys/admin_a.pem       key A (0600)
    keys/store_pool.pem    the pool store key (0600; only after "store key")
    pool/                  the folder of pool_tool.py: pool.json, secrets.json, roster.bin, webpass.json
                           (and store_tok.json: the store token of each member; friends_out.json: the friend entries
                           that wait for their removal; recover.json: the state of the last "recover", the old
                           request keys of the boards that it did not reach yet, "recover" again sends to them)
    service/               the task file of the service on Windows ("service install")
    addrs.json             MAC (upper hex) -> {"ip", "t", "app", "id"}
    lock                   the change lock (one change of the pool at a time)
    cache/fw/, cache/store/
    log/poolmgr.log        one line for each change and each refusal (no secret, no key value)
    log/serve.log          the lines of "serve" (pm_serve.py; serve.log.1 = the lines before)

WHO MAKES THE FOLDER: only "init" and "restore" make a data folder. Each other command first checks that the folder
has a pool (need_pool(), change()), so a command that stops with "no pool" leaves the folder as it was.

The change lock is a lock of the operating system on the file "lock" (fcntl on Linux and macOS, msvcrt on Windows),
with the process ID in the file. The system releases the lock when the process stops, so the lock of a process that
is gone is free at once.

THE RIGHTS (protect(), rights()): on Linux and macOS the folder has the mode 0700. On Windows the program sets the
access list of a folder that it makes: no inherited rights, full control for this user, SYSTEM and the
administrators (the icacls program; the SID of the user from whoami). rights() finds the other accounts with rights
on a folder (doctor shows them with the command that repairs the folder; with no SID of the user, no command: the
list can have the user too; doctor also checks keys/ and pool/). init and restore give keys/ and pool/ their own
rights before the first key file goes into them (private_tmp()), also in a data folder that exists; "store key" and
"recover" do it again before they write a key file (private_parts(): a data folder of an older poolmgr can have keys/
and pool/ with the rights of the folder), and the first change lock of each command does it for each part that
another account can read (secure_parts()). So a key never takes the rights of a folder that other accounts can read.
Each file with a key or a request key is in keys/ or pool/ (also pool/recover.json). The files that the program writes
outside the data folder (the .tfnkey file of key R, a .tfnbak backup) get the rights of protect_file() before their
content goes into them.

The programs of Windows (whoami, icacls) start with no console window (NO_WINDOW): the service of Windows runs
pythonw.exe, which has no console, so each program would open a window of its own.
"""
import contextlib
import getpass
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

from pm_shared import InputError, pool

FORMAT = 1
DEFAULT_CFG = {'format': FORMAT, 'store': 'official', 'port': 47480, 'auto_update': False, 'channel': {},
               'lan_url': ''}
LOCK_WAIT = 30.0
ADDR_KEEP = 60
DONE = True
NO_POOL = 'no pool in %s (a new pool: poolmgr init <name>; a pool from a backup: poolmgr restore <file>)'
# the folders of a data folder (a data folder of an older poolmgr can have them empty, with no pool)
PARTS = ('keys', 'pool', 'log', 'cache', 'service')
SYSTEM_SID, ADMINS_SID = 'S-1-5-18', 'S-1-5-32-544'
# the SIDs that the short names of an access list (SDDL) stand for
SDDL_SIDS = {'WD': 'S-1-1-0', 'CO': 'S-1-3-0', 'OW': 'S-1-3-4', 'NU': 'S-1-5-2', 'IU': 'S-1-5-4', 'SU': 'S-1-5-6',
             'AN': 'S-1-5-7', 'AU': 'S-1-5-11', 'SY': SYSTEM_SID, 'LS': 'S-1-5-19', 'NS': 'S-1-5-20',
             'BA': ADMINS_SID, 'BU': 'S-1-5-32-545', 'BG': 'S-1-5-32-546', 'PU': 'S-1-5-32-547',
             'RD': 'S-1-5-32-555', 'AC': 'S-1-15-2-1'}
SDDL_NAMES = {'S-1-1-0': 'Everyone', 'S-1-5-11': 'Authenticated Users', 'S-1-5-32-545': 'Users',
              'S-1-5-4': 'INTERACTIVE', 'S-1-5-7': 'ANONYMOUS LOGON', 'S-1-5-32-546': 'Guests'}
# OWNER RIGHTS and CREATOR OWNER (the account that makes a file) are not other accounts
KEEP_SIDS = (SYSTEM_SID, ADMINS_SID, 'S-1-3-4', 'S-1-3-0')
# the built-in Administrator account of the computer (SDDL "LA"; its SID ends with -500) is an administrator too
ADMIN_ACCOUNT_RX = re.compile(r'^(LA|S-1-5-21-\d+-\d+-\d+-500)$')
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)   # the creationflags of a program (0 on Linux and macOS)
ACE_RX = re.compile(r'\(([^;()]*);([^;()]*);([^;()]*);([^;()]*);([^;()]*);([^;()]*)\)')


def default_root():
    return os.environ.get('TFN_POOLMGR_DATA') or os.path.join(os.path.expanduser('~'), '.tfn-poolmgr')


def no_pool(root):
    """The InputError of a data folder with no pool."""
    return InputError(NO_POOL % root)


def damaged(what, why):
    """The InputError of a damaged file of the data folder (exit 2)."""
    return InputError('a file of the data folder is damaged: %s (%s). Repair the file, or restore the backup into a '
                      'new data folder (poolmgr --data <new folder> restore <file>)' % (what, why))


def write_file(path, data, mode=0o600, private=False):
    """Write a file in one step (a temporary file of this process, then a replace), with the mode for a new file.
    Windows refuses a replace while another process reads the file: the replace tries again for 1 s. A fault (also
    Ctrl+C) removes the temporary file: no copy of the data stays next to the file. private: the temporary file gets
    the rights of protect_file() before the data goes into it (a file outside the data folder); the result is then
    the fault of the rights ('' = done), else ''."""
    tmp = '%s.%d.tmp' % (path, os.getpid())
    err = ''
    try:
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, 'O_BINARY', 0), mode)
        with os.fdopen(fd, 'wb') as h:
            if private:
                err = protect_file(tmp)
            h.write(data)
        retry(lambda: os.replace(tmp, path))
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise
    return err


def read_strict(path, default, kind, form):
    """The JSON value of a file of the data folder: default when the file is not there. InputError (damaged()) when
    the file is there and is not JSON of the type kind (form = the words of its form): the program stops with the
    name of the file, and it does not write over it."""
    try:
        with open(path, 'rb') as h:
            raw = h.read()
    except FileNotFoundError:
        return default
    try:
        v = json.loads(raw.decode('utf-8'))
    except ValueError as e:
        raise damaged(path, e)
    if not isinstance(v, kind):
        raise damaged(path, 'not the form %s' % form)
    return v


def addr_good(a):
    """An entry of addrs.json that the program can use: "ip" is text and "t" is an integer (set_addr writes both)."""
    t = a.get('t')
    return isinstance(a.get('ip'), str) and isinstance(t, int) and not isinstance(t, bool)


def retry(fn, tries=20):
    """fn() again for 1 s while Windows refuses it (another process reads the file: a virus scanner, an index)."""
    for i in range(tries):
        try:
            return fn()
        except PermissionError:
            if i == tries - 1:
                raise
            time.sleep(0.05)


class Data:
    """The data folder of one pool. Data(root): root = --data, TFN_POOLMGR_DATA or ~/.tfn-poolmgr."""

    def __init__(self, root=None):
        self.root = os.path.abspath(os.path.expanduser(root or default_root()))
        self.cfg = dict(DEFAULT_CFG)
        self.pool_dir = os.path.join(self.root, 'pool')
        self.key_a = os.path.join(self.root, 'keys', 'admin_a.pem')
        self.key_store = os.path.join(self.root, 'keys', 'store_pool.pem')
        self.cfg_file = os.path.join(self.root, 'config.json')
        self.addrs_file = os.path.join(self.root, 'addrs.json')
        self.lock_file = os.path.join(self.root, 'lock')
        self.log_file = os.path.join(self.root, 'log', 'poolmgr.log')
        self.cache = os.path.join(self.root, 'cache')
        self.pool = None
        self.lock_h = None
        self.lock_n = 0
        self.parts_done = False   # secure_parts() ran for this Data

    # ---- the folder
    def has_pool(self):
        return os.path.isfile(os.path.join(self.pool_dir, 'pool.json'))

    def need_pool(self):
        """The pool (pool.Pool); InputError with the text of NO_POOL when the folder has none. Nothing is made."""
        if self.pool is None:
            self.load()
        if self.pool is None:
            raise no_pool(self.root)
        return self.pool

    def make_root(self):
        """Make the data folder itself (mode 0700; on Windows the rights of protect()). True when this call made
        it; a folder that exists stays as it is."""
        if os.path.isdir(self.root):
            return False
        os.makedirs(self.root, mode=0o700)
        protect(self.root)
        return True

    def make(self):
        """Make the folder and its parts (mode 0700): only init, restore and the writes of an existing pool. A
        folder that exists stays as it is. True when this call made the data folder."""
        made = self.make_root()
        for d in (os.path.join(self.root, 'keys'), self.pool_dir, os.path.dirname(self.log_file),
                  os.path.join(self.cache, 'fw'), os.path.join(self.cache, 'store')):
            os.makedirs(d, mode=0o700, exist_ok=True)
        return made

    def private_parts(self):
        """keys/ and pool/ get their own rights (protect()) before "store key" or "recover" writes a key file into
        them: a data folder of an older poolmgr can have them with the rights of the root. InputError when the rights
        do not set (then no key file is written)."""
        for d in (os.path.join(self.root, 'keys'), self.pool_dir):
            os.makedirs(d, mode=0o700, exist_ok=True)
            err = protect(d)
            if err:
                raise InputError('the rights of %s do not set (%s): no key file is written' % (d, err))

    def secure_parts(self):
        """Once for each Data, at the first change lock: keys/ and pool/ with the rights of another account (a data
        folder of an older poolmgr) get their own rights (protect()) before a command writes a pool key, request keys
        or web passwords into them. A part with its own rights stays as it is. When the rights do not read or do not
        set (for example a drive with no access lists), a WARN line tells it and the command goes on: doctor shows the
        rights and the command that repairs them."""
        if self.parts_done:
            return
        self.parts_done = True
        for d in (os.path.join(self.root, 'keys'), self.pool_dir):
            if not os.path.isdir(d):
                continue
            try:
                others = rights(d)[0]
            except OSError:
                continue
            err = protect(d) if others else ''
            if err:
                print('WARN: the rights of %s do not set (%s): other accounts can read it (poolmgr doctor)' % (d, err))

    def load(self):
        """Read config.json and the pool (pool.Pool), or None when the folder has no pool. InputError (with the name
        of the file) for a damaged file: then the values of the last good read stay (serve goes on with them)."""
        if os.path.isfile(self.cfg_file):
            with open(self.cfg_file, 'rb') as h:
                try:
                    c = json.loads(h.read().decode('utf-8'))
                except ValueError as e:
                    raise damaged(self.cfg_file, e)
            if not isinstance(c, dict):
                raise damaged(self.cfg_file, 'no JSON object')
            if not isinstance(c.get('channel', {}), dict):
                raise damaged(self.cfg_file, '"channel" is not a JSON object')
            if c.get('format') != FORMAT:
                raise InputError('%s has the format %r; this program reads format %d' %
                                 (self.cfg_file, c.get('format'), FORMAT))
            self.cfg = dict(DEFAULT_CFG, **c)
        if not self.has_pool():
            self.pool = None
            return None
        try:
            self.pool = pool.Pool.load(self.pool_dir)
        except (ValueError, KeyError, TypeError, AttributeError) as e:
            raise damaged('%s or %s' % (os.path.join(self.pool_dir, 'pool.json'), 'secrets.json'), '%s %s' % (
                type(e).__name__, e))
        return self.pool

    def save_cfg(self):
        self.make_root()
        write_file(self.cfg_file, (json.dumps(self.cfg, indent=1, sort_keys=True) + '\n').encode('utf-8'))

    # ---- the change lock
    @contextlib.contextmanager
    def change(self, wait=LOCK_WAIT, new=False):
        """The change lock: one change of the pool at a time (the CLI and serve). The same process can take it again
        (a counter). The folder must have a pool (else InputError: no pool, and nothing is made); new=True: the lock
        of init and restore in a folder that exists (make_root()) and has no pool yet."""
        if self.lock_n:
            self.lock_n += 1
            try:
                yield self
            finally:
                self.lock_n -= 1
            return
        if not new and not self.has_pool():
            raise no_pool(self.root)
        h = open(self.lock_file, 'a+b')
        end = time.time() + wait
        while not try_lock(h):
            if time.time() >= end:
                h.close()
                raise InputError('THE POOL IS BUSY: another poolmgr changes it (%s)' % self.lock_file)
            time.sleep(0.1)
        h.seek(0)
        h.truncate()
        h.write(b'%d\n' % os.getpid())
        h.flush()
        self.lock_h, self.lock_n = h, 1
        try:
            if not new:
                self.secure_parts()
            yield self
        finally:
            self.lock_n = 0
            self.lock_h = None
            unlock(h)
            h.close()

    # ---- the addresses
    def addrs(self):
        """MAC (upper hex) -> {'ip', 't', 'app', 'id'}. {} for no file and for a file that is not a JSON object (the
        addresses are only a cache: the beacons give them again); an entry that is not an object, or that has an "ip"
        that is not text or a "t" that is not an integer, goes out."""
        try:
            with open(self.addrs_file, 'rb') as h:
                a = json.loads(h.read().decode('utf-8'))
        except (FileNotFoundError, ValueError):
            return {}
        if not isinstance(a, dict):
            return {}
        return {k: v for k, v in a.items() if isinstance(v, dict) and addr_good(v)}

    def set_addr(self, mac, ip, app, dev_id):
        """Keep the address of a board. The file changes when a value changes, or when the time of the entry is
        older than ADDR_KEEP s (serve gets a beacon each 2 s). A folder with no pool keeps no address."""
        if not self.has_pool():
            return
        a = self.addrs()
        now = int(time.time())
        old = a.get(mac.upper()) or {}
        if (old.get('ip'), old.get('app'), old.get('id')) == (ip, app, dev_id) and now - old.get('t', 0) < ADDR_KEEP:
            return
        a[mac.upper()] = {'ip': ip, 't': now, 'app': app, 'id': dev_id}
        write_file(self.addrs_file, (json.dumps(a, indent=1, sort_keys=True) + '\n').encode('utf-8'))

    # ---- the log
    def log(self, text):
        """One line in log/poolmgr.log (never a secret or a key value). A folder that is not there gets no log."""
        if not os.path.isdir(self.root):
            return
        os.makedirs(os.path.dirname(self.log_file), mode=0o700, exist_ok=True)
        line = '%s %s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), text.replace('\n', ' '))
        with open(self.log_file, 'ab') as h:
            h.write(line.encode('utf-8'))


def leftovers(root):
    """The names in the folder root that are not the empty parts of a data folder with no pool (PARTS with no file in
    them, the change lock, and on Windows the task file of "service install" of an older poolmgr). [] = root can take
    a restore or an init."""
    bad = []
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name)
        if name == 'lock' and os.path.isfile(p) and os.path.getsize(p) <= 64:
            continue
        if name in PARTS and os.path.isdir(p) and empty_tree(p, name == 'service'):
            continue
        bad.append(name)
    return bad


def empty_tree(d, service=False):
    """True when the folder d has no file (only empty folders); service: the task file of Windows can be there."""
    for base, _, names in os.walk(d):
        for n in names:
            if not (service and base == d and n == 'tfn-poolmgr-task.xml'):
                return False
    return True


def move_parts(root, tmp, parts):
    """Move the parts (file and folder names) of the folder tmp into the data folder root, in the order of parts
    (pool last: then the pool is there). A part that is not in tmp is skipped; an empty folder of the same name in
    root goes first. A fault moves the parts back into tmp (the caller removes tmp)."""
    done = []
    try:
        for part in parts:
            src, dst = os.path.join(tmp, part), os.path.join(root, part)
            if not os.path.exists(src):
                continue
            if os.path.isdir(dst) and not os.listdir(dst):
                os.rmdir(dst)   # (an empty part of an older poolmgr)
            retry(lambda: os.replace(src, dst))
            done.append((src, dst))
    except BaseException:
        for src, dst in reversed(done):
            try:
                os.replace(dst, src)
            except OSError:
                pass
        raise


def remove_tree(path):
    """Remove a folder of this program (Windows: again while another process reads a file). True when it is gone."""
    for i in range(20):
        try:
            shutil.rmtree(path)
            return True
        except FileNotFoundError:
            return True
        except OSError:
            time.sleep(0.05)
    return not os.path.exists(path)


def private_tmp(parent, prefix):
    """A temporary folder in parent for the parts of init and restore, with the folders keys/ and pool/ in it. The
    three folders get their own rights (protect()) before a key file goes into them: on Windows, Python before 3.12.4
    gives a temporary folder the inherited rights of parent, and parent can be a folder that other accounts can read.
    keys/ and pool/ keep their own rights after the move into the data folder. InputError when the rights do not set
    (then no folder stays)."""
    tmp = tempfile.mkdtemp(prefix=prefix, dir=parent)
    try:
        for d in (tmp, os.path.join(tmp, 'keys'), os.path.join(tmp, 'pool')):
            os.makedirs(d, mode=0o700, exist_ok=True)
            err = protect(d)
            if err:
                raise InputError('the rights of %s do not set (%s): no key file is written' % (d, err))
    except BaseException:
        remove_tree(tmp)
        raise
    return tmp


def drop_root(root):
    """After a failed init or restore that made the data folder: remove the folder when only the change lock is in
    it."""
    try:
        names = os.listdir(root)
        if set(names) <= {'lock'}:
            for n in names:
                os.remove(os.path.join(root, n))
            os.rmdir(root)
    except OSError:
        pass


# ---------------------------------------------------------------- the rights of the folder
def win_tool(name):
    """The full path of a program of Windows (System32): the search never takes a program of the current folder."""
    return os.path.join(os.environ.get('SystemRoot') or r'C:\Windows', 'System32', name)


_SID = []


def user_sid():
    """The SID of this user (whoami /user, one time in a process), else None."""
    if _SID:
        return _SID[0]
    try:
        r = subprocess.run([win_tool('whoami.exe'), '/user', '/fo', 'csv', '/nh'], capture_output=True, timeout=30,
                           creationflags=NO_WINDOW)
        found = re.findall(rb'S-1-\d+(?:-\d+)+', r.stdout or b'') if r.returncode == 0 else []
    except (OSError, subprocess.SubprocessError):
        found = []
    _SID.append(found[-1].decode('ascii') if found else None)
    return _SID[0]


def user_name():
    """DOMAIN\\USER of this user (the place of the SID when whoami does not answer)."""
    dom, user = os.environ.get('USERDOMAIN', ''), os.environ.get('USERNAME') or getpass.getuser()
    return '%s\\%s' % (dom, user) if dom else user


def grant_args(path, remove=(), folder=True):
    """The arguments of icacls that give the folder path the rights of a data folder: no inherited rights, full
    control for this user, SYSTEM and the administrators; remove = the SIDs of other accounts with their own entry.
    folder False: the same rights for a file (no inheritance flags)."""
    sid = user_sid()
    me = '*' + sid if sid else user_name()
    inh = '(OI)(CI)' if folder else ''
    args = ['icacls', path, '/inheritance:r', '/grant:r', '%s:%sF' % (me, inh), '*%s:%sF' % (SYSTEM_SID, inh),
            '*%s:%sF' % (ADMINS_SID, inh)]
    for s in remove:
        args += ['/remove:g', '*' + s]
    return args


def protect(path, folder=True):
    """Give the folder path the rights of a data folder (Windows: icacls; else mode 0700). '' = done, else the
    fault. folder False: a file (Windows: the same rights with no inheritance flags; else mode 0600)."""
    if sys.platform != 'win32':
        try:
            os.chmod(path, 0o700 if folder else 0o600)
            return ''
        except OSError as e:
            return str(e)
    args = grant_args(path, folder=folder)
    try:
        r = subprocess.run([win_tool('icacls.exe')] + args[1:], capture_output=True, timeout=60,
                           creationflags=NO_WINDOW)
    except (OSError, subprocess.SubprocessError) as e:
        return 'icacls: %s' % e
    return '' if r.returncode == 0 else 'icacls: exit %d %s' % (
        r.returncode, (r.stdout + r.stderr).decode('utf-8', 'replace').strip()[-200:])


def protect_file(path):
    """Give the file path only the rights of this user, SYSTEM and the administrators (the .tfnkey file of key R and
    a .tfnbak backup in the current folder, which can give rights to other accounts). '' = done, else the fault (for
    example a USB stick with no access lists)."""
    return protect(path, folder=False)


def file_warn(path, err):
    """The WARN line of a key file or a backup whose rights did not set ('' when err is '')."""
    if not err:
        return ''
    return ('WARN: the rights of %s do not set (%s): other accounts of this computer can have rights on it; keep it '
            'on a USB stick or in a folder of only your user' % (path, err))


def aces(path):
    """The entries of the access list of path (Windows): a list of (type, flags, SID). OSError when icacls cannot
    read it."""
    fd, tmp = tempfile.mkstemp(prefix='tfnpm_acl_', suffix='.txt')
    os.close(fd)
    try:
        r = subprocess.run([win_tool('icacls.exe'), path, '/save', tmp], capture_output=True, timeout=60,
                           creationflags=NO_WINDOW)
        if r.returncode:
            raise OSError('icacls: exit %d %s' % (r.returncode, (r.stdout + r.stderr).decode('utf-8', 'replace')
                                                  .strip()[-200:]))
        with open(tmp, 'rb') as h:
            text = h.read().decode('utf-16-le', 'replace').lstrip('\ufeff')
    finally:
        try:
            os.remove(tmp)
        except OSError:
            pass
    dacl = text.split('D:', 1)[1] if 'D:' in text else ''
    return [(t, f, SDDL_SIDS.get(s, s)) for t, f, _, _, _, s in ACE_RX.findall(dacl.split('S:', 1)[0])]


def rights(path):
    """The other accounts with rights on the folder path: (a list of texts, the command that repairs the folder).
    ([], '') when only this user, SYSTEM and the administrators have rights. OSError when the rights do not read. On
    Windows with no SID of this user (whoami does not answer), the list can have this user too, and the command is ''
    (a /remove:g of that SID would remove the own entry of this user)."""
    if sys.platform != 'win32':
        mode = os.stat(path).st_mode & 0o777
        if mode & 0o077:
            return ['the mode %03o (the group or the other users can read it)' % mode], 'chmod 700 %s' % sh_word(path)
        return [], ''
    me = user_sid()
    keep = set(KEEP_SIDS) | ({me} if me else set())
    others, own = [], []
    for kind, flags, sid in aces(path):
        if kind not in ('A', 'OA') or sid in keep or ADMIN_ACCOUNT_RX.match(sid):
            continue
        name = SDDL_NAMES.get(sid, sid)
        if name not in others:
            others.append(name)
        if 'ID' not in flags and sid.startswith('S-1-') and sid not in own:
            own.append(sid)
    if not others:
        return [], ''
    if not me:
        return others, ''
    return others, ' '.join(cmd_word(a) for a in grant_args(path, own))


def cmd_word(a):
    """A word of the repair command as cmd.exe and PowerShell both read it."""
    if a == 'icacls' or (a.startswith('/') and ' ' not in a):
        return a
    return '"%s"' % a


def sh_word(a):
    if a and all(c.isalnum() or c in '-_./:=@%+,~' for c in a):
        return a
    return "'" + a.replace("'", "'\"'\"'") + "'"


def rights_lines(path, what='the data folder'):
    """The texts of the WARN lines of rights(path) for a person (doctor, init, restore): [] when the rights are
    good. what = the words for the folder (the data folder, or its part keys/ or pool/)."""
    try:
        others, fix = rights(path)
    except OSError as e:
        return ['the rights of %s do not read (%s)' % (path, e)]
    if not others:
        return []
    if sys.platform != 'win32':
        return ['%s %s has %s' % (what, path, others[0]),
                'the command that gives the rights only to your user: %s' % fix]
    if not fix:
        return ['these accounts have rights on %s %s: %s (icacls "%s" shows their names)' % (
                    what, path, ', '.join(others), path),
                'the SID of your user is not known (whoami.exe gives no answer), so the list can have your user too, '
                'and poolmgr gives no command that repairs the folder: look at the names with icacls']
    return ['other accounts have rights on %s %s: %s (icacls "%s" shows their names)' % (
                what, path, ', '.join(others), path),
            'the command that gives the rights only to your user, SYSTEM and the administrators: %s' % fix]


def all_rights_lines(root):
    """rights_lines() of the data folder root and of its parts keys/ and pool/ (when they are there): a part can
    have other rights than the root (a data folder of an older poolmgr, or a change by a person)."""
    lines = rights_lines(root)
    for part in ('keys', 'pool'):
        d = os.path.join(root, part)
        if os.path.isdir(d):
            lines += rights_lines(d, 'the part %s/ of the data folder' % part)
    return lines


# ---------------------------------------------------------------- the lock of the system
if sys.platform == 'win32':
    import msvcrt

    def try_lock(h):
        h.seek(0)
        try:
            msvcrt.locking(h.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False

    def unlock(h):
        h.seek(0)
        try:
            msvcrt.locking(h.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
else:
    import fcntl

    def try_lock(h):
        try:
            fcntl.flock(h.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False

    def unlock(h):
        fcntl.flock(h.fileno(), fcntl.LOCK_UN)
