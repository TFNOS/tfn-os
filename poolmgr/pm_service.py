#!/usr/bin/env python3
"""service (design 2.13): the service file that runs "poolmgr serve" at each logon of the user, for this system.

  Linux, Raspberry Pi   a systemd USER unit   ~/.config/systemd/user/tfn-poolmgr.service (at the logon of the user;
                        with "loginctl enable-linger" at each start of the computer)
  macOS                 a launchd agent       ~/Library/LaunchAgents/tfn-os.poolmgr.plist (at the logon of the user)
  Windows               a task of the Task Scheduler (schtasks /XML), at the logon of this user;
                        the task file is <data folder>/service/tfn-poolmgr-task.xml

All three run as the user (no administrator, no root), with the data folder of this pool. serve writes its lines into
log/serve.log of the data folder (pm_serve.py): the task of Windows runs pythonw.exe, which has no output. The task of
Windows gets the variable OPENSSL as "serve --openssl <path>" (a task has no variables of its own).

  text(data, program)            the file and the commands, as text ("poolmgr service" prints it; nothing changes)
  install(data, program, ...)    write the file and start the service; each step prints one line first. The data
                                 folder must have a pool (else InputError, and nothing is written)
  remove(data, program, ...)     stop the service and remove its file
  dry_run=True                   print the steps only: nothing is written, nothing starts

From the command line:
  python poolmgr/poolmgr.py [--data D] service [show | install | remove] [--dry-run] [--system linux|macos|windows]
  python poolmgr/pm_service.py [--data D] [show | install | remove] [--dry-run] [--system ...]   (the same)
"""
import argparse
import getpass
import os
import plistlib
import subprocess
import sys
from xml.etree import ElementTree
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pm_data  # noqa: E402
from pm_shared import Failed, InputError  # noqa: E402

DONE = True
SYSTEMS = ('linux', 'macos', 'windows')
UNIT = 'tfn-poolmgr.service'
LABEL = 'tfn-os.poolmgr'
TASK = 'TFN poolmgr'
TASK_FILE = 'tfn-poolmgr-task.xml'
SERVE_PORT = 47480
TASK_NS = 'http://schemas.microsoft.com/windows/2004/02/mit/task'


def this_system():
    if sys.platform == 'win32':
        return 'windows'
    if sys.platform == 'darwin':
        return 'macos'
    return 'linux'


class Step:
    """One command of an install or a remove. may_fail: a fault of the command does not stop the steps (for example
    the stop of a service that does not run)."""

    def __init__(self, argv, may_fail=False):
        self.argv, self.may_fail = list(argv), may_fail

    def text(self):
        return subprocess.list2cmdline(self.argv) if os.name == 'nt' else ' '.join(sh_quote(a) for a in self.argv)


class Plan:
    """The service of one system: the file (path, body bytes), the steps of install and remove, the notes."""

    def __init__(self, system, path, body, install, remove, notes, argv):
        self.system, self.path, self.body = system, path, body
        self.install, self.remove, self.notes, self.argv = install, remove, notes, argv


# ---------------------------------------------------------------- the quoting
def sh_quote(a):
    if a and all(c.isalnum() or c in '-_./:=@%+,' for c in a):
        return a
    return "'" + a.replace("'", "'\"'\"'") + "'"


def systemd_quote(a):
    """One word of ExecStart: '%' and '$' are special to systemd (%% and $$); a word with a space, a quote or a
    backslash goes in double quotes."""
    a = a.replace('%', '%%').replace('$', '$$')
    if a and not any(c in a for c in ' \t"\'\\;'):
        return a
    return '"' + a.replace('\\', '\\\\').replace('"', '\\"') + '"'


# ---------------------------------------------------------------- the plan of each system
def serve_argv(data, program, python=None, port=None, openssl=None):
    """The command line of the service: python, poolmgr.py, the data folder, serve (and --port, --openssl)."""
    argv = [python or sys.executable, os.path.abspath(program), '--data', data.root, 'serve']
    if port and int(port) != SERVE_PORT:
        argv += ['--port', str(int(port))]
    if openssl:
        argv += ['--openssl', openssl]
    return argv


def windows_python(python):
    """pythonw.exe next to python.exe (no console window at the logon), else python itself."""
    d, n = os.path.split(python)
    if n.lower() == 'python.exe':
        w = os.path.join(d, 'pythonw.exe')
        if os.path.isfile(w):
            return w
    return python


