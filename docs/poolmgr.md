# The pool manager `poolmgr`

`poolmgr` keeps a POOL of TFN OS boards from one computer (Windows, macOS, Linux or a Raspberry Pi). A pool is a group
of up to 16 boards that trust each other: the members find each other on the LAN, the games for 2 boards and WALKIE
work between them, and the packets of other boards do not get in. [pools.md](pools.md) tells how a pool works on the
boards.

`poolmgr` does these jobs:
- it makes the pool, its keys and an encrypted backup;
- it adopts each board with the 6 digits (the JOIN);
- it makes the members friends, and it gives each member with no web password a random one;
- it updates the firmware of the members from the public releases, one board at a time;
- it sets the store of the members: the official store, or a local store on the computer with the pool's own apps;
- it gives the way back when the computer is lost or stolen (the recovery key R).

`poolmgr` is a command line program with one background command (`serve`). It has no web page and no admin route on
the network. A change of the pool comes from a command that you type on the computer, or from `serve` after a LEAVE
on a board (section 11).

In this guide, `poolmgr <command>` means `python poolmgr/poolmgr.py <command>`. On Linux and macOS, write `python3`
in place of `python` when your system has no `python` command.

## 1. What you need

- The boards, with the firmware of a public release of TFN OS, on the same LAN (the same WiFi network) as the
  computer. The boards send their JOIN packets and their beacons to the LAN broadcast, so the computer must be in the
  same subnet.
- The web password of a board, when the board has one (SETUP > WEB PASSWORD). `adopt` asks for it, and the board
  keeps it (section 5.1).
- Python 3.9 or later. The program uses only the standard library of Python. On macOS, the Python of the system
  (3.9.6) has no `hashlib.scrypt`. The program then writes its encrypted files with PBKDF2, and it cannot open a
  `.tfnkey` or `.tfnbak` file that another computer wrote with scrypt (`the file needs scrypt`). For a `restore`, a
  `check-backup` or a `recover` of such a file, install Python from python.org or with Homebrew, and use that Python.
- The `openssl` program. macOS and Linux have it. On Windows, the program finds `openssl.exe` of Git for Windows in its
  default folders (`C:\Program Files\Git\mingw64\bin`, `C:\Program Files\Git\usr\bin`). Else set the variable
  `OPENSSL=<the path of openssl.exe>`.
- Only for the option `--console`: the Python package `pyserial` (`pip install pyserial`) and a USB cable to the
  board. On Linux, your user must be in the group of the serial ports (`dialout` on Debian, Ubuntu and Raspberry Pi
  OS). The USB serial of the board restarts the chip when DTR is low and RTS is high. The program never sets that
  state, and it keeps DTR and RTS low after the open. On Windows, the port opens with both low. On Linux and macOS,
  the system opens the port with both high, then the program sets RTS low first and DTR low after it. This order of
  Linux and macOS is not tested on a board yet: if the board restarts at the open, wait for its start, then do the
  command again. A port that does not open stops the command with `STOP: the console <port> does not open: ...`.

The program uses two ports:

| Port | What uses it |
| --- | --- |
| UDP 47474 | The program only listens: the boards send their JOIN packets and their beacons there. `find` and `adopt` listen for 10 s, another command for 3 s when a member does not answer at its last address, and `serve` all the time |
| TCP 47480 | Only `serve` in the store mode `local` (the local store). `serve --port` sets another port |

Windows and macOS ask for a firewall rule at the first listen: allow the PRIVATE network only (section 10).

## 2. Install

### 2.1 The release zip

Download `tfn-poolmgr-<version>.zip` from the release page of `tfn-os` on GitHub, and unzip it. You get one folder
`poolmgr/` with the program, copies of the shared modules (`pool.py`, `pool_crypto.py`, `pool_tool.py`, `fw_sign.py`,
`store_index.py`) and `channel.json`. No other file of the repo is necessary.

`channel.json` is the CHANNEL of that release: the address of the web site of `tfn-os`, the name of the repo on
GitHub, and the official keys (the public key of the store and the public keys of the OTA). `init` copies its
values into `config.json` of the data folder.

### 2.2 A checkout of the repo

In a git checkout of `tfn-os`, the program finds the shared modules in `sdk/tools/`. The folder `poolmgr/` has no
`channel.json`. Download `channel.json` from a release page, and give it to `init` with `--keys`:

```
poolmgr init "OUR HOME" --keys channel.json
```

Do not give `tfn-keys.json` of a release to `--keys`. That file has only the keys: no web site and no repo. With that
file, `init` writes the line `WARN: <file> has only the keys (no web site, no repo): ...` below the key IDs. Next, it
asks for the pass phrases. Stop `init` there with Ctrl+C: before the pass phrases, it makes nothing (section 4.1).
Then do `init` again with `channel.json`.

A pool that you make with `tfn-keys.json` has no web site in its channel. Then `update` stops with
`no channel (no web site of the releases) in ...`, and `store sync` stops with
`the channel has no pages address (the web site of the releases): ...`. Give the pool the channel of a release:

```
poolmgr channel channel.json
```

Then the channel line of `poolmgr doctor` must start with `OK`.

### 2.3 The channel of a pool: `channel`

```
poolmgr channel                    the channel of the pool: the web site, the repo, the key IDs
poolmgr channel channel.json       channel.json of a release gives the pool its web site and its repo
```

`channel` with a file sets the channel of a pool that exists (a pool from a `tfn-keys.json`, or a new release zip:
its `poolmgr/channel.json`). It writes the web site, the repo and the place of the release files into
`config.json`. The store key and the OTA keys of the file must be the same as the keys of the pool. If they are not
the same, the program stops (exit 2, `... has other official keys ...`) and changes nothing: the official keys of a
pool never change. For the releases of another key (a fork), make another pool in another data folder (section
12). The program also
stops (exit 2) for a file that is not there and for a file with no web site (a `tfn-keys.json`). The program prints
the key IDs: compare them with the release page of `tfn-os`. A new channel changes `config.json`: make a new backup
(the program prints `BACKUP IS OLD`). Then the channel line of `poolmgr doctor` must start with `OK`.

### 2.4 Check the computer: `doctor`

```
poolmgr doctor
```

`doctor` checks:
- the version of Python (3.9 or later) and the folders of the shared modules;
- `openssl`: it makes a test key and a test signature;
- `pyserial` (`WARN` when it is not there: only `--console` needs it);
- the ports UDP 47474 and TCP 47480 (`WARN` when a port is in use, for example while `serve` runs);
- the LAN address of this computer;
- the data folder: `config.json` and the pool must load, and the line gives the name of the pool. A folder with no
  pool, but with files of an older `init` that stopped there, gives a `WARN` line (section 4.1);
- the rights of the data folder and of its parts `keys/` and `pool/`: a `WARN` line names the other accounts (Linux
  and macOS: the group and the other users), and a second `WARN` line gives the command that repairs the folder
  (section 12);
- key A: `keys/admin_a.pem` must be the admin key A of the pool. When it is not there, or it is another key: a
  `FAIL` line with the way back, `poolmgr recover --recovery <the .tfnkey file of key R>` (section 10). The check
  needs `openssl`: when `openssl` does not work, the line of key A is `WARN key A: not checked`;
- the channel: one request for the firmware index of the web site. With a pool, `doctor` checks the channel of the
  pool (`config.json`); with no pool, `channel.json` of the program folder.

