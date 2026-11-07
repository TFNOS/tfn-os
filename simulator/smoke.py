"""The smoke test of the TFN OS simulator: the real firmware, driven as a user drives the board.

  python simulator/smoke.py                 # all apps, about 2 s each
  python simulator/smoke.py --apps CHESS "SEA BATTLE" --seconds 4
  python simulator/smoke.py --no-build      # use the library that is there
  python simulator/smoke.py --headless      # no sound card (CI)

It starts the firmware with an empty data folder (simulator/.build/smoke-data), then:
  1. the menu opens after the start;
  2. a pull from the top edge (touch points of the chip, as a finger gives them) opens the settings panel of the
     system layer, and the BOOT key closes it;
  3. each app opens (as the web page opens an app), gets the tap of its self-test (main/apps.json test349), random
     touches, tilts, shakes, knocks and microphone levels; it must keep its frames coming and not stop the firmware;
     a short push of PWR must go back to the menu;
  4. the NVS file has the settings of the firmware, and PWR held for 2 s powers the board off;
  5. a fault in a task of the firmware stops the board (the window would show it), not the program.
The result: simulator/.build/smoke.json and one line for each app; exit code 0 when all is good.
"""
import argparse
import json
import random
import shutil
import sys
import time

import tfnsim as T


def wait_for(cond, timeout, step=0.02):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(step)
    return cond()


def fingers_view(board, pose, pts):
    board.touch([T.view_to_panel(pose, int(x), int(y)) for x, y in pts])


def tap(board, pose, x, y, ms=60):
    fingers_view(board, pose, [(x, y)])
    time.sleep(ms / 1000)
    board.touch([])
    time.sleep(0.08)


def drag(board, pose, x0, y0, x1, y1, ms=200, steps=12):
    for k in range(steps + 1):
        fingers_view(board, pose, [(x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps)])
        time.sleep(ms / 1000 / steps)
    board.touch([])
    time.sleep(0.08)


def push(board, key, ms=120):
    board.key(key, True)
    time.sleep(ms / 1000)
    board.key(key, False)
    # main.c acts on PWR when the key goes up, in pwr_key_poll() after the app switch of the frame: give it some frames,
    # else an app that the next step opens can get this push (and go back to the menu at once)
    time.sleep(0.08)


def alive(board):
    s, why = board.state()
    return s == T.RUN, why


def blank(board, pose):
    w, h, rgb = board.view(pose, 160)
    return len(set(rgb[i:i + 3] for i in range(0, len(rgb), 3 * 7))) <= 1


