"""Build the TFN OS simulator (simulator/.build/<app set>/tfnsim.dll on Windows, libtfnsim.so on Linux, libtfnsim.dylib
on macOS). No ESP-IDF is necessary. On Windows the compiler is Zig (its clang with the MinGW libraries); on Linux and
macOS it is the C compiler of the PC (clang, else cc), or TFN_SIM_CC.

The DLL has the real firmware: main/main.c (the main loop), the touch engine (input.c), the synthesizer (audio.c),
the settings (settings.c), the system layer (sysui.c), the files (storage.c, export.c), the menu and all the apps of
main/apps.json. simulator/src/ gives the board, the motion sensor, FreeRTOS, NVS, the file paths and the sound card of
the PC; simulator/idf/ has the ESP-IDF headers for them. tests/host/stubs.c (the stubs of the host fuzz) gives the rest
(WiFi, the radio link, Bluetooth, the installed apps): each function of it that a real file or the simulator also has
gets another name at compile time, so the real one is used. The sources of the apps that a board loads as app files
get src/sim_app_mem.h: their memory calls go through src/sim_app_mem.c, as the loader of the board sends them through
its own functions (the free first stops each sound that uses the block).

  python simulator/build.py                 # build (only the changed files compile again)
  python simulator/build.py --apps on       # the apps that are "on" in main/apps.json (all apps is the default)
  python simulator/build.py --apps slim     # the slim firmware of the 3.49 (the core apps only)
  python simulator/build.py --setup         # Windows: get the pinned portable compiler (Zig) if there is none
  python simulator/build.py --frame watcher # another board: cores3, watcher, am175, tab5 (.build/all-<frame>)
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

WINDOWS = sys.platform == "win32"
MACOS = sys.platform == "darwin"

SIM = Path(__file__).resolve().parent
ROOT = SIM.parent
MAIN = ROOT / "main"
HOST = ROOT / "tests" / "host"
OUT = SIM / ".build"
ZIG_VERSION = "0.14.1"
ZIG_FOLDER = f"zig-x86_64-windows-{ZIG_VERSION}"
ZIG_URL = f"https://ziglang.org/download/{ZIG_VERSION}/{ZIG_FOLDER}.zip"
ZIG_SHA256 = "554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c"   # (ziglang.org/download/index.json)

# the real files of the firmware that the host fuzz does not compile (main/apps.json host_skip), but the simulator does
REAL = ["main.c", "main_saver.c", "main_keys.c", "main_selftest.c", "input.c", "audio.c", "settings.c", "sysui.c", "sysui_layout.c", "sysui_panel.c", "storage.c", "export.c"]
QRCODE = ROOT / "managed_components" / "espressif__qrcode"      # (after an ESP-IDF build: real QR codes in SETUP)
APP_SETS = ("all", "on", "slim")
# the frames of the boards: the folder of the sdkconfig.h of the host tests, the flags (simulator/idf/sdkconfig.h)
# (simulator/idf/sdkconfig.h selects the board; the capabilities come from boards/<kind>/board_caps.h)
FRAMES = {"lcd349": ("inc349", []), "cores3": ("incs3", ["-DSIM_FRAME_CORES3=1"]),
          "watcher": ("incwt", ["-DSIM_FRAME_WATCHER=1"]), "am175": ("incwt", ["-DSIM_FRAME_AM175=1"]),
          "tab5": ("incs3", ["-DSIM_FRAME_TAB5=1"])}
# the files whose info lines (ESP_LOGI) of a frame name a variable that only the board build (ESP_PLATFORM) has: they
# compile with no info lines, as in the host fuzz (simulator/idf/esp_log.h SIM_LOG_INFO_OFF). A fix of the file
# removes its entry.
LOG_INFO_OFF = {}


class BuildError(Exception):
    pass


def tool_dirs():
    """The folders that can hold the portable compiler: TFN_SIM_TOOLS, the folder next to the repo, simulator/.tools."""
    dirs = []
    if os.environ.get("TFN_SIM_TOOLS"):
        dirs.append(Path(os.environ["TFN_SIM_TOOLS"]))
    return dirs + [ROOT.parent / ".tools", SIM / ".tools"]


def lib_name():
    """The file name of the simulator library on this PC."""
    return "tfnsim.dll" if WINDOWS else "libtfnsim.dylib" if MACOS else "libtfnsim.so"


def lib_path(apps="all", frame="lcd349"):
    return build_dir(apps, frame) / lib_name()


def host_compiler():
    """Linux and macOS: clang (the compiler of the Windows build too), else cc; [cc args], [c++ args]."""
    for cc, cxx in (("clang", "clang++"), ("cc", "c++")):
        if shutil.which(cc):
            return [shutil.which(cc)], [shutil.which(cxx) or cxx]
    raise BuildError("no C compiler: install clang (or gcc), or set TFN_SIM_CC")


def compiler(setup=False):
    """The C compiler: [cc args], [c++ args]. TFN_SIM_CC first; then on Windows Zig (its clang, with the MinGW
    libraries of Windows), on Linux and macOS the compiler of the PC."""
    env = os.environ.get("TFN_SIM_CC")
    if env:
        z = Path(env)
        if z.stem.lower() == "zig":
            return [str(z), "cc"], [str(z), "c++"]
        # (clang-18 -> clang++-18, gcc-13 -> g++-13, cc -> c++)
        cxx = re.sub(r"^(clang|gcc|cc)", lambda m: {"clang": "clang++", "gcc": "g++", "cc": "c++"}[m.group(1)], z.stem)
        return [str(z)], [shutil.which(cxx) or str(z.with_name(cxx))]
    if not WINDOWS:
        return host_compiler()
    for d in tool_dirs():
        z = d / ZIG_FOLDER / "zig.exe"
        if z.exists():
            return [str(z), "cc"], [str(z), "c++"]
    z = shutil.which("zig")
    if z:
        return [z, "cc"], [z, "c++"]
    if not setup:
        raise BuildError("no C compiler: run  python simulator/build.py --setup  (it gets Zig %s, about 80 MB), or set "
                         "TFN_SIM_CC to a zig.exe" % ZIG_VERSION)
    dest = SIM / ".tools"
    dest.mkdir(exist_ok=True)
    archive = dest / f"zig-{ZIG_VERSION}.zip"
    print(f"Download Zig {ZIG_VERSION} into {dest} ...", flush=True)
    urllib.request.urlretrieve(ZIG_URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ZIG_SHA256:
        archive.unlink()
        raise BuildError("the SHA-256 of the Zig download is wrong: it is not extracted")
    with zipfile.ZipFile(archive) as z:
        z.extractall(dest)
    archive.unlink()
    return compiler(False)


def build_dir(apps, frame="lcd349"):
    """The build folder of an app set: .build/all, .build/on, .build/slim, .build/sel-<name> (each set keeps its
    objects: a change of the set does not compile all again). Another frame: .build/all-cores3, .build/all-watcher ... Linux and macOS:
    .build/linux-all, .build/darwin-all ... (a folder that two systems share, as with WSL, keeps the objects of each
    one)."""
    name = apps if apps in APP_SETS else "sel-" + re.sub(r"[^\w-]", "_", Path(apps).stem)
    if frame != "lcd349":
        name += "-" + frame
    return OUT / (name if WINDOWS else sys.platform + "-" + name)


def app_lists(apps, bdir):
    """Run main/apps_gen.py --host for the app set: bdir/apps gets apps_gen.h, apps_list.inc, srcs.txt, flags.txt and
    incs.txt. It does not touch main/apps_all.inc (--no-all)."""
    gen = bdir / "apps"
    cmd = [sys.executable, str(MAIN / "apps_gen.py"), "--host", str(gen), "--prefix", MAIN.as_posix() + "/", "--no-all"]
    if apps == "on":
        cmd += ["--on-flags"]
    elif apps == "slim":
        cmd += ["--on-flags", "--kind", "lcd349"]
    elif apps != "all":
        cmd += ["--select", str(Path(apps).resolve())]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise BuildError("main/apps_gen.py failed:\n" + r.stdout + r.stderr)
    summary = r.stdout.strip().splitlines()
    srcs = [Path(s) for s in (gen / "srcs.txt").read_text().split()]
    flags = (gen / "flags.txt").read_text().split()
    incs = (gen / "incs.txt").read_text().split()
    return srcs, flags, incs, summary


# A definition at the start of a line (the style of the repo): a function "type name(...) {" or a variable
# "type name[...] = ..." / "type name;". static, extern and typedef lines are not definitions of global names.
DEF_FN = re.compile(r"^(?!static\b|extern\b|typedef\b|return\b|else\b|if\b|while\b|for\b|switch\b|case\b)"
                    r"(?:__attribute__\(\(\w+\)\)\s*)?[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{", re.M)
DEF_VAR = re.compile(r"^(?!static\b|extern\b|typedef\b|return\b|struct\s+\w+\s*\{|enum\b|union\b|#)"
                     r"(?:__attribute__\(\(\w+\)\)\s*)?(?:const\s+|volatile\s+)*[A-Za-z_]\w*(?:\s+const)?[\s\*]+"
                     r"(?:const\s+)?\*?\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:=|;)", re.M)
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "else", "do"}


def definitions(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    names = set(DEF_FN.findall(text)) | set(DEF_VAR.findall(text))
    return names - KEYWORDS


def renames(stubs, others):
    """The names of tests/host/stubs.c that a real file or the simulator also defines: -D name=hoststub_name."""
    mine = set()
    for p in others:
        mine |= definitions(p)
    return sorted(definitions(stubs) & mine)


def app_file_sources(frame="lcd349"):
    """The sources of the apps that a board of the frame loads as app files: each app of main/apps.json that is not a
    core app and that the slim firmware of the kind does not keep (with no slim firmware: each app that is not "on"),
    without the sources of the modules and of the core (the firmware has them). The simulator builds these apps in, and
    their memory calls go through src/sim_app_mem.h, as the loader of the board (main/tfn_app.c) sends the calls of an
    app file to its own functions. A set of resolved paths."""
    if str(MAIN) not in sys.path:
        sys.path.insert(0, str(MAIN))
    import apps_gen
    doc = apps_gen.load(str(MAIN / "apps.json"))
    slim = doc.get("slim", {})
    is_slim = frame in slim.get("kinds", [])
    keep = set(slim.get("keep", {}).get(frame, []))
    firmware = set(doc["core"]["srcs"])
    for m in doc["modules"].values():
        firmware |= set(m["srcs"])
    out = set()
    for a in doc["apps"]:
        if a.get("core") or a["name"] in keep or (not is_slim and a.get("on", True)):
            continue
        for f in a["srcs"]:
            p = apps_gen.locate(str(MAIN), f)
            if p and f not in firmware:
                out.add(Path(p).resolve())
    return out


def wrapped_copy(path, names):
    """The name of the file that this source only includes ('#include "kbd.c"': TOY 3.11 puts system code that the ABI
    does not have into its app file), when that file is in the build too; else None. Built into one program with the
    real file, the copy would define each name two times."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text).strip()
    m = re.fullmatch(r'#include\s+"([\w./-]+\.c)"', text)
    return m.group(1) if m and Path(m.group(1)).name in names else None