`doctor` changes nothing. Each line starts with `OK`, `WARN`, `FAIL` or `NOTE`. The last line is `DOCTOR OK` or
`DOCTOR FAILED (<n>)`. Only a `FAIL` line gives `DOCTOR FAILED`, so read the `WARN` lines too. For example,
`WARN the channel of the pool: none` tells that `update` and `store sync` cannot work (section 2.3).

### 2.5 The output and the exit codes

Each command prints plain lines (a log or a pipe can read them). `poolmgr --help` lists the commands, and
`poolmgr <command> --help` tells one command.

| Exit code | What it tells |
| --- | --- |
| 0 | OK |
| 1 | A board refused, or a check failed. The line starts with `FAILED:` (`doctor`: `DOCTOR FAILED`) |
| 2 | A fault of the input, of the program or of the disk. The line starts with `STOP:` (a wrong option: `error:`) |

A command that needs a pool stops in a data folder with no pool, with
`STOP: no pool in <folder> (a new pool: poolmgr init <name>; a pool from a backup: poolmgr restore <file>)`. It makes
and changes nothing in the folder.

A damaged file of the data folder (`config.json`, `pool/pool.json`, `pool/secrets.json`) stops each command with
`STOP: a file of the data folder is damaged: <file> (...)` (exit 2). Repair the file, or restore the backup into a new
data folder (`poolmgr --data <new folder> restore <file>`, section 9). A damaged `pool/webpass.json` stops a command
that reads it with `STOP: <file> is damaged (...)` (exit 2): the program does not write over it. A damaged
`pool/store_tok.json` (the store tokens) or `pool/friends_out.json` (the friend entries that wait) stops a command that
reads it with the same `STOP: a file of the data folder is damaged` line, and the program does not write over it
(`remove` checks them before its first change; `serve` prints a `FAILED` line and goes on). A damaged
`pool/recover.json` (the state of the last `recover`: not JSON, or boards that wait with no old manager ID or no old
request keys) stops `recover` with the same line before its first change. The program does not write over it,
because it has the only copy of the old request keys of the boards that did not answer (section 10).

## 3. The start in 10 minutes

1. Check the computer:
   ```
   poolmgr doctor
   ```
2. Make the pool (in a folder where you want the two key files):
   ```
   poolmgr init "OUR HOME"
   ```
   In a git checkout, add `--keys channel.json` (section 2.2). The program asks two times for the pass phrase of
   key R, then two times for the pass phrase of the backup (12 characters or more). It writes two files in the
   current folder: `OUR_HOME-recovery.tfnkey` (key R, encrypted) and `OUR_HOME-backup.tfnbak` (the first backup).
   Copy the two files to a USB stick or to another computer now (section 9). When `init` stops (a pass phrase,
   Ctrl+C, a fault), it leaves nothing: do `init` again (section 4.1).
3. On each board: SETUP > POOL > JOIN. The JOIN screen stays open for 10 minutes.
4. Adopt the boards:
   ```
   poolmgr adopt --all
   ```
   For each board, the program prints 6 digits, and the board shows 6 digits. Type the digits of the board. When they
   are the same, tap ALLOW on the board in 2 minutes. When they are not the same, tap NO (section 5.1). When a board
   has a web password, the program asks for it.
5. Look at the pool:
   ```
   poolmgr list --live
   ```
   Each member shows `[ok v<roster version> e<key epoch>]`, its firmware, its kind and its open app.
6. The members changed, so make a new backup, and copy it to the USB stick again:
   ```
   poolmgr backup OUR_HOME-backup.tfnbak
   ```
7. Optional: let `serve` start at each logon of your user (section 11):
   ```
   poolmgr service install
   ```

Now the members are friends: the games for 2 boards and WALKIE pair at once. Each board that had no web password has
a new random one: `poolmgr webpass show <member>` prints it. A board with a web password of its own keeps it.

## 4. The commands

| Command | Options | What it does |
| --- | --- | --- |
| `doctor` | | Checks this computer and the data folder (section 2.4) |
| `init <pool name>` | `--keys <channel.json>` | Makes the pool, keys A and R, and the first backup (section 4.1) |
| `channel [<channel.json>]` | | Shows the channel of the pool, or sets its web site and its repo from `channel.json` of a release with the same official keys (section 2.3) |
| `find` | `--secs <s>` (default 10) | Lists the boards with an open JOIN screen (section 5.1) |
| `adopt [<address>]` | `--all`, `--console <ports>`, `--password [<password>]`, `--name <name>`, `--role member\|child`, `--no-webpass`, `--no-friends` | A board joins the pool (section 5.1) |
| `list` | `--live` | The members (section 5.2) |
| `push [<member>...]` | | The newest roster and pool key to the members (section 5.3) |
| `remove <member>` | `--keep-webpass` | A board leaves the pool (section 5.4) |
| `webpass show <member>`, `webpass new [<member>...]` | | The web passwords of the boards (section 5.5) |
| `friends` | `--check` | Each member a friend of each other member (section 6) |
| `update check [<member>...]`, `update <member>...`, `update --all` | `--version <V>`, `--now` | The firmware (section 7) |
| `store official`, `store local`, `store sync`, `store key` | | The store (section 8) |
| `store trust [<member>...]` | `--console <port>` | The pool store key to the boards (section 8.1) |
| `store publish <folder>` | | The pool's own apps (section 8.1) |
| `backup <file>`, `check-backup <file>`, `restore <file>` | `--replace` (`backup`) | The backup (section 9) |
| `recover` | `--recovery <file>`, `--new-key` | A new key A with key R (section 10) |
| `serve` | `--port <port>` (default 47480), `--openssl <path>` | The background command (section 11) |
| `service [show\|install\|remove]` | `--dry-run`, `--system linux\|macos\|windows`, `--port <port>` | `serve` at each logon of your user (section 11) |

The option `--data <folder>` comes before the command (section 12).

A MEMBER in a command is its name, its device ID (16 hex digits with no `-`, as `list` prints it), its MAC (12 hex
digits, with or without `:`) or its address. Upper case and lower case are the same.

### 4.1 `init`

```
poolmgr init "OUR HOME"
poolmgr init "OUR HOME" --keys channel.json       (a git checkout: channel.json of a release)
```

The pool name has 1 to 24 bytes. `init` stops when the data folder has a pool already (a pool is never overwritten),
or when one of its two output files is in the current folder already.

`init` does these steps:
1. It reads the channel (`channel.json` of the program folder, or the file of `--keys`) and prints the key IDs of the
   official keys (16 hex digits each). Compare them with the `key_id` values of `tfn-keys.json` on the release page.
   When the file has no web site (a `tfn-keys.json`), it also writes a `WARN` line (section 2.2).
2. It asks for the pass phrase of key R (two times), then for the pass phrase of the backup (two times). Each
   question names its file, for example
   `The pass phrase of the key file <the current folder>/OUR_HOME-recovery.tfnkey (key R) (12 characters or
   more):`.
3. It makes the data folder when it is not there (on Windows with the rights of section 12), and a temporary folder
   `.init-<letters>` in it. The temporary folder and its folders `keys/` and `pool/` get their own rights (section
   12) before the first key file, also in a data folder that was there before.
4. In the temporary folder, it makes KEY A, the admin key of the pool (`keys/admin_a.pem`), and KEY R, the recovery
   key. Key R is never on the disk in clear: the program writes only its encrypted file, opens it again and checks
   the key.