def run(dll, apps=None, seconds=2.0, seed=1, out=None, verbose=True, sound=False, headless=False):
    data = T.OUT / "smoke-data"
    shutil.rmtree(data, ignore_errors=True)
    board = T.Board(dll, data, headless=headless)
    board.sound(sound, 0.5)             # (the meter of the board works with the sound of the PC off)
    rnd = random.Random(seed)
    results, fails = [], []

    def say(text):
        if verbose:
            print(text, flush=True)

    def check(ok, what):
        if not ok:
            fails.append(what)
            say("  FAIL " + what)
        return ok

    # 1. the start
    check(wait_for(lambda: board.app() == "MENU" and board.frames() > 5, 5), "the menu did not open at the start")
    pose = board.orient()
    board.pose(pose)
    say(f"start: {board.app()} ({T.ORIENT_NAMES.get(pose)}), {len(board.apps)} apps")

    # 2. the settings panel: a pull from the top edge (the grip), then the BOOT key closes it
    time.sleep(0.4)
    vw, vh = T.view_size(pose)
    drag(board, pose, vw // 2, 2, vw // 2, vh * 0.8, ms=220)
    check(wait_for(board.panel_on, 1.5), "a pull from the top edge did not open the settings panel")
    time.sleep(0.4)
    push(board, T.KEY_BOOT)
    check(wait_for(lambda: not board.panel_on(), 1.5), "the BOOT key did not close the settings panel")
    say("settings panel: " + ("ok" if not fails else "FAIL"))

    # 3. each app
    names = apps or [n for n in board.apps if n != "MENU"]
    for name in names:
        i = board.apps.index(name) if name in board.apps else -1
        if i < 0:
            check(False, f"{name}: no such app in this build")
            continue
        r = {"app": name}
        t0 = time.monotonic()
        ok = board.open(name) and wait_for(lambda: board.app() == name, 2.0)
        if not check(ok, f"{name}: did not open (the app now: {board.app()})"):
            results.append(dict(r, ok=False))
            push(board, T.KEY_PWR)
            wait_for(lambda: board.app() == "MENU", 2)
            continue
        pose = board.orient()
        board.pose(pose)
        vw, vh = T.view_size(pose)
        time.sleep(0.35)
        x, y, _ = board.app_test(i)
        if x >= 0:
            tap(board, pose, min(x, vw - 1), min(y, vh - 1))
        f0, t1 = board.frames(), time.monotonic()
        end = t1 + seconds
        loud = 0
        while time.monotonic() < end and alive(board)[0]:
            loud = max(loud, board.sound_level())
            k = rnd.random()
            if k < 0.35:
                tap(board, pose, rnd.randrange(vw), rnd.randrange(12, vh))
            elif k < 0.55:
                drag(board, pose, rnd.randrange(vw), rnd.randrange(12, vh), rnd.randrange(vw), rnd.randrange(12, vh),
                     ms=rnd.randrange(80, 300), steps=6)
            elif k < 0.65:
                fingers_view(board, pose, [(rnd.randrange(vw), rnd.randrange(12, vh)), (rnd.randrange(vw), rnd.randrange(12, vh))])
                time.sleep(0.15)
                board.touch([])
            elif k < 0.75:
                board.motion(rnd.randrange(6))
            board.sensors(rnd.uniform(-0.6, 0.6), rnd.uniform(-0.6, 0.6), rnd.choice([0, 0, 0, 1.5]), rnd.uniform(-3, 3))
            board.mic(rnd.choice([0, 0.05, 0.6]), rnd.choice([0, 440]))
            time.sleep(0.03)
        board.touch([])
        board.sensors()
        board.mic(0)
        up, why = alive(board)
        frames, spent = board.frames() - f0, time.monotonic() - t1
        r.update(orient=T.ORIENT_NAMES.get(pose), frames=frames, fps=round(frames / max(spent, 0.1), 1), sound=loud)
        if not check(up, f"{name}: the firmware stopped: {why}"):
            results.append(dict(r, ok=False, error=why))
            break
        check(frames > seconds * 10, f"{name}: only {frames} frames in {seconds} s")
        r["blank"] = blank(board, pose)
        if out:
            board.screenshot(out / f"{name.replace(' ', '_').replace('/', '_')}.png", pose)
        # back to the menu: a short push of PWR (an app may have opened another app: PWR goes to the menu all the same)
        back = False
        for _ in range(3):
            push(board, T.KEY_PWR)
            if wait_for(lambda: board.app() == "MENU", 1.0):
                back = True
                break
        check(back, f"{name}: PWR did not go back to the menu (the app now: {board.app()})")
        r.update(ok=up and back, seconds=round(time.monotonic() - t0, 1))
        results.append(r)
        say(f"  {'ok  ' if r['ok'] else 'FAIL'} {name:16s} {r['orient']:18s} {r['fps']:5.1f} fps  sound {loud:3d}"
            f"{'  (blank screen)' if r['blank'] else ''}")

    # 4. the NVS of the board is in its file (settings_defaults.inc wrote its one-time keys), and PWR held 2 s powers
    #    the board off (main.c saves the settings first)
    nvs = data / "nvs.txt"
    check(nvs.exists() and "arcade\tdef_v\t" in nvs.read_text(), "the NVS file has no settings of the firmware")
    if alive(board)[0]:
        push(board, T.KEY_PWR, ms=2400)
        off = wait_for(lambda: board.state()[0] == T.OFF, 2.0)
        check(off, f"PWR held 2 s did not power the board off (state {board.state()})")
        say("power off: " + ("ok" if off else "FAIL"))

    # 5. a fault in a task of the firmware stops the board, not the program
    if board.state()[0] in (T.RUN, T.OFF):
        board.test_fault()
        stopped = wait_for(lambda: board.state()[0] == T.HALT, 3)
        why = board.state()[1]
        check(stopped and "access violation" in why, f"a fault did not stop the board as it must (state {board.state()})")
        say("fault check: " + ("ok, " + why if stopped else "FAIL"))

    log = board.log()
    errors = [ln for ln in log.splitlines() if ln.startswith("E (") and "fault" not in ln]
    with_sound = sum(1 for r in results if r.get("sound", 0) > 0)
    summary = {"apps": len(results), "ok": sum(1 for r in results if r.get("ok")), "with_sound": with_sound,
               "fails": fails, "results": results, "error_log_lines": errors[-50:]}
    (T.OUT / "smoke.json").write_text(json.dumps(summary, indent=1))
    say(f"{summary['ok']} of {len(results)} apps ok ({with_sound} made sound); {len(fails)} faults" +
        (":\n  " + "\n  ".join(fails) if fails else ""))
    return not fails


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--apps", nargs="*", help="only these apps (the names of the menu)")
    p.add_argument("--seconds", type=float, default=2.0, help="the time of the random input in each app (default 2)")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--shots", action="store_true", help="a picture of each app into simulator/.build/smoke-shots/")
    p.add_argument("--sound", action="store_true", help="play the sound on the PC (it is off in the test)")
    p.add_argument("--headless", action="store_true", help="no sound card (CI): the audio task keeps its pace with "
                   "the clock")
    p.add_argument("--no-build", action="store_true", help="do not build (use the DLL of the last build)")
    p.add_argument("--build-apps", default="all", help="the app set of the build (all, on, slim, or a JSON file)")
    a = p.parse_args()
    import build
    if a.no_build:
        dll = build.lib_path(a.build_apps)
    else:
        try:
            dll = build.build(a.build_apps)
        except build.BuildError as e:
            print(f"Build failed: {e}", file=sys.stderr)
            return 1
    out = None
    if a.shots:
        out = T.OUT / "smoke-shots"
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
    ok = run(dll, a.apps, a.seconds, a.seed, out, sound=a.sound, headless=a.headless)
    sys.stdout.flush()
    # (the firmware threads never end: leave at once)
    import os
    os._exit(0 if ok else 1)


if __name__ == "__main__":
    main()