def target_flags():
    """The flags of each compile and of the link for this PC."""
    if WINDOWS:
        # (-gcodeview: the debug data of Windows in tfnsim.pdb, for the stack of a fault in sim_crash.c)
        return ["-target", "x86_64-windows-gnu", "-gcodeview"]
    return ["-fPIC", "-pthread"]


def link_flags(bdir):
    """The libraries of the link, and a link error for a name that no file has (as the link of a DLL gives)."""
    if WINDOWS:
        return ["-Wl,--out-implib," + str(bdir / "tfnsim.lib"), "-lwinmm", "-ldbghelp", "-lm"]
    if MACOS:
        return ["-lm"]
    return ["-Wl,--no-undefined", "-lm"]


def obj_name(src):
    h = hashlib.sha1(str(src).lower().encode()).hexdigest()[:8]
    return f"{Path(src).stem}-{h}.o"


def deps_newer(dep_file, obj_time):
    """True when the object is older than a file of its dependency list (from -MMD), or the list is missing."""
    try:
        text = dep_file.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return True
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    parts = re.split(r":\s", text, maxsplit=1)          # ("C:\...\x.o: deps": not the colon of the drive)
    body = parts[1] if len(parts) == 2 else text
    # (the paths have no spaces in this repo; a backslash before a space would escape it)
    for f in re.split(r"(?<!\\)\s+", body):
        f = f.replace("\\ ", " ").strip()
        if not f:
            continue
        try:
            if os.path.getmtime(f) > obj_time:
                return True
        except OSError:
            return True
    return False


