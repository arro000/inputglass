#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/build"}
BUILD_TYPE=${BUILD_TYPE:-Release}
CC=${CC:-x86_64-w64-mingw32-gcc}
RC=${RC:-x86_64-w64-mingw32-windres}

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchains/mingw-w64-x86_64.cmake" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_RC_COMPILER="$RC"
cmake --build "$BUILD_DIR" --parallel

printf '%s\n' "$BUILD_DIR/bin/InputGlass.exe"
