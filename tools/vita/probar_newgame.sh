#!/usr/bin/env bash
# Prueba en la Vita (se ejecuta DENTRO de WSL): lanza el juego instalado, espera la carga, toca NEW GAME con el
# tactil frontal (toque = deslizamiento de un punto al mismo punto) y captura la pantalla en cada paso.
# Uso: probar_newgame.sh <etiqueta> [segundos_de_carga]
set -u
TAG="$1"; LOAD="${2:-75}"
A=~/wsl-agent.sh
OUT=/mnt/d/Proyectos/NeoScavengerVita/work
D=ux0:data/neoscavvita
tap() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 250 --coordinate-space screenshot --width 960 --height 544 | tail -c 120; echo; }
shot() { $A screen capture --output "$OUT/shot_${TAG}_$1.jpg" | tail -c 80; echo; }
echo "== lanzar"; $A app launch NEOS00001 | tail -c 120; echo
sleep "$LOAD"
shot 1_menu
echo "== NEW GAME"; tap 470 127
sleep 20
shot 2_habilidades
echo "== Hacking + Tracking + Confirm x2"; tap 112 34; sleep 3; tap 185 58; sleep 3; tap 480 188; sleep 8; tap 480 188
sleep 25
shot 3_despertar
echo "== cerrar"; $A app close NEOS00001 | tail -c 120; echo
sleep 3
$A fs move "$D/game.log" "$D/log_$TAG.txt" | tail -c 100; echo
$A fs download "$D/log_$TAG.txt" "$OUT/log_$TAG.txt" | tail -c 120; echo
