#!/usr/bin/env bash
# Flujo de desarrollo en PC: decompilado corregido -> traduccion -> compilacion incremental -> ejecucion.
# Uso: tools/as3cpp/run_pc.sh [dir_gen]     (por defecto work/gen17; reutiliza los objetos ya compilados)
# Requiere: work/as3_fixed (fuente decompilada y corregida) y work/gameroot (datos del juego).
export PATH=/c/msys64/ucrt64/bin:$PATH
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GEN="${1:-$ROOT/work/gen17}"
mkdir -p "$GEN"
GEN="$(cd "$GEN" && pwd)"
find "$ROOT/work/as3_fixed" -name "*.as" > "$GEN/sources.txt"
(cd "$ROOT/tools/as3cpp" && PYTHONIOENCODING=utf-8 python -I as3cpp.py "$GEN" $(tr '\n' ' ' < "$GEN/sources.txt")) | head -3
bash "$ROOT/tools/as3cpp/build_pc.sh" "$GEN" 8 > "$GEN/build.log" 2>&1 || { echo "[build] FALLO, ver $GEN/build.log"; tail -25 "$GEN/build.log"; exit 1; }
tail -2 "$GEN/build.log"
cd "$GEN" && ./neoscav_pc.exe "${2:-$ROOT/work/gameroot}"
