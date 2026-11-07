# A general solver for the puzzles of a game: it proves a design before the game is built.
#
# A puzzle is any object with these methods (the state must be hashable: a tuple, or a State of this module):
#   start()          -> the state at the start
#   succ(state)      -> (cost, label, next state) for each move: cost 0 = a tap that takes no time, 1 = a step
#   is_goal(state)   -> True at the end of the puzzle
#   describe(state)  -> a short text (optional)
#
# The tools (each one works on any puzzle):
#   solve(p)                    the cheapest solution (a 0-1 search: taps cost 0, steps cost 1)
#   reachable(p)                all the states that the player can come to
#   dead_ends(p, states)        the states from which the goal cannot be reached (a player stuck for ever)
#   necessary(p, labels)        for each label: is the puzzle still solvable when that move is not allowed?
#   count_solutions(p, extra)   the number of different paths that are at most `extra` steps over the best one
#   report(p, labels)           all of the above as text
#
# WalkPuzzle is a ready base for the puzzles of the "walk between screens" kind: screens, paths with a test each,
# taps in a screen, a clock that each step moves (a cycle such as the seasons of a year) and the rules of each step.
#   python sdk/tools/puzzle/solver.py      the self test (a small puzzle with a clock)
from collections import deque


# ---------------------------------------------------------------- a state with names
class State(tuple):
    """an immutable state with named fields: S = State.make(pos=0, t=0, wind=0); S.wind; S.set(wind=2)"""
    __slots__ = ()
    _names = {}

    @classmethod
    def make(cls, **kw):
        names = tuple(sorted(kw))
        key = names
        cls._names.setdefault(key, {n: i for i, n in enumerate(names)})
        st = tuple.__new__(cls, (names,) + tuple(kw[n] for n in names))
        return st

    def __getattr__(self, name):
        idx = State._names[self[0]]
        if name not in idx:
            raise AttributeError(name)
        return self[1 + idx[name]]

    def set(self, **kw):
        names = self[0]
        idx = State._names[names]
        vals = list(self[1:])
        for n, v in kw.items():
            vals[idx[n]] = v
        return tuple.__new__(State, (names,) + tuple(vals))

    def fields(self):
        return dict(zip(self[0], self[1:]))


# ---------------------------------------------------------------- the search tools
def solve(p, start=None, allow=None):
    """the cheapest solution: (cost, [(label, state)]) or (None, []). allow(label) = False bans a move."""
    s0 = p.start() if start is None else start
    dist, prev = {s0: 0}, {s0: None}
    dq = deque([s0])
    while dq:
        st = dq.popleft()
        if p.is_goal(st):
            path = []
            while prev[st] is not None:
                ps, label = prev[st]
                path.append((label, st))
                st = ps
            return dist[path[0][1]] if path else 0, path[::-1]
        d = dist[st]
        for cost, label, s2 in p.succ(st):
            if allow is not None and not allow(label):
                continue
            c = d + cost
            if s2 not in dist or c < dist[s2]:
                dist[s2] = c
                prev[s2] = (st, label)
                (dq.appendleft if cost == 0 else dq.append)(s2)
    return None, []


def reachable(p, start=None, limit=2_000_000):
    s0 = p.start() if start is None else start
    seen, q = {s0}, deque([s0])
    while q:
        st = q.popleft()
        for _, _, s2 in p.succ(st):
            if s2 not in seen:
                seen.add(s2)
                if len(seen) > limit:
                    raise RuntimeError("more than %d states: make the state smaller" % limit)
                q.append(s2)
    return seen


def dead_ends(p, states=None):
    """the states that cannot come to the goal (a reverse search from the goal states)"""
    states = reachable(p) if states is None else states
    rev = {}
    for st in states:
        for _, _, s2 in p.succ(st):
            rev.setdefault(s2, []).append(st)
    good = {st for st in states if p.is_goal(st)}
    q = deque(good)
    while q:
        st = q.popleft()
        for ps in rev.get(st, ()):
            if ps not in good:
                good.add(ps)
                q.append(ps)
    return [st for st in states if st not in good]


def necessary(p, labels):
    """{label: (the cost of the best solution with no move of that label) or None (no solution: necessary)}"""
    return {lb: solve(p, allow=lambda l, lb=lb: l != lb)[0] for lb in labels}


