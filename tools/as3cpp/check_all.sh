#!/usr/bin/env bash
# Comprueba la sintaxis (g++ -fsyntax-only) de todas las clases generadas y resume los errores.
# Uso: tools/as3cpp/check_all.sh [dir_gen] [jobs]
export PATH=/c/msys64/ucrt64/bin:$PATH
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GEN="${1:-$ROOT/work/gen}"
JOBS="${2:-8}"
OUT="$ROOT/work/check"
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$GEN"
ls *.cpp | xargs -P "$JOBS" -I{} sh -c "g++ -std=c++17 -fsyntax-only -fmax-errors=30 -I'$ROOT/runtime' -I'$GEN' {} > '$OUT/{}.log' 2>&1 || echo {} >> '$OUT/failed.txt'"
total=$(ls *.cpp | wc -l)
failed=0; [ -f "$OUT/failed.txt" ] && failed=$(wc -l < "$OUT/failed.txt")
echo "compilan: $((total - failed)) de $total"
cat "$OUT"/*.log | grep -E "error:" | sed -E "s#^[^ ]*/(gen|runtime)/##; s/'[^']*'/'X'/g; s/[0-9]+:[0-9]+: //" | sed -E 's/^[A-Za-z0-9_§.]+\.(h|cpp):[0-9]+: ?//' | sort | uniq -c | sort -rn | head -${TOP:-30}
