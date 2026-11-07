#!/bin/sh
# The host test of the web password (main/web_auth_core.c): sh tests/web_auth/build.sh -> WEB AUTH CORE OK.
# clang with UBSan (and ASan where the toolchain has it: Linux, or clang on Windows with its runtime).
cd "$(dirname "$0")" || exit 1
SAN="${SAN:--fsanitize=address,undefined}"
clang -g -O1 -Wall -Wextra -Werror $SAN -I ../../main ../../main/web_auth_core.c test_core.c -o test_core.exe || exit 1
./test_core.exe
