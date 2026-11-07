"""The C source reader of the contract checks (Python stdlib only).

The checks read only the files that git tracks, so the result is the same on each computer (a local private file
does not change it). This module removes the comments, finds the function bodies, the #define strings and the
string arrays, and resolves a key expression (a literal, a macro, a macro prefix + a literal, an array, an
snprintf() format) to its strings.
"""
import functools
import os
import re
import subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

# Vendored code: the checks do not read it (it has no keys, routes or commands of TFN OS).
VENDORED = ('third_party/', 'components/')
C_EXT = ('.c', '.cc', '.h', '.inc')


def tracked(prefixes):
    """The tracked C files under the prefixes (sorted, '/' paths). Files in a 'tools/' folder are host tools."""
    try:
        out = subprocess.run(['git', 'ls-files', '-z', '--'] + list(prefixes), cwd=ROOT, capture_output=True,
                             check=True).stdout.decode('utf-8')
        files = [f for f in out.split('\0') if f]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for p in prefixes:
            for d, _, names in os.walk(os.path.join(ROOT, p)):
                files += [os.path.relpath(os.path.join(d, n), ROOT).replace(os.sep, '/') for n in names]
    res = []
    for f in files:
        if not f.endswith(C_EXT) or f.startswith(VENDORED) or '/tools/' in f:
            continue
        if os.path.isfile(os.path.join(ROOT, f)):
            res.append(f)
    return sorted(res)


_COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?(?:\*/|\Z)|"(?:\\.|[^"\\\n])*"?|\'(?:\\.|[^\'\\\n])*\'?', re.S)
_TOKEN = re.compile(r'"(?:\\.|[^"\\\n])*"?|\'(?:\\.|[^\'\\\n])*\'?|[(){}\[\]]')


def _blank(m):
    t = m.group(0)
    if t.startswith('/'):
        return re.sub(r'[^\n]', ' ', t)
    return t


def strip_comments(text):
    """The text with each comment replaced by spaces (the strings and the line numbers stay)."""
    return _COMMENT_OR_STRING.sub(_blank, text)


def match_close(text, i, open_ch='(', close_ch=')'):
    """The index of the bracket that closes the bracket at text[i] (strings skipped); -1 when it does not close."""
    depth = 0
    for m in _TOKEN.finditer(text, i):
        t = m.group(0)
        if t == open_ch:
            depth += 1
        elif t == close_ch:
            depth -= 1
            if depth == 0:
                return m.start()
    return -1


def split_args(s):
    """Split a call argument text at the top-level commas."""
    args, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c in '"\'':
            j = i + 1
            while j < len(s) and s[j] != c:
                j += 2 if s[j] == '\\' else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            args.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if cur or args:
        args.append(''.join(cur).strip())
    return args


@functools.lru_cache(maxsize=None)
def _call_re(names):
    return re.compile(r'(?<![\w.>])(' + '|'.join(re.escape(n) for n in names) + r')\s*\(')


def calls(text, names):
    """Each call of one of the names: (name, start index, the argument list)."""
    for m in _call_re(tuple(names)).finditer(text):
        o = m.end() - 1
        c = match_close(text, o)
        if c < 0:
            continue
        yield m.group(1), m.start(), split_args(text[o + 1:c])


STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
IDENT_RE = re.compile(r'[A-Za-z_]\w*$')
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'else', 'do'}


