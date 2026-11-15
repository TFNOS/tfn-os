"""The NVS contract: each namespace and each key that the code of main/, libs/ and apps/ reads or writes.

A board keeps its settings in these keys. A lost or renamed key loses the settings of each user at the next update.

The ways that the code names a key (all are found):
- store_get(key, ...) / store_set(key, ...): the settings store (the NVS namespace of main/settings.c, and the spill
  file for a full NVS). The result shows these keys under the namespace "<store>".
- nvs_open(ns, ...), nvs_entry_find(part, ns, ...): the namespaces. nvs_get_*(h, key, ...), nvs_set_*(h, key, ...),
  nvs_erase_key(h, key): the key of the namespace of the nearest nvs_open() before the call.
- The key expression: a literal, a #define, a macro prefix + a literal (TW_KEY "pos"), an array of keys
  (KEYS[i]), a local buffer made by snprintf() (the format stays as a pattern, for example "ck_l%d"), a local
  function that makes the key (best_key()), a const string.
- A wrapper function that takes the key as a parameter (notify.c, pad_config.c ...): its callers give the key.
- The settings panel items {PI_PICK, ..., "key"} (main/sysui.h pitem_t: the last field is the store key).
An expression that the reader cannot resolve goes into "unresolved" (file + function + expression) as information.
"""
import os
import re

import csrc

STORE = '<store>'
STORE_CALLS = ('store_get', 'store_set')
NVS_KEY_CALLS = ('nvs_get_u8', 'nvs_get_i8', 'nvs_get_u16', 'nvs_get_i16', 'nvs_get_u32', 'nvs_get_i32',
                 'nvs_get_u64', 'nvs_get_i64', 'nvs_get_str', 'nvs_get_blob', 'nvs_set_u8', 'nvs_set_i8',
                 'nvs_set_u16', 'nvs_set_i16', 'nvs_set_u32', 'nvs_set_i32', 'nvs_set_u64', 'nvs_set_i64',
                 'nvs_set_str', 'nvs_set_blob', 'nvs_erase_key', 'nvs_find_key')
PREFIXES = ('main/', 'libs/', 'apps/')


def _namespace(src, folder, pos):
    """The namespace of the nearest nvs_open() before pos in the same function (else in the file)."""
    f = src.func_at(pos)
    for lo in ([f[0]] if f else []) + [0]:
        best = None
        for _, p, args in csrc.calls(src.text[lo:pos], ('nvs_open',)):
            best = args[0] if args else None
        if best:
            r = csrc.resolve(best, src, folder, pos)
            if r:
                return sorted(r)[0]
            return '<?' + best + '>'
    return '<?>'


def is_decl(arg):
    """True for a parameter of a declaration (const char *key), not for an argument of a call."""
    return re.fullmatch(r'(?:const\s+)?\w+(?:\s+|\s*\*\s*)(?:const\s+)?\w+', arg.strip()) is not None


class Scan:
    def __init__(self):
        self.files = csrc.tracked(PREFIXES)
        self.sources = [csrc.Source(f) for f in self.files]
        shared = [s for s in self.sources if s.rel.startswith('main/') and s.rel.endswith('.h')]
        self.folder = csrc.Folder(self.sources, shared)
        self.keys = {}
        self.namespaces = set()
        self.unresolved = set()
        self.wrappers = {}

    def add(self, ns, keys):
        self.keys.setdefault(ns, set()).update(k for k in keys if k)

    def key_call(self, src, pos, expr, ns_kind, wrapper_ok=True):
        r = csrc.resolve(expr, src, self.folder, pos)
        ns = STORE if ns_kind == 'store' else _namespace(src, self.folder, pos)
        if r is not None:
            self.add(ns, r)
            return
        f = src.func_at(pos)
        var = expr.strip()
        if f and f[2] in STORE_CALLS:
            return                                  # (the store itself: its callers give the keys)
        if wrapper_ok and f and csrc.IDENT_RE.match(var) and var in f[3]:
            self.wrappers[f[2]] = (f[3].index(var), ns_kind, src.rel)
            return
        fmt = self.format_param(src, f, pos, var) if wrapper_ok and f else None
        if fmt:
            self.wrappers[f[2]] = (f[3].index(fmt), ns_kind, src.rel)
            return
        self.unresolved.add('%s: %s(): %s' % (src.rel, f[2] if f else '-', var))

    @staticmethod
    def format_param(src, f, pos, var):
        """The parameter that is the snprintf() format of the local key buffer var (its callers give the key)."""
        for _, _, args in csrc.calls(src.text[f[0]:pos], ('snprintf',)):
            if len(args) > 2 and re.sub(r'\s', '', args[0]) == var and args[2] in f[3]:
                return args[2]
        return None

    def run(self):
        for src in self.sources:
            for _, pos, args in csrc.calls(src.text, ('nvs_open',)):
                if args:
                    r = csrc.resolve(args[0], src, self.folder, pos)
                    self.namespaces.update(r or {'<?' + args[0] + '>'})
            for _, pos, args in csrc.calls(src.text, ('nvs_entry_find',)):
                if len(args) > 1 and args[1] not in ('NULL',):
                    r = csrc.resolve(args[1], src, self.folder, pos)
                    if r:
                        self.namespaces.update(r)
            for _, pos, args in csrc.calls(src.text, STORE_CALLS):
                if args and not is_decl(args[0]):
                    self.key_call(src, pos, args[0], 'store')
            for _, pos, args in csrc.calls(src.text, NVS_KEY_CALLS):
                if len(args) > 1 and not is_decl(args[1]):
                    self.key_call(src, pos, args[1], 'nvs')
            for m in re.finditer(r'\{\s*PI_PICK\s*,', src.text):
                c = csrc.match_close(src.text, m.start(), '{', '}')
                parts = csrc.split_args(src.text[m.start() + 1:c]) if c > 0 else []
                if len(parts) >= 7 and parts[-1] != 'NULL':
                    self.key_call(src, m.start(), parts[-1], 'store', wrapper_ok=False)
        # the callers of the wrappers give the key (one level)
        done = set()
        while set(self.wrappers) - done:
            names = sorted(set(self.wrappers) - done)
            done |= set(names)
            for src in self.sources:
                mine = [n for n in names if os.path.dirname(self.wrappers[n][2]) == os.path.dirname(src.rel)]
                if not mine:
                    continue
                for name, pos, args in csrc.calls(src.text, mine):
                    idx, kind, home = self.wrappers[name]
                    if is_decl(args[idx]) if idx < len(args) else True:
                        continue
                    f = src.func_at(pos)
                    if f and f[2] == name or idx >= len(args):
                        continue
                    self.key_call(src, pos, args[idx], kind)
        settings = next((s for s in self.sources if s.rel == 'main/settings.c'), None)
        if settings and STORE in self.keys:
            ns = csrc.resolve('NVS_NS', settings, self.folder, 0)
            if ns:
                self.namespaces.update(ns)
        return self


def extract():
    s = Scan().run()
    keys = {ns: sorted(v) for ns, v in sorted(s.keys.items())}
    return {
        'namespaces': sorted(s.namespaces),
        'keys': keys,
        'wrappers': sorted('%s (%s, arg %d)' % (n, w[2], w[0]) for n, w in s.wrappers.items()),
        'unresolved': sorted(s.unresolved),
    }


# The parts of the result that must not lose an item, and the parts that are only information.
CONTRACT = ('namespaces', 'keys')
INFO = ('wrappers', 'unresolved')
