#!/bin/sh
# TFN OS SDK: build the host tool iconshot for each board kind (the icon frames and the app_t flags of an app; see
# iconshot.c). All apps of main/apps.json are in (the list goes into this folder).
# It also makes, for each kind, iconlib_<kind>.a (all the objects but iconshot.c) and iconflags_<kind>.txt (the compile
# options): mk_app.py links an app out of main/ with them (its real icon and its real app_t flags).
#   sh sdk/tools/host/build.sh [lcd349 cores3 am18 watcher]
cd "$(dirname "$0")"
HERE="$(pwd -W 2>/dev/null || pwd)"
H="$(cd ../../../tests/host && (pwd -W 2>/dev/null || pwd))"
M="$(cd ../../../main && (pwd -W 2>/dev/null || pwd))"
G="$HERE/apps_gen"
F="-g -O1 -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-field-initializers"
python "$M/apps_gen.py" --host apps_gen --prefix ../../../main/ --no-all > /dev/null || exit 1
LIST=""
for s in $(cat apps_gen/srcs.txt); do LIST="$LIST $HERE/$s"; done
SRC="$H/stubs.c $H/common/miniz.c $LIST"
F="$F $(cat apps_gen/flags.txt) -DBK_HOST_DIR=\"$M/books\""
# the source folders (main/, libs/<module>/, apps/<app>/) as full paths (the objects build in obj_<kind>/)
INCS=""
for i in $(cat apps_gen/incs.txt); do INCS="$INCS -I $HERE/${i#-I}"; done
# the state of the source of this build (mk_app.py checks it before it reads the flags of an app)
STAMP="$(python ../mk_app.py --src-stamp)" || exit 1
B="$*"
[ -z "$B" ] && B="lcd349 cores3 am18 watcher"
for b in $B; do
    case $b in
    lcd349) I="-I $H/inc349" ;;
    cores3) I="-I $H/incs3" ;;
    am18) I="-I $H/incam -I $H/incs3" ;;
    watcher) I="-I $H/incwt" ;;
    esac
    (
        mkdir -p "obj_$b" && cd "obj_$b" || exit 1
        # (the folders and the sources in a response file: the command line of Windows has a limit of 32 K characters)
        echo "$INCS $SRC" > srcs.rsp
        clang $F $I -I "$H/common" -I "$M" -I "$G" -c @srcs.rsp || { echo "BUILD FAIL $b"; exit 1; }
        llvm-ar rcs "../iconlib_$b.a" ./*.o || { echo "AR FAIL $b"; exit 1; }
        cd .. && clang $F $I -I "$H/common" -I "$M" -I "$G" $INCS iconshot.c "iconlib_$b.a" -o "iconshot_$b.exe" -lm || { echo "BUILD FAIL $b"; exit 1; }
        echo "$F $I -I $H/common -I $M -I $G $INCS" > "iconflags_$b.txt"
        echo "$STAMP" > "src_stamp_$b.txt"
    ) &
done
wait
ls -la iconshot_*.exe iconlib_*.a