5. It makes the pool with its manager member (the ID of key A), signs the first roster, and writes the first backup.
6. It writes `<pool name>-recovery.tfnkey` and `<pool name>-backup.tfnbak` in the current folder. Then it moves
   `config.json`, `keys/` and `pool/` into the data folder (`pool/` last), and removes the temporary folder.

When `init` stops on the way (the two pass phrases are not the same, a pass phrase has fewer than 12 characters,
Ctrl+C, a fault of `openssl` or of the disk), it removes the parts that it made: no key, no pool and no file in the
current folder stay. Do `init` again. Before step 3, `init` makes nothing.

`init` never deletes or overwrites a key. It stops with `<folder> has key files but no pool (keys/admin_a.pem): ...`
when the data folder has key files and no pool (for example the parts of an `init` of an older `poolmgr` that stopped
there). Move that folder to another place, or give another folder with `--data`. The empty folders and the file
`lock` of an older `poolmgr` do not stop `init`.

In the file names, only the letters `A` to `Z` and `a` to `z`, the digits, `-` and `_` stay. Each group of other
characters becomes one `_`, and the program removes `_` at the start and at the end ("OUR HOME" gives `OUR_HOME`). A
name with no letter `A` to `Z` or `a` to `z` and no digit (for example a name in Hebrew) gives `pool-` and the first
8 hex digits of the pool ID: for example `pool-1a2b3c4d-recovery.tfnkey` and `pool-1a2b3c4d-backup.tfnbak`. The two
pass phrases can be the same or not.

## 5. The members

### 5.1 `find` and `adopt`

The JOIN of a board:
1. On the board: SETUP > POOL > JOIN. The board sends JOIN packets to the LAN for 10 minutes.
2. `poolmgr adopt` finds the board (it listens for 10 s) and starts the JOIN. The program prints 6 digits (for example
   `482 913`), and the board shows "MANAGER ... WANTS TO MANAGE THIS BOARD" with 6 digits.
3. Compare the digits:
   - the same digits: tap ALLOW on the board;
   - not the same digits: tap NO on the board. Another device is between the computer and the board, and the program
     stops the JOIN.
4. The board shows THIS BOARD IS IN THE POOL. The program checks the pool state of the board, and sends the new
   roster to the other members. When that check gets no answer, the board stays in the roster, and the program prints
   `WARN: <name> is in the roster of the pool <pool>, but the check of its state did not answer (...): poolmgr list
   --live`. The web password and the friends of the board still follow.

A BOARD WITH A WEB PASSWORD (SETUP > WEB PASSWORD). The first request of the JOIN has no password. When the board
refuses it (its web gate answers `401`, `login`), the program takes the password from:
1. the option `--password`. Give it with no value: the program asks for the password (no echo). Give the address
   before this option (`adopt 192.0.2.21 --password`): else the option takes the address as the password. A value
   (`--password <password>`) also works, but the value goes into the history of the shell, and into the process
   list that the other users of the computer can read while the JOIN waits for ALLOW;
2. else, only for a board at an address that you give (`adopt <address>`), the web password that this pool gave the
   board before (the board left the pool; `webpass.json` keeps it, section 5.4): the line `<address> has the web
   password that this pool gave it before: poolmgr uses it`. The program does not send that password to a board that
   it finds by its JOIN packet, because each device on the LAN can send a JOIN packet with the MAC of a board. It
   prints `<address>: this pool keeps the web password of a board with this MAC (the board left the pool). poolmgr
   sends it only to an address that you give: poolmgr adopt <address>`;
3. else a question in the terminal: `<address> has a web password. The web password of the board (SETUP > WEB
   PASSWORD):`. With no terminal (a script), the JOIN stops with
   `<address>: THE JOIN STOPPED: <address> has a web password (SETUP > WEB PASSWORD on the board): adopt <address>
   --password (the program asks for it), or remove the web password on the board`.

The questions give 3 tries, and an empty answer is a try too. `--password` with no value and no terminal stops the
command: `STOP: no terminal for the question ...` (exit 2). A wrong password gives
`<address>: the web password is not right ...`. After many wrong passwords the board waits before it takes a new one:
`<address> refuses web passwords for <n> s ...`. Wait, then do the `adopt` again. A board with a web password of
your own keeps it after the JOIN (the program gives it no random password):
`<name> keeps its own web password (SETUP > WEB PASSWORD on the board)`. When the data folder keeps a web password of
the pool for the board, and that password does not open the board now, the password goes out of the data folder:
`<name>: the web password that this pool kept for it does not open it now: it goes out of the data folder`.

The ways to compare the 6 digits:

| Option | What the program does |
| --- | --- |
| (none) | It asks you to type the digits that the board shows. When they are the same as its own digits, it tells you to tap ALLOW on the board. It waits 2 minutes for the tap |
| `--console <port>` | It uses the USB console of the board: it opens the JOIN there (`pool join`), reads the 6 digits there and sends `pool allow` there. No tap is necessary, also not on SETUP > POOL > JOIN. For more boards, give one port for each board, with commas between them (`--console /dev/ttyACM0,/dev/ttyACM1`; on Windows the names of the COM ports) |

When the JOIN gets no ALLOW, it stops with one of these lines. Then open SETUP > POOL > JOIN on the board again, and
do the `adopt` again:
- `nobody tapped ALLOW on the board in 120 s: ...`: nobody tapped ALLOW in the 2 minutes of the program;
- `the board ended the JOIN (NO on the board, or no ALLOW in 2 minutes): ...`: a person tapped NO, or the 2 minutes
  of the board ended first.

The boards of a command:

| Command | The boards |
| --- | --- |
| `poolmgr adopt` | The board with an open JOIN screen. When more boards have one, the program lists them and asks for a number (or `all`) |
| `poolmgr adopt --all` | Each board with an open JOIN screen, one after the other |
| `poolmgr adopt 192.0.2.21` | The board at that address, with no listen. SETUP > WEB PAGE and the console command `ip` show the address of a board |

`poolmgr find` lists the boards with an open JOIN screen: the name, the last 4 hex digits of the MAC and the address.
`--secs` sets the time of the listen (default 10 s).

While `serve` runs on the computer, it has the UDP port: `find` and `adopt` with no address see no JOIN screen. Then
use the address. `serve` prints a line for each open JOIN screen with the command:
`JOIN SCREEN OPEN: <name> (MAC ...<4 hex digits>) <address>: to adopt it: poolmgr adopt <address>`. The service
writes these lines into `log/serve.log` of the data folder (section 11).

After the JOIN, the program does these steps (each one can be off):
- a random web password (16 characters) for a board with no web password of its own (section 5.5). With
  `--no-webpass`, the board keeps its web password as it is (or no web password);
- the friends: the new member becomes a friend of each other member, and each other member a friend of it
  (section 6). `--no-friends` makes no change;
- in the store mode `local`: the address of the local store (section 8).

The other options:
- `--name <name>`: the member name (1 to 16 bytes). The default is the name in the JOIN packet (the name of the
  board). With an address and no `--name`, the name is `TFN-<the first 4 hex digits of the device ID>`.
- `--role child`: a CHILD cannot leave the pool on its screen (only `remove` or the USB console `pool leave` takes
  it out). The default is `member`.

