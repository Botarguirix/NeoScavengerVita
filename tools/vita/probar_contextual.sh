#!/usr/bin/env bash
# Vita: panel trasero (arrastrar mapa), L (inventario) y pulsacion larga sobre un objeto del suelo (menu contextual).
set -u
TAG="$1"; A=~/wsl-agent.sh; OUT=/mnt/d/Proyectos/NeoScavengerVita/work; D=ux0:data/neoscavvita
C=/mnt/d/Proyectos/NeoScavengerVita/tools/vita/controles
tap() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 250 --coordinate-space screenshot --width 960 --height 544 >/dev/null; sleep "${3:-4}"; }
hold() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 900 --coordinate-space screenshot --width 960 --height 544 >/dev/null; sleep "${3:-3}"; }
shot() { $A screen capture --output "$OUT/shot_${TAG}_$1.jpg" >/dev/null; }
inp() { $A input submit "$C/$1.json" >/dev/null; sleep "${2:-2}"; }
$A screen on >/dev/null; $A app launch NEOS00001 >/dev/null; sleep 75; $A screen on >/dev/null
tap 470 127 20; tap 112 34 3; tap 185 58 3; tap 480 188 8; tap 480 188 25
tap 95 265 4; tap 480 300 6
for i in 1 2 3 4 5; do tap 755 273 20; done
shot t0_mapa
echo "== panel trasero"; inp trasero_arrastre 2; shot t1_trasero
echo "== L"; inp boton_L 6; shot t2_inventario
echo "== pulsacion larga sobre objeto del suelo"; hold 150 42 3; shot t3_contextual
$A app close NEOS00001 >/dev/null; sleep 3
$A fs move $D/game.log $D/log_$TAG.txt >/dev/null
$A fs download $D/log_$TAG.txt $OUT/log_$TAG.txt >/dev/null; echo "== listo"
