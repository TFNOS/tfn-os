#!/bin/sh
# Build the all-app host fuzz for the 3.49 (fuzz349.exe), the CoreS3 (fuzzs3.exe) and the round Watcher (fuzzwt.exe).
# The apps and their sources come from main/apps.json: main/apps_gen.py --host writes the list of ALL apps (also the
# apps that are off in the firmware, so that they keep compiling) into apps_gen/ (apps_gen.h, apps_list.inc for
# fuzz.c and app_menu.c, srcs.txt, flags.txt). The files of host_skip in apps.json are not compiled (stubs.c has
# their functions). BOOKS is in only when its files exist (not in the public repo). All apps are always in
# (WITH_NEW and WITH_SIDEB are
# not necessary). A test that has its own copy of a main/ file: NO_SRC="app_menu" (a regex of
# the main/ file names to leave out).
# It runs on Windows (MSYS sh + clang) and on Linux and macOS (clang or gcc). The output names end in .exe on each OS.
#   CC=<compiler>   the compiler (default clang). For a cache: CC="ccache clang".
#   JOBS=<n>        compile each source to an object in obj_<board>/ with n compilers at the same time, then link.
#                   With no JOBS: one compiler call for each board.
#   FRAMES="349 s3 wt"  the boards to build (default these three). More frames: a175 (fuzza175.exe), t5
#                   (fuzzt5.exe) and am18 (fuzzam18.exe), for example FRAMES="a175 t5 am18".
#   NOMEM=1         allocation failures (nomem.c, common/host_compat.h): run with FUZZ_NOMEM_NTH=<n> (the n-th big
#                   allocation fails) or FUZZ_NOMEM=<per mille>, and FUZZ_ONLY=<app name>. The default build has none.
cd "$(dirname "$0")"
CC="${CC:-clang}"
FRAMES="${FRAMES:-349 s3 wt}"
M=../../main
G=apps_gen
F="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-field-initializers"
# common/host_compat.h: the C library differences of Linux and macOS (see the file)
F="$F -include common/host_compat.h"
W=""
PY=python
command -v python > /dev/null 2>&1 || PY=python3
$PY $M/apps_gen.py --host $G --prefix $M/ $W || exit 1
LIST="$(cat $G/srcs.txt)"
[ -n "$NO_SRC" ] && LIST="$(echo $LIST | tr ' ' '\n' | grep -v -E "/($NO_SRC)\.(c|cc)\$" | tr '\n' ' ')"   # (the sources are in main/, libs/<m>/, apps/<app>/)
SRC="fuzz.c stubs.c common/miniz.c $LIST"
[ -n "$NOMEM" ] && SRC="nomem.c $SRC" && F="$F -DHOST_NOMEM"
F="$F $(cat $G/flags.txt) -DBK_HOST_DIR=\"../../main/books\""
I="$(cat $G/incs.txt)"   # the source folders (main/, libs/<m>/, apps/<app>/), after the board folder

# build_frame <board> <output> [more flags]: one fuzz binary for the board folder inc<board>/
build_frame()
{
    b=$1
    out=$2
    shift 2
    if [ -z "$JOBS" ]; then
        $CC $F "$@" -I inc$b -I common -I $M -I $G $I $SRC -o $out -lm
        return $?
    fi
    o=obj_$b
    mkdir -p $o
    # the flags go into a response file (one flag on a line; a quote gets a backslash)
    for a in $F "$@" -I inc$b -I common -I $M -I $G $I; do
        printf '%s\n' "$a" | sed 's/"/\\"/g'
    done > $o/flags.rsp
    printf '%s\n' $SRC | CC="$CC" O=$o xargs -P "$JOBS" -n 1 sh -c \
        'obj="$O/$(printf "%s" "$1" | tr "/." "__").o"; $CC @$O/flags.rsp -c "$1" -o "$obj"' sh || return 1
    objs=""
    for s in $SRC; do
        objs="$objs $o/$(printf '%s' "$s" | tr '/.' '__').o"
    done
    $CC -fsanitize=address,undefined $objs -o $out -lm
}

for b in $FRAMES; do
    case $b in
    349) build_frame 349 fuzz349.exe || exit 1 ;;
    s3) build_frame s3 fuzzs3.exe || exit 1 ;;
    # the round Watcher (the round apps get the 412 x 412 frame and the wheel)
    wt) build_frame wt fuzzwt.exe || exit 1 ;;
    # more frames (not in the default list): the round AMOLED-1.75C (the 466 x 466 frame of APP_ROUND_HI, no wheel),
    # the Tab5 (the tablet UI) and the AMOLED-1.8
    a175) build_frame a175 fuzza175.exe || exit 1 ;;
    t5) build_frame t5 fuzzt5.exe || exit 1 ;;
    am18) build_frame am18 fuzzam18.exe || exit 1 ;;
    *) echo "unknown board $b (349, s3, wt, a175, t5, am18)"; exit 1 ;;
    esac
done
echo built