`--name` is for one board: with more boards, the program stops (exit 2). When the JOIN of a board stops, the program
goes on with the next board, and the exit code is 1 at the end. With more boards, this is also true for a fault of the
input (for example the end of the input at a question, or a full pool); with one board, such a fault stops the command
(exit 2). The last lines are `BACKUP IS OLD: poolmgr backup <file>` and
`<n> board(s) joined the pool <pool name>: <names>`. When `adopt` stops on the way (Ctrl+C) after some boards joined,
the line `NOTE: <n> board(s) joined before the stop: <names>. ...` names them and the steps that did not run for them
(`poolmgr friends --check`, and `poolmgr webpass new <member>` for a board with no web password of its own), then
`BACKUP IS OLD: ...`.

### 5.2 `list`

```
poolmgr list
poolmgr list --live
```

`list` prints the pool (its name, its ID, the roster version, the key epoch, the store mode and the key IDs of the
admin keys) and one line for each member: its name, its device ID, its MAC, its role and its last address.

`--live` also asks each board for its state. Each line then has `[<pool state> v<roster version> e<key epoch>]`, the
firmware version (`fw`), the `kind`, the mode of the update check (`sig`), `own_sig`, `minsv`, the open `app` (`MENU`
at the menu), the counts of the pool packets (`v4 ok`, `bad`) and the `store` of the board: `local` (the board has a
manager store: the local store of `serve`), `official` (no manager store) or `?` (no answer). `NOT THE NEWEST` means
that the board has an older roster or pool key: `poolmgr push`. `NOT THE STORE OF THE POOL` means that the store of
the board is not the store mode of the pool: `poolmgr store official` or `poolmgr store local` again. A board that
does not answer, or that is not the newest, gives the exit code 1.

### 5.3 `push`

```
poolmgr push                  all members
poolmgr push KITCHEN          one member
```

`push` sends the newest roster and pool key to the members (to all members when you give none). Use it for a board
that was off at a change of the pool, when `serve` does not run. It also takes out the friend entries that wait for
their removal (section 5.4). The last line is `PUSH OK: <n> member(s)`.

### 5.4 `remove`

```
poolmgr remove KITCHEN
poolmgr remove KITCHEN --keep-webpass
```

`remove` does these steps, in this order:
1. The board goes back to the official store.
2. The friend pairs of the board and the other members go out, in the two directions. The other friends of the board
   stay.
3. The web password that the pool gave the board goes off: the board has no web password then (as before the adopt),
   and the line `(3) KITCHEN has no web password now ...` tells it. Set your own on the board (SETUP > WEB PASSWORD).
   The board answers the request before it changes the password, so the program reads the board again with no
   password: only when the board then lets it in, the password goes out of the data folder. Else the line is
   `(3) KITCHEN did not turn off the web password (the board still asks for a password): it keeps the web password of
   the pool: ...`. With `--keep-webpass`, the board keeps the web password of the pool. A board with a web password
   of its own (the adopt kept it, or a person set another one on the board later) keeps it in each case.
4. A new roster without the board, and a new pool key (the next epoch). The board gets the roster and leaves the
   pool. The other members get the new key.

Before the first request to the board, `remove` reads `GET /api/pool` at the last address of the board (this request
needs no password) and compares the device ID. When another board answers there (for example the router gave the
address to another device while the board was off), when a device that is not a TFN board answers there (for example
the web page of a printer), or when nothing answers, no request goes to the board: the line
`KITCHEN is not at its address (another board answers at <address>, ...): no request goes to it`,
`KITCHEN is not at its address (another device answers at <address>, ... (it is not a TFN board)): no request goes
to it` or `KITCHEN did not answer (...): no request goes to it`. So the web password of the pool never goes to another
device.

A board that does not answer (or is not at its address) still leaves the pool: the new key cuts it off. It shows its
old pool until a person takes it out on the board: SETUP > POOL > LEAVE, or on a CHILD the USB console `pool leave` (a
CHILD refuses LEAVE on its screen). Use `remove` also for a board that is lost or stolen.

A board that keeps the web password of the pool (`--keep-webpass`; the board did not answer, was not at its address,
or did not turn off the password at step 3): the data folder keeps the password, and
`poolmgr webpass show <MAC of the board>` prints it (the line of step 3 gives the command). A new `adopt <address>` of
the board uses it by itself (section 5.1). A person can change or remove the password on the board (SETUP > WEB
PASSWORD).

A person can also tap SETUP > POOL > LEAVE on a board (not on a CHILD). `serve` sees it. It takes the board out of the
roster and out of the friend list of each other member, and it sends a new key to the other members. `serve` sends no
request to the board that left, so that board keeps its own friend list and the web password of the pool
(`webpass show <MAC of the board>`). With no `serve`, use `poolmgr remove <member>`.

The exit code is 1 when a member did not get the new key: `poolmgr push` later (`serve` does it by itself).

A line `<board>: <other board> not out: ... (poolmgr tries again: push, friends, serve)` of step 2 tells that a friend
entry stays on a member (for example, the member was off). The data folder keeps the entry (`pool/friends_out.json`),
and `push`, `friends`, `friends --check` and the upkeep of `serve` take it out later. A line with
`(on <board>: SETUP > FRIENDS)` is about the friend list of the board that left: remove that friend on the board.

### 5.5 `webpass`

```
poolmgr webpass show KITCHEN
poolmgr webpass new               all members
poolmgr webpass new KITCHEN
```

Each member with no web password of its own gets a random web password at the adopt. `poolmgr` itself does not need
it: each request of the program has the request header of the pool, and the board lets it in. The program sends the
web password too (for an older board), but only with a signed request, and only after the board at the address gave
the device ID of the member in `GET /api/pool`. Another board or device at an old address of a member gets no signed
request and no password: the line of that member is `FAILED` (or `[no answer: ...]` in `list --live`). You need the password
for the web pages of the board (`http://<address of the board>/`): `webpass show <member>` prints it (`<name>: user
admin, web password <password>`; the login page of the board asks only for the password). `webpass new` makes a new
random password (also for a board with a web password of its own). The passwords are in `pool/webpass.json` of the
data folder.

The board answers `webpass new` (and the web password step of `adopt`) before it sets the password. So the program
reads the board again: the data folder keeps the new password only when it opens the board, and a request with no
password does not. Else the line is `<name>: FAILED: the board answered, but the new web password does not open it
(...)`, the data folder keeps the web password that it had before, and the exit code is 1. When the board takes the
request but does not answer the read after it, the line is `<name>: NOT CHECKED: ...`: the board can have the new
password, so the data folder keeps it (`poolmgr webpass show <name>`), and the exit code is 1. When that password does
not open the board, do `poolmgr webpass new <name>` again.

`webpass show <MAC>` also prints the web password of the pool that a board kept after it left the pool (section 5.4):
`<MAC> (not a member of the pool now): user admin, web password <password>`.

## 6. The friends

The games for 2 boards and WALKIE pair only with friends ([user_guide.md](user_guide.md), section 8.1). The members of
a pool are not friends by themselves. `poolmgr` makes them friends:
- `adopt` makes the new member a friend of each other member, and each other member a friend of it.
- `poolmgr friends` makes each member a friend of each other member (all pairs).
- `poolmgr friends --check` reads the friend list of each member and adds only the entries that are not there (for
  example after an adopt while a board was off).
- `remove` takes out the friend pairs of the removed board and the other members. The other friends of a board stay.
- After a LEAVE on a board, `serve` takes that board out of the friend list of each other member (section 5.4).
- `friends` and `friends --check` first take out the friend entries that wait for their removal (section 5.4).

