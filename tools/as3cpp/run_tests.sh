#!/usr/bin/env bash
# Compila y ejecuta la prueba v2 enlazando codigo traducido real.  Uso: tools/as3cpp/run_tests.sh [dir_gen]
export PATH=/c/msys64/ucrt64/bin:$PATH
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GEN="${1:-$ROOT/work/gen2}"
OUT="$ROOT/work/test_v2.exe"
g++ -std=c++17 -O1 -Wall -Wno-unused-variable -I"$ROOT/runtime" -I"$GEN" "$ROOT/runtime/tests/test_v2.cpp" \
    "$GEN/FlxPoint.cpp" "$GEN/ChargeProfile.cpp" "$GEN/Weather.cpp" "$GEN/GameStats.cpp" -o "$OUT" && "$OUT"
