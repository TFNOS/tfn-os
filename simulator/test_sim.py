"""The tests of the TFN OS simulator.

  python -m unittest discover -s simulator -p test_sim.py -v

Geometry and BuildHelpers need nothing. Simulator builds tfnsim.dll (python simulator/build.py) and runs the real
firmware: the smoke test of a few apps (smoke.py) and the window, driven with Tk events (run.py --self-test). It is
skipped when there is no compiler (python simulator/build.py --setup gets one). TFN_SIM_NO_GUI=1 skips the window.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import build as B
import tfnsim as T

SIM = Path(__file__).resolve().parent
POSES = (T.PORTRAIT, T.PORTRAIT_FLIP, T.LAND_A, T.LAND_B)


class Geometry(unittest.TestCase):
    POINTS = [(0, 0), (171, 639), (171, 0), (0, 639), (85, 320), (3, 600)]

    def test_round_trip(self):
        for pose in POSES:
            for px, py in self.POINTS:
                vx, vy = T.panel_to_view(pose, px, py)
                vw, vh = T.view_size(pose)
                self.assertTrue(0 <= vx < vw and 0 <= vy < vh, (pose, px, py))
                self.assertEqual(T.view_to_panel(pose, vx, vy), (px, py))

    def test_same_as_gfx_touch_to_logical(self):
        # main/gfx.c: the logical point of a physical touch point (W = 172, H = 640)
        W, H = T.LCD_W, T.LCD_H
        gfx = {T.PORTRAIT: lambda px, py: (px, py), T.PORTRAIT_FLIP: lambda px, py: (W - 1 - px, H - 1 - py),
               T.LAND_A: lambda px, py: (py, W - 1 - px), T.LAND_B: lambda px, py: (H - 1 - py, px)}
        for pose in POSES:
            for px, py in self.POINTS:
                self.assertEqual(T.panel_to_view(pose, px, py), gfx[pose](px, py))

    def test_turn(self):
        pose = T.LAND_A
        seen = set()
        for _ in range(4):
            seen.add(pose)
            pose = T.CLOCKWISE[pose]
        self.assertEqual(pose, T.LAND_A)
        self.assertEqual(seen, set(POSES))
        # clockwise: the top end of the panel (y = 0) goes from the top of the view to its right side
        self.assertEqual(T.panel_to_view(T.CLOCKWISE[T.PORTRAIT], T.LCD_W // 2, 0)[0], T.LCD_H - 1)


class BuildHelpers(unittest.TestCase):
    def write(self, text):
        f = tempfile.NamedTemporaryFile("w", suffix=".c", delete=False)
        f.write(text)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_definitions(self):
        path = self.write(
            "#include <stdio.h>\n"
            "settings_t g_settings = {80};\n"
            "const char *const NAMES[3] = {0};\n"
            "static int hidden(void) { return 1; }\n"
            "extern int declared;\n"
            "void sfx_play(sfx_t s) { n++; }\n"
            "__attribute__((weak)) bool sysui_active(void) { return false; }\n"
            "int proto(void);\n"
            "// void commented(void) {}\n"
            "uint32_t store_get(const char *k,\n    uint32_t d) { return d; }\n")
        self.assertEqual(B.definitions(path), {"g_settings", "NAMES", "sfx_play", "sysui_active", "store_get"})

    def test_renames_of_the_host_stubs(self):
        real = [B.MAIN / f for f in B.REAL] + sorted((SIM / "src").glob("*.c"))
        ren = set(B.renames(B.HOST / "stubs.c", real))
        # main.c, settings.c, audio.c, input.c and the simulator win over the stubs of the host fuzz
        for name in ("app_open", "app_current", "store_get", "g_settings", "sfx_play", "input_points",
                     "imu_tilt_logical", "board_battery"):
            self.assertIn(name, ren)
        # the stubs of WiFi and of the installed apps stay (no real file of them is in the simulator)
        for name in ("wifi_scan", "tfn_count", "link_send"):
            self.assertNotIn(name, ren)

    def test_app_file_sources(self):
        # the sources of the store apps get the memory calls of the loader (src/sim_app_mem.h); the core, the modules
        # and the core apps do not
        files = B.app_file_sources("lcd349")
        names = {p.name for p in files}
        for name in ("app_echokey.c", "app_lens.c", "oa_engine.c", "app_tuner.c"):
            self.assertIn(name, names)
        for name in ("audio.c", "main.c", "tfsfx.c", "midi_bg.c", "app_chat.c"):
            self.assertNotIn(name, names)
        self.assertTrue(all(p.is_file() for p in files))

    def test_wrapped_copy(self):
        copy = self.write('// TOY 3.11: the keyboard of the system in the app file\n#include "kbd.c"\n')
        self.assertEqual(B.wrapped_copy(copy, {"kbd.c", "gfx.c"}), "kbd.c")
        self.assertIsNone(B.wrapped_copy(copy, {"gfx.c"}))
        code = self.write('#include "kbd.c"\nint more(void) { return 2; }\n')
        self.assertIsNone(B.wrapped_copy(code, {"kbd.c"}))


class Simulator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            B.build("all", quiet=True)
        except B.BuildError as e:
            if "no C compiler" in str(e):
                raise unittest.SkipTest(str(e))
            raise

    def run_py(self, *args, timeout=400):
        r = subprocess.run([sys.executable, *args], cwd=SIM, capture_output=True, text=True, errors="replace",
                           timeout=timeout)
        lines = [ln for ln in r.stdout.splitlines() if not ln.startswith(("I (", "W ("))]
        return r.returncode, "\n".join(lines[-40:]) + r.stderr[-2000:]

    def test_smoke(self):
        code, out = self.run_py("smoke.py", "--no-build", "--seconds", "1.5", "--apps", "CHESS", "SIMON", "SEA BATTLE",
                                "SETUP", "TUNER", "CRUISE")
        self.assertEqual(code, 0, out)
        self.assertIn("6 of 6 apps ok", out)

    @unittest.skipIf(os.environ.get("TFN_SIM_NO_GUI"), "TFN_SIM_NO_GUI")
    def test_window(self):
        with tempfile.TemporaryDirectory() as data:
            code, out = self.run_py("run.py", "--no-build", "--mute", "--self-test", "--data", data, timeout=120)
            self.assertEqual(code, 0, out)
            self.assertIn("self-test: ok", out)
            self.assertTrue(list(Path(data, "shots").glob("MENU-*.png")))


if __name__ == "__main__":
    unittest.main()