A FRIEND ENTRY is one direction: board A has board B in its friend list. A pair is two entries, so 3 boards have 6
entries. The line `friends: <n> new friend entries, <k> were there` (also in the output of `adopt`) counts the new
entries and the entries that the boards had already (with `--check`, the program sends only the missing entries, so
`<k>` is 0). The last line is `FRIENDS OK: <n> new friend entries`.

A board has 16 friends at most. For a full board the program prints
`FRIENDS FULL ON <name> (16 friends): <other name> is not its friend`, and the other pairs still work. The exit code
is 1 when an entry is missing at the end: a full board (the board refused the entry) or a member that did not answer.
The last line is then `FAILED: friend entries are missing: FULL ON <name> (...)` or `... <n> member(s) or entries
did not answer (...)`. For a full board, remove a friend on that board (SETUP > FRIENDS), then do
`poolmgr friends --check`. For a member that did not answer, do `poolmgr friends --check` later. In `adopt`, the
friends do not change the exit code: the JOIN is the result of `adopt`.

## 7. The firmware updates: `update`

```
poolmgr update check                     for each member: the kind, the version now, the newest version
poolmgr update --all                     all members, one board at a time
poolmgr update KITCHEN HALL              some members
poolmgr update --all --version 1.2.0     a given version (also an older one)
poolmgr update --all --now               also the boards with an open app
```

THE CHANNEL. The program reads the newest version of each board kind on the web site of the channel:
`<site>/firmware/index.json`, then `<site>/firmware/<kind>/manifest.json`, then the signed app file (the manifest has
its size and its SHA-256). With `--version V` it reads the files of the GitHub release `vV`: `tfn-os-V-<kind>.bin`
and `SHA256SUMS`. The files stay in `cache/fw/<version>/<kind>.bin` of the data folder.

THE CHECKS. Before a byte goes to a board, the program checks:
1. the size and the SHA-256 that the channel gives;
2. the signature of the file (its trailer): an official OTA key of the channel, the kind of the board, the version,
   and a security version (`sec_ver`) that is not lower than the `minsv` of the board;
3. the keys of the board: a board in the mode ENFORCE gives its key IDs, and the program sends no file of a key that
   the board does not have.

A file that fails a check does not go to the board (`not sent: the check of the program says <code>`). The board
checks the file again with its own keys, and it refuses any other file. After the restart, the new firmware does a
self-test. A firmware that does not start well goes back to the old firmware (a rollback).

THE ORDER. One board at a time. The program sends the file to a board, then reads the state of the board each 2 s,
for up to 3 minutes. The board is good when it runs the new version, its self-test passed, and (in ENFORCE) its own
signature is `ok`. Only then does the next board get the file. The first failure (a refusal, a rollback, no new
version in 3 minutes) stops the run: the other boards show `not sent: the run stopped`, and the exit code is 1.

The result of each board:

| Result | What it tells |
| --- | --- |
| `ok` | The board runs the new version |
| `up to date` | The board needs no update |
| `busy: waits for the next run (<app> is open)` | An app is open on the board. The program updates only a board at the menu; `--now` also updates a board with an open app |
| `no answer: waits for the next run` | The board did not answer. The run goes on |
| `no channel for this kind` | The release has no file for this board kind (for example the Tab5) |
| `not sent: ...` | A check of the program failed, or the run stopped |
| `refused by the board: ...`, `rollback: ...` | The board refused the file, or it went back to its old firmware |

`update check` changes nothing. It gives a line for each member: `member`, `kind`, `now`, `newest`, `state` (`update`,
`up to date`, `no channel for this kind`, `no answer: waits for the next run`), `sig` and `minsv`.

THE AUTOMATIC UPDATE. With `"auto_update": true` in `config.json` of the data folder, `serve` updates the members by
the same rules each 6 hours (the first run 10 minutes after its start; only the boards at the menu). The default is
`false`. `serve` reads `config.json` again when the file changes.

## 8. The store: `store`

| Mode | What the boards do |
| --- | --- |
| `official` (the default) | Each board reads the official store of `tfn-os` itself (the static store, over HTTPS) and checks each app with the official store key |
| `local` | Each board reads the LOCAL STORE of `serve` on this computer (HTTP on the LAN): a checked copy (the MIRROR) of the official store, and the pool's own apps. `serve` must run |

```
poolmgr store official
poolmgr store local
poolmgr store sync
```

- `store official` tells each member that it has no manager store. Then the board reads the official store.
- `store local` sends the address of the local store (`http://<the address of this computer>:47480`) to each member.
  When the mirror is empty, the program tells you to do `store sync`.
- `store sync` makes the mirror of the official store in `cache/store/` of the data folder. It copies only the apps
  of the board kinds of the members. Before the mirror changes, the program checks the signature of the index
  (`index.sig`) with the official store key, the form of the index, the size and the SHA-256 of each file, each
  `manifest.txt`, and the signature of each app. One fault stops the sync, and the mirror stays as it was. In the mode
  `local`, `serve` does the sync each 6 hours (the first one 1 minute after its start).

The boards use the local store only when SETUP > STORE SOURCE is AUTO (the default) or MANAGER
([user_guide.md](user_guide.md), section 6.2). The local store of `serve` answers only 3 requests:
`GET /api/store/policy`, `/api/store/catalog` and `/api/store/file`. Each other request gets 404. The answers have
no secret.

A board installs an app of the local store only when its signature is good: the official store key (an official
app), or the pool store key after the ALLOW on that board (a pool app).

### 8.1 The pool's own apps

You can give the members your own apps (for example an app that you made with the SDK). A board installs such an app
only with the signature of a key that it trusts: the POOL STORE KEY. Each board trusts it only after a person allows
it at the board.

1. Make the pool store key (one time):
   ```
   poolmgr store key
   ```
   The program prints its key ID as the board shows it (`XXXX-XXXX-XXXX-XXXX`). The key is `keys/store_pool.pem` of
   the data folder. When the key exists, it stays as it is.
2. Send the key to the members:
   ```
   poolmgr store trust                                  all members: tap ALLOW on each board
   poolmgr store trust KITCHEN --console /dev/ttyACM0   one member, through its USB console
   ```
   Each board shows "TRUST THE KEY <pool name>?" (15 characters of the name at most) with the key ID, ALLOW and NO,
   for 2 minutes. Compare the key ID with the key ID of `store key`, then tap ALLOW. With `--console <port>` (one
   member only), the program sends the line `pub allow <key ID>` on the USB console of the board: no tap is
   necessary. The USB console counts as the tap, because a person with the cable can write any firmware to the board
   ([security.md](security.md), section 4.4). The board then shows the key in SETUP > TRUSTED PUBLISHERS with the flag
   STORE.
3. Build the app with the SDK, then add it to the local store:
   ```
   python <sdk>/tools/tfn_app.py build my_app --kinds lcd349,cores3
   poolmgr store publish my_app/out
   ```
   `store publish` takes the output folder of `tfn_app.py build` (`<folder>/<id>/<kind>/`), a folder
   `<folder>/<kind>/`, or one package folder (with `manifest.txt`). It signs `manifest.txt` of each package with the
   pool store key (the file `sig`) and adds the package to the local store. A pool app with the ID of an official app
   takes its place.
4. The boards get the pool's apps only in the mode `local`: `poolmgr store local`, and `serve` must run.

The result of `store trust` for each board is `trusted` or `trusted already`, else `FAILED` with the cause, for
example `no ALLOW on the board`, or a message of the board (`the list is full (8 keys): remove a key first`). A board
keeps 8 added keys at most.

## 9. The backup