def env_extra():
    """The environment values that the service needs too (the place of openssl)."""
    return {k: os.environ[k] for k in ('OPENSSL',) if os.environ.get(k)}


def plan_linux(data, argv, home):
    path = os.path.join(home, '.config', 'systemd', 'user', UNIT)
    env = ''.join('Environment=%s\n' % systemd_quote('%s=%s' % kv) for kv in sorted(env_extra().items()))
    body = ('[Unit]\n'
            'Description=TFN OS pool manager (poolmgr serve)\n'
            '\n'
            '[Service]\n'
            'Type=simple\n'
            'ExecStart=%s\n'
            'Restart=on-failure\n'
            'RestartSec=10\n'
            'Environment=PYTHONUNBUFFERED=1\n'
            '%s'
            '\n'
            '[Install]\n'
            'WantedBy=default.target\n') % (' '.join(systemd_quote(a) for a in argv), env)
    user = os.environ.get('USER') or getpass.getuser()
    inst = [Step(['systemctl', '--user', 'daemon-reload']),
            Step(['systemctl', '--user', 'enable', '--now', UNIT])]
    rem = [Step(['systemctl', '--user', 'disable', '--now', UNIT], may_fail=True)]
    notes = ['the lines of serve: journalctl --user -u %s -f, or %s' % (UNIT, os.path.join(data.root, 'log',
                                                                                        'serve.log')),
             'the unit starts at each logon of your user; with no login (a Raspberry Pi with no screen), the user '
             'services start only after: loginctl enable-linger %s' % user]
    return Plan('linux', path, body.encode('utf-8'), inst, rem, notes, argv)


def plan_macos(data, argv, home):
    path = os.path.join(home, 'Library', 'LaunchAgents', LABEL + '.plist')
    log = os.path.join(data.root, 'log', 'serve.log')
    err = os.path.join(data.root, 'log', 'serve.err')
    env = {'PYTHONUNBUFFERED': '1'}
    env.update(env_extra())
    # (serve writes log/serve.log itself, with a size limit: its output goes to no file; a fault of Python goes to
    # serve.err)
    body = plistlib.dumps({'Label': LABEL, 'ProgramArguments': argv, 'RunAtLoad': True,
                           'KeepAlive': {'SuccessfulExit': False}, 'ThrottleInterval': 10,
                           'ProcessType': 'Background', 'StandardOutPath': '/dev/null', 'StandardErrorPath': err,
                           'EnvironmentVariables': env})
    uid = getattr(os, 'getuid', lambda: 501)()
    inst = [Step(['launchctl', 'bootout', 'gui/%d/%s' % (uid, LABEL)], may_fail=True),
            Step(['launchctl', 'bootstrap', 'gui/%d' % uid, path])]
    rem = [Step(['launchctl', 'bootout', 'gui/%d/%s' % (uid, LABEL)], may_fail=True)]
    notes = ['the lines of serve: %s (a fault of Python: %s)' % (log, err),
             'the agent starts at each logon of your user',
             'macOS asks for the firewall rule at the first listen: allow the PRIVATE network only']
    return Plan('macos', path, body, inst, rem, notes, argv)


def task_xml(argv, user):
    """The task of the Task Scheduler (version 1.2): at the logon of the user, as the user, no time limit, also on
    battery. RestartOnFailure is for a task that does not start; the guide does not count on it after serve stops
    with a fault (exit 2): the task can stay stopped until the next logon (or schtasks /Run)."""
    cmd = escape(argv[0])
    args = escape(subprocess.list2cmdline(argv[1:]))
    who = escape(user)
    return ('<?xml version="1.0" encoding="UTF-16"?>\n'
            '<Task version="1.2" xmlns="%s">\n'
            '  <RegistrationInfo>\n'
            '    <Description>TFN OS pool manager (poolmgr serve)</Description>\n'
            '  </RegistrationInfo>\n'
            '  <Triggers>\n'
            '    <LogonTrigger>\n'
            '      <Enabled>true</Enabled>\n'
            '      <UserId>%s</UserId>\n'
            '    </LogonTrigger>\n'
            '  </Triggers>\n'
            '  <Principals>\n'
            '    <Principal id="Author">\n'
            '      <UserId>%s</UserId>\n'
            '      <LogonType>InteractiveToken</LogonType>\n'
            '      <RunLevel>LeastPrivilege</RunLevel>\n'
            '    </Principal>\n'
            '  </Principals>\n'
            '  <Settings>\n'
            '    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n'
            '    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n'
            '    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n'
            '    <StartWhenAvailable>true</StartWhenAvailable>\n'
            '    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n'
            '    <RestartOnFailure>\n'
            '      <Interval>PT1M</Interval>\n'
            '      <Count>999</Count>\n'
            '    </RestartOnFailure>\n'
            '    <Enabled>true</Enabled>\n'
            '  </Settings>\n'
            '  <Actions Context="Author">\n'
            '    <Exec>\n'
            '      <Command>%s</Command>\n'
            '      <Arguments>%s</Arguments>\n'
            '    </Exec>\n'
            '  </Actions>\n'
            '</Task>\n') % (TASK_NS, who, who, cmd, args)


