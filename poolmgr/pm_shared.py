"""The shared modules of poolmgr and the faults of the program.

The shared modules (pool.py, pool_crypto.py, pool_tool.py, fw_sign.py, store_index.py) are in one of these places:
  1. the folder of this file (the release zip tfn-poolmgr-V.zip has copies of them);
  2. ../sdk/tools (the public tree);
  3. ../manager + ../sdk/tools (a tree that keeps the pool modules and fw_sign.py in manager/).
setup() puts the first complete place at the start of sys.path. Then "from pm_shared import pool" gives the module.

The exit codes of the program (poolmgr.py): 0 = ok, 1 = a board refused or a check failed (Failed, pool_tool.Refused),
2 = a fault of the input or of the tool (InputError, NotDone, pool_tool.ToolError).
"""
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# the modules that must be in a place (store_index.py is optional: only the local store needs it)
NEED = ('fw_sign', 'pool_crypto', 'pool', 'pool_tool')


class Failed(Exception):
    """A check failed, or a board refused (exit 1)."""


class InputError(Exception):
    """A fault of the input or of the tool (exit 2)."""


class NotDone(InputError):
    """A part of the program that is not done (exit 2)."""


# Where the channel comes from (the hint of each message about a missing channel). The channel is channel.json of a
# release (sdk/tools/release_pack.py): the web site ("pages"), the repo and the official keys. A tfn-keys.json of a
# release has only the keys: update and store sync have no web site then. init reads the channel of a new pool;
# "poolmgr channel <file>" sets the channel of a pool that exists (the same official keys).
CHANNEL_HINT_NEW = ('the release zip has poolmgr/channel.json; from a git checkout: poolmgr init <pool name> --keys '
                    '<channel.json of a GitHub release> (a tfn-keys.json has only the keys: no web site, no repo)')
CHANNEL_HINT_POOL = ('poolmgr channel <channel.json of a GitHub release of tfn-os> sets the channel of the pool (the '
                     'same official keys as the pool; the release zip has poolmgr/channel.json; a tfn-keys.json has '
                     'only the keys: no web site, no repo)')


def channel_hint(data):
    """The hint for a missing channel: the way that works for the data folder (init for no pool, else channel)."""
    return CHANNEL_HINT_POOL if data.has_pool() else CHANNEL_HINT_NEW


def not_done(what):
    """Stop with NotDone: the stub of a part that a track fills."""
    raise NotDone('NOT DONE: %s' % what)


def places():
    """The groups of folders, in the order of the search."""
    tools = os.path.join(ROOT, 'sdk', 'tools')
    return [[HERE], [tools], [os.path.join(ROOT, 'manager'), tools]]


def has_all(group):
    return all(any(os.path.isfile(os.path.join(d, n + '.py')) for d in group) for n in NEED)


FOLDERS = None


def setup():
    """Find the shared modules (the first complete group) and put their folders at the start of sys.path."""
    global FOLDERS
    if FOLDERS is not None:
        return FOLDERS
    for group in places():
        if has_all(group):
            for d in reversed(group):
                if d not in sys.path:
                    sys.path.insert(0, d)
            FOLDERS = group
            return group
    raise InputError('the shared modules (pool.py, pool_crypto.py, pool_tool.py, fw_sign.py) are not near the '
                     'program: use the release zip, or a checkout of the repo')


setup()
fw_sign = importlib.import_module('fw_sign')
pool_crypto = importlib.import_module('pool_crypto')
pool = importlib.import_module('pool')
pool_tool = importlib.import_module('pool_tool')


def store_index():
    """The module store_index.py (only its pure functions: they need no package "cryptography")."""
    return importlib.import_module('store_index')
