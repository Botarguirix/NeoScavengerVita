#!/usr/bin/env bash
# Vita: abrir el juego y pulsar CONTINUE (carga la partida guardada en ux0:data/neoscavvita/save/).
set -u
TAG="$1"; A=~/wsl-agent.sh; OUT=/mnt/d/Proyectos/NeoScavengerVita/work; D=ux0:data/neoscavvita
tap() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 250 --coordinate-space screenshot --width 960 --height 544 >/dev/null; sleep "${3:-4}"; }
$A screen on >/dev/null; $A app launch NEOS00001 >/dev/null; sleep 75; $A screen on >/dev/null
tap 470 44 40
$A screen capture --output "$OUT/shot_${TAG}_continuar.jpg" >/dev/null
$A app close NEOS00001 >/dev/null; sleep 3
$A fs move $D/game.log $D/log_$TAG.txt >/dev/null; $A fs download $D/log_$TAG.txt $OUT/log_$TAG.txt >/dev/null
echo "== listo"