def count_solutions(p, extra=0, cap=100000):
    """the number of different move sequences to the goal with a cost of at most best + extra (taps count as moves)"""
    best, _ = solve(p)
    if best is None:
        return 0
    limit = best + extra
    # (a depth-first walk with the distance to the goal as the bound: each path is counted one time)
    states = reachable(p)
    to_goal = _dist_to_goal(p, states)
    n = 0
    stack = [(p.start(), 0, 0)]
    while stack and n < cap:
        st, cost, taps = stack.pop()
        if p.is_goal(st):
            n += 1
            continue
        if taps > 16:                                   # (no endless taps in one place)
            continue
        for c, _, s2 in p.succ(st):
            g = to_goal.get(s2)
            if g is not None and cost + c + g <= limit:
                stack.append((s2, cost + c, taps + 1 if c == 0 else 0))
    return n


def _dist_to_goal(p, states):
    rev = {}
    for st in states:
        for c, _, s2 in p.succ(st):
            rev.setdefault(s2, []).append((st, c))
    dist = {st: 0 for st in states if p.is_goal(st)}
    dq = deque(dist)
    while dq:
        st = dq.popleft()
        for ps, c in rev.get(st, ()):
            d = dist[st] + c
            if ps not in dist or d < dist[ps]:
                dist[ps] = d
                (dq.appendleft if c == 0 else dq.append)(ps)
    return dist


def report(p, labels=(), show=None):
    """a text report: the best solution, the states, the dead ends, the necessity of each label"""
    show = show or getattr(p, "describe", None) or (lambda s: str(s))
    out = []
    cost, path = solve(p)
    out.append("BEST SOLUTION: %s steps, %d moves" % (cost, len(path)))
    out.append("      " + show(p.start()))
    n = 0
    for label, st in path:
        if not label.startswith("tap "):
            n += 1
        out.append("  %3d %-26s %s" % (n, label, show(st)))
    states = reachable(p)
    dead = dead_ends(p, states)
    out.append("states %d, dead ends %d" % (len(states), len(dead)))
    for st in dead[:8]:
        out.append("  DEAD END: " + show(st))
    if labels:
        for lb, c in necessary(p, labels).items():
            out.append("  without %-24s %s" % (lb, "NO SOLUTION (necessary)" if c is None else "solvable in %d steps" % c))
    return "\n".join(out)


# ---------------------------------------------------------------- a base for "walk between screens" puzzles
class WalkPuzzle:
    """Screens and paths, taps in a screen, a clock that each step moves.
    The state is a State with at least the fields pos (the screen) and t (the clock, 0 .. period - 1).
    Subclasses (or instances) fill in:
      period            the length of the clock cycle (the steps of a year); 0 = no clock
      paths             [(a, b, test(state) -> bool, both ways, the reason when closed)]
      taps(state)       -> [(label, next state)]: the actions in the screen of the state (no step)
      tick(old, new)    -> the next state after the rules of a step (old = before the step, new = moved)
      is_goal(state)
    A path test sees the state before the step (the season in which the player sets out)."""
    period = 0
    paths = []

    def neighbours(self, st):
        for a, b, test, both, _ in self.paths:
            for x, y in ((a, b), (b, a)) if both else ((a, b),):
                if x == st.pos and test(st):
                    yield y

    def tick(self, old, new):
        return new

    def taps(self, st):
        return []

    def succ(self, st):
        for label, s2 in self.taps(st):
            yield 0, "tap " + label, s2
        for y in self.neighbours(st):
            t2 = (st.t + 1) % self.period if self.period else st.t + 1
            yield 1, "go %s" % self.screen_name(y), self.tick(st, st.set(pos=y, t=t2))

    def screen_name(self, i):
        names = getattr(self, "screens", None)
        return names[i] if names else str(i)


# ---------------------------------------------------------------- the self test
class _Demo(WalkPuzzle):
    """a gate opens only in phase 3 of a clock of 4; a lever must be pulled first (only in phase 2: A is on even steps)"""
    period = 4
    screens = ["A", "B", "C"]
    paths = [(0, 1, lambda s: True, True, ""), (1, 2, lambda s: s.lever and s.t == 3, False, "the gate")]

    def start(self):
        return State.make(pos=0, t=0, lever=False)

    def taps(self, st):
        return [("pull", st.set(lever=True))] if st.pos == 0 and st.t == 2 and not st.lever else []

    def is_goal(self, st):
        return st.pos == 2

    def describe(self, st):
        return "%s t%d lever %d" % (self.screens[st.pos], st.t, st.lever)


if __name__ == "__main__":
    d = _Demo()
    cost, path = solve(d)
    assert cost is not None, "the demo must be solvable"
    labels = [lb for lb, _ in path]
    assert "tap pull" in labels
    assert necessary(d, ["tap pull"])["tap pull"] is None
    assert not dead_ends(d)
    print(report(d, ["tap pull"]))
    print("SOLVER OK")
