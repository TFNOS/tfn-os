"""The interface of the tfnsim library (the TFN OS simulator: tfnsim.dll, libtfnsim.so, libtfnsim.dylib) for Python:
the board, its geometry and PNG pictures.

The library runs the firmware in threads of its own. With the real clock (the window) it runs on the clock of the PC.
With the fixed clock (Board(fixed=...): the tests) it runs only in Board.run(ms), and the same input gives the same
frames on each run and on each PC. This module loads a copy of the library (so that build.py can make a new one while
a simulator runs), starts the firmware and gives the calls of simulator/src/sim_api.c. Python standard library only.
"""
import ctypes as C
import os
from pathlib import Path
import shutil
import struct
import time
import zlib

SIM = Path(__file__).resolve().parent
OUT = SIM / ".build"
DATA = SIM / ".data"

LCD_W, LCD_H = 172, 640             # the physical frame of the 3.49 (portrait)
END_PX = 120                        # the end buttons of the two-player games: this far from each end (input.h)
PORTRAIT, PORTRAIT_FLIP, LAND_A, LAND_B = 0, 1, 2, 3        # orient_t of main/gfx.h
ORIENT_NAMES = {PORTRAIT: "portrait", PORTRAIT_FLIP: "portrait (turned)", LAND_A: "landscape",
                LAND_B: "landscape (turned)"}
CLOCKWISE = {PORTRAIT: LAND_B, LAND_B: PORTRAIT_FLIP, PORTRAIT_FLIP: LAND_A, LAND_A: PORTRAIT}
RUN, OFF, RESTART, HALT = 0, 1, 2, 3                        # tfnsim_state()
KEY_PWR, KEY_BOOT = 0, 1
MOTION_KNOCK, MOTION_UP, MOTION_DOWN, MOTION_LEFT, MOTION_RIGHT, MOTION_STEP = range(6)


def is_landscape(pose):
    return pose in (LAND_A, LAND_B)


def view_size(pose, lcd=(LCD_W, LCD_H)):
    """The size of the panel as the user sees it in this position (panel px). lcd: the physical frame (Board.lcd)."""
    w, h = lcd
    return (h, w) if is_landscape(pose) else (w, h)


def view_to_panel(pose, vx, vy, lcd=(LCD_W, LCD_H)):
    """A point of the view (as the user sees the screen; with pose = the orientation of the app this is also the
    logical point of the app) to the physical point of the touch chip (gfx_touch_to_logical() in reverse)."""
    w, h = lcd
    if pose == PORTRAIT:
        return vx, vy
    if pose == PORTRAIT_FLIP:
        return w - 1 - vx, h - 1 - vy
    if pose == LAND_A:
        return w - 1 - vy, vx
    return vy, h - 1 - vx


def panel_to_view(pose, px, py, lcd=(LCD_W, LCD_H)):
    w, h = lcd
    if pose == PORTRAIT:
        return px, py
    if pose == PORTRAIT_FLIP:
        return w - 1 - px, h - 1 - py
    if pose == LAND_A:
        return py, w - 1 - px
    return h - 1 - py, px


