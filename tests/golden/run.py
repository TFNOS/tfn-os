"""The golden frames of TFN OS: the simulator opens a screen with a fixed script and compares the frame with a stored
PNG, pixel for pixel (no tolerance).

  python tests/golden/run.py                  # build the simulator, then check all screens and the tunes
  python tests/golden/run.py menu_home clock  # only these screens (and no tunes)
  python tests/golden/run.py --update         # write the frames of now as the new golden files
  python tests/golden/run.py --list           # the names of the screens
  python tests/golden/run.py --frames watcher am175 tab5 --no-tunes   # only the screens of these boards

Each screen runs in a process of its own, with an empty data folder, no sound card and the fixed clock of the
simulator (simulator/tfnsim.py, Board(fixed=...)): the firmware runs only in the steps of the script, the random
values start from one seed and the time of day is FIXED_EPOCH. So the same script gives the same pixels on each run
and on each PC. The scripts are in screens.json (the steps below); the golden files are in <frame>/<name>.png.

On a fault the runner writes out/<name>.new.png (the frame of now) and out/<name>.diff.png (the golden frame, dark,
with each pixel that changed in red), and the exit code is 1.

The steps of a script (the points are in the view of the app, as smoke.py gives them):
  ["wait", ms]                  the firmware runs ms
  ["open", "NAME"]              open an app (as the web page does), then the board turns to the orientation of the app
  ["tap", x, y]                 a finger down for 60 ms, then up
  ["hold", x, y, ms]            a finger down for ms, then up
  ["drag", x0, y0, x1, y1, ms]  a finger moves from the first point to the second in ms
  ["key", "PWR" | "BOOT", ms]   a push of a key
  ["tilt", tx, ty]              the tilt of the board (-1..1), as the sliders of the window
The keys of a screen: "name", "steps", "lang" ("en" or "he": SETUP > APP LANGUAGE), "nvs" (more NVS keys of the
namespace arcade, u32 values), "frame" (the board of simulator/build.py --frame: "lcd349", the default; "cores3", the 320
x 240 frame; "watcher" and "am175", the round boards; "tab5", the tablet UI). The points of a script are points of the
view (as the user sees the panel): a round board has a square view (the Watcher 412 x 412, the AMOLED-1.75C 466 x 466),
the Tab5 a view of 720 x 1280 (1280 x 720 in landscape).

The tunes (tunes.json): each start tune of tunes.json plays with the fixed clock; the recorder of the simulator keeps
the output of the mixer, and its SHA-256, peak and RMS must be the same as in the file.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "simulator"))
import build as B          # noqa: E402
import tfnsim as T         # noqa: E402

OUT = HERE / "out"
SEED = 20261103
TAP_MS = 60
FILES_HOST = "/files"
REAL_LIMIT_S = 120          # a step that takes longer on the PC: a task does not stop


def load_screens():
    return json.loads((HERE / "screens.json").read_text(encoding="utf-8"))["screens"]


def load_tunes():
    return json.loads((HERE / "tunes.json").read_text(encoding="utf-8"))


def tune_id(name):
    """The value of a name of the enum tune_t (main/tfn/tunes.h)."""
    text = (ROOT / "main" / "tfn" / "tunes.h").read_text(encoding="utf-8")
    body = text[text.index("typedef enum {") + 14:text.index("} tune_t;")]
    body = re.sub(r"//.*", "", body)
    names = [n.split("=")[0].strip() for n in body.split(",")]
    return [n for n in names if n].index(name)


def frame_of(screen):
    return screen.get("frame", "lcd349")


def screen_id(screen):
    """The name of a screen in the runner: "clock_en" (the 3.49), "cores3/clock_en" (another frame)."""
    frame = frame_of(screen)
    return screen["name"] if frame == "lcd349" else frame + "/" + screen["name"]


def file_id(screen):
    return screen_id(screen).replace("/", "_")


def golden_path(screen):
    return HERE / frame_of(screen) / (screen["name"] + ".png")


# ---------------------------------------------------------------- one screen (in its own process)
def preset_nvs(data, screen):
    """The NVS keys of the script, before the start (simulator/src/sim_nvs.c: namespace, key, type, value)."""
    keys = dict(screen.get("nvs", {}))
    if "lang" in screen:
        keys["lang_all"] = 1 if screen["lang"] == "he" else 0
    lines = ["arcade\t%s\t04\t%d\n" % (k, int(v)) for k, v in sorted(keys.items())]
    (data / "nvs.txt").write_text("".join(lines), encoding="utf-8")


def follow(board):
    """The board turns to the orientation of the app (as smoke.py does: no turn of the screen by the motion sensor)."""
    pose = board.orient()
    board.pose(pose)
    return pose


def finger(board, pose, x, y):
    board.touch([T.view_to_panel(pose, int(round(x)), int(round(y)), board.lcd)])


def step_move(board, pose, x0, y0, x1, y1, ms):
    n = max(1, int(ms) // 10)
    for k in range(n + 1):
        finger(board, pose, x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n)
        board.run(ms / n if k < n else TAP_MS, REAL_LIMIT_S)
    board.touch([])


def do_step(board, pose, step):
    """One step of a script; the result is the orientation after it."""
    kind, args = step[0], step[1:]
    if kind == "wait":
        board.run(args[0], REAL_LIMIT_S)
    elif kind == "open":
        if not board.open(args[0]):
            raise RuntimeError(f"no app {args[0]} in this build")
        board.run(200, REAL_LIMIT_S)
        pose = follow(board)
    elif kind in ("tap", "hold"):
        finger(board, pose, args[0], args[1])
        board.run(args[2] if kind == "hold" else TAP_MS, REAL_LIMIT_S)
        board.touch([])
    elif kind == "drag":
        step_move(board, pose, *args)
    elif kind == "key":
        key = {"PWR": T.KEY_PWR, "BOOT": T.KEY_BOOT}[args[0]]
        board.key(key, True)
        board.run(args[1], REAL_LIMIT_S)
        board.key(key, False)
    elif kind == "tilt":
        board.sensors(args[0], args[1])
    else:
        raise ValueError(f"an unknown step {step}")
    return pose


def make_data(name, screen):
    """An empty data folder of the board. FILES (main/app_files.c, a build with no ESP-IDF) shows the folders of
    FILES_HOST: /files is in the data folder (simulator/src/sim_fs.c), with an empty card and empty board files."""
    data = OUT / "data" / name
    shutil.rmtree(data, ignore_errors=True)
    for sub in ("files/card", "files/data"):
        (data / sub).mkdir(parents=True)
    os.environ["FILES_HOST"] = FILES_HOST
    if screen:
        preset_nvs(data, screen)
    return data


def run_screen(lib, screen, out_png):
    data = make_data(file_id(screen), screen)
    board = T.Board(lib, data, headless=True, fixed=SEED)
    board.run(1500, REAL_LIMIT_S)               # the start: the menu
    pose = follow(board)
    for step in screen["steps"]:
        pose = do_step(board, pose, step)
    state, why = board.state()
    if state != T.RUN:
        raise RuntimeError(f"the board stopped: {why}")
    w, h, rgb = board.shot(pose)
    T.write_png(out_png, rgb, w, h)


def run_tune(lib, tune, out_json):
    """A start tune with the recorder on: the SHA-256, the peak and the RMS of the output of the mixer."""
    data = make_data("tune_" + tune["name"], None)
    board = T.Board(lib, data, headless=True, fixed=SEED)
    board.run(1500, REAL_LIMIT_S)
    prefix = OUT / ("tune_" + tune["name"])
    board.rec(prefix)
    ms = board.lib.tfnsim_tune(tune_id(tune["name"]))
    board.run(ms + 300, REAL_LIMIT_S)
    board.rec(None)
    pcm = Path(str(prefix) + ".pcm").read_bytes()
    samples = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    peak = max((abs(v) for v in samples), default=0)
    rms = math.sqrt(sum(v * v for v in samples) / len(samples)) if samples else 0.0
    result = {"samples": len(samples), "sha256": hashlib.sha256(pcm).hexdigest(), "peak": peak, "rms": round(rms, 3)}
    Path(out_json).write_text(json.dumps(result), encoding="utf-8")


def child(a):
    lib = Path(a.lib)
    try:
        if a.tune:
            tune = next(t for t in load_tunes() if t["name"] == a.tune)
            run_tune(lib, tune, a.out)
        else:
            screen = next(s for s in load_screens() if screen_id(s) == a.one)
            run_screen(lib, screen, a.out)
        code = 0
    except Exception as e:                      # (the parent prints the reason)
        print(f"FAULT: {e}", flush=True)
        code = 1
    sys.stdout.flush()
    os._exit(code)                              # (the firmware threads never end)


# ---------------------------------------------------------------- the parent: the check of all screens
def spawn(lib, args, timeout_s):
    cmd = [sys.executable, str(Path(__file__).resolve()), "--lib", str(lib)] + args
    try:
        env = dict(os.environ, FILES_HOST=FILES_HOST)
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout_s, env=env)
    except subprocess.TimeoutExpired:
        return False, f"no result in {timeout_s} s"
    faults = [ln for ln in r.stdout.splitlines() if ln.startswith(("FAULT", "E ("))]
    return r.returncode == 0, "\n".join(faults[-8:])


def diff_picture(gold, new, path):
    """The golden frame, dark, with each pixel that changed in red; the result is the count of the changed pixels."""
    w, h, a = gold
    b = new[2]
    out = bytearray(len(a))
    changed = 0
    for i in range(0, len(a), 3):
        if a[i:i + 3] != b[i:i + 3]:
            out[i:i + 3] = b"\xff\x00\x00"
            changed += 1
        else:
            out[i:i + 3] = bytes(v // 3 for v in a[i:i + 3])
    T.write_png(path, bytes(out), w, h)
    return changed


def check_screen(lib, screen, update):
    """The result: (ok, a line of text)."""
    name = file_id(screen)
    new_png = OUT / (name + ".new.png")
    ok, why = spawn(lib, ["--one", screen_id(screen), "--out", str(new_png)], REAL_LIMIT_S * 4)
    if not ok:
        return False, f"FAIL {name}: the script did not end\n{why}"
    gold_png = golden_path(screen)
    if update:
        gold_png.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(new_png, gold_png)
        return True, f"new  {name}"
    if not gold_png.exists():
        return False, f"FAIL {name}: no golden file {gold_png.relative_to(ROOT)} (run with --update)"
    gold, new = T.read_png(gold_png), T.read_png(new_png)
    if gold == new:
        return True, f"ok   {name}"
    if gold[:2] != new[:2]:
        return False, f"FAIL {name}: the size changed: {gold[:2]} -> {new[:2]}"
    n = diff_picture(gold, new, OUT / (name + ".diff.png"))
    return False, f"FAIL {name}: {n} pixels changed (out/{name}.new.png, out/{name}.diff.png)"


def check_tunes(lib, update, ex):
    path = HERE / "tunes.json"
    tunes = load_tunes()
    lines, ok_all = [], True

    def one(t):
        res = OUT / ("tune_" + t["name"] + ".json")
        return res, spawn(lib, ["--tune", t["name"], "--out", str(res)], REAL_LIMIT_S * 2)
    for t, (res, (ok, why)) in zip(tunes, ex.map(one, tunes)):
        if not ok:
            lines.append(f"FAIL tune {t['name']}: the recording did not end\n{why}")
            ok_all = False
            continue
        got = json.loads(res.read_text(encoding="utf-8"))
        if update:
            t["expect"] = got
            lines.append(f"new  tune {t['name']}: peak {got['peak']}, RMS {got['rms']}")
        elif t.get("expect") == got:
            lines.append(f"ok   tune {t['name']}")
        else:
            lines.append(f"FAIL tune {t['name']}: {t.get('expect')} -> {got}")
            ok_all = False
    if update:
        path.write_text(json.dumps(tunes, indent=1) + "\n", encoding="utf-8")
    return ok_all, lines


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("names", nargs="*", help="only these screens (the default: all screens and the tunes)")
    p.add_argument("--update", action="store_true", help="write the frames of now as the golden files")
    p.add_argument("--list", action="store_true", help="the names of the screens")
    p.add_argument("--no-build", action="store_true", help="use the simulator library of the last build")
    p.add_argument("--no-tunes", action="store_true", help="only the screens")
    p.add_argument("--frames", nargs="+", choices=sorted(B.FRAMES), help="only the screens of these boards")
    p.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2, help="screens at the same time")
    p.add_argument("--lib", help=argparse.SUPPRESS)
    p.add_argument("--one", help=argparse.SUPPRESS)
    p.add_argument("--tune", help=argparse.SUPPRESS)
    p.add_argument("--out", help=argparse.SUPPRESS)
    a = p.parse_args()
    if a.one or a.tune:
        child(a)
    screens = load_screens()
    if a.list:
        print("\n".join(screen_id(s) for s in screens))
        return 0
    if a.names:
        unknown = set(a.names) - {screen_id(s) for s in screens}
        if unknown:
            print("No such screens: " + ", ".join(sorted(unknown)), file=sys.stderr)
            return 2
        screens = [s for s in screens if screen_id(s) in a.names]
    if a.frames:
        screens = [s for s in screens if frame_of(s) in a.frames]
    tunes = not a.names and not a.no_tunes
    libs = {}
    try:
        for frame in sorted({frame_of(s) for s in screens} | ({"lcd349"} if tunes else set())):
            libs[frame] = B.lib_path(frame=frame) if a.no_build else B.build(frame=frame)
    except B.BuildError as e:
        print(f"Build failed: {e}", file=sys.stderr)
        return 1
    OUT.mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        results = list(ex.map(lambda s: check_screen(libs[frame_of(s)], s, a.update), screens))
        tunes_ok, lines = check_tunes(libs["lcd349"], a.update, ex) if tunes else (True, [])
    for _, line in results:
        print(line)
    print("\n".join(lines))
    ok = all(r[0] for r in results) and tunes_ok
    fails = sum(1 for r in results if not r[0])
    print(f"{len(results) - fails} of {len(results)} screens the same" + ("" if ok else "; FAULTS above"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