class Source:
    """One C file: the text with no comments, its functions and its string definitions."""

    def __init__(self, rel):
        self.rel = rel
        with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as f:
            self.text = strip_comments(f.read())
        self.funcs = self._functions()
        self.defines = {}
        for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(.+)$', self.text, re.M):
            self.defines.setdefault(m.group(1), m.group(2).strip())
        self.arrays = {}
        for m in re.finditer(r'(\w+)\s*\[[^\]]*\]\s*(?:\[[^\]]*\]\s*)*=\s*\{', self.text):
            c = match_close(self.text, m.end() - 1, '{', '}')
            if c > 0:
                self.arrays.setdefault(m.group(1), self.text[m.end():c])
        self.consts = {}
        for m in re.finditer(r'(?:const\s+)?char\s*(?:\*\s*(?:const\s+)?)?(\w+)\s*(?:\[\s*\])?\s*=\s*([^;{]+);',
                             self.text):
            self.consts.setdefault(m.group(1), m.group(2).strip())

    def _functions(self):
        res = []
        t = self.text
        depth = 0
        start = 0
        for m in _TOKEN.finditer(t):
            tok = m.group(0)
            if tok == '{':
                if depth == 0:
                    start = m.start()
                depth += 1
            elif tok == '}':
                depth -= 1
                if depth == 0:
                    self._add_function(res, start, m.start())
                depth = max(depth, 0)
        return res

    def _add_function(self, res, i, end):
        t = self.text
        k = i - 1
        while k >= 0 and t[k] in ' \t\r\n':
            k -= 1
        if k < 0 or t[k] != ')':
            return
        o = self._open_of(k)
        if o <= 0:
            return
        j = o - 1
        while j >= 0 and t[j] in ' \t\r\n':
            j -= 1
        e = j + 1
        while j >= 0 and (t[j].isalnum() or t[j] == '_'):
            j -= 1
        name = t[j + 1:e]
        if not name or name in KEYWORDS:
            return
        params = [re.findall(r'\w+', p)[-1] for p in split_args(t[o + 1:k]) if re.findall(r'\w+', p)]
        res.append((i, end, name, params))

    def _open_of(self, close):
        depth = 0
        for k in range(close, -1, -1):
            if self.text[k] == ')':
                depth += 1
            elif self.text[k] == '(':
                depth -= 1
                if depth == 0:
                    return k
        return -1

    def func_at(self, pos):
        for f in self.funcs:
            if f[0] <= pos <= f[1]:
                return f
        return None

    def line_of(self, pos):
        return self.text.count('\n', 0, pos) + 1


class Folder:
    """The string definitions of a group of files (a file sees the #define of the other files of its folder)."""

    def __init__(self, sources, shared=()):
        self.by_dir = {}
        for s in list(sources) + list(shared):
            self.by_dir.setdefault(os.path.dirname(s.rel), []).append(s)
        self.shared = list(shared)

    def lookup(self, src, name, table):
        for s in [src] + self.by_dir.get(os.path.dirname(src.rel), []) + self.shared:
            v = getattr(s, table).get(name)
            if v is not None:
                return v
        return None


def unescape(s):
    return s.encode('utf-8').decode('unicode_escape', errors='replace') if '\\' in s else s


def resolve(expr, src, folder, pos, depth=0):
    """The strings that a key expression can give (a format such as 'hs%d' stays a pattern); None = unknown."""
    expr = expr.strip()
    expr = re.sub(r'^\(\s*(?:const\s+)?char\s*\*\s*\)', '', expr).strip()
    if depth > 6 or not expr or expr in ('NULL', '0'):
        return None if depth > 6 else set()
    while expr.startswith('(') and match_close(expr, 0) == len(expr) - 1:
        expr = expr[1:-1].strip()
    tern = split_ternary(expr)
    if tern:
        res = set()
        for p in tern:
            r = resolve(p, src, folder, pos, depth + 1)
            if r is None:
                return None
            res |= r
        return res
    toks = re.findall(r'"(?:[^"\\]|\\.)*"|[A-Za-z_]\w*|\S', expr)
    if toks and all(t.startswith('"') or IDENT_RE.match(t) for t in toks) and any(t.startswith('"') for t in toks):
        out = ['']
        for t in toks:
            if t.startswith('"'):
                out = [o + unescape(t[1:-1]) for o in out]
            else:
                r = resolve(t, src, folder, pos, depth + 1)
                if not r:
                    return None
                out = [o + x for o in out for x in sorted(r)]
        return set(out)
    m = re.fullmatch(r'([A-Za-z_]\w*)\s*\[.*\]', expr)
    if m:
        body = folder.lookup(src, m.group(1), 'arrays')
        if body is None:
            return None
        return array_strings(body, src, folder, pos, depth)
    m = re.fullmatch(r'([A-Za-z_]\w*)\s*\(.*\)', expr, re.S)
    if m:
        return func_strings(m.group(1), src, folder, depth)
    if IDENT_RE.match(expr):
        v = folder.lookup(src, expr, 'defines')
        if v is not None:
            return resolve(v, src, folder, pos, depth + 1)
        r = local_strings(expr, src, folder, pos, depth)
        if r is not None:
            return r
        f = src.func_at(pos)
        if f and expr in f[3]:
            return None                             # (a parameter: the callers give the value)
        v = src.consts.get(expr)
        if v is not None:
            return resolve(v, src, folder, pos, depth + 1)
    return None


