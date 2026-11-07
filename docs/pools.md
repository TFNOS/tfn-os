# Pools

A pool is a group of boards that trust each other. The boards of a pool find each other on the network, play the
games for 2 boards, and talk with WALKIE and TALKIE. A board of another pool cannot see these boards and cannot talk
to them. Anyone can make a pool: the pool manager `poolmgr` on a computer ([poolmgr.md](poolmgr.md)), or the pool
tool (`sdk/tools/pool_tool.py`, section 10).

A board is in 0 or 1 pool. A board with no pool works alone, with its friends (the user guide, section 8).

## 1. The parts of a pool

| Part | What it is |
| --- | --- |
| Pool ID and name | 16 random bytes, and a name of 1 to 24 bytes |
| Pool key | 32 random bytes: the secret of the group. The keys of the packets come from it |
| Roster | The signed list of the pool: the members (up to 16 boards and the manager), their roles, the admin keys, the addresses of the manager, the flags, a version |
| Admin keys | One or two P-256 keys that sign the roster: key A (the manager uses it) and key R (a recovery key, kept off line) |
| Roles | MEMBER, MANAGER (it can wake TALKIE and change the pool), CHILD (it cannot leave by itself) |
| Device key | Each board makes its own key pair at the first start (`/api/id`, the console `id`). The device ID comes from it |
| Box key | An X25519 key of each board, signed with its device key. The manager sends a new pool key to it with no tap |

The board keeps the roster in `/data/tfn_pool.bin` and the secrets in the NVS namespace `tfnpool`. The board takes
a roster only when an admin key signed it, its version is higher than the last one, and the board is a member. A
file or a key that does not verify = POOL DAMAGED: the board works as with no pool until the manager sends the pool
again.

## 2. Join a pool (the 6 digits)

1. On the board: SETUP > POOL > JOIN. The board waits for a manager for 10 minutes.
2. On the manager: adopt the board (`poolmgr adopt`, or `python sdk/tools/pool_tool.py adopt <address of the board>`).
3. The board and the manager show the same 6 digits, for example `482 913`. The board asks
   "THE MANAGER <name> WANTS TO MANAGE THIS BOARD".
4. Tap ALLOW on the board only when the manager shows the same 6 digits. Then confirm on the manager.
5. The board shows THIS BOARD IS IN THE POOL.

ALLOW takes only a real finger on the glass: a tap from the web page or from the console does not count (the board
shows "A REMOTE TAP CANNOT ALLOW"). The finger must come down 0.8 s or more after the digits show: a finger that was on
the glass for another thing does not answer. ALLOW is for the digits on the screen: other digits end the JOIN. A
person with the USB cable can allow on the console: `pool allow 482913`. When the digits are not the same, tap NO: a
device between the board and the manager can try to take the board.

A board with a web password refuses a JOIN with no password (`401`, `login`). `poolmgr adopt` then asks for the
password (or takes `--password`), and the board keeps it. `pool_tool.py adopt <address> --password` does the same.

A JOIN clears the manager of before (its addresses and its token).

## 3. The token JOIN (an old network of boards)

A build with the option `CONFIG_TFN_POOL_LEGACY` (an old network of boards that moves to pools) has one more way: a
board that has the store token of its manager and no pool joins the pool of that manager one time with the token, with
no digits and no tap. After the JOIN the token never changes the pool. A public build (the option off) has no token
JOIN.

The limit: a board with no token takes the token from the first client that sends `POST /api/apps?op=mgr` (trust on
first use). So on a new board of a LEGACY build, the first device on the LAN can set the token and then make the board
join its own pool, with no tap ([security.md](security.md) section 2.5). Use a LEGACY build only on a network that you
trust.

## 4. Leave a pool

- On the board: SETUP > POOL > LEAVE (a CHILD cannot leave by itself; the manager removes it).
- On the USB console: `pool leave`.
- The manager can remove a board. Then it sends a new pool key to the other members, so the board that left cannot
  read the pool any more. A board takes the frames of the previous key for 1 hour after a new key.

The board erases its pool data. It keeps the lowest roster version, so an old roster cannot come back.

## 5. RELAXED and STRICT

The build option `CONFIG_TFN_POOL_STRICT` sets the receive rules of a board in a pool. Nothing on the network
changes it. `GET /api/pool` shows it (`"strict"`).

| A packet that comes in | RELAXED | STRICT |
| --- | --- | --- |
| A packet of this pool with a good tag and a new counter | take | take |
| A packet of another pool, a bad tag, a counter that came before | drop | drop |
| An old (v3) packet with the address of a member | take (a member on an old firmware) | drop (a forged member) |
| An old packet from a friend in another pool | take | drop |
| An old packet from a friend with no pool, or from a board that is not a friend | take (the apps check the friends) | take |
| A plain beacon of a board that is not a member and not a friend | into the list of the boards | not into the list |
| WAKE (TALKIE opens) | from a friend | from a MANAGER of the roster, or from a friend with no pool |

RELAXED lets a pool work while some members still run an older firmware. STRICT is the target: use it when all
members run the pool code. A board with no pool works the same in both modes.

A board in a pool also sends an old (v3) copy of the packets of a game while a board with no pool plays that game
with it (for 5 s after its last old packet). So a pool board and a board with no pool can still play.

## 6. The beacons

