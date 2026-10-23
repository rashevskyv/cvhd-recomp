#!/bin/bash
# Compiles and runs tests/untile_test.cpp against the SDK (needs its librexruntime.so):
#   REXSDK_DIR=/home/user/rexglue/rexglue-sdk tests/run-untile-test.sh
set -eu
SDK=${REXSDK_DIR:-/home/user/rexglue/rexglue-sdk}
OUT=${TMPDIR:-/tmp}/untile_test
clang++-20 -std=c++23 -O1 -g -fsanitize=address -DREX_PLATFORM_LINUX=1 -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL \
    -I"$SDK/include" -I"$SDK/thirdparty/fmt/include" -I"$SDK/thirdparty/spdlog/include" -I"$SDK/thirdparty/simde" -I"$SDK/thirdparty/simde/simde" \
    -I"$SDK/thirdparty" -I"$SDK/thirdparty/xxHash" -mssse3 -msse4.1 \
    "$(dirname "$0")/untile_test.cpp" -o "$OUT" -L"$SDK/out/linux-amd64" -Wl,-rpath,"$SDK/out/linux-amd64" -lrexruntime
"$OUT"