The BACKUP (`.tfnbak`) is an encrypted file of the pool: `config.json`, key A, the pool store key, and the folder
`pool/` (the roster, the pool key, the request key of each board, the web passwords, the store tokens of the boards,
the friend entries that wait for their removal, the state of the last `recover`). Key R is not in the backup: it has
its own file (`.tfnkey`). The cache is not in the backup (the firmware files, the mirror, the pool's own apps).

```
poolmgr backup OUR_HOME-backup.tfnbak            write a backup (it replaces an older backup of this pool)
poolmgr backup OUR_HOME-backup.tfnbak --replace  replace also a newer or different backup of this pool, or the
                                                 backup of another pool
poolmgr check-backup OUR_HOME-backup.tfnbak      check a backup, and compare it with the data folder
poolmgr check-backup OUR_HOME-recovery.tfnkey    check the file of key R
poolmgr restore OUR_HOME-backup.tfnbak           put a backup into a new data folder
```

Each question of these commands and of `recover` names the file: `backup` asks two times
`The pass phrase of the new backup OUR_HOME-backup.tfnbak (12 characters or more):`; `check-backup` and `restore` of
a `.tfnbak` file ask `The pass phrase of the backup <file> ...`; `check-backup` of a `.tfnkey` file and `recover` ask
`The pass phrase of the key file <file> (key R) ...`. A file that is not of the correct kind stops the command before
the question. `backup` writes a new file or replaces an older backup: it stops before the question (exit 2) for a
name that ends with `.tfnkey`, for a folder and for a file that is not a backup. So a wrong file name never replaces
the key file of key R.

`backup` replaces an older backup only when the new pass phrase opens it, it is a backup of the same pool, and it is
not newer than the data folder. Two pools with the same name (in two data folders) have the same file name, so
`backup` stops (exit 2) with
`<file> is the backup of another pool (<name>, ID <8 hex digits>): give another name, or --replace replaces it`, or
with `<file> is a backup that the new pass phrase does not open (...)`. A backup with a higher roster version or key
epoch, or with another key A (for example the newest backup after a `recover`, and an older copy of the data folder on
a second computer), also stops `backup` (exit 2): `<file> is a NEWER backup of the pool <name>` (or `a DIFFERENT
backup`), with the roster version, the epoch and the key ID of key A of the backup and of the data folder. Keep that
backup: it can be the only copy that manages the boards. A backup with parts that the data folder does not have also
stops `backup` (exit 2): `<file> is a backup of the pool <name> (roster version ..., epoch ...) with parts that the
data folder does not have (...)`. A key file (`keys/*.pem`, for example `keys/store_pool.pem` of `store key`) that the
data folder does not have, or has with other bytes, stops `backup` at each roster version, because the program never
deletes a key file. With the same roster version and epoch, another file of the backup or the web password of a board
in `pool/webpass.json` also stops `backup`. A web password with another value does not stop `backup` (`webpass new`
changes it in the data folder). Give another name, or add `--replace` when you are sure (for example after a change
of the pass phrase).

The `.tfnbak` file and the `.tfnkey` file of key R get only the rights of your user, `SYSTEM` and `Administrators`
(Windows; on Linux and macOS the mode 0600), also in a current folder that gives rights to other accounts. When the
rights do not set (for example on a USB stick with no access lists), the program prints
`WARN: the rights of <file> do not set (...)`.

The rules:
- WHEN. `init` writes the first backup. Make a new backup after each change of the pool. The program prints
  `BACKUP IS OLD: poolmgr backup <file>` after `adopt`, `remove`, `recover` and `channel`, and `serve` prints it after
  a LEAVE on a board. Make a new backup also after `store key`, `store local`, `store official` and `webpass new`.
  `check-backup` tells each part of an old backup (THE CHECK). `serve` never writes a backup: it has no pass phrase.
- WHERE. On a USB stick or on another computer, not only on this computer. Keep the `.tfnkey` file of key R there
  too: it is the way back when this computer is stolen.
- THE PASS PHRASE. 12 characters or more. For a new file the program asks two times. With no pass phrase, a file does
  not open, and nobody can open it for you. Keep the pass phrases in a safe place, for example a password manager.
- THE CHECK. Do `check-backup` after each backup. It opens the file and checks each part. For a `.tfnbak`: the pool
  loads, the roster has the signature of an admin key of the pool, key A and the pool store key sign a test text, and
  each board has a request key. Then it compares each file of the backup with the data folder: the members and the
  roster, the pool key and the request keys, the web passwords, the store tokens, the friend entries that wait, the
  state of the last `recover`, key A, the pool store key and each value of `config.json` (but not the address of the
  local store: `serve` finds it again). For each part that is not the same, it prints a line, for example
  `BACKUP IS OLD: config.json: the store mode (the backup: official, the data folder: local)` or
  `BACKUP IS OLD: the pool store key (keys/store_pool.pem): only in the data folder`, then
  `BACKUP IS OLD: make a new backup (poolmgr backup <file>)`. Else it prints `the backup has the state of the data
  folder (each file is the same)`. For a `.tfnkey`: key R signs a test text, and it is the recovery key of the pool of
  the data folder. The last line is `CHECK OK` (the file itself is good, also when it is old).

A person with the backup and its pass phrase can manage your boards. Keep the file and the pass phrase apart.

`restore` writes the backup into a new data folder (for example on a new computer). The folder can be empty, or it can
have the empty folders and the file `lock` that an older `poolmgr` made there (on Windows also the task file of
`service install` in `service/`); a folder with other files stops the restore (exit 2), and the message names them. Do not do `init` there: `init` makes a new pool, not your pool. Then
`poolmgr list --live` must show each member. In the mode `local`, do `store sync` and `store publish` again (the cache
is not in the backup).

In a data folder with no pool, the other commands stop with
`STOP: no pool in <folder> (a new pool: poolmgr init <name>; a pool from a backup: poolmgr restore <file>)`, and they
make and change nothing there. `service install` stops in the same way. On a new computer, the correct step is
`restore`. The parts of an `init` of an older `poolmgr` that stopped (`config.json`, `keys/admin_a.pem`) stop the
restore: `poolmgr` never deletes a key. Move that folder to another place first.

The files use scrypt (else PBKDF2-HMAC-SHA256 with 600 000 rounds) and ChaCha20-Poly1305. A wrong pass phrase or a
changed byte: the file does not open. A Python with no scrypt (the system Python of macOS) cannot open a scrypt file
(section 1).

For a script, the variable `TFN_POOLMGR_PHRASE` gives the pass phrase, with no question (`init` then uses it for the
two files). Use it only on a computer that only you use.

## 10. The recovery

`recover` is the way back when key A is lost, or when another person has a copy of it (a stolen computer):

```
poolmgr recover --recovery OUR_HOME-recovery.tfnkey
```

The program asks for the pass phrase of the `.tfnkey` file. Then key R signs a roster with a new key A and a new
manager member, and the program makes a new pool key (the next epoch) and a new request key for each board. Each
board gets the roster with its old request key, then its new keys. After that, the old key A and the old request keys
do not work on the boards. `recover` needs the folder `pool/` of the data folder (it has the old request keys): on a
new computer, do `restore` first. A board that does not answer stays on a list in `pool/recover.json`: when it is on, do
the same `recover` command again. Then make a new backup.

While boards of the last `recover` wait, `recover` only sends to them again, and key A does NOT change. The program
tells it with `WARN: key A did NOT change (key A <key ID> of the last recover) ...`. When the computer with that key
A is lost or stolen too, add `--new-key`:

```
poolmgr recover --recovery OUR_HOME-recovery.tfnkey --new-key
```

Then key R signs a roster with a new key A, and the program makes a new pool key and new request keys, as above. A
board that still waits keeps its old manager ID and its old request key in `pool/recover.json`, so a later `recover`
(with or without `--new-key`) still reaches it.

The way back in each case:

| Case | The way back |
| --- | --- |
| The computer is lost or broken, and you have a backup | On another computer: install `poolmgr`, then `poolmgr restore <backup>` (section 9), and `poolmgr list --live`. Nothing changes on the boards. Then `service install` again, and in the mode `local`: `store sync` and `store publish` again |
| The computer is stolen, and you have a backup | On another computer: install `poolmgr`, then `restore` (section 9), then at once `recover --recovery <the .tfnkey file>`. When `recover` prints `WARN: key A did NOT change` (the backup has boards that wait for an earlier `recover`), the stolen computer has that key A: do `recover --recovery <the .tfnkey file> --new-key`. Then `webpass new` (the old web passwords are in the stolen data folder). When the pool has its own store key: `poolmgr store key` prints the key ID of the old key. Remove that key on each board (SETUP > TRUSTED PUBLISHERS, or the USB console `pub rm <key ID>`), delete `keys/store_pool.pem` of the data folder, then do `store key`, `store trust` and `store publish` again. Then, as in the row above: `service install` again, and in the mode `local`: `store sync`. Then make a new backup |
| The file of key A is lost or damaged, and the rest of the data folder is there (`doctor`: `FAIL key A`) | `recover --recovery <the .tfnkey file>` |
| You have no backup, only the `.tfnkey` file | Key R alone cannot send a request to a board (the request keys are in the backup). Do as in the next row |
| You have no backup | On each board SETUP > POOL > LEAVE (a CHILD: the USB console `pool leave`), and remove the web password of the pool (SETUP > WEB PASSWORD, an empty text): nobody knows it now. Then make a new pool (`init` with a new data folder, section 12) and `adopt` the boards again. The friends, the apps and the settings of the boards stay |
| You forgot the pass phrase of the backup, and this computer works | Make a new backup now, with a new pass phrase. Delete the old file |
| You forgot the pass phrase of the backup, and the computer is lost | The backup does not open: do as for "You have no backup" |
| You forgot the pass phrase of key R | `recover` is not possible, but the pool works on. To get a new key R, make a new pool (as in the next row) |
| Key R leaks (another person has the `.tfnkey` file and its pass phrase) | Key R can sign a roster of the pool, and `recover` cannot change key R. Make a new pool: `remove` each board (or LEAVE on each board; a CHILD: the USB console `pool leave`). Then `init` with a new data folder and `adopt` the boards again |
| A board is lost or stolen | `remove` it: the new pool key cuts it off |
| A board was off at a change of the pool | `serve` gives it the newest roster and key when it comes back. With no `serve`: `poolmgr push <member>`. Off at the adopt of another board: `poolmgr friends --check`. Off at `recover`: the same `recover` command again. Off at `update`: `update` again |
| The computer has a new address | In the mode `official`: nothing to do. In the mode `local`: `serve` checks the address each minute and sends the new address of the local store to the boards. With no `serve`: `poolmgr store local` again |
| A firewall blocks UDP 47474 or TCP 47480 | `find` sees no board, `serve` sees no beacon, and the boards cannot read the local store. Allow Python in the firewall of the computer, for the PRIVATE network only. On Windows, a rule is for one program file: allow `python.exe`, and also `pythonw.exe` for the service (section 11). With `ufw` on Linux: `sudo ufw allow from 192.0.2.0/24 to any port 47474 proto udp`, and the same for TCP 47480 (use the address range of your LAN). `adopt <address>` works with no `find` |

A new pool needs new file names in the current folder: `init` never overwrites a `.tfnkey` or `.tfnbak` file. Do
`init` in another folder. Another pool name helps only when it gives other file names (section 4.1).

## 11. In the background: `serve` and `service`

```
poolmgr serve
poolmgr serve --port 47490
poolmgr serve --openssl "C:\Program Files\Git\usr\bin\openssl.exe"
```

`serve` runs until you stop it (Ctrl+C, or the stop of the service). `--openssl <path>` names the `openssl` program
for `serve` (the service of Windows uses it: a task of Windows has no variables of its own). It does these jobs:
- It listens for the beacons of the members (UDP 47474) and keeps their addresses in `addrs.json` (for example a new
  address from DHCP).
- THE UPKEEP, each 30 s: it reads the pool state of each member whose beacon came, and of each other member with a
  known address each 2 minutes. A member with an older roster or key, or with a damaged pool, gets the newest roster
  and key (one time a minute for each board at most). A member that left with SETUP > POOL > LEAVE goes out of the
  roster and out of the friend list of each other member, and the other members get a new key. A member that keeps
  a friend entry that waits for its removal (section 5.4) gets the removal. A device at the address of a member that
  does not answer as a TFN board (another service, or an answer with bad values) gets no upkeep: one `FAULT probe`
  line, and the next try after 5 minutes.
- It reads the beacons of the previous pool key for 1 hour after a change of the key (a board that was off at the
  change).
- It prints a line for each open JOIN screen, with the `adopt` command. It never adopts a board by itself.
- In the mode `local`: the local store on TCP 47480 (only the 3 store requests), the store sync each 6 hours, and a
  check of the address of this computer each minute (a new address goes to the boards).
- With `"auto_update": true`: the updates each 6 hours (section 7).

`serve` reads `config.json` and the folder `pool/` again when a command changes them, and it starts or stops the local
store when the store mode changes. A command and `serve` never change the pool at the same time (section 12). A fault
of one job prints one line, and `serve` goes on.

`serve` prints its lines, and it also writes them into `log/serve.log` of the data folder. At 256 KB the file becomes
`log/serve.log.1` (the file before goes), so the two files stay small.

The lines of `serve`:

| Line | What it tells |
| --- | --- |
| `SERVE: pool <name>, ...` | The start: the members, the store mode, the ports, the automatic update |
| `LOCAL STORE: TCP <port> (GET /api/store/policy, ...)` | The local store starts (the mode `local`) |
| `LOCAL STORE: stopped (the store mode is <mode>)` | The store mode is not `local` now: the local store stops |
| `READ the pool again: <n> members, roster v<version>, key epoch <epoch>, store <mode>` | A command changed `config.json` or the pool, and `serve` uses the new values |
| `ADDRESS <name>: <address> (was <address>)` | A member has a new address |
| `UPKEEP <name>: <cause>` | The member gets the newest roster and key. With the cause `LEAVE on the board`, the member goes out of the roster and out of the friend lists of the other members, and the other members get a new key. With the cause `a friend entry waits for its removal`, the member gets the removal (section 5.4) |
| `  <line>` (2 spaces first) | A result of the line above it, for example the answer of each member, `BACKUP IS OLD: ...`, or a friend entry that a member did not take out |
| `JOIN SCREEN OPEN: ...` | A board with an open JOIN screen, and the `adopt` command for it |
| `LAN ADDRESS: ...` | The new address of the local store goes to the boards |
| `STORE SYNC: <n> packages in the mirror` | The mirror is new |
| `AUTO UPDATE: ...` | The result of the automatic update of a member |
| `STATUS: ...` | Each 10 minutes: the members, the members with a beacon, the store requests |
| `BUSY: ...` | A command changes the pool: the upkeep waits for the next round |
| `FAILED <job>: ...`, `FAULT <job>: ...` | A job failed (`FAULT`: a fault of the program), and `serve` goes on |
| `STOP: <fault>` | `serve` stops (exit 2), for example at the start: `the UDP port 47474 does not open ...` (another `serve` or another manager of boards runs on this computer) or `the TCP port 47480 does not open ...`. The line is also in `log/serve.log` and `log/poolmgr.log`: the service has no window. The systemd unit and the launchd agent start `serve` again after 10 s. The task of Windows can stay stopped until the next logon of your user: to start it now, `schtasks /Run /TN "TFN poolmgr"` |
| `SERVE STOP` | The end |

`service` makes `serve` start at each logon of your user. It runs as your user (no administrator). The data folder
must have a pool: else `service install` stops (a `serve` with no pool stops at each start):

```
poolmgr service                   the service file for this system, and the steps of the install
poolmgr service install           write the file and start the service
poolmgr service remove            stop the service and remove its file
```

`--dry-run` (with `install` or `remove`) prints the steps and changes nothing. `--system linux|macos|windows` shows
the file of another system (with `show`, or with `--dry-run`). `--port <port>` gives `serve` another TCP port.

| System | The service | Its output |
| --- | --- | --- |
| Linux, Raspberry Pi | A systemd user unit: `~/.config/systemd/user/tfn-poolmgr.service`. It gets the variable `OPENSSL` when you set it | `journalctl --user -u tfn-poolmgr.service -f`, or `log/serve.log` of the data folder |
| macOS | A launchd agent: `~/Library/LaunchAgents/tfn-os.poolmgr.plist`. It gets the variable `OPENSSL` when you set it | `log/serve.log` of the data folder (a fault of Python: `log/serve.err`) |
| Windows | A task of the Task Scheduler (`TFN poolmgr`) that starts at the logon of your user. Its file is `service/tfn-poolmgr-task.xml` of the data folder. The variable `OPENSSL` goes into the task as `serve --openssl <path>` | `log/serve.log` of the data folder: the task runs `pythonw.exe` with no window, and `openssl`, `icacls` and `whoami` start with no window too |

Set the variable `OPENSSL` before `service install` when the service needs it. The firewall rule of the Windows task
is for `pythonw.exe` (section 10).

On a Linux computer with no login (for example a Raspberry Pi with no screen), the user services start only after
`loginctl enable-linger <your user>`. Then the service starts at each start of the computer. The service names the
path of Python, the path of `poolmgr.py` and the data folder: when you move the program folder, do `service install`
again.

## 12. The data folder

The default data folder is `~/.tfn-poolmgr` (on Windows: `%USERPROFILE%\.tfn-poolmgr`). One data folder has one
pool. For another pool, give another folder: `--data <folder>` BEFORE the command (`poolmgr --data ~/pool2 list`),
or the variable `TFN_POOLMGR_DATA`. Let `init` or `restore` make the folder: then it gets the rights of THE RIGHTS
(below).

```
~/.tfn-poolmgr/            (mode 0700)
  config.json              the store mode, the TCP port, "auto_update", the channel (a copy of channel.json), the
                           address of the local store
  keys/admin_a.pem         key A (0600)
  keys/store_pool.pem      the pool store key (0600; only after "store key")
  pool/                    the pool: pool.json, secrets.json (0600), roster.bin, webpass.json (0600),
                           store_tok.json (the store token of each board), friends_out.json (the friend
                           entries that wait for their removal), recover.json (the state of the last
                           "recover", 0600)
  addrs.json               the last address of each member
  lock                     the change lock
  service/                 the task file of the service (Windows)
  cache/fw/                the firmware files of "update"
  cache/store/             the mirror of the official store and the pool's own apps
  log/poolmgr.log          one line for each change and each refusal (no secret, no key value)
  log/serve.log            the lines of serve (and serve.log.1: the lines before)
```

THE RIGHTS. The modes in the tree (0700 and 0600) are for Linux and macOS. On Windows, when `init` or `restore` makes
the data folder, the program sets its rights with `icacls` (each version of Python): no rights from the parent
folder, and full control only for your user, `SYSTEM` and `Administrators`. The folders and files in it get the same
rights. `init` and `restore` give `keys/` and `pool/` their own rights of this kind before the first key file goes
into them, also in a data folder that was there before. `store key` and `recover` do it again before they write a key
file (a data folder of an older `poolmgr` can have `keys/` and `pool/` with the rights of the folder). Each other
command that changes the pool (for example `push`, `remove`, `adopt`, `webpass new`) does it at its first change, for
each of the two parts that another account can read. When the rights do not set (for example on a drive with no
access lists), the command prints `WARN: the rights of <folder> do not set (...)` and goes on. So the key files never
get the rights of the folder above them. Each file with a key, a request key or a web password is in `keys/` or
`pool/` (also the state of `recover`).

`doctor` checks the rights of the data folder and of its parts `keys/` and `pool/` (also of a folder that you made,
or that an older `poolmgr` made). When other accounts have rights (on Linux and macOS: the group or the other users),
it prints two `WARN` lines for each folder: the accounts, then the command that gives the rights only to your user
(and on Windows to `SYSTEM` and `Administrators`). `init` and `restore` print the same lines for a data folder that
was there before. On Windows the
command names your user by its SID, for example:
```
icacls "<the data folder>" /inheritance:r /grant:r "*S-1-5-21-<your SID>:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"
```
Copy the command from the `WARN` line: it works in `cmd.exe` and in PowerShell. The accounts of the first `WARN`
line are names (for example `Users`) or SIDs; `icacls "<the data folder>"` shows the names of all of them. When
`whoami.exe` gives no SID of your user, the second `WARN` line has no command (the list of the first line can then
have your own user too, and a command could remove your own rights): look at the names with `icacls`. Keep the
data folder in your user folder (the default), not in a shared folder or in a folder that a sync program shares.

One change at a time: each change of the pool takes the lock of the data folder. Another `poolmgr` waits up to 30 s,
then it stops with `THE POOL IS BUSY` (exit 2).

The folder `pool/` has the form of the pool tool ([pools.md](pools.md), section 10), so
`pool_tool.py --dir <data folder>/pool --key <data folder>/keys/admin_a.pem show --live` works on it.

## 13. The limits

- A pool has 16 boards at most. A board is in one pool at most. One data folder has one pool.
- One `serve` on one computer: the UDP port 47474 is the same for each pool.
- The computer and the boards must be in the same LAN: the JOIN packets and the beacons go to the LAN broadcast.
- While `serve` runs, `find` and `adopt` with no address see no JOIN screen: use the address (section 5.1).
- A board has 16 friends and 8 added publisher keys at most.
- `update` sends only the files of the public releases, signed with the official OTA keys. A board in the mode
  ENFORCE with no official OTA key in its key list does not get them.
- The channel in `config.json` is a copy that `init` makes. `poolmgr channel <channel.json>` changes it (section
  2.3).
- No command sets `"auto_update"`: edit `config.json` of the data folder.
- The program is not a link peer: it sends no beacon, and it has no WALKIE, no TALKIE and no inbox.
- Not in this version: WALKIE, TALKIE and the inbox through the program; the screen picture of a board; a web page;
  Docker; profiles and required apps; a VPN for a far board; friends across two pools; a pool with no computer (one
  board as the admin); the store of one pool for other pools.
