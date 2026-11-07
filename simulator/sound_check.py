"""The check of the sound memory of TFN OS in the simulator: no sound reads the memory of an app after its free.

  python simulator/sound_check.py              # build (only the changed files), then check
  python simulator/sound_check.py --no-build   # use the library that is there

The firmware runs headless with an empty data folder (simulator/.build/sound-data). The steps:
  1. the checks of main/audio.c in a task of the firmware (src/sim_check.c): audio_stop_mem stops a sample voice (in
     the queue and while it plays), a tune, TIA steps, a capture and a record; a drop from the queue keeps the order of
     the other messages and their count; audio_capture_stop, audio_set_tick(NULL) and audio_mic_tap(NULL) are barriers;
  2. ECHO KEYLLER leaves while a key loops its sound, with the audio task late 0, 45, 120 and 400 ms after each block
     (as a flash write on the board or a busy PC makes it). The big blocks of the app are in pages of their own, and a
     free makes their pages unusable (src/sim_app_mem.c), so a read after the free stops the firmware at once. With
     the free hook (the loader of the board, ABI rev 24 and later): no stop; up to 120 ms late no sound uses a block
     at its free (the release of the app, libs/sndrel, ends the voice first); at 400 ms the time limit of sndrel_wait
     (300 ms with no block) ends the wait first, and the free hook stops the voice;
  3. the same with no free hook (an older firmware) at 0, 45 and 120 ms: the release of the app alone must be enough;
  4. the proof that the check sees the fault: no free hook and 400 ms late. The voice reads the freed samples, and
     the firmware must stop with an access violation.
The last line: "SOUND CHECK: OK (...)" or "SOUND CHECK: FAIL (...)"; the exit code is 0 or 1.
"""
import argparse
import os
import shutil
import sys
import time

import tfnsim as T

APP = "ECHO KEYLLER"
KEY1 = (269, 46)    # the middle of key 1 of the 3.49 art (apps/echo_keyller/app_echokey.c, EK_KEYS[0])
GUARD = 16384       # the blocks of the app in pages of their own: this size and more (the samples, the art)
LATE = (0, 45, 120, 400)
LIMIT_MS = 300      # SNDREL_LIMIT_MS of libs/sndrel/sndrel.h


def wait_for(cond, timeout, step=0.02):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(step)
    return cond()


def leave_round(b, late_ms, hook):
    """Open the app, hold key 1 (its sound loops), push PWR while the sound plays. The result: (None, why) when the
    round did not run, else (the frees that found a sound on the block, the state, why)."""
    b.app_mem(hook, GUARD)
    b.audio_late(late_ms)
    if not (b.open(APP) and wait_for(lambda: b.app() == APP, 5)):
        return None, "the app did not open (now %s)" % b.app()
    pose = b.orient()
    b.pose(pose)
    time.sleep(0.6)
    found0 = b.app_mem_found()
    b.touch([T.view_to_panel(pose, *KEY1)])
    time.sleep(max(0.35, 2.5 * late_ms / 1000 + 0.1))   # (the tick of the app starts the voice: two blocks at most)
    if b.voices_on() < 1:
        b.touch([])
        return None, "no voice plays after the key"
    b.key(T.KEY_PWR, True)
    time.sleep(0.12)
    b.key(T.KEY_PWR, False)
    wait_for(lambda: b.app() == "MENU" or b.state()[0] != T.RUN, 5)
    time.sleep(0.3 + 2.5 * late_ms / 1000)               # (the blocks after the free)
    b.touch([])
    time.sleep(0.2)
    s, why = b.state()
    return b.app_mem_found() - found0, s, why


def run(dll, rounds=2, verbose=False):
    data = T.OUT / "sound-data"
    shutil.rmtree(data, ignore_errors=True)
    b = T.Board(dll, data, headless=True)
    fails, notes = [], []

    def fail(what):
        fails.append(what)
        print("  FAIL " + what, flush=True)

    if not wait_for(lambda: b.app() == "MENU" and b.frames() > 5, 8):
        fail("the menu did not open at the start")
        return fails, notes

    # 1. the checks of main/audio.c
    n, text = b.check_audio()
    lines = [ln for ln in text.splitlines() if ln.strip()]
    print("the checks of main/audio.c (src/sim_check.c):", flush=True)
    for ln in lines:
        print("  " + ln, flush=True)
    if n != 0:
        fail("the checks of main/audio.c: %s" % ("no result" if n < 0 else "%d failed" % n))
    notes.append("audio.c %d of %d" % (sum(1 for ln in lines if ln.startswith("ok")), len(lines)))

    # 2. and 3. ECHO KEYLLER leaves while its sound plays
    done = 0
    for hook in (True, False):
        for late in LATE:
            if late > LIMIT_MS and not hook:
                continue
            for r in range(1, rounds + 1):
                res = leave_round(b, late, hook)
                where = "%s, %d ms late, free hook %s, round %d" % (APP, late, "on" if hook else "off", r)
                if res[0] is None:
                    fail("%s: %s" % (where, res[1]))
                    continue
                found, s, why = res
                print("%s: %d free(s) found a sound; firmware %s %s" % (where, found, "RUN" if s == T.RUN else "HALT",
                                                                         why if s != T.RUN else ""), flush=True)
                if s != T.RUN:
                    fail("%s: the firmware stopped: %s" % (where, why))
                    return fails, notes
                if late <= LIMIT_MS - 100 and found:
                    fail("%s: a sound still used a block at its free (the release of the app did not end it)" % where)
                if late > LIMIT_MS and found < 1:
                    fail("%s: no free found the voice (the round did not test the free hook)" % where)
                done += 1
    if b.app_mem_bad():
        fail("audio_mem_check did not stop each sound of a block (%d times)" % b.app_mem_bad())
    notes.append("%d leaves of %s" % (done, APP))

    # 4. the proof that the check sees a read after the free
    res = leave_round(b, 400, False)
    if res[0] is None:
        fail("the proof round: %s" % res[1])
    else:
        found, s, why = res
        print("the proof round (no free hook, 400 ms late): %d free(s) found a sound; firmware %s %s" %
              (found, "RUN" if s == T.RUN else "HALT", why), flush=True)
        if s == T.RUN or "access violation" not in why:
            fail("the proof round: the firmware did not stop with an access violation (the check does not see a read "
                 "after the free)")
        else:
            notes.append("the fault seen with no free hook")
    if verbose:
        for ln in b.log().splitlines():
            if "simmem" in ln or "fault" in ln or "echokey" in ln:
                print("  " + ln)
    return fails, notes


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--no-build", action="store_true", help="do not build (use the library of the last build)")
    p.add_argument("--rounds", type=int, default=2, help="the leaves of the app for each case (default 2)")
    p.add_argument("-v", "--verbose", action="store_true", help="also the lines of the log of the memory hook")
    a = p.parse_args()
    import build
    if a.no_build:
        dll = build.lib_path("all")
    else:
        try:
            dll = build.build("all")
        except build.BuildError as e:
            print(f"Build failed: {e}", file=sys.stderr)
            return 1
    fails, notes = run(dll, a.rounds, a.verbose)
    if fails:
        print("SOUND CHECK: FAIL (%s)" % "; ".join(fails), flush=True)
    else:
        print("SOUND CHECK: OK (%s)" % "; ".join(notes), flush=True)
    sys.stdout.flush()
    os._exit(1 if fails else 0)   # (the firmware threads never end: leave at once)


if __name__ == "__main__":
    main()
