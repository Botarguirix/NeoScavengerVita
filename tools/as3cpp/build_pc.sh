#!/usr/bin/env bash
# Compila TODAS las clases generadas a objetos (con cabecera precompilada) y las enlaza con un main de PC.
# Uso: tools/as3cpp/build_pc.sh <dir_gen> [jobs] [main.cpp]
# Salida: <dir_gen>/$OBJ/*.o, <dir_gen>/$EXE   (el informe de simbolos sin resolver queda en <dir_gen>/link.log)
export PATH=/c/msys64/ucrt64/bin:$PATH
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GEN="$(cd "${1:?falta dir_gen}" && pwd)"
JOBS="${2:-8}"
MAIN="${3:-$ROOT/port/pc/main_pc.cpp}"
# OPT=1: compilacion optimizada sin rastreo (para medir tiempos), en obj_o2/ y neoscav_pc_o2.exe
if [ "${OPT:-0}" = 1 ]; then FLAGS="-std=c++17 -O2 -g0 -w ${EXTRA:-} -I$ROOT/runtime -I$GEN"; OBJ=obj_o2${SUF:-}; EXE=neoscav_pc_o2${SUF:-}.exe
else FLAGS="-std=c++17 -O0 -g0 -w -DAS3_TRACE -I$ROOT/runtime -I$GEN"; OBJ=obj; EXE=neoscav_pc.exe; fi
mkdir -p "$GEN/$OBJ"
cd "$GEN"
if [ "$OBJ" = obj ] && { [ ! -f all.h.gch ] || [ all.h -nt all.h.gch ] || [ "$(ls -t "$ROOT"/runtime/*.h | head -1)" -nt all.h.gch ]; }; then
  echo "== precompilando all.h"
  g++ $FLAGS -x c++-header all.h -o all.h.gch || exit 1
fi
# la cabecera del runtime mas reciente: si cambia cualquiera (platform.h, png.h, font.h...) se recompila todo
NEWEST_RT=$(ls -t "$ROOT"/runtime/*.h | head -1)
echo "== compilando objetos"
rm -f $OBJ/FALLOS.txt
ls *.cpp | xargs -P "$JOBS" -I{} sh -c "[ $OBJ/{}.o -nt {} ] && [ $OBJ/{}.o -nt $NEWEST_RT ] && { [ ! -f all.h.gch ] || [ $OBJ/{}.o -nt all.h.gch ]; } || g++ $FLAGS -c {} -o $OBJ/{}.o > $OBJ/{}.log 2>&1 || { rm -f $OBJ/{}.o; echo {} >> $OBJ/FALLOS.txt; }"
if [ -s $OBJ/FALLOS.txt ]; then
  echo "== ERROR: no compilan $(wc -l < $OBJ/FALLOS.txt) archivos:"; cat $OBJ/FALLOS.txt
  head -20 "$OBJ/$(head -1 $OBJ/FALLOS.txt).log"; exit 1
fi
echo "== main + enlace"
g++ $FLAGS -c "$MAIN" -o $OBJ/main_pc.o || exit 1
[ $OBJ/fastalloc.o -nt "$ROOT/runtime/fastalloc.cpp" ] || g++ $FLAGS -c "$ROOT/runtime/fastalloc.cpp" -o $OBJ/fastalloc.o || exit 1
[ $OBJ/minimp3_impl.o -nt "$ROOT/runtime/third_party/minimp3.h" ] || g++ $FLAGS -c "$ROOT/runtime/minimp3_impl.cpp" -o $OBJ/minimp3_impl.o || exit 1
rm -f $EXE
g++ ${EXTRA:-} $OBJ/*.o -static-libstdc++ -static-libgcc -lz -lpsapi -lwinmm -o $EXE > link.log 2>&1 || { echo "== ERROR de enlace"; grep -m5 "undefined reference\|error" link.log; exit 1; }
tail -3 link.log
grep -c "undefined reference" link.log
ls -la $EXE 2>/dev/null
