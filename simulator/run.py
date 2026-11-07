"""TFN OS simulator: the Waveshare ESP32-S3-Touch-LCD-3.49 with the firmware of this repo, on the PC.

Double-click "simulator/Run Simulator.cmd", or:
  python simulator/run.py                   # build what changed, then start
  python simulator/run.py --apps slim       # only the apps of the slim firmware of the 3.49 (as the board has them)
  python simulator/run.py --app CHESS       # open an app at the start
  python simulator/run.py --fresh           # a new board: no saved settings, scores or files

The mouse is the finger (the right button: a second finger). Esc = PWR, B = BOOT, the arrow keys tilt the board, Space
shakes it, Q / E turn it (the dial), K knocks on it, Z / X touch the two ends (the two-player games), R turns the board
by 90 degrees, F5 builds the firmware again and starts it (a new "flash"), Ctrl+S saves a picture of the screen.
Python standard library only (Tkinter); see simulator/README.md.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

import tfnsim as T

ERROR_COLOR = "#ff6b6b"
WARN_COLOR = "#f0b45a"


class Window:
    def __init__(self, board, args):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk, self.b, self.args = tk, ttk, board, args
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)      # (sharp pixels on a scaled display, not a stretched window)
        except (AttributeError, OSError):
            pass
        self.root = root = tk.Tk()
        self.dpi = k = max(1.0, root.winfo_fpixels("1i") / 96)
        root.title("TFN OS simulator - Waveshare ESP32-S3-Touch-LCD-3.49")
        root.geometry(args.geometry or f"{int(1420 * k)}x{int(800 * k)}")
        root.minsize(int(1000 * k), int(600 * k))
        root.configure(bg="#14161c")
        root.protocol("WM_DELETE_WINDOW", self.close)
        style = ttk.Style(root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        bg, fg, panel = "#14161c", "#d7dbe3", "#1c1f27"
        style.configure(".", background=panel, foreground=fg, fieldbackground="#262a35", bordercolor="#2c303b")
        style.configure("TFrame", background=panel)
        style.configure("Top.TFrame", background=bg)
        style.configure("TLabel", background=panel, foreground=fg)
        style.configure("Top.TLabel", background=bg, foreground=fg)
        style.configure("Head.TLabel", background=panel, foreground="#8fb8ff", font=("Segoe UI", 10, "bold"))
        style.configure("Dim.TLabel", background=panel, foreground="#8a90a0")
        style.configure("TCheckbutton", background=panel, foreground=fg)
        style.configure("TRadiobutton", background=panel, foreground=fg)
        style.configure("TButton", padding=(8, 3))
        style.configure("Key.TButton", padding=(12, 3), font=("Segoe UI", 9, "bold"))
        style.map("TCombobox", fieldbackground=[("readonly", "#262a35")], foreground=[("readonly", fg)],
                  selectbackground=[("readonly", "#262a35")], selectforeground=[("readonly", fg)])
        style.configure("Horizontal.TScale", troughcolor="#2a2f3b", background="#6d7a96", bordercolor="#2a2f3b",
                        lightcolor="#6d7a96", darkcolor="#4d5870")
        style.configure("Horizontal.TProgressbar", troughcolor="#2a2f3b", background="#5fbf7f", bordercolor="#2a2f3b",
                        lightcolor="#5fbf7f", darkcolor="#3f8f5f")
        root.option_add("*TCombobox*Listbox.background", "#262a35")
        root.option_add("*TCombobox*Listbox.foreground", fg)

        self.pose = board.orient()
        self.follow = tk.BooleanVar(value=True)
        self.pose_var = tk.IntVar(value=self.pose)
        self.tilt_x, self.tilt_y = tk.DoubleVar(value=0), tk.DoubleVar(value=0)
        self.shake, self.dial = tk.DoubleVar(value=0), tk.DoubleVar(value=0)
        self.mic, self.mic_hz = tk.DoubleVar(value=0), tk.DoubleVar(value=0)
        self.batt, self.usb, self.sdcard = tk.IntVar(value=82), tk.BooleanVar(value=True), tk.BooleanVar(value=True)
        self.sound_on, self.volume = tk.BooleanVar(value=not args.mute), tk.DoubleVar(value=0.6)
        self.app_var = tk.StringVar(value="")
        self.status = tk.StringVar(value="")
        self.keys = set()
        self.fingers = {}               # finger -> view point (panel px)
        self.ends = set()               # 0, 1: Z / X hold an end of the panel
        self.photo = None
        self.img_item = None
        self.view_geo = (0, 0, 1.0, 0, 0)   # x0, y0, scale, w, h of the picture on the canvas
        self.last_frames = -1
        self.last_bl = -1
        self.force = True
        self.state = T.RUN
        self.building = False
        self.fps_t, self.fps_n, self.fps = time.monotonic(), board.frames(), 0.0
        self.t_last = time.monotonic()

        # ---- the top bar
        top = ttk.Frame(root, style="Top.TFrame", padding=(10, 8))
        top.pack(fill="x")
        ttk.Label(top, text="TFN OS", style="Top.TLabel", font=("Segoe UI", 13, "bold")).pack(side="left")
        ttk.Label(top, text="  Waveshare 3.49  ", style="Top.TLabel", foreground="#8a90a0").pack(side="left")
        for text, key in (("PWR  (Esc)", T.KEY_PWR), ("BOOT  (B)", T.KEY_BOOT)):
            btn = ttk.Button(top, text=text, style="Key.TButton", takefocus=0)
            btn.pack(side="left", padx=3)
            btn.bind("<ButtonPress-1>", lambda e, k=key: self.key(k, True))
            btn.bind("<ButtonRelease-1>", lambda e, k=key: self.key(k, False))
        ttk.Label(top, text="   Open app ", style="Top.TLabel").pack(side="left")
        self.combo = ttk.Combobox(top, textvariable=self.app_var, values=board.apps, state="readonly", width=18)
        self.combo.pack(side="left")
        self.combo.bind("<<ComboboxSelected>>", lambda e: self.open_app())
        ttk.Button(top, text="Restart (F5)", command=self.rebuild_restart, takefocus=0).pack(side="right", padx=3)
        ttk.Button(top, text="Screenshot", command=self.screenshot, takefocus=0).pack(side="right", padx=3)
        ttk.Button(top, text="Turn board (R)", command=self.turn, takefocus=0).pack(side="right", padx=3)

        # ---- the status and the log (packed before the body: a small window makes the board smaller, not the log)
        bottom = ttk.Frame(root, style="Top.TFrame", padding=(10, 2, 10, 8))
        bottom.pack(side="bottom", fill="x")
        ttk.Label(bottom, textvariable=self.status, style="Top.TLabel", font=("Consolas", 10)).pack(fill="x")
        logf = ttk.Frame(bottom, style="Top.TFrame")
        logf.pack(fill="x", pady=(4, 0))
        self.log = tk.Text(logf, height=6, bg="#0e1015", fg="#aeb4c2", insertbackground=fg, relief="flat",
                           font=("Consolas", 9), wrap="none", takefocus=0)
        sb = ttk.Scrollbar(logf, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set, state="disabled")
        sb.pack(side="right", fill="y")
        self.log.pack(side="left", fill="x", expand=True)
        self.log.tag_configure("E", foreground=ERROR_COLOR)
        self.log.tag_configure("W", foreground=WARN_COLOR)

        # ---- the body: the board and the controls
        body = ttk.Frame(root, style="Top.TFrame")
        body.pack(fill="both", expand=True)
        side = ttk.Frame(body, padding=(12, 8), width=int(290 * self.dpi))
        side.pack(side="right", fill="y")
        side.pack_propagate(False)
        self.canvas = tk.Canvas(body, bg=bg, highlightthickness=0, cursor="hand2")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.build_side(side)

        # ---- the input
        for finger, button in ((0, 1), (1, 3)):
            self.canvas.bind(f"<ButtonPress-{button}>", lambda e, f=finger: self.finger_down(e, f))
            self.canvas.bind(f"<B{button}-Motion>", lambda e, f=finger: self.finger_move(e, f))
            self.canvas.bind(f"<ButtonRelease-{button}>", lambda e, f=finger: self.finger_up(e, f))
        root.bind("<KeyPress>", self.key_down)
        root.bind("<KeyRelease>", self.key_up)
        root.bind("<FocusOut>", self.focus_out)
        root.bind("<F5>", lambda e: self.rebuild_restart())
        root.bind("<F1>", lambda e: self.help())
        root.bind("<Control-s>", lambda e: self.screenshot())
        self.canvas.bind("<Configure>", lambda e: self.relayout())
        self.canvas.focus_set()
        self.push_settings()
        if args.app:
            board.open(args.app)
        if args.exit_after:
            root.after(int(args.exit_after * 1000), self.close)
        if args.self_test:
            self.run_steps(self.self_test())
        root.after(10, self.tick)

    # ------------------------------------------------------------------------------------------------ the controls
    def build_side(self, side):
        tk, ttk = self.tk, self.ttk

        def head(text, pad=(10, 3)):
            ttk.Label(side, text=text, style="Head.TLabel").pack(anchor="w", pady=pad)

        def slider(text, var, lo, hi, cmd=None):
            row = ttk.Frame(side)
            row.pack(fill="x", pady=1)
            ttk.Label(row, text=text, width=13).pack(side="left")
            s = ttk.Scale(row, from_=lo, to=hi, variable=var, takefocus=0,
                          command=(lambda v: cmd()) if cmd else None)
            s.pack(side="left", fill="x", expand=True)
            return s

        head("Position of the board", (0, 3))
        grid = ttk.Frame(side)
        grid.pack(fill="x")
        for i, (text, o) in enumerate((("Landscape", T.LAND_A), ("Landscape (turned)", T.LAND_B),
                                       ("Portrait", T.PORTRAIT), ("Portrait (turned)", T.PORTRAIT_FLIP))):
            ttk.Radiobutton(grid, text=text, value=o, variable=self.pose_var, takefocus=0,
                            command=self.pose_picked).grid(row=i // 2, column=i % 2, sticky="w")
        ttk.Checkbutton(side, text="Follow the app (turn with its screen)", variable=self.follow,
                        takefocus=0).pack(anchor="w")

        head("Motion")
        slider("Tilt left/right", self.tilt_x, -1, 1)
        slider("Tilt fwd/back", self.tilt_y, -1, 1)
        slider("Shake (g)", self.shake, 0, 3)
        slider("Dial (rad)", self.dial, -6.28, 6.28)
        row = ttk.Frame(side)
        row.pack(fill="x", pady=(3, 0))
        for text, kind in (("Knock", T.MOTION_KNOCK), ("Step", T.MOTION_STEP), ("Level", None)):
            ttk.Button(row, text=text, takefocus=0, width=7,
                       command=(lambda k=kind: self.b.motion(k)) if kind is not None else self.level).pack(side="left", padx=1)
        row = ttk.Frame(side)
        row.pack(fill="x", pady=(2, 0))
        ttk.Label(row, text="Flick", width=6).pack(side="left")
        for text, kind in (("↑", T.MOTION_UP), ("↓", T.MOTION_DOWN), ("←", T.MOTION_LEFT),
                           ("→", T.MOTION_RIGHT)):
            ttk.Button(row, text=text, width=3, takefocus=0, command=lambda k=kind: self.b.motion(k)).pack(side="left", padx=1)

        head("Microphone")
        slider("Level", self.mic, 0, 1)
        slider("Tone (Hz, 0 off)", self.mic_hz, 0, 1500)

        head("Power and card")
        slider("Battery %", self.batt, 0, 100, self.push_settings)
        ttk.Checkbutton(side, text="USB power (charging)", variable=self.usb, takefocus=0,
                        command=self.push_settings).pack(anchor="w")
        ttk.Checkbutton(side, text="SD card in the slot", variable=self.sdcard, takefocus=0,
                        command=self.push_settings).pack(anchor="w")

        head("Sound of the PC")
        row = ttk.Frame(side)
        row.pack(fill="x")
        ttk.Checkbutton(row, text="On", variable=self.sound_on, takefocus=0, command=self.push_settings).pack(side="left")
        ttk.Scale(row, from_=0, to=1, variable=self.volume, takefocus=0,
                  command=lambda v: self.push_settings()).pack(side="left", fill="x", expand=True, padx=(6, 0))
        self.meter = ttk.Progressbar(side, maximum=100, mode="determinate")
        self.meter.pack(fill="x", pady=(3, 0))

        row = ttk.Frame(side)
        row.pack(fill="x", pady=(12, 0))
        ttk.Button(row, text="Keys (F1)", takefocus=0, command=self.help).pack(side="left")
        ttk.Button(row, text="Data folder", takefocus=0, command=self.open_data).pack(side="left", padx=4)

    HELP = ("The mouse is a finger: click = tap, hold = long press, drag = swipe.\n"
            "The right button is a second finger (also while the left one is down).\n"
            "A pull down from the top edge of the screen opens the settings panel.\n\n"
            "Esc or P: PWR. A short push goes back (to the menu); hold 2 s = power off.\n"
            "B: BOOT. Hold it 0.7 s to open CHAT; apps such as WRONG ROOM use each push.\n"
            "Arrow keys: tilt the board. Space: shake it. K: knock on it. Q / E: turn it (the dial).\n"
            "Z / X: a finger on each end of the panel (the end buttons of the two-player games).\n"
            "R: turn the board 90 degrees (the menu turns with it; this stops 'Follow the app').\n\n"
            "F5: build the firmware again (only the changed files) and start it: a new flash.\n"
            "Ctrl+S: a picture of the screen (into the data folder, shots/).\n\n"
            "The data folder holds the NVS of the board (nvs.txt: the settings, the scores),\n"
            "/data (data/) and the SD card (sdcard/). Start with --fresh for a new board.")

    def help(self):
        from tkinter import messagebox
        messagebox.showinfo("Keys of the simulator", self.HELP)

    def open_data(self):
        os.startfile(str(self.b.data))

    def push_settings(self):
        self.b.battery(self.batt.get(), self.usb.get())
        self.b.sd(self.sdcard.get())
        self.b.sound(self.sound_on.get(), self.volume.get())

    def level(self):
        for v in (self.tilt_x, self.tilt_y, self.shake, self.dial):
            v.set(0)

    def pose_picked(self):
        self.follow.set(False)
        self.set_pose(self.pose_var.get())

    def set_pose(self, pose):
        if pose != self.pose:
            self.pose = pose
            self.pose_var.set(pose)
            self.b.pose(pose)
            self.release_fingers()
            self.relayout()

    def turn(self):
        self.follow.set(False)
        self.set_pose(T.CLOCKWISE[self.pose])

    def open_app(self):
        self.release_fingers()
        self.b.open(self.app_var.get())
        self.canvas.focus_set()

    # ------------------------------------------------------------------------------------------------ the fingers
    def to_view(self, event):
        x0, y0, s, w, h = self.view_geo
        vw, vh = T.view_size(self.pose)
        vx = (event.x - x0) / s
        vy = (event.y - y0) / s
        return vx, vy, 0 <= vx < vw and 0 <= vy < vh

    def send_fingers(self):
        pts = []
        for f in sorted(self.fingers):
            vx, vy = self.fingers[f]
            vw, vh = T.view_size(self.pose)
            vx = min(max(int(vx), 0), vw - 1)
            vy = min(max(int(vy), 0), vh - 1)
            pts.append(T.view_to_panel(self.pose, vx, vy))
        for e in sorted(self.ends):     # (the ends: after the mouse fingers, so that the events follow the mouse)
            pts.append((T.LCD_W // 2, T.END_PX // 2 if e == 0 else T.LCD_H - 1 - T.END_PX // 2))
        self.b.touch(pts)
        self.draw_fingers()

    def finger_down(self, event, f):
        self.canvas.focus_set()
        if self.state != T.RUN:
            return
        vx, vy, inside = self.to_view(event)
        if not inside:
            return
        self.fingers[f] = (vx, vy)
        self.send_fingers()

    def finger_move(self, event, f):
        if f in self.fingers:
            vx, vy, _ = self.to_view(event)
            self.fingers[f] = (vx, vy)
            self.send_fingers()

    def finger_up(self, event, f):
        if self.fingers.pop(f, None) is not None:
            self.send_fingers()

    def release_fingers(self):
        self.fingers.clear()
        self.ends.clear()
        self.b.touch([])
        self.draw_fingers()

    def draw_fingers(self):
        self.canvas.delete("finger")
        x0, y0, s, w, h = self.view_geo
        pts = [self.fingers[f] for f in sorted(self.fingers)]
        for e in sorted(self.ends):
            pts.append(T.panel_to_view(self.pose, T.LCD_W // 2, T.END_PX // 2 if e == 0 else T.LCD_H - 1 - T.END_PX // 2))
        for vx, vy in pts:
            cx, cy, r = x0 + vx * s, y0 + vy * s, max(9, 7 * s)
            self.canvas.create_oval(cx - r, cy - r, cx + r, cy + r, outline="#ffd166", width=2, tags="finger")

    # ------------------------------------------------------------------------------------------------ the keys
    def key(self, key, down):
        if key == T.KEY_PWR and down and self.state == T.OFF:
            self.restart(rebuild=False)             # (PWR starts a board that is off)
            return
        self.b.key(key, down)

    def typing(self, event):
        return isinstance(event.widget, str) or event.widget.winfo_class() in ("TCombobox", "Entry", "TEntry", "Spinbox")

    def key_down(self, event):
        k = event.keysym.lower()
        if k in self.keys or self.typing(event):
            return
        self.keys.add(k)
        if k in ("escape", "p"):
            self.key(T.KEY_PWR, True)
        elif k == "b":
            self.key(T.KEY_BOOT, True)
        elif k == "k":
            self.b.motion(T.MOTION_KNOCK)
        elif k == "r":
            self.turn()
        elif k in ("z", "x"):
            self.ends.add(0 if k == "z" else 1)
            self.send_fingers()

    def key_up(self, event):
        k = event.keysym.lower()
        self.keys.discard(k)
        if k in ("escape", "p"):
            self.key(T.KEY_PWR, False)
        elif k == "b":
            self.key(T.KEY_BOOT, False)
        elif k in ("z", "x"):
            self.ends.discard(0 if k == "z" else 1)
            self.send_fingers()

    def focus_out(self, event):
        # (only when the window loses the focus; not a change of the focus inside it)
        if not self.root.tk.call("focus", "-displayof", self.root._w):
            self.keys.clear()
            self.b.key(T.KEY_PWR, False)
            self.b.key(T.KEY_BOOT, False)
            self.release_fingers()

    # ------------------------------------------------------------------------------------------------ the picture
    def relayout(self):
        self.force = True

    def draw_board(self):
        cw, ch = max(self.canvas.winfo_width(), 50), max(self.canvas.winfo_height(), 50)
        vw, vh = T.view_size(self.pose)
        bez = 26
        s = min((cw - 2 * bez - 40) / vw, (ch - 2 * bez - 40) / vh, 3.0)
        s = max(s, 0.4)
        w, h, rgb = self.b.view(self.pose, vw * s)
        s = w / vw
        x0, y0 = (cw - w) // 2, (ch - h) // 2
        self.view_geo = (x0, y0, s, w, h)
        ppm = b"P6 %d %d 255\n" % (w, h) + rgb
        c = self.canvas
        if self.photo is None or (self.photo.width(), self.photo.height()) != (w, h):
            self.photo = self.tk.PhotoImage(width=w, height=h)
            self.force = True
        # (a put into the same photo: about 15 % faster than a new photo for each frame)
        self.root.tk.call(self.photo.name, "put", ppm, "-format", "ppm")
        if self.force:
            c.delete("bezel")
            r = bez
            x1, y1, x2, y2 = x0 - bez, y0 - bez, x0 + w + bez, y0 + h + bez
            c.create_polygon(x1 + r, y1, x2 - r, y1, x2, y1, x2, y1 + r, x2, y2 - r, x2, y2, x2 - r, y2, x1 + r, y2,
                             x1, y2, x1, y2 - r, x1, y1 + r, x1, y1, smooth=True, fill="#05060a", outline="#3a3f4c",
                             width=2, tags="bezel")
            c.delete("screen")
            self.img_item = c.create_image(x0, y0, image=self.photo, anchor="nw", tags="screen")
            c.tag_raise("finger")
            c.tag_raise("overlay")
            self.force = False

    def overlay(self, text, color):
        c = self.canvas
        c.delete("overlay")
        if not text:
            return
        x0, y0, s, w, h = self.view_geo
        c.create_rectangle(x0, y0, x0 + w, y0 + h, fill="#000000", stipple="gray50", outline="", tags="overlay")
        c.create_text(x0 + w / 2, y0 + h / 2, text=text, fill=color, font=("Segoe UI", 14, "bold"), justify="center",
                      width=max(w - 40, 200), tags="overlay")

    def screenshot(self):
        folder = self.b.data / "shots"
        folder.mkdir(exist_ok=True)
        name = self.b.app().replace(" ", "_").replace("/", "_") or "screen"
        path = folder / f"{name}-{time.strftime('%Y%m%d-%H%M%S')}.png"
        self.b.screenshot(path, self.pose)
        self.add_log(f"I (sim) screenshot: {path}\n")

    # ------------------------------------------------------------------------------------------------ the loop
    def push_sensors(self, dt):
        k = self.keys
        tx = self.tilt_x.get() + 0.7 * (("right" in k) - ("left" in k))
        ty = self.tilt_y.get() + 0.7 * (("down" in k) - ("up" in k))
        shake = max(self.shake.get(), 2.0 if "space" in k else 0.0)
        turn = ("e" in k) - ("q" in k)
        if turn:
            self.dial.set(max(-6.28, min(6.28, self.dial.get() + 2.5 * turn * dt)))
        self.b.sensors(max(-1, min(1, tx)), max(-1, min(1, ty)), shake, self.dial.get())
        self.b.mic(self.mic.get(), self.mic_hz.get() if self.mic_hz.get() >= 20 else 0)

    def tick(self):
        t0 = time.monotonic()
        dt, self.t_last = t0 - self.t_last, t0
        state, why = self.b.state()
        if state != self.state:
            self.state = state
            self.release_fingers()
            if state == T.RESTART:
                self.restart(rebuild=False)
                return
            self.tick_overlay(state, why)
        if self.follow.get() and state == T.RUN:
            o = self.b.orient()
            if o != self.pose:
                self.set_pose(o)
        if state == T.RUN:
            self.push_sensors(dt)
        frames, bl = self.b.frames(), self.b.backlight()
        if frames != self.last_frames or bl != self.last_bl or self.force:
            self.last_frames, self.last_bl = frames, bl
            had_overlay = bool(self.canvas.find_withtag("overlay"))
            forced = self.force
            self.draw_board()
            if forced:
                self.draw_fingers()
                if had_overlay or state in (T.OFF, T.HALT):
                    self.tick_overlay(state, why)
        if t0 - self.fps_t >= 0.5:
            self.fps = (frames - self.fps_n) / (t0 - self.fps_t)
            self.fps_t, self.fps_n = t0, frames
            app = self.b.app()
            if self.combo.get() != app and self.root.focus_get() is not self.combo:
                self.app_var.set(app)
            screen = "off" if bl == 0 else f"on ({bl})"
            self.status.set(f"{app:14s}  {T.ORIENT_NAMES.get(self.b.orient(), '?'):18s}  {self.fps:5.1f} fps  "
                            f"screen {screen:9s}  board {T.ORIENT_NAMES.get(self.pose)}"
                            + ("   building..." if self.building else ""))
            self.meter["value"] = self.b.sound_level()
            text = self.b.log()
            if text:
                self.add_log(text)
        self.root.after(max(1, int(15 - (time.monotonic() - t0) * 1000)), self.tick)

    def tick_overlay(self, state, why):
        if state == T.OFF:
            self.overlay("POWERED OFF\n\nPush PWR (Esc) to start the board again", "#d7dbe3")
        elif state == T.HALT:
            self.overlay("THE FIRMWARE STOPPED\n\n" + why + "\n\nThe log has the stack. Push Restart (F5).", ERROR_COLOR)

    def add_log(self, text):
        log = self.log
        log.configure(state="normal")
        for line in text.splitlines(True):
            tag = line[:1] if line[:1] in ("E", "W") and line[1:3] == " (" else ""
            log.insert("end", line, tag)
        n = int(log.index("end-1c").split(".")[0])
        if n > 600:
            log.delete("1.0", f"{n - 600}.0")
        log.configure(state="disabled")
        log.see("end")

    # ------------------------------------------------------------------------------------------------ restart
    def rebuild_restart(self):
        """F5: build the firmware again (what changed), then start the new one, as a new flash of the board. A build
        error keeps the simulator that runs now."""
        if self.building:
            return
        self.building = True
        self.add_log("I (sim) build...\n")

        def work():
            import build
            try:
                build.build(self.args.apps, quiet=True)
                err = None
            except Exception as e:      # (BuildError, or a fault of the build tool: never leave "building" on)
                err = str(e) or type(e).__name__
            self.root.after(0, lambda: self.built(err))
        threading.Thread(target=work, daemon=True).start()

    def built(self, err):
        self.building = False
        if err:
            self.add_log("E (sim) " + err.replace("\n", "\nE (sim) ") + "\n")
            from tkinter import messagebox
            messagebox.showerror("Build failed", err[:3000] + ("\n..." if len(err) > 3000 else ""))
            return
        self.restart(rebuild=False)

    def restart(self, rebuild):
        args = [sys.executable, str(Path(__file__).resolve()), "--apps", self.args.apps, "--fps", str(self.args.fps),
                "--data", str(self.b.data), "--geometry", self.root.winfo_geometry()]
        if not rebuild:
            args.append("--no-build")
        if not self.sound_on.get():
            args.append("--mute")
        subprocess.Popen(args, cwd=os.getcwd())
        self.close()

    def close(self, code=0):
        try:
            self.b.touch([])
            self.root.destroy()
        finally:
            sys.stdout.flush()
            os._exit(code)      # (the firmware threads never end)

    # ------------------------------------------------------------------------------------------------ the self-test
    def run_steps(self, gen):
        """Run a generator of steps; each yield gives the wait (ms) before the next step."""
        def step():
            try:
                self.root.after(max(1, int(next(gen))), step)
            except StopIteration:
                pass
        self.root.after(200, step)

    def self_test(self):
        """--self-test: the window as a user drives it: a pull from the top edge (Tk mouse events on the canvas) opens
        the settings panel, B closes it, R turns the board and the menu turns with it. Exit code 0 when all is good
        (simulator/test_sim.py runs it)."""
        from types import SimpleNamespace

        def ev(kind, vx, vy, **kw):
            x0, y0, s, w, h = self.view_geo
            self.canvas.event_generate(kind, x=int(x0 + (vx + 0.5) * s), y=int(y0 + (vy + 0.5) * s), **kw)

        def key(keysym, down):
            # (the handlers of the key bindings: Tk gives a made-up key event only to a window that has the focus of
            # the PC, and a test must not take the focus from the user)
            e = SimpleNamespace(keysym=keysym, widget=self.canvas)
            (self.key_down if down else self.key_up)(e)

        fails = []
        for _ in range(250):
            if self.b.app() == "MENU" and self.b.frames() > 10:
                break
            yield 20
        yield 600
        vw, vh = T.view_size(self.pose)
        ev("<ButtonPress-1>", vw // 2, 1)
        for k in range(1, 13):
            yield 18
            ev("<B1-Motion>", vw // 2, 1 + (vh * 0.8) * k / 12)
        ev("<ButtonRelease-1>", vw // 2, vh * 0.8)
        yield 900
        if not self.b.panel_on():
            fails.append("a mouse pull from the top edge did not open the settings panel")
        key("b", True)
        yield 150
        key("b", False)
        yield 900
        if self.b.panel_on():
            fails.append("the B key did not close the settings panel")
        before = self.pose
        key("r", True)
        key("r", False)
        for _ in range(100):
            yield 20
            if self.b.orient() == T.CLOCKWISE[before]:
                break
        if self.pose != T.CLOCKWISE[before] or self.b.orient() != self.pose:
            fails.append(f"R did not turn the board and the menu (board {self.pose}, menu {self.b.orient()})")
        yield 300
        self.screenshot()
        print("self-test: " + ("ok" if not fails else "FAIL: " + "; ".join(fails)), flush=True)
        self.close(1 if fails else 0)

    def run(self):
        self.root.mainloop()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--apps", default="all", help="the app set: all (default), on, slim, or a selection file (JSON)")
    p.add_argument("--app", help="open this app at the start (its name in the menu)")
    p.add_argument("--data", help="the data folder of the board (default simulator/.data/lcd349)")
    p.add_argument("--fresh", action="store_true", help="remove the data of the board first (a new board)")
    p.add_argument("--fps", type=int, default=60, help="the most frames a second (default 60; the board: 30..60)")
    p.add_argument("--mute", action="store_true", help="start with the sound of the PC off")
    p.add_argument("--no-build", action="store_true", help="do not build; use the DLL of the last build of the app set")
    p.add_argument("--geometry", help=argparse.SUPPRESS)
    p.add_argument("--exit-after", type=float, help="close after this many seconds (a check of the start)")
    p.add_argument("--self-test", action="store_true", help="drive the window with Tk events, then close (a check)")
    a = p.parse_args()
    import build
    if a.no_build:
        dll = build.lib_path(a.apps)
        if not dll.exists():
            print(f"There is no {dll}: run without --no-build", file=sys.stderr)
            return 1
    else:
        try:
            dll = build.build(a.apps)
        except build.BuildError as e:
            print(f"Build failed: {e}", file=sys.stderr)
            return 1
    data = Path(a.data) if a.data else T.DATA / "lcd349"
    if a.fresh:
        shutil.rmtree(data, ignore_errors=True)
    board = T.Board(dll, data, a.fps)
    Window(board, a).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
