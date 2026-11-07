#!/usr/bin/env python3
"""The local CI of TFN OS: the steps of .github/workflows/ci.yml, sim.yml and sdk.yml on the PC of a developer.

  python tests/ci_local.py                      # the full run (before a release)
  python tests/ci_local.py --quick              # the fast steps only (before each merge)
  python tests/ci_local.py --only golden,smoke  # only these steps
  python tests/ci_local.py --skip sdk_pack,sdk_template
  python tests/ci_local.py --list               # the names of the steps and the tools that they need

Each step prints one line: OK, FAIL or SKIP, the seconds and a short note. The last line is "CI LOCAL: OK (n steps)"
or "CI LOCAL: FAIL (the names)". The exit code is 0 or 1. The full output of each step goes into
tests/host/ci_local/<step>.log (git ignores the .log files).

Where each step runs:
  - The quick steps and the host fuzz use the working tree (also the changes that are not committed).
  - The simulator steps (sim_build, golden, smoke, sound) and the SDK steps (sdk_pack, sdk_template) use a CLEAN
    worktree of HEAD in the temp folder of the system (git worktree add; git worktree remove --force at the end). The
    reason: the golden menu frames show the count of the apps, and a private app file of the working tree (BOOKS)
    changes it. So commit your change before you run these steps.
  - The step gitleaks scans a copy of the files that git tracks (their content of the working tree) in the temp folder
    of the system, as a checkout of CI: a file that git ignores or does not track stays out (the git-ignored work
    folders and builds of the developer; stage a new file with "git add" to have it in the scan). So the scan also
    reads the changes that are not committed. Then it scans the new commits.

The tools (an environment variable gives another place):
  CLANG_FORMAT   clang-format 22.1.8 (the version of CI). Else tests/host/cf22/clang_format/data/bin/ of this
                 repo or of the main worktree, else clang-format on PATH (when it is 22.1.8).
  WSL + gcc      the unit tests of CI use gcc on Linux: here they run in WSL (wsl -e gcc). With no WSL: SKIP.
  clang + bash   the host fuzz: tests/host/build.sh in the bash of Git for Windows, with clang on PATH (llvm-mingw).
  TFN_SIM_TOOLS  the folder with the Zig compiler of the simulator (simulator/.tools of this repo or of the main
                 worktree). With none: python simulator/build.py --setup.
  TFN_IDF        the ESP-IDF 6.1 folder for the SDK steps; default C:/esp/v6.1/esp-idf, else IDF_PATH.
  IDF_PROFILE    Windows: the activation script of ESP-IDF 6.1 (as dev.ps1); default the v6.1 PowerShell profile
                 of the ESP-IDF installation manager in C:/Espressif/tools, else export.ps1 of TFN_IDF.
  GITLEAKS       gitleaks (CI uses 8.28.0; a relative path starts at the current folder). With none: SKIP; a
                 GITLEAKS that is not a program: FAIL.
  node           the test of the web flasher. With none: SKIP.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

HERE = Path(__file__).resolve().parent
WINDOWS = sys.platform == "win32"
CF_VERSION = "22.1.8"
GITLEAKS_VERSION = "8.28.0"
CF_REL = Path("tests/host/cf22/clang_format/data/bin") / ("clang-format.exe" if WINDOWS else "clang-format")
SDK_KINDS = ["lcd349", "cores3", "am18", "am175", "watcher", "tab5"]
SDK_EXTRA = {"lcd349": "", "cores3": "sdkconfig.defaults.cores3se", "am18": "sdkconfig.defaults.amoled18",
             "am175": "sdkconfig.defaults.amoled175", "watcher": "sdkconfig.defaults.watcher",
             "tab5": "sdkconfig.defaults.tab5"}
SIM_FRAMES = ["lcd349", "cores3", "watcher", "am175", "tab5"]
FUZZ_FRAMES = ["349", "s3", "wt"]
FUZZ_ARGS = ["3000", "1"]
TAIL = 25

PRINT_LOCK = threading.Lock()


class Ctx:
    """The state of one run."""

    def __init__(self, root):
        self.root = root
        self.logs = root / "tests" / "host" / "ci_local"
        self.clean = None            # the clean worktree of HEAD (a Path), when a step needs it
        self.clean_error = None
        self.results = {}            # name -> (status, seconds, note)
        self.env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")


class Result(Exception):
    """A step ends early with this status and note (SKIP or FAIL)."""

    def __init__(self, status, note, tail=None):
        super().__init__(note)
        self.status = status
        self.note = note
        self.tail = tail or []


# --- helpers -----------------------------------------------------------------------------------------------------

def run(ctx, name, cmd, cwd=None, env=None, timeout=3600, append=False):
    """Run a command, keep its output in the log of the step; return (exit code, the lines of the output)."""
    ctx.logs.mkdir(parents=True, exist_ok=True)
    log = ctx.logs / (name + ".log")
    try:
        r = subprocess.run(cmd, cwd=str(cwd or ctx.root), env=env or ctx.env, capture_output=True, timeout=timeout)
        out = (r.stdout + r.stderr).decode("utf-8", "replace")
        code = r.returncode
    except subprocess.TimeoutExpired as e:
        out = ((e.stdout or b"") + (e.stderr or b"")).decode("utf-8", "replace") + "\nTIMEOUT after %d s" % timeout
        code = 124
    except OSError as e:
        out = "cannot start the command: %s" % e
        code = 127
    with open(log, "a" if append else "w", encoding="utf-8", newline="\n") as f:
        f.write("$ %s\n%s\n" % (" ".join(str(c) for c in cmd[:6]), out))
    return code, out.splitlines()


def check(ctx, name, cmd, cwd=None, env=None, timeout=3600, append=False):
    """Run a command; a fault (an exit code other than 0) ends the step with FAIL. Returns the last line."""
    code, lines = run(ctx, name, cmd, cwd, env, timeout, append)
    if code:
        last = [ln for ln in lines if ln.strip()]
        raise Result("FAIL", "exit code %d" % code, last[-TAIL:])
    last = [ln for ln in lines if ln.strip()]
    return last[-1].strip() if last else ""


def py(*args):
    return [sys.executable] + list(args)


def git(ctx, *args, cwd=None):
    r = subprocess.run(["git"] + list(args), cwd=str(cwd or ctx.root), capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def main_worktree(root):
    """The folder of the main worktree of the repo (git rev-parse --git-common-dir), or None."""
    r = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=str(root),
                       capture_output=True, text=True)
    if r.returncode:
        return None
    common = Path(r.stdout.strip())
    return common.parent if common.name == ".git" else None


def find_clang_format(root):
    """The clang-format of CI (22.1.8): (path, version) or (None, the reason)."""
    cands = []
    if os.environ.get("CLANG_FORMAT"):
        cands.append(Path(os.environ["CLANG_FORMAT"]))
    cands.append(root / CF_REL)
    mw = main_worktree(root)
    if mw:
        cands.append(mw / CF_REL)
    w = shutil.which("clang-format")
    if w:
        cands.append(Path(w))
    seen = []
    for c in cands:
        if not c.is_file():
            continue
        r = subprocess.run([str(c), "--version"], capture_output=True, text=True)
        ver = r.stdout.strip().split("version")[-1].split()[0] if "version" in r.stdout else "?"
        if ver == CF_VERSION:
            return c, ver
        seen.append(ver)
    why = "no clang-format %s" % CF_VERSION
    if seen:
        why += " (found %s)" % ", ".join(seen)
    return None, why + ": set CLANG_FORMAT, or pip install clang-format==%s" % CF_VERSION


def git_bash():
    """The bash of Git for Windows (not the bash.exe of WSL in System32); on Linux and macOS: sh."""
    if not WINDOWS:
        return shutil.which("sh")
    if os.environ.get("CI_BASH"):
        return os.environ["CI_BASH"]
    g = shutil.which("git")
    if g:
        gp = Path(g).resolve()
        for base in (gp.parents[1], gp.parents[2]):
            for rel in ("bin/bash.exe", "usr/bin/bash.exe", "usr/bin/sh.exe"):
                if (base / rel).is_file():
                    return str(base / rel)
    return None


def wsl_ok():
    """True when WSL has gcc and python3 (the unit tests of CI on Linux)."""
    if not WINDOWS:
        return False
    w = shutil.which("wsl")
    if not w:
        return False
    try:
        r = subprocess.run([w, "-e", "sh", "-c", "gcc --version && python3 --version"], capture_output=True,
                           timeout=120)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return r.returncode == 0


def sim_tools(ctx):
    """The folder with the Zig compiler of the simulator, or None."""
    if os.environ.get("TFN_SIM_TOOLS"):
        return Path(os.environ["TFN_SIM_TOOLS"])
    cands = [ctx.root / "simulator" / ".tools", ctx.root.parent / ".tools"]
    mw = main_worktree(ctx.root)
    if mw:
        cands.append(mw / "simulator" / ".tools")
    for c in cands:
        if c.is_dir() and any(p.name.startswith("zig-") and (p / "zig.exe").is_file() for p in c.iterdir()):
            return c
    return None


def idf_env(ctx):
    """The environment of ESP-IDF 6.1 (its export script), as a dict; Result SKIP when there is no ESP-IDF."""
    cands = []
    if os.environ.get("TFN_IDF"):
        cands.append(Path(os.environ["TFN_IDF"]))
    if WINDOWS:
        cands.append(Path("C:/esp/v6.1/esp-idf"))
    if os.environ.get("IDF_PATH"):
        cands.append(Path(os.environ["IDF_PATH"]))
    for idf in cands:
        ver = idf / "tools" / "cmake" / "version.cmake"
        if not ver.is_file():
            continue
        text = ver.read_text(encoding="utf-8", errors="replace")
        if "IDF_VERSION_MAJOR 6" not in text or "IDF_VERSION_MINOR 1" not in text:
            continue
        if WINDOWS:
            # the activation script of the ESP-IDF installation manager (as dev.ps1), else export.ps1 of ESP-IDF
            act = Path(os.environ.get("IDF_PROFILE") or "C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1")
            if not act.is_file():
                act = idf / "export.ps1"
            sh = shutil.which("pwsh") or shutil.which("powershell")
            script = ("$env:IDF_PATH = $null; . '%s' *> $null; "
                      "[Environment]::GetEnvironmentVariables() | ConvertTo-Json -Compress" % act)
            cmd = [sh, "-NoProfile", "-NonInteractive", "-Command", script]
        else:
            cmd = ["bash", "-c", '. "%s/export.sh" > /dev/null 2>&1 && python3 -c '
                   '"import json, os; print(json.dumps(dict(os.environ)))"' % idf]
        r = subprocess.run(cmd, capture_output=True, timeout=600)
        if r.returncode:
            raise Result("FAIL", "the export script of ESP-IDF 6.1 failed",
                         r.stderr.decode("utf-8", "replace").splitlines()[-TAIL:])
        lines = [ln for ln in r.stdout.decode("utf-8", "replace").splitlines() if ln.startswith("{")]
        if not lines:
            raise Result("FAIL", "no environment from the activation script of ESP-IDF 6.1")
        env = {str(k): str(v) for k, v in json.loads(lines[-1]).items()}
        if Path(env.get("IDF_PATH", "")).resolve() != idf.resolve():
            raise Result("FAIL", "the activation script did not select ESP-IDF 6.1 (set IDF_PROFILE or TFN_IDF)")
        env.update(PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
        # idf.py stops with no work in an MSYS shell (Git Bash sets MSYSTEM): this run is a native Windows one
        for k in [k for k in env if k.upper().startswith("MSYSTEM")]:
            del env[k]
        return env
    raise Result("SKIP", "no ESP-IDF 6.1 (set TFN_IDF to its folder)")


def idf_python(env):
    """The Python of the ESP-IDF environment (its venv)."""
    p = env.get("IDF_PYTHON_ENV_PATH")
    if p:
        exe = Path(p) / ("Scripts/python.exe" if WINDOWS else "bin/python")
        if exe.is_file():
            return str(exe)
    return shutil.which("python", path=env.get("PATH")) or sys.executable


# --- the clean worktree -------------------------------------------------------------------------------------------

CLEAN_BASE = Path(tempfile.gettempdir()) / "tfn_ci_local"


def pid_alive(pid):
    """True when the process pid runs (a run of this script that uses its clean worktree now)."""
    if WINDOWS:
        import ctypes
        k = ctypes.windll.kernel32
        h = k.OpenProcess(0x1000, False, pid)      # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ok = k.GetExitCodeProcess(h, ctypes.byref(code))
        k.CloseHandle(h)
        return bool(ok) and code.value == 259     # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def clean_open(ctx):
    """Make a clean worktree of HEAD (detached) in the temp folder of the system: repo_<the process id>."""
    CLEAN_BASE.mkdir(exist_ok=True)
    git(ctx, "worktree", "prune")
    # the worktree of an earlier run that stopped: remove it (git, not a shell delete)
    code, out = git(ctx, "worktree", "list", "--porcelain")
    for ln in out.splitlines():
        if ln.startswith("worktree "):
            p = Path(ln[9:].strip())
            try:
                if p.resolve().parent != CLEAN_BASE.resolve() or not p.name.startswith("repo_"):
                    continue
                pid = int(p.name.split("_")[1])
            except (OSError, ValueError, IndexError):
                continue
            if not pid_alive(pid):
                git(ctx, "worktree", "remove", "--force", str(p))
    path = CLEAN_BASE / ("repo_%d" % os.getpid())
    n = 0
    while path.exists():
        n += 1
        path = CLEAN_BASE / ("repo_%d_%d" % (os.getpid(), n))
    code, out = git(ctx, "worktree", "add", "--detach", str(path), "HEAD")
    if code:
        ctx.clean_error = "git worktree add failed: " + out.splitlines()[-1] if out else "git worktree add failed"
        return
    ctx.clean = path


def clean_close(ctx, keep):
    if not ctx.clean:
        return
    if keep:
        say("NOTE  the clean worktree stays: %%TEMP%%\\tfn_ci_local\\%s (remove it: git worktree remove --force)"
            % ctx.clean.name)
        return
    code, out = git(ctx, "worktree", "remove", "--force", str(ctx.clean))
    if code:
        say("NOTE  git worktree remove failed (%s); git worktree prune removes the entry later"
            % (out.splitlines()[-1] if out else "?"))


def need_clean(ctx):
    if ctx.clean_error:
        raise Result("FAIL", ctx.clean_error)
    if not ctx.clean:
        raise Result("FAIL", "no clean worktree")
    return ctx.clean


# --- the steps ----------------------------------------------------------------------------------------------------

def s_format(ctx):
    cf, ver = find_clang_format(ctx.root)
    if not cf:
        raise Result("FAIL", ver)
    env = dict(ctx.env, CLANG_FORMAT=str(cf))
    return check(ctx, "format", py("tests/style/check_format.py"), env=env)


def s_apps(ctx):
    check(ctx, "apps", py("main/apps_gen.py", "--check"))
    check(ctx, "apps", py("main/apps_gen.py", "--all"), append=True)
    code, out = git(ctx, "diff", "--exit-code", "--stat", "--", "main/apps_all.inc")
    if code:
        raise Result("FAIL", "main/apps_all.inc is not current (python main/apps_gen.py --all, then commit it)",
                     out.splitlines()[-TAIL:])
    return check(ctx, "apps", py("sdk/tools/app_json.py", "--check"), append=True)


def s_contracts(ctx):
    return check(ctx, "contracts", py("tests/contracts/check_all.py"))


def s_unit(ctx):
    if not wsl_ok():
        if not WINDOWS and shutil.which("gcc"):
            return check(ctx, "unit", py("tests/unit/run.py"), env=dict(ctx.env, CC="gcc"))
        raise Result("SKIP", "CI uses gcc on Linux: no WSL with gcc and python3 (wsl --install, then "
                             "sudo apt install gcc python3 openssl)")
    cmd = ["wsl", "-e", "bash", "-c", 'cd "$(wslpath -a "$1")" && CC=gcc python3 tests/unit/run.py', "_",
           str(ctx.root)]
    return check(ctx, "unit", cmd) + " [WSL gcc]"


def s_comments(ctx):
    return check(ctx, "comments", py("tests/style/check_comments.py"))


def s_board_if(ctx):
    return check(ctx, "board_if", py("tests/style/check_board_if.py"))


def s_api(ctx):
    return check(ctx, "api", py("tests/style/check_api.py"))


def s_api_doc(ctx):
    return check(ctx, "api_doc", py("sdk/tools/gen_api_doc.py", "--check"))


def s_fsize(ctx):
    return check(ctx, "fsize", py("tests/style/check_fsize.py"))


def s_boards(ctx):
    return check(ctx, "boards", py("tests/boards/check_boards.py"))


def s_hil(ctx):
    return check(ctx, "hil", py("tests/hil/test_hil.py"))


def s_licences(ctx):
    return check(ctx, "licences", py("tests/licence/check_notice.py"))


def s_flasher(ctx):
    node = shutil.which("node")
    if not node:
        raise Result("SKIP", "no node (install Node.js)")
    return check(ctx, "flasher", [node, "site/test/flasher.test.js"])


def s_same_code(ctx):
    return check(ctx, "same_code", py("tests/refactor/test_same_code.py"))


def tracked_tree(ctx, dest):
    """Copy the files that git tracks (the paths of the index: also a new file after "git add") into dest, with their
    content of the working tree. These are the files of a checkout of CI, with the changes that are not committed. The
    files that git ignores and the untracked files stay out (for example the TEST keys of a developer in an ignored
    work folder: a checkout of CI does not have them). A tracked file that is not in the working tree gets its content
    of the index. Returns the count of the files."""
    r = subprocess.run(["git", "ls-files", "-z"], cwd=str(ctx.root), capture_output=True)
    if r.returncode:
        raise Result("FAIL", "git ls-files failed", r.stderr.decode("utf-8", "replace").splitlines()[-TAIL:])
    n = 0
    for rel in [x for x in r.stdout.decode("utf-8").split("\0") if x]:
        src = ctx.root / rel
        dst = dest / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.is_file():
            shutil.copyfile(src, dst)
        else:
            blob = subprocess.run(["git", "show", ":" + rel], cwd=str(ctx.root), capture_output=True)
            if blob.returncode:
                raise Result("FAIL", "%s: no file in the working tree and no content in the index" % rel)
            dst.write_bytes(blob.stdout)
        n += 1
    return n


def gitleaks_tree(ctx, dest):
    """The files of the step gitleaks: the copy of tracked_tree in dest. A copy with no file, or with no .gitleaks.toml
    (the rules of the scan), ends the step with FAIL: a scan of no file, or with no rules of this repo, says OK for any
    tree. Returns the count of the files."""
    n = tracked_tree(ctx, dest)
    if n == 0:
        raise Result("FAIL", "the copy of the tracked files is empty (git ls-files gave no file)")
    if not (dest / ".gitleaks.toml").is_file():
        raise Result("FAIL", "the copy of the tracked files has no .gitleaks.toml (git must track it)")
    return n


def gitleaks_exe():
    """The program gitleaks (GITLEAKS, else the PATH) as an absolute path, or "" when there is none. The dir scan runs
    in a temporary folder: a relative path (GITLEAKS=tools/gitleaks) must not change its meaning there."""
    found = shutil.which(os.environ.get("GITLEAKS") or "gitleaks")
    return os.path.abspath(found) if found else ""


def s_gitleaks(ctx):
    gl = gitleaks_exe()
    if not gl and os.environ.get("GITLEAKS"):
        raise Result("FAIL", "GITLEAKS=%s is not a program" % os.environ["GITLEAKS"])
    if not gl:
        raise Result("SKIP", "no gitleaks: winget install Gitleaks.Gitleaks, or get v%s from "
                             "github.com/gitleaks/gitleaks/releases and set GITLEAKS" % GITLEAKS_VERSION)
    r = subprocess.run([gl, "version"], capture_output=True, text=True)
    ver = r.stdout.strip().lstrip("v")
    # the files: a copy of the tracked files (as a checkout of CI), with the rules of .gitleaks.toml of the copy (the
    # working tree also has the git-ignored work folders and builds of the developer: they stay out)
    tmp = Path(tempfile.mkdtemp(prefix="tfn_gitleaks_"))
    try:
        n = gitleaks_tree(ctx, tmp)
        check(ctx, "gitleaks", [gl, "dir", ".", "--config", ".gitleaks.toml", "--redact", "--no-banner"], cwd=tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    # the commits of this branch that main does not have (as CI for a new branch); on main: the last commit
    code, base = git(ctx, "merge-base", "origin/main", "HEAD")
    code2, head = git(ctx, "rev-parse", "HEAD")
    rng = "%s..HEAD" % base if code == 0 and base != head else "HEAD~1..HEAD"
    check(ctx, "gitleaks", [gl, "git", ".", "--log-opts=" + rng, "--config", "tests/secrets/gitleaks-new.toml",
                            "--redact", "--no-banner"], append=True)
    note = "%d tracked files + commits %s" % (n, rng)
    if ver != GITLEAKS_VERSION:
        note += " (gitleaks %s; CI uses %s)" % (ver, GITLEAKS_VERSION)
    return note


def s_fuzz(ctx):
    sh = git_bash()
    if not sh:
        raise Result("SKIP", "no bash of Git for Windows (set CI_BASH)")
    cc = os.environ.get("CC") or "clang"
    if not shutil.which(cc.split()[-1]):
        raise Result("SKIP", "no clang on PATH (llvm-mingw on Windows)")
    env = dict(ctx.env, CC=cc, JOBS=str(os.cpu_count() or 4))
    check(ctx, "fuzz", [sh, "tests/host/build.sh"], env=env, timeout=7200)
    env = dict(ctx.env, ASAN_OPTIONS="detect_leaks=0")
    ext = ".exe"
    hdir = ctx.root / "tests" / "host"

    def one(b):
        return b, run(ctx, "fuzz_" + b, [str(hdir / ("fuzz%s%s" % (b, ext)))] + FUZZ_ARGS, cwd=hdir, env=env,
                      timeout=7200)

    bad = []
    tail = []
    with concurrent.futures.ThreadPoolExecutor(len(FUZZ_FRAMES)) as ex:
        for b, (code, lines) in ex.map(one, FUZZ_FRAMES):
            last = [ln for ln in lines if ln.strip()]
            if code or not last or last[-1].strip() != "all ok":
                bad.append(b)
                tail += ["fuzz%s: %s" % (b, ln) for ln in last[-8:]]
    if bad:
        raise Result("FAIL", "fuzz %s (logs tests/host/ci_local/fuzz_<frame>.log)" % ", ".join(bad), tail)
    return "all ok on %s (%s frames, seed %s)" % (", ".join(FUZZ_FRAMES), FUZZ_ARGS[0], FUZZ_ARGS[1])


def sim_env(ctx):
    env = dict(ctx.env)
    if WINDOWS and not env.get("TFN_SIM_CC"):
        tools = sim_tools(ctx)
        if not tools and not shutil.which("zig"):
            raise Result("FAIL", "no Zig for the simulator: python simulator/build.py --setup (or set TFN_SIM_TOOLS)")
        if tools:
            env["TFN_SIM_TOOLS"] = str(tools)
    elif not WINDOWS:
        env.setdefault("TFN_SIM_CC", "clang")
    return env


def s_sim_build(ctx):
    clean = need_clean(ctx)
    env = sim_env(ctx)
    for i, f in enumerate(SIM_FRAMES):
        check(ctx, "sim_build", py("simulator/build.py", "--frame", f), cwd=clean, env=env, append=i > 0)
    return "frames " + ", ".join(SIM_FRAMES)


def s_golden(ctx):
    clean = need_clean(ctx)
    if ctx.results.get("sim_build", ("OK",))[0] != "OK":
        raise Result("FAIL", "the simulator build failed")
    return check(ctx, "golden", py("tests/golden/run.py", "--no-build"), cwd=clean, env=sim_env(ctx))


def s_smoke(ctx):
    clean = need_clean(ctx)
    if ctx.results.get("sim_build", ("OK",))[0] != "OK":
        raise Result("FAIL", "the simulator build failed")
    return check(ctx, "smoke", py("simulator/smoke.py", "--no-build", "--headless", "--seconds", "1"), cwd=clean,
                 env=sim_env(ctx))


def s_sound(ctx):
    clean = need_clean(ctx)
    if ctx.results.get("sim_build", ("OK",))[0] != "OK":
        raise Result("FAIL", "the simulator build failed")
    return check(ctx, "sound", py("simulator/sound_check.py", "--no-build"), cwd=clean, env=sim_env(ctx), timeout=900)


def s_sdk_pack(ctx):
    clean = need_clean(ctx)
    env = idf_env(ctx)
    env["IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS"] = "1"
    pyx = idf_python(env)
    idf_py = str(Path(env["IDF_PATH"]) / "tools" / "idf.py")
    for i, kind in enumerate(SDK_KINDS):
        extra = SDK_EXTRA[kind]
        defaults = "sdkconfig.defaults" + (";" + extra if extra else "")
        b = clean / "build_ci" / kind
        b.mkdir(parents=True, exist_ok=True)
        cmd = [pyx, idf_py, "-B", str(b), "-D", "SDKCONFIG=%s" % (b / "sdkconfig").as_posix(),
               "-D", "SDKCONFIG_DEFAULTS=%s" % defaults]
        if kind == "tab5":
            cmd += ["-D", "IDF_TARGET=esp32p4"]
        check(ctx, "sdk_pack", cmd + ["reconfigure"], cwd=clean, env=env, append=i > 0, timeout=1800)
        if not (b / "compile_commands.json").is_file():
            raise Result("FAIL", "the configure of %s made no compile_commands.json (tests/host/ci_local/sdk_pack.log)"
                         % kind)
    out = clean / "sdk_out" / "tfn-sdk"
    check(ctx, "sdk_pack", [pyx, "sdk/tools/sdk_pack.py", "--build-root", "build_ci", "--kinds", ",".join(SDK_KINDS),
                            "--out", str(out), "--force"], cwd=clean, env=env, append=True)
    z = clean / "sdk_out" / "tfn-sdk.zip"
    if not z.is_file():
        raise Result("FAIL", "no sdk_out/tfn-sdk.zip")
    return "tfn-sdk.zip %d KB, kinds %s" % (z.stat().st_size // 1024, ",".join(SDK_KINDS))


def s_sdk_template(ctx):
    clean = need_clean(ctx)
    if ctx.results.get("sdk_pack", ("OK",))[0] != "OK":
        raise Result("FAIL", "no SDK package (the step sdk_pack failed or did not run)")
    z = clean / "sdk_out" / "tfn-sdk.zip"
    if not z.is_file():
        raise Result("FAIL", "no SDK package: run the step sdk_pack too")
    env = idf_env(ctx)
    pyx = idf_python(env)
    # as the template job of CI: only the folder of the template and the zip, out of the repo
    # (one folder for all runs: each run writes over its files, and the check below takes only the new app.elf)
    work = CLEAN_BASE / "template"
    app = work / "my_app"
    shutil.copytree(clean / "sdk" / "templates" / "hello", app, dirs_exist_ok=True)
    unz = work / "sdk_zip"
    with zipfile.ZipFile(z) as zf:
        zf.extractall(unz)
    found = sorted(unz.glob("*/sdk.json")) + sorted(unz.glob("sdk.json"))
    if not found:
        raise Result("FAIL", "no sdk.json in the zip")
    t0 = time.time()
    # (build.sh of the template: python3 <sdk>/tools/tfn_app.py build <app> --sdk <sdk>)
    check(ctx, "sdk_template", [pyx, str(found[0].parent / "tools" / "tfn_app.py"), "build", str(app), "--sdk",
                                str(found[0].parent)], cwd=app, env=env, timeout=1800)
    elfs = [p for p in sorted((app / "out").glob("*/*/app.elf")) if p.stat().st_mtime >= t0 - 2]
    if not elfs:
        raise Result("FAIL", "no new out/*/*/app.elf")
    return "%d app.elf (%s)" % (len(elfs), ", ".join("%s %d B" % (p.parent.name, p.stat().st_size) for p in elfs))


# name, function, quick, lane (steps of one lane run in this order; the lanes run at the same time), clean worktree
STEPS = [
    ("format", s_format, True, None, False),
    ("apps", s_apps, True, None, False),
    ("contracts", s_contracts, True, None, False),
    ("unit", s_unit, True, None, False),
    ("comments", s_comments, True, None, False),
    ("board_if", s_board_if, True, None, False),
    ("api", s_api, True, None, False),
    ("api_doc", s_api_doc, True, None, False),
    ("fsize", s_fsize, True, None, False),
    ("boards", s_boards, True, None, False),
    ("hil", s_hil, True, None, False),
    ("licences", s_licences, False, None, False),
    ("flasher", s_flasher, False, None, False),
    ("same_code", s_same_code, False, None, False),
    ("gitleaks", s_gitleaks, False, None, False),
    ("fuzz", s_fuzz, False, "host", False),
    ("sim_build", s_sim_build, False, "host", True),
    ("golden", s_golden, False, "host", True),
    ("smoke", s_smoke, False, "host", True),
    ("sound", s_sound, False, "host", True),
    ("sdk_pack", s_sdk_pack, False, "sdk", True),
    ("sdk_template", s_sdk_template, False, "sdk", True),
]
NAMES = [s[0] for s in STEPS]


def say(text):
    with PRINT_LOCK:
        print(text.encode("ascii", "backslashreplace").decode("ascii"), flush=True)


def do_step(ctx, step):
    name, fn = step[0], step[1]
    t0 = time.monotonic()
    tail = []
    try:
        note = fn(ctx) or ""
        status = "OK"
    except Result as r:
        status, note, tail = r.status, r.note, r.tail
    except Exception as e:      # a fault of this script: show it as a FAIL of the step
        status, note = "FAIL", "%s: %s" % (type(e).__name__, e)
    dt = time.monotonic() - t0
    ctx.results[name] = (status, dt, note)
    with PRINT_LOCK:
        line = "%-4s  %-12s %7.1f s  %s" % (status, name, dt, note)
        print(line[:200].encode("ascii", "backslashreplace").decode("ascii"), flush=True)
        for ln in tail:
            print(("      | " + ln)[:200].encode("ascii", "backslashreplace").decode("ascii"), flush=True)


def run_lane(ctx, steps):
    for s in steps:
        do_step(ctx, s)


def select(args):
    names = NAMES if not args.quick else [s[0] for s in STEPS if s[2]]
    if args.only:
        only = [n.strip() for n in args.only.split(",") if n.strip()]
        bad = [n for n in only if n not in NAMES]
        if bad:
            sys.exit("unknown step: %s (the steps: %s)" % (", ".join(bad), ", ".join(NAMES)))
        names = [n for n in NAMES if n in only]
        # golden, smoke and sound need the simulator build of the same clean worktree
        if ("golden" in names or "smoke" in names or "sound" in names) and "sim_build" not in names:
            names = [n for n in NAMES if n in names or n == "sim_build"]
        if "sdk_template" in names and "sdk_pack" not in names:
            names = [n for n in NAMES if n in names or n == "sdk_pack"]
    if args.skip:
        skip = [n.strip() for n in args.skip.split(",") if n.strip()]
        bad = [n for n in skip if n not in NAMES]
        if bad:
            sys.exit("unknown step: %s (the steps: %s)" % (", ".join(bad), ", ".join(NAMES)))
        names = [n for n in names if n not in skip]
    return [s for s in STEPS if s[0] in names]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--quick", action="store_true", help="the fast steps only (before each merge)")
    p.add_argument("--only", help="only these steps (names with commas)")
    p.add_argument("--skip", help="not these steps (names with commas)")
    p.add_argument("--root", help="the root of the repo (default: the folder above tests/)")
    p.add_argument("--jobs", type=int, default=8, help="light steps at the same time (1 = one step at a time)")
    p.add_argument("--keep", action="store_true", help="keep the clean worktree (to look at tests/golden/out)")
    p.add_argument("--list", action="store_true", help="the names of the steps")
    args = p.parse_args()
    if args.list:
        for s in STEPS:
            print("%-12s %s%s" % (s[0], "quick" if s[2] else "full ", "  (clean worktree of HEAD)" if s[4] else ""))
        return 0
    root = Path(args.root).resolve() if args.root else HERE.parent
    if not (root / "main" / "apps.json").is_file():
        sys.exit("not the root of the TFN OS repo: %s" % root)
    ctx = Ctx(root)
    steps = select(args)
    if not steps:
        sys.exit("no step to run")
    code, head = git(ctx, "rev-parse", "--short", "HEAD")
    code, dirty = git(ctx, "status", "--porcelain", "--untracked-files=no")
    say("CI LOCAL: %s run, %d steps, HEAD %s%s" % ("quick" if args.quick else "full" if not args.only else "partial",
                                                  len(steps), head, " (+ changes not committed)" if dirty else ""))
    t0 = time.monotonic()
    if any(s[4] for s in steps):
        clean_open(ctx)
    try:
        lanes = {}
        light = []
        for s in steps:
            if s[3]:
                lanes.setdefault(s[3], []).append(s)
            else:
                light.append(s)
        if args.jobs <= 1:
            for s in light:
                do_step(ctx, s)
            for lane in lanes.values():
                run_lane(ctx, lane)
        else:
            with concurrent.futures.ThreadPoolExecutor(len(lanes) + 1) as lex, \
                    concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
                futs = [lex.submit(run_lane, ctx, lane) for lane in lanes.values()]
                futs += [pool.submit(do_step, ctx, s) for s in light]
                for f in futs:
                    f.result()
    finally:
        clean_close(ctx, args.keep)
    ok = [s[0] for s in steps if ctx.results.get(s[0], ("FAIL",))[0] == "OK"]
    skipped = [s[0] for s in steps if ctx.results.get(s[0], ("FAIL",))[0] == "SKIP"]
    failed = [s[0] for s in steps if ctx.results.get(s[0], ("FAIL",))[0] == "FAIL"]
    if skipped:
        say("SKIPPED: %s" % ", ".join(skipped))
    say("time %.0f s; the logs: tests/host/ci_local/<step>.log" % (time.monotonic() - t0))
    if failed:
        say("CI LOCAL: FAIL (%s)" % ", ".join(failed))
        return 1
    say("CI LOCAL: OK (%d steps)" % len(ok))
    return 0


if __name__ == "__main__":
    sys.exit(main())