| Beacon | Who sends it |
| --- | --- |
| `'A' 'P'`: encrypted with a key of the pool (name, app, flags, device ID) | each board of a pool, each 2 s |
| `'A' 'B'`: plain | a LEGACY build with no pool; a pool board only while the roster flag `LEGACY_BEACON` is on |
| `'A' 'J'`: plain (MAC, device ID, name) | a board while its JOIN screen is open |

A public build with no pool sends no beacon. The flag `LEGACY_BEACON` lets the boards with no pool see the members
while a network moves to pools. Turn it off when all members run STRICT.

## 7. The packets of a pool (link v4)

Each link packet of a pool board has a pool tag byte, a 32-bit counter and an 8-byte SipHash-2-4 tag over the sender
MAC and the packet. The receiver checks the pool tag (another pool: `other`), the sender (not a member: `who`), the
tag (`bad`) and the counter (seen, or more than 64 below the highest: `old`). A good packet counts as `ok`. The apps
get the same messages as before: no app changes for the pools. The WALKIE calls and the invites of a pool have
version 2 with the same tag and counter.

## 8. The console `pool`

| Command | What it does |
| --- | --- |
| `pool` | the state (`POOL STATE <none/join/ok/damaged> ...`), the members, the counts `POOL V4 OK n OTHER n WHO n BAD n OLD n`, and the 6 digits of a JOIN |
| `pool join` | opens the JOIN (as SETUP > POOL > JOIN) |
| `pool stop` | ends the JOIN |
| `pool allow <6 digits>` | ALLOW |
| `pool no` | NO |
| `pool leave` | LEAVE |

## 9. `/api/pool`

`GET /api/pool` gives only public values and needs no web password:

| Field | What it is |
| --- | --- |
| `state` | `none`, `join`, `ok` or `damaged` |
| `pool`, `name`, `v`, `epoch` | the pool ID, the pool name, the roster version, the number of the pool key |
| `role` | `member`, `manager` or `child` ("" with no pool) |
| `strict`, `legacy` | the two build options |
| `members`, `list` | the number of members and the list `[{id, mac, role, flags, me, name}]` |
| `mgr`, `mgr_lan`, `mgr_vpn` | the ID of admin key A and the addresses of the manager |
| `id`, `mac`, `bx`, `bxs` | the device ID, the MAC, the box key and its signature |
| `bn` | a random value of this start (for the request header) |
| `join`, `step`, `why`, `left` | the JOIN: its state, its step, the cause of a failure, the seconds left |
| `v4` | `{ok, other, who, bad, old}`: the counts of the packets since the start |

`POST /api/pool?op=<op>` changes the pool: `hello` and `reveal` (the JOIN), `set` (a roster), `key` (a new pool key),
`webpass` (the web password), `leave`. Except in a JOIN, these need the request header of the pool manager:
`X-TFN-Auth: <manager ID>.<counter>.<HMAC-SHA256>` with the request key of the board, the value `bn` of this start,
the counter (higher at each request), the method and the address. A request of another key, an old counter, or a
request of before the last start gets `401`.

## 10. Your own pool: the pool manager and the pool tool

A person with a computer uses the pool manager `poolmgr` ([poolmgr.md](poolmgr.md)). It makes the pool, the keys and
an encrypted backup, adopts the boards with the 6 digits, makes the members friends, updates their firmware one board
at a time, serves a local store, and gives the way back when the computer is lost or stolen.

The pool tool `sdk/tools/pool_tool.py` stays the low-level tool under `poolmgr`: a pool with no manager, for a test or
for the work with the keys. It needs Python 3 and the `openssl` program, and it never prints a secret. It also works
on the pool of `poolmgr`: `--dir <data folder>/pool --key <data folder>/keys/admin_a.pem`.

```
python sdk/tools/pool_tool.py keygen my_admin.pem
python sdk/tools/pool_tool.py --dir my_pool --key my_admin.pem create "MY POOL" --manager [--recovery <pem or hex>]
python sdk/tools/pool_tool.py --dir my_pool --key my_admin.pem adopt <address> [--console <port>] [--password]
python sdk/tools/pool_tool.py --dir my_pool --key my_admin.pem webpass
python sdk/tools/pool_tool.py --dir my_pool --key my_admin.pem show --live
python sdk/tools/pool_tool.py --dir my_pool --key my_admin.pem remove <member>
python sdk/tools/pool_tool.py --dir my_pool check-backup my_admin.pem
```

`--manager` makes the tool a MANAGER member of the pool (with the ID of the admin key). A board in a pool takes a
request of the tool (the roster of the next `adopt`, `push`, `remove`, `webpass`) only from a MANAGER member: with
no `--manager`, `create` gives a warning, and each request after the first `adopt` gets `401`.

Keep the admin key and the folder of the pool (it holds the pool key) safe. Keep an encrypted copy of a recovery key
off line: with the recovery key you can replace a lost admin key.

## 11. The gate of two pools

`tests/pool/gate.py` checks on real boards that a second pool cannot see or talk to a first pool: the radio games,
the lists of the boards, the calls and the invites, the pool requests, the replay of a recorded packet, a forged
member, and no effect on the first pool. At the end each board goes back to its pool. The tool must have the UDP port
47474 of the computer alone: when another program has the port, it stops at the start and changes no board. On macOS
the tool cannot see a program that sets SO_REUSEADDR and binds the LAN address of the computer on that port: stop
such a program before the test. The test of the tool with fake boards: `python tests/unit/test_pool_gate.py`.