def split_ternary(expr):
    """The two values of 'c ? a : b' at the top level (strings and brackets skipped); None for no ternary."""
    level, q = 0, -1
    nest = 0
    i = 0
    while i < len(expr):
        c = expr[i]
        if c in '"\'':
            j = i + 1
            while j < len(expr) and expr[j] != c:
                j += 2 if expr[j] == '\\' else 1
            i = j + 1
            continue
        if c in '([{':
            level += 1
        elif c in ')]}':
            level -= 1
        elif level == 0 and c == '?':
            if q < 0:
                q = i
            else:
                nest += 1
        elif level == 0 and c == ':' and q >= 0:
            if nest == 0:
                return expr[q + 1:i], expr[i + 1:]
            nest -= 1
        i += 1
    return None


def array_strings(body, src, folder, pos, depth):
    res = set()
    for item in split_args(re.sub(r'[{}]', ',', body)):
        if not item or item == 'NULL':
            continue
        r = resolve(item, src, folder, pos, depth + 1)
        if r is None:
            if STR_RE.search(item):
                res |= {unescape(x) for x in STR_RE.findall(item)}
            continue
        res |= r
    return res


FMT_CALLS = ('snprintf', 'sprintf', 'strcpy', 'strlcpy', 'strncpy')


def local_strings(var, src, folder, pos, depth):
    """The strings that a local variable can hold before pos: snprintf/strcpy into it, or an assignment."""
    f = src.func_at(pos)
    lo = f[0] if f else max(0, pos - 3000)
    region = src.text[lo:pos]
    res = set()
    found = False
    for name, _, args in calls(region, FMT_CALLS):
        if len(args) < 2 or re.sub(r'\s', '', args[0]) != var:
            continue
        fmt = args[2] if name in ('snprintf', 'strlcpy', 'strncpy') and len(args) > 2 else args[1]
        if name in ('strlcpy', 'strncpy'):
            fmt = args[1]
        r = resolve(fmt, src, folder, lo, depth + 1)
        if r is None:
            continue
        res |= r
        found = True
    local = {f[2] for f in src.funcs}
    for name, _, args in calls(region, sorted(local)) if local else ():
        if var in [re.sub(r'\s', '', a) for a in args]:
            r = func_strings(name, src, folder, depth + 1)
            if r:
                res |= r
                found = True
    for m in re.finditer(r'(?<![\w.>])' + re.escape(var) + r'\s*=\s*([^;=][^;]*);', region):
        r = resolve(m.group(1), src, folder, lo, depth + 1)
        if r:
            res |= r
            found = True
    return res if found else None


def func_strings(name, src, folder, depth):
    """The strings that a local function gives (its snprintf formats and its returned literals)."""
    for s in [src] + folder.by_dir.get(os.path.dirname(src.rel), []):
        for f in s.funcs:
            if f[2] != name:
                continue
            body = s.text[f[0]:f[1]]
            res = set()
            for _, _, args in calls(body, ('snprintf', 'sprintf')):
                fmt = args[2] if len(args) > 2 and not args[1].startswith('"') else args[1] if len(args) > 1 else ''
                r = resolve(fmt, s, folder, f[0], depth + 1)
                if r:
                    res |= r
            for m in re.finditer(r'return\s+([^;]+);', body):
                r = resolve(m.group(1), s, folder, f[0], depth + 1)
                if r:
                    res |= r
            return res or None
    return None