def task_command(body):
    """The command and the arguments in a task file of plan_windows (a check of the file)."""
    root = ElementTree.fromstring(body.decode('utf-16').split('\n', 1)[1])
    ex = root.find('{%s}Actions/{%s}Exec' % (TASK_NS, TASK_NS))
    return ex.find('{%s}Command' % TASK_NS).text, ex.find('{%s}Arguments' % TASK_NS).text


def plan_windows(data, argv, home):
    path = os.path.join(data.root, 'service', TASK_FILE)
    dom, user = os.environ.get('USERDOMAIN', ''), os.environ.get('USERNAME') or getpass.getuser()
    who = '%s\\%s' % (dom, user) if dom else user
    body = task_xml(argv, who).encode('utf-16')
    inst = [Step(['schtasks', '/Create', '/TN', TASK, '/XML', path, '/F']),
            Step(['schtasks', '/Run', '/TN', TASK])]
    rem = [Step(['schtasks', '/End', '/TN', TASK], may_fail=True),
           Step(['schtasks', '/Delete', '/TN', TASK, '/F'], may_fail=True)]
    notes = ['the lines of serve: %s (the task runs pythonw.exe: no window)' % os.path.join(data.root, 'log',
                                                                                           'serve.log'),
             'Windows asks for the firewall rule at the first listen: allow the PRIVATE network only (for '
             'pythonw.exe)',
             'the task starts at each logon of %s; Task Scheduler > Task Scheduler Library > %s shows it' %
             (who, TASK)]
    return Plan('windows', path, body, inst, rem, notes, argv)


def plan(data, program, system=None, home=None, python=None, port=None):
    """The Plan of the service of system (default: this system). home = the home folder of the user (default ~)."""
    system = system or this_system()
    if system not in SYSTEMS:
        raise InputError('the system is one of %s' % ', '.join(SYSTEMS))
    home = home or os.path.expanduser('~')
    py = python or sys.executable
    if system == 'windows' and python is None:
        py = windows_python(py)
    # (a task of Windows has no variables: the place of openssl goes on its command line)
    ssl = env_extra().get('OPENSSL') if system == 'windows' else None
    argv = serve_argv(data, program, py, port, ssl)
    return {'linux': plan_linux, 'macos': plan_macos, 'windows': plan_windows}[system](data, argv, home)


def body_text(p):
    return p.body.decode('utf-16' if p.system == 'windows' else 'utf-8')


def text(data, program):
    """(the text of the service file, where it goes and how to start it) for this system; program = the path of
    poolmgr.py."""
    p = plan(data, program)
    lines = ['THE FILE: %s' % p.path,
             'TO START IT: python %s --data %s service install' % (program, data.root),
             '  (it writes the file, then runs:)']
    lines += ['    %s' % s.text() for s in p.install]
    lines += ['TO STOP AND REMOVE IT: python %s --data %s service remove' % (program, data.root),
              '  (it runs:)']
    lines += ['    %s' % s.text() for s in p.remove]
    lines += ['    (then it removes the file)']
    lines += ['NOTE: %s' % n for n in p.notes]
    return body_text(p), '\n'.join(lines)


# ---------------------------------------------------------------- install and remove
def runner(step):
    """Run one command of a step: the CompletedProcess."""
    return subprocess.run(step.argv, capture_output=True, encoding='utf-8', errors='replace')


def check_system(p, dry_run, run):
    if not dry_run and run is None and p.system != this_system():
        raise InputError('a service of %s installs only on that system (here: %s); use --dry-run to see the steps'
                         % (p.system, this_system()))


