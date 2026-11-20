# Full ESP GCC compile (-c, -Werror -Wextra, the real flags) of a source file for each board kind that has a build (a
# name of main/, libs/<module>/ or apps/<app>/: apps_gen.source_path finds it).
# Usage: python espcheck.py app_pour.c   (the .o files go into this folder)
# The flags come from build_dev/<kind>/compile_commands.json of this project. The environment variable ESPCHECK_BUILDS
# gives another project folder for the builds (for example a worktree with a newer DEV build).
# A build can compile a copy of the source (the DEV build of the Windows wrapper uses build_dev/work). In the flags,
# the tool puts the source folders of this project (SRC_DIRS: main/, apps/, libs/ ...) in the place of the same folders
# of the build (CMAKE_HOME_DIRECTORY of its CMake cache): the file and the headers of the project come from this
# project, and never mix with the headers of the copy. A folder that this project does not have stays that of the
# build. All other paths stay those of the build: the components of the component manager (managed_components/, with
# the versions of the build) and the files that the build made (build_dev/<kind>/config/sdkconfig.h).
# The source folders of the copy come last (-idirafter): a header that only the copy has comes from there. Only a
# header that git ignores in this project is correct there (a private header such as main/tfn_local.h, which the config
# of the build can need): a NOTE names it. Each other header from the copy (a header that this project removed or
# renamed, and the copy still has) is a fault: the tool names it and exits with 1.
import json, os, re, shlex, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(os.path.dirname(HERE))  # the project folder (this script is in tests/host/)
BD = os.environ.get("ESPCHECK_BUILDS") or PROJ
KINDS = ("lcd349", "cores3", "am18", "watcher", "tab5", "am175")
# the source folders of the project: the tool takes them from this project
SRC_DIRS = ("main", "apps", "libs", "components", "third_party", "boards")
# the source files of the build whose compile command the tool takes as the template (the first one that the build has)
TEMPLATES = ("/app_pong.c", "/app_stack.c", "/app_settings.c")


