#!/bin/sh
# Build the app of this folder with the TFN OS SDK package only (no file of the TFN OS repo).
#
#   sh build.sh <tfn-sdk.zip or the package folder> [the kinds, for example lcd349,cores3]
#
# ESP-IDF must be installed at the version of the package (IDF_PATH), with the compilers of the kinds
# (IDF_TOOLS_PATH or ~/.espressif). With no kinds, the build uses "kinds" of app.json, else all kinds of the package.
# The result: out/<id>/<kind>/ (app.elf, icon.bin, manifest.txt) for each kind.
set -eu
# python3, else python (on Windows, "python3" can be only the Microsoft Store alias, which runs no Python)
py=python3
"$py" -c "import sys" >/dev/null 2>&1 || py=python
here=$(cd "$(dirname "$0")" && pwd)
sdk=${1:?give the SDK package: sh build.sh <tfn-sdk.zip or folder> [kinds]}
kinds=${2:-}
if [ -f "$sdk" ]; then
    dir=$(mktemp -d "${TMPDIR:-/tmp}/tfn-sdk.XXXXXX")
    "$py" -m zipfile -e "$sdk" "$dir"
    # (the zip holds one folder with sdk.json: tfn-sdk/)
    found=$(find "$dir" -maxdepth 2 -name sdk.json | sort | head -n 1)
    sdk=$(dirname "${found:-$dir/tfn-sdk/sdk.json}")
fi
[ -f "$sdk/sdk.json" ] || { echo "no SDK package in $sdk (no sdk.json)"; exit 1; }
[ -n "${IDF_PATH:-}" ] || { echo "IDF_PATH is not set: install ESP-IDF and run its export script"; exit 1; }
if [ -n "$kinds" ]; then
    "$py" "$sdk/tools/tfn_app.py" build "$here" --sdk "$sdk" --kinds "$kinds"
else
    "$py" "$sdk/tools/tfn_app.py" build "$here" --sdk "$sdk"
fi
# the packages of the build
find "$here/out" -type f | sort