def do_steps(steps, dry_run, run, out):
    for s in steps:
        out('RUN %s' % s.text())
        if dry_run:
            continue
        try:
            r = (run or runner)(s)
            code, why = r.returncode, ((r.stdout or '') + (r.stderr or '')).strip()
        except OSError as e:
            code, why = -1, str(e)
        if code and not s.may_fail:
            raise Failed('the command "%s" stopped with the exit code %d: %s' % (s.text(), code, why[-300:]))
        if code:
            out('  (exit code %d: no problem here)' % code)


def install(data, program, dry_run=False, system=None, home=None, run=None, out=print, port=None):
    """Write the service file and start the service (each step prints one line first). dry_run: print only.
    run(step) = the function that runs a command (default subprocess.run): the tests give a fake. The data folder
    must have a pool: a service of serve with no pool stops at each start. The exit code."""
    p = plan(data, program, system, home, port=port)
    check_system(p, dry_run, run)
    if not data.has_pool():
        if not dry_run:
            raise pm_data.no_pool(data.root)
        out('NOTE: %s has no pool: "service install" stops until the folder has one' % data.root)
    out('SERVICE %s: %s' % (p.system, ' '.join(sh_quote(a) for a in p.argv)))
    out('WRITE %s' % p.path)
    if not dry_run:
        inside = os.path.dirname(p.path).startswith(data.root + os.sep)
        os.makedirs(os.path.dirname(p.path), mode=0o700 if inside else 0o777, exist_ok=True)
        with open(p.path, 'wb') as h:
            h.write(p.body)
    do_steps(p.install, dry_run, run, out)
    for n in p.notes:
        out('NOTE: %s' % n)
    out('DRY RUN: nothing was written or started' if dry_run else 'SERVICE INSTALLED: "poolmgr serve" starts by itself '
        'now and at each logon of your user')
    return 0


def remove(data, program, dry_run=False, system=None, home=None, run=None, out=print):
    """Stop the service and remove its file. dry_run: print only. The exit code."""
    p = plan(data, program, system, home)
    check_system(p, dry_run, run)
    do_steps(p.remove, dry_run, run, out)
    if os.path.isfile(p.path):
        out('DELETE %s' % p.path)
        if not dry_run:
            os.remove(p.path)
    else:
        out('no file %s' % p.path)
    if p.system == 'linux':
        do_steps([Step(['systemctl', '--user', 'daemon-reload'], may_fail=True)], dry_run, run, out)
    out('DRY RUN: nothing was changed' if dry_run else 'SERVICE REMOVED')
    return 0


# ---------------------------------------------------------------- the command line
def add_args(p):
    """The arguments of "service" (for poolmgr.py): [show|install|remove] [--dry-run] [--system S]."""
    p.add_argument('op', nargs='?', choices=('show', 'install', 'remove'), default='show',
                   help='show (default): print the file and the steps; install; remove')
    p.add_argument('--dry-run', action='store_true', help='print the steps only: nothing is written or started')
    p.add_argument('--system', choices=SYSTEMS, help='the file of another system (only with show or --dry-run)')
    p.add_argument('--port', type=int, help='the TCP port of "serve" (default %d)' % SERVE_PORT)


def command(a, data, program):
    """Do the "service" command of the arguments of add_args(): the exit code."""
    if a.op == 'install':
        return install(data, program, a.dry_run, a.system, port=a.port)
    if a.op == 'remove':
        return remove(data, program, a.dry_run, a.system)
    p = plan(data, program, a.system, port=a.port)
    if a.system:
        print(body_text(p))
        print('THE FILE: %s' % p.path)
        return 0
    body, where = text(data, program)
    print(body)
    print(where)
    return 0


def main(argv=None):
    import pm_data
    ap = argparse.ArgumentParser(prog='pm_service', description='The service that runs "poolmgr serve" (a systemd '
                                 'user unit, a launchd agent or a task of the Task Scheduler).')
    ap.add_argument('--data', help='the data folder (default: TFN_POOLMGR_DATA, else ~/.tfn-poolmgr)')
    add_args(ap)
    a = ap.parse_args(argv)
    data = pm_data.Data(a.data)
    try:
        return command(a, data, os.path.join(HERE, 'poolmgr.py'))
    except Failed as e:
        print('FAILED: %s' % e)
        return 1
    except InputError as e:
        print('STOP: %s' % e)
        return 2


if __name__ == '__main__':
    sys.exit(main())