def build(apps="all", setup=False, force=False, jobs=None, quiet=False, frame="lcd349"):
    """Build .build/<app set>/<lib_name()>; returns its path. Only the files that changed (or whose headers changed)
    compile again."""
    if frame not in FRAMES:
        raise BuildError(f"no frame {frame}: {', '.join(FRAMES)}")
    bdir = build_dir(apps, frame)
    (bdir / "obj").mkdir(parents=True, exist_ok=True)
    cc, cxx = compiler(setup)
    srcs, app_flags, incs, summary = app_lists(apps, bdir)
    real = [MAIN / f for f in REAL]
    sim_srcs = sorted((SIM / "src").glob("*.c"))
    extra = [HOST / "common" / "miniz.c"]
    if (QRCODE / "qrcodegen.c").exists():
        real.append(MAIN / "qr.c")
        extra += [QRCODE / f for f in ("qrcodegen.c", "esp_qrcode_main.c", "esp_qrcode_wrapper.c")]
    stubs = HOST / "stubs.c"
    ren = renames(stubs, real + sim_srcs)
    names = {s.name for s in srcs + real}
    skipped = [s for s in srcs if wrapped_copy(s, names)]
    srcs = [s for s in srcs if s not in skipped]
    firmware = srcs + real + extra
    inc = ["-I" + str(SIM / "idf"), "-I" + str(SIM / "src"), "-I" + str(HOST / FRAMES[frame][0]),
           "-I" + str(HOST / "common"), "-I" + str(MAIN), "-I" + str(bdir / "apps")] + incs
    if (QRCODE / "qrcodegen.c").exists():
        inc += ["-I" + str(QRCODE / "include"), "-I" + str(QRCODE)]
    # (-ffp-contract=off: no fused multiply-add, so the float results are the same on each PC)
    base = target_flags() + ["-O1", "-g", "-ffp-contract=off", "-fno-sanitize=undefined", "-DTFN_SIMULATOR=1",
            "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
            "-Wno-missing-field-initializers", "-Wno-date-time", "-Wno-format", "-Wno-unknown-pragmas",
            "-Wno-unused-const-variable", "-Wno-pointer-sign", "-Wno-char-subscripts", "-Wno-misleading-indentation",
            "-Wno-deprecated-declarations", "-Wno-unknown-warning-option", "-Wno-ignored-attributes",
            '-DBK_HOST_DIR="/data/books"'] + FRAMES[frame][1] + app_flags + inc
    force_inc = ["-include", str(SIM / "src" / "sim_force.h")]
    det_inc = ["-include", str(SIM / "src" / "sim_det.h")]     # (rand() and time() of the stubs)
    app_inc = ["-include", str(SIM / "src" / "sim_app_mem.h")]  # (the memory calls of an app file)
    app_files = app_file_sources(frame)
    log_off = {(ROOT / f).resolve() for f in LOG_INFO_OFF.get(frame, [])}
    jobs_list = []
    for s in firmware + [stubs] + sim_srcs:
        is_cxx = s.suffix in (".cc", ".cpp")
        cmd = (cxx + ["-std=gnu++17", "-fno-exceptions", "-fno-rtti"] if is_cxx else cc + ["-std=gnu11"]) + base
        if s in firmware and not is_cxx:
            cmd += force_inc            # (the paths of the board: only the files of the firmware)
            if s.resolve() in app_files:
                cmd += app_inc
        if s.resolve() in log_off:
            cmd += ["-DSIM_LOG_INFO_OFF=1"]
        if s == stubs:
            cmd += det_inc + ["-D%s=hoststub_%s" % (n, n) for n in ren]
        jobs_list.append((s, cmd))
    sig = hashlib.sha1(json.dumps([j[1] for j in jobs_list] + [apps]).encode()).hexdigest()
    manifest_path = bdir / "obj" / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, ValueError):
        manifest = {}
    env = os.environ.copy()
    tools = next((d for d in tool_dirs() if (d / ZIG_FOLDER).exists()), SIM / ".tools")
    env.setdefault("ZIG_GLOBAL_CACHE_DIR", str(tools / "zig-cache"))
    env.setdefault("ZIG_LOCAL_CACHE_DIR", str(bdir / "zig-cache"))

    def one(job):
        src, cmd = job
        obj = bdir / "obj" / obj_name(src)
        dep = obj.with_suffix(".d")
        key = hashlib.sha1(json.dumps(cmd).encode()).hexdigest()
        if (not force and obj.exists() and manifest.get(obj.name) == key and
                os.path.getmtime(src) <= os.path.getmtime(obj) and not deps_newer(dep, os.path.getmtime(obj))):
            return obj, key, None
        full = cmd + ["-MMD", "-MF", str(dep), "-c", str(src), "-o", str(obj)]
        r = subprocess.run(full, cwd=bdir, env=env, capture_output=True, text=True, errors="replace")
        if r.returncode:
            return obj, None, f"{src}:\n{r.stderr.strip()}"
        warn = r.stderr.strip()
        return obj, key, ("WARN " + warn) if warn and os.environ.get("TFN_SIM_WARN") else None

    errors, objs = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs or os.cpu_count() or 4) as ex:
        for obj, key, msg in ex.map(one, jobs_list):
            objs.append(obj)
            if key is None:
                errors.append(msg)
                manifest.pop(obj.name, None)
                continue
            if msg:
                print(msg[5:])          # (TFN_SIM_WARN=1: the warnings of the compiler)
            manifest[obj.name] = key
    manifest_path.write_text(json.dumps(manifest))
    if errors:
        raise BuildError("compile errors:\n\n" + "\n\n".join(errors))
    dll = bdir / lib_name()
    stamp = bdir / "tfnsim.json"
    link_inputs = sorted(str(o) for o in objs)
    try:
        old = json.loads(stamp.read_text())
    except (OSError, ValueError):
        old = {}
    newest = max(os.path.getmtime(o) for o in objs)
    if not force and dll.exists() and old.get("sig") == sig and old.get("objs") == link_inputs and \
            os.path.getmtime(dll) >= newest:
        return dll
    cxx_link = any(s.suffix in (".cc", ".cpp") for s in firmware)
    # (the window loads a copy of the DLL, so this file is free; the DLL keeps the path of tfnsim.pdb for the stack of a
    # fault, sim_crash.c)
    cmd = (cxx if cxx_link else cc) + target_flags() + ["-shared", "-g", "-o", str(dll)]
    # (with all apps the list of the objects is longer than the 32 K of a Windows command line: a response file holds
    # it)
    rsp = bdir / "link_inputs.rsp"
    rsp.write_text("\n".join('"%s"' % str(o).replace("\\", "/") for o in link_inputs) + "\n")
    cmd += ["@" + str(rsp)] + link_flags(bdir)
    if not quiet:
        print(f"Link {dll.name} ({len(objs)} objects, {len(firmware)} firmware files) ...", flush=True)
    r = subprocess.run(cmd, cwd=bdir, env=env, capture_output=True, text=True, errors="replace")
    if r.returncode:
        raise BuildError("link errors:\n" + r.stderr.strip())
    stamp.write_text(json.dumps({"sig": sig, "objs": link_inputs, "apps": apps, "summary": summary,
                                 "renamed": ren, "skipped_copies": [str(s) for s in skipped]}, indent=1))
    return dll


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--apps", default="all", help="all (default), on, slim, or a selection file (JSON) of apps_gen.py")
    p.add_argument("--frame", default="lcd349", choices=sorted(FRAMES), help="the board: lcd349 (default), cores3 "
                   "(the 320 x 240 frame), watcher or am175 (the round boards), tab5 (the tablet UI)")
    p.add_argument("--setup", action="store_true", help="get the pinned portable compiler (Zig) if there is none")
    p.add_argument("--force", action="store_true", help="compile all files again")
    p.add_argument("-j", "--jobs", type=int, help="parallel compiles (default: the count of CPUs)")
    a = p.parse_args()
    try:
        print(build(a.apps, a.setup, a.force, a.jobs, frame=a.frame))
    except BuildError as e:
        print(f"Build failed: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