def source_home(build):
    """The source folder of a build (CMAKE_HOME_DIRECTORY of its CMake cache, with slashes), or ''."""
    try:
        with open(os.path.join(BD, build, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            for ln in f:
                if ln.startswith("CMAKE_HOME_DIRECTORY:"):
                    return ln.split("=", 1)[1].strip().replace("\\", "/").rstrip("/")
    except OSError:
        pass
    return ""


def norm(path):
    """A path with slashes and no slash at the end, in lower case on Windows (for a compare)."""
    path = path.replace("\\", "/").rstrip("/")
    return path.lower() if os.name == "nt" else path


def to_project(args, home, keep=()):
    """The arguments with each path in a source folder of the build (home/<a folder of SRC_DIRS>/...) replaced by the
    same path in this project. A path stays as it is when this project does not have it, or when it is in a folder of
    keep (the folders of the build). The other paths stay as they are (home/managed_components/: the components of the
    build). The case of a path does not count on Windows. Returns (the arguments, the include folders of the build
    that this project replaced)."""
    if not home:
        return args, []
    proj = PROJ.replace("\\", "/").rstrip("/")
    rx = re.compile(re.escape(home) + "/(?:" + "|".join(SRC_DIRS) + ")(?=/|$)", re.I if os.name == "nt" else 0)
    kept = [norm(k) + "/" for k in keep if k]
    out, moved = [], []
    for a in args:
        m = rx.search(a)
        if m:
            path = a[m.start():]
            new = proj + path[len(home):]
            if os.path.exists(new) and not any((norm(path) + "/").startswith(k) for k in kept):
                if a.startswith("-I") and norm(new) != norm(path):
                    moved.append(path)
                a = a[:m.start()] + new
        out.append(a)
    return out, moved


def from_copy(dep, moved, home):
    """The headers of the dependency file dep (gcc -MD) that came from an include folder of the build copy (moved):
    the paths from home, sorted."""
    try:
        with open(dep, encoding="utf-8", errors="replace") as f:
            text = f.read().replace("\\\n", " ")
    except OSError:
        return []
    names = [t.replace("\\ ", " ") for t in re.split(r"(?<!\\)\s+", text.split(": ", 1)[-1]) if t]
    dirs = [norm(m) + "/" for m in moved]
    return sorted({n.replace("\\", "/")[len(home) + 1:] for n in names if any(norm(n).startswith(d) for d in dirs)})


def not_ignored(paths):
    """The paths (from the project folder) that git does not ignore in this project (git check-ignore). When git
    cannot tell, each path."""
    if not paths:
        return []
    try:
        r = subprocess.run(["git", "-C", PROJ, "check-ignore", "-z", "--stdin"], capture_output=True,
                           input="\0".join(paths).encode("utf-8"))
    except OSError:
        return list(paths)
    if r.returncode not in (0, 1):
        return list(paths)
    ignored = {n for n in r.stdout.decode("utf-8", "replace").split("\0") if n}
    return [p for p in paths if p not in ignored]


def template_entry(db):
    """The entry of compile_commands.json (db) of the first file of TEMPLATES that the build compiles, or None."""
    for t in TEMPLATES:
        for x in db:
            if x["file"].replace("\\", "/").endswith(t):
                return x
    return None


def main(argv):
    name = os.path.basename(argv[1])   # (any path, or only the name)
    sys.path.insert(0, os.path.join(PROJ, "main"))
    import apps_gen                    # noqa: E402
    src = apps_gen.source_path(name).replace("\\", "/")
    ok = True
    builds = [b for b in ("build_dev/" + k for k in KINDS)
              if os.path.exists(os.path.join(BD, b, "compile_commands.json"))]
    if not builds:
        print("no build_dev/<kind>/compile_commands.json in %s: make a DEV build, or set ESPCHECK_BUILDS to the "
              "project folder of a build" % BD)
        return 2
    for build in builds:
        db = json.load(open(os.path.join(BD, build, "compile_commands.json")))
        e = template_entry(db)
        if e is None:
            print("STOP: %s/compile_commands.json has no compile command of %s (the template of the flags): make a "
                  "full DEV build of this kind" % (build, ", ".join(t[1:] for t in TEMPLATES)))
            return 2
        TPL = os.path.basename(e["file"].replace("\\", "/"))
        # (the path backslashes become slashes; an escaped quote of a -D value stays: mbedTLS needs it)
        home = source_home(build)
        args, moved = to_project(shlex.split(re.sub(r"\\(?!\")", "/", e["command"])), home,
                                 (os.path.join(BD, build), e["directory"]))
        comp, rest, skip = args[0], [], False
        for a in args[1:]:
            if skip:
                skip = False
                continue
            if a in ("-o", "-MF", "-MT"):
                skip = True
                continue
            if a in ("-c", "-MD"):
                continue
            if a.endswith(TPL):
                a = src
            rest.append(a)
        out = os.path.join(HERE, "%s_%s.o" % (name, build.replace("/", "_"))).replace("\\", "/")
        # (libs/ and third_party/ themselves: an app includes a lib in the SDK form "tfdraw/tfdraw.h"; an older build
        # does not give them)
        rest += ["-I" + os.path.join(PROJ, "libs").replace("\\", "/"), "-I" + os.path.join(PROJ, "third_party").replace("\\", "/")]
        rest += ["-idirafter" + p for p in moved]
        dep = os.path.join(HERE, "dep_%s_%s.d" % (name, build.replace("/", "_"))).replace("\\", "/")
        rest += ["-Werror", "-Wextra", "-MD", "-MF", dep, "-c", "-o", out]
        rsp = os.path.join(HERE, "rsp_%s_%s.txt" % (name, build.replace("/", "_")))      # (one file for each source: parallel runs)
        with open(rsp, "w") as f:
            for a in rest:
                f.write('"%s"\n' % a.replace('"', '\\"'))
        r = subprocess.run([comp, "@" + rsp], cwd=e["directory"], capture_output=True, text=True)
        print(build, "exit", r.returncode)
        copy = from_copy(dep, moved, home)
        bad = not_ignored(copy)
        if bad:
            ok = False
            print("FAULT: %s takes from the build copy a header that this project does not have and git does not ignore "
                  "(removed or renamed?): %s" % (build, ", ".join(bad)))
        good = [c for c in copy if c not in bad]
        if good:
            print("NOTE: %s takes from the build copy (git ignores it in this project): %s" % (build, ", ".join(good)))
        if os.path.exists(dep):
            try:
                os.remove(dep)
            except OSError:
                pass  # (a file sync tool or a virus scanner can hold the file for a moment)
        if r.returncode:
            ok = False
            print((r.stdout + r.stderr)[-3000:])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