def write_png(path, rgb, w, h):
    """An RGB picture (bytes, 3 per pixel) to a PNG file (no packages)."""
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def read_png(path):
    """A PNG file (8-bit RGB or RGBA, no interlace) to (w, h, RGB bytes)."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, lace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or lace:
                raise ValueError(f"{path}: only 8-bit RGB or RGBA PNG files")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    rows = _unfilter(zlib.decompress(idat), w, h, bpp)
    if bpp == 4:
        rows = b"".join(rows[i:i + 3] for i in range(0, len(rows), 4))
    return w, h, rows


def _unfilter(raw, w, h, bpp):
    """The 5 row filters of PNG, undone."""
    stride = w * bpp
    out = bytearray(stride * h)
    prev = bytearray(stride)
    for y in range(h):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return bytes(out)


def _load_copy(dll):
    """Load a copy of the library and its symbols (run/<pid>/: the DLL names "tfnsim.pdb" next to itself, for the stack
    of a fault); remove the old copies (more than an hour old: a copy that another simulator only starts to load must
    stay; on Windows the copy of a running simulator is locked and stays too)."""
    dll = Path(dll)
    run = OUT / "run"
    run.mkdir(parents=True, exist_ok=True)
    for old in run.iterdir():
        try:
            if time.time() - old.stat().st_mtime > 3600:
                shutil.rmtree(old, ignore_errors=True)
        except OSError:
            continue
    here = run / str(os.getpid())
    here.mkdir(exist_ok=True)
    shutil.copy2(dll, here / dll.name)
    if dll.with_suffix(".pdb").exists():
        shutil.copy2(dll.with_suffix(".pdb"), here / "tfnsim.pdb")
    return C.CDLL(str(here / dll.name))


HEADLESS, FIXED = 1, 2                                      # tfnsim_options()
FIXED_EPOCH = 1781686800                                    # the fixed time of day: 17 June 2026, 09:00:00 UTC


class Board:
    """The simulated Waveshare 3.49 with TFN OS (one for each process: the firmware cannot start two times).

    headless: no sound card. fixed: the fixed clock (the firmware runs only in run(); the random values start from the
    seed fixed, the time of day from epoch)."""

    def __init__(self, dll, data, fps=60, headless=False, fixed=None, epoch=FIXED_EPOCH):
        self.lib = L = _load_copy(dll)
        if L.tfnsim_abi() != 3:
            raise RuntimeError("the tfnsim library has another interface: build it again (python simulator/build.py)")
        sig = {
            "tfnsim_abi": (C.c_int, []), "tfnsim_start": (C.c_int, [C.c_char_p, C.c_int]),
            "tfnsim_state": (C.c_int, [C.c_char_p, C.c_int]), "tfnsim_frames": (C.c_uint, []),
            "tfnsim_view": (C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]),
            "tfnsim_backlight": (C.c_int, []),
            "tfnsim_touch": (None, [C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]),
            "tfnsim_key": (None, [C.c_int, C.c_int]), "tfnsim_pose": (None, [C.c_int]),
            "tfnsim_tilt": (None, [C.c_float, C.c_float]), "tfnsim_shake": (None, [C.c_float]),
            "tfnsim_dial": (None, [C.c_float]), "tfnsim_motion": (None, [C.c_int]),
            "tfnsim_mic": (None, [C.c_float, C.c_float]), "tfnsim_sound": (None, [C.c_int, C.c_float]),
            "tfnsim_sound_level": (C.c_int, []), "tfnsim_battery": (None, [C.c_int, C.c_int]),
            "tfnsim_sd": (None, [C.c_int]), "tfnsim_fps": (None, [C.c_int]),
            "tfnsim_app": (C.c_char_p, []), "tfnsim_orient": (C.c_int, []),
            "tfnsim_app_count": (C.c_int, []), "tfnsim_app_name": (C.c_char_p, [C.c_int]),
            "tfnsim_app_test": (C.c_int, [C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]),
            "tfnsim_app_landscape": (C.c_int, [C.c_int]), "tfnsim_open": (C.c_int, [C.c_char_p]),
            "tfnsim_log": (C.c_int, [C.c_char_p, C.c_int]), "tfnsim_panel_on": (C.c_int, []),
            "tfnsim_test_fault": (None, []),
            "tfnsim_rec": (C.c_int, [C.c_char_p]), "tfnsim_rec_samples": (C.c_longlong, []),
            "tfnsim_peak_take": (C.c_int, []), "tfnsim_limiter_take": (C.c_int, []), "tfnsim_over_take": (C.c_int, []),
            "tfnsim_music_off": (None, [C.c_int]),
            "tfnsim_options": (None, [C.c_int, C.c_uint, C.c_longlong]), "tfnsim_run": (C.c_int, [C.c_int, C.c_int]),
            "tfnsim_now_ms": (C.c_longlong, []), "tfnsim_lcd": (None, [C.POINTER(C.c_int), C.POINTER(C.c_int)]),
            "tfnsim_shot": (C.c_int, [C.c_void_p, C.c_int, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]),
            "tfnsim_audio_late": (None, [C.c_int]), "tfnsim_voices_on": (C.c_int, []),
            "tfnsim_check_audio": (C.c_int, [C.c_char_p, C.c_int]),
            "tfnsim_app_mem": (None, [C.c_int, C.c_int]), "tfnsim_app_mem_found": (C.c_int, []),
            "tfnsim_app_mem_bad": (C.c_int, []),
        }
        for name, (res, args) in sig.items():
            f = getattr(L, name)
            f.restype, f.argtypes = res, args
        self.fixed = fixed is not None
        lw, lh = C.c_int(), C.c_int()
        L.tfnsim_lcd(C.byref(lw), C.byref(lh))
        self.lcd = (lw.value, lh.value)         # the physical frame of the simulated board (portrait)
        L.tfnsim_options((HEADLESS if headless else 0) | (FIXED if self.fixed else 0), fixed or 0, epoch)
        self.data = Path(data)
        self.data.mkdir(parents=True, exist_ok=True)
        self._buf = (C.c_ubyte * (8 << 20))()       # (the picture: at most 2560 px on its long side)
        self._logbuf = C.create_string_buffer(1 << 16)
        self._why = C.create_string_buffer(256)
        self.apps = [L.tfnsim_app_name(i).decode("utf-8", "replace") for i in range(L.tfnsim_app_count())]
        if L.tfnsim_start(str(self.data).encode("utf-8"), fps) != 1:
            raise RuntimeError("the firmware did not start")

    # ---- the state
    def state(self):
        s = self.lib.tfnsim_state(self._why, len(self._why))
        return s, self._why.value.decode("utf-8", "replace")

    def frames(self):
        return self.lib.tfnsim_frames()

    def app(self):
        return self.lib.tfnsim_app().decode("utf-8", "replace")

    def orient(self):
        return self.lib.tfnsim_orient()

    def backlight(self):
        return self.lib.tfnsim_backlight()

    def panel_on(self):
        return bool(self.lib.tfnsim_panel_on())

    def log(self):
        n = self.lib.tfnsim_log(self._logbuf, len(self._logbuf))
        return self._logbuf.raw[:n].decode("utf-8", "replace") if n else ""

    def app_test(self, i):
        """(x, y, ms): the tap point of the self-test of app i (logical, main/apps.json test349); x < 0 = none."""
        x, y = C.c_int(), C.c_int()
        ms = self.lib.tfnsim_app_test(i, C.byref(x), C.byref(y))
        return x.value, y.value, ms

    def view(self, pose, width):
        """The picture of the panel in a position, width px wide: (w, h, RGB bytes)."""
        vw, vh = view_size(pose, self.lcd)
        width = max(vw // 4, min(int(width), 2560, 2560 * vw // vh))
        w, h = C.c_int(), C.c_int()
        n = self.lib.tfnsim_view(self._buf, len(self._buf), pose, int(width), C.byref(w), C.byref(h))
        return w.value, h.value, C.string_at(self._buf, n)

    def shot(self, pose=None):
        """The picture of the panel for a test: each pixel once, the colours of the frame with no backlight:
        (w, h, RGB bytes)."""
        pose = self.orient() if pose is None else pose
        w, h = C.c_int(), C.c_int()
        n = self.lib.tfnsim_shot(self._buf, len(self._buf), pose, C.byref(w), C.byref(h))
        return w.value, h.value, C.string_at(self._buf, n)

    # ---- the fixed clock
    def run(self, ms, real_limit_s=60):
        """The firmware runs ms of the clock of the board (with the real clock: a real sleep). False: a task did not
        stop in real_limit_s of the PC (the log tells which)."""
        return bool(self.lib.tfnsim_run(int(ms), int(real_limit_s * 1000)))

    def now_ms(self):
        return self.lib.tfnsim_now_ms()

    def screenshot(self, path, pose=None):
        pose = self.orient() if pose is None else pose
        w, h, rgb = self.view(pose, view_size(pose, self.lcd)[0])
        write_png(path, rgb, w, h)
        return path

    # ---- the inputs
    def touch(self, points):
        """The fingers now: a list of physical points (the 3.49: x 0..171, y 0..639); [] = no finger."""
        n = len(points)
        xs = (C.c_int * max(n, 1))(*[int(p[0]) for p in points])
        ys = (C.c_int * max(n, 1))(*[int(p[1]) for p in points])
        self.lib.tfnsim_touch(n, xs, ys)

    def key(self, key, down):
        self.lib.tfnsim_key(key, 1 if down else 0)

    def pose(self, orient):
        self.lib.tfnsim_pose(orient)

    def sensors(self, tilt_x=0.0, tilt_y=0.0, shake=0.0, dial=0.0):
        L = self.lib
        L.tfnsim_tilt(tilt_x, tilt_y)
        L.tfnsim_shake(shake)
        L.tfnsim_dial(dial)

    def motion(self, kind):
        self.lib.tfnsim_motion(kind)

    def mic(self, level, tone_hz=0.0):
        self.lib.tfnsim_mic(level, tone_hz)

    def sound(self, on, volume):
        self.lib.tfnsim_sound(1 if on else 0, volume)

    def sound_level(self):
        return self.lib.tfnsim_sound_level()

    def battery(self, pct, usb):
        self.lib.tfnsim_battery(int(pct), 1 if usb else 0)

    def sd(self, present):
        self.lib.tfnsim_sd(1 if present else 0)

    def open(self, name):
        return bool(self.lib.tfnsim_open(name.encode("utf-8")))

    def test_fault(self):
        self.lib.tfnsim_test_fault()

    # ---- the recorder: <prefix>.vid (the frames) and <prefix>.pcm (the sound), one clock
    def rec(self, prefix):
        """Start a recording (prefix: the path of the two files without the extension), or stop it (None).
        The result of a stop: the count of the frames."""
        return self.lib.tfnsim_rec(str(prefix).encode("utf-8") if prefix else None)

    def rec_samples(self):
        return self.lib.tfnsim_rec_samples()

    # ---- the meters of the mix (percent): the peak before the VOLUME (over 100 = the limiter works), the lowest gain
    # of the limiter, the loudest note of MIDI_BG (over 100 = a note was cut). Each call starts a new measure.
    def peak_take(self):
        return self.lib.tfnsim_peak_take()

    def limiter_take(self):
        return self.lib.tfnsim_limiter_take()

    def over_take(self):
        return self.lib.tfnsim_over_take()

    # ---- the checks of the sound memory (simulator/sound_check.py)
    def audio_late(self, ms):
        """The audio task is late by ms after each block (0 = the normal pace)."""
        self.lib.tfnsim_audio_late(int(ms))

    def voices_on(self):
        """The voices of the mixer that play now."""
        return self.lib.tfnsim_voices_on()

    def check_audio(self):
        """The checks of the sound memory of main/audio.c (src/sim_check.c): (failed checks or -1, the lines)."""
        out = C.create_string_buffer(8192)
        n = self.lib.tfnsim_check_audio(out, len(out))
        return n, out.value.decode("utf-8", "replace")

    def app_mem(self, hook=True, guard_min=0):
        """The memory calls of the app files (src/sim_app_mem.c): hook = the free stops the sounds that use the block
        (as the loader of the board); guard_min > 0 = each block of at least that size in pages of its own (a read
        after its free is a fault)."""
        self.lib.tfnsim_app_mem(1 if hook else 0, int(guard_min))

    def app_mem_found(self):
        """The frees of app blocks that found a sound on the block (since the start)."""
        return self.lib.tfnsim_app_mem_found()

    def app_mem_bad(self):
        """The stops that did not clear the block (a fault of audio_mem_check)."""
        return self.lib.tfnsim_app_mem_bad()
