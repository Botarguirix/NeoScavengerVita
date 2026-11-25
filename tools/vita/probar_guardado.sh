#!/usr/bin/env bash
# Vita: partida nueva hasta el mapa, R (fin de turno = autoguardado), cerrar, volver a abrir y CONTINUE.
set -u
TAG="$1"; A=~/wsl-agent.sh; OUT=/mnt/d/Proyectos/NeoScavengerVita/work; D=ux0:data/neoscavvita
C=/mnt/d/Proyectos/NeoScavengerVita/tools/vita/controles
tap() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 250 --coordinate-space screenshot --width 960 --height 544 >/dev/null; sleep "${3:-4}"; }
shot() { $A screen capture --output "$OUT/shot_${TAG}_$1.jpg" >/dev/null; }
getlog() { $A app close NEOS00001 >/dev/null; sleep 3; $A fs move $D/game.log $D/log_${TAG}_$1.txt >/dev/null; $A fs download $D/log_${TAG}_$1.txt $OUT/log_${TAG}_$1.txt >/dev/null; }
$A screen on >/dev/null; $A app launch NEOS00001 >/dev/null; sleep 75; $A screen on >/dev/null
tap 470 127 20; tap 112 34 3; tap 185 58 3; tap 480 188 8; tap 480 188 25
tap 95 265 4; tap 480 300 6
for i in 1 2 3 4 5; do tap 755 273 20; done
tap 680 190 15
$A input submit $C/boton_R.json >/dev/null; sleep 25
shot s1_antes_de_cerrar
echo "== cerrar"; getlog 1
echo "== volver a abrir y CONTINUE"; $A screen on >/dev/null; $A app launch NEOS00001 >/dev/null; sleep 75; $A screen on >/dev/null
tap 470 44 40
shot s2_continuar
getlog 2
echo "== listo"
