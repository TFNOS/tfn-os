"""Offline authorization tests; no board, credentials or configuration changes."""

import argparse
from pathlib import Path
import subprocess
import tempfile

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--cc", nargs="+", default=["clang"])
args = ap.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="device-guard-") as scratch:
    exe = Path(scratch) / "test.exe"
    subprocess.run(
        args.cc
        + [
            "-std=c11",
            "-O1",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=undefined",
            str(root / "tests/device_guard/test_core.c"),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
