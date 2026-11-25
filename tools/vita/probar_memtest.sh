#!/usr/bin/env bash
# Instala y ejecuta el VPK de pruebas de memoria (NEOS00002) y descarga memtest.log (se ejecuta DENTRO de WSL).
# Uso: probar_memtest.sh <etiqueta> [segundos]
set -u
TAG="$1"; WAIT="${2:-120}"
A=~/wsl-agent.sh
OUT=/mnt/d/Proyectos/NeoScavengerVita/work
D=ux0:data/neoscavvita
N="memtest-$TAG.vpk"
$A screen on >/dev/null 2>&1
echo "== subir";    $A fs upload --yes /mnt/d/Proyectos/NeoScavengerVita/build_memtest/memtest.vpk "$D/$N" | tail -c 120; echo
echo "== instalar"; $A app install --yes "$D/$N" --timeout 120 | tail -c 200; echo
echo "== lanzar";   $A app launch NEOS00002 | tail -c 120; echo
sleep "$WAIT"
$A screen capture --output "$OUT/shot_memtest_$TAG.jpg" >/dev/null
echo "== cerrar";   $A app close NEOS00002 | tail -c 120; echo
sleep 2
$A fs move "$D/memtest.log" "$D/memtest_$TAG.txt" >/dev/null
$A fs download "$D/memtest_$TAG.txt" "$OUT/memtest_$TAG.txt" >/dev/null
echo "== listo"
