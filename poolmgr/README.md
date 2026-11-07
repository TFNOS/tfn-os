# poolmgr: the pool manager of TFN OS

`poolmgr` keeps a pool of up to 16 TFN OS boards from one computer (Windows, macOS, Linux or a Raspberry Pi): the
keys and the backup, the JOIN with the 6 digits, the friends, the firmware updates, a local store and the recovery.
It needs Python 3.9 or later and the `openssl` program (`pyserial` only for `--console`). On macOS, the Python of
the system has no scrypt: it cannot open a backup of another computer (`the file needs scrypt`); install Python
from python.org or with Homebrew for a restore. The full guide is `docs/poolmgr.md` of the repo `tfn-os`.

## The start

```
python poolmgr/poolmgr.py doctor                 check this computer
python poolmgr/poolmgr.py init "OUR HOME"        the pool, key A, key R (encrypted), the first backup
```

Copy `OUR_HOME-recovery.tfnkey` and `OUR_HOME-backup.tfnbak` to a USB stick or to another computer.

On each board: SETUP > POOL > JOIN (a board with a web password keeps it: `adopt` asks for it), and on the
computer:

```
python poolmgr/poolmgr.py adopt --all            compare the 6 digits, tap ALLOW on each board
python poolmgr/poolmgr.py list --live            the members and their state
python poolmgr/poolmgr.py backup OUR_HOME-backup.tfnbak
python poolmgr/poolmgr.py update check           the firmware of the boards and the newest release
python poolmgr/poolmgr.py service install        "serve" starts at each logon of your user
```

`python poolmgr/poolmgr.py --help` lists all commands, and `<command> --help` tells one command. The exit code:
0 = OK, 1 = a board refused or a check failed, 2 = a fault of the input or of the program. `doctor` also checks the
data folder: its rights (with the command that repairs them) and key A.

The release zip `tfn-poolmgr-<version>.zip` has this folder with copies of the shared modules (`pool.py`,
`pool_crypto.py`, `pool_tool.py`, `fw_sign.py`, `store_index.py`) and `channel.json` (the web site, the repo and the
public keys of the release). In a git checkout, the program finds the modules in `../sdk/tools/`, and `init` needs
`--keys <channel.json of a release>`. Do not use `tfn-keys.json` there: it has only the keys and no web site, so
`update` and `store sync` cannot work. `poolmgr channel <channel.json>` gives a pool that exists the web site of
the releases later (the official keys must be the same), and `poolmgr channel` shows it (the guide, sections 2.2
and 2.3).
