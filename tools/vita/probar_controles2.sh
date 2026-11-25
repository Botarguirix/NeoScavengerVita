#!/usr/bin/env bash
# Vita: resto de controles. Partida nueva -> mapa -> mover al bosque -> L -> pulsacion larga sobre un objeto (menu
# contextual) -> circulo -> cruceta -> dos dedos atras (clic) -> cuadrado -> START. Captura en cada paso.
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
tap 680 190 15; inp boton_R 25
shot k0_mapa
echo "== L + pulsacion larga sobre objeto"; inp boton_L 6; shot k1_inventario; hold 150 42 3; shot k2_contextual
echo "== circulo"; tap 600 300 2; inp boton_O 6; shot k3_circulo
echo "== cruceta"; inp cruceta_der 2; shot k4_cruceta
echo "== dos dedos atras"; inp trasero_dos_dedos 3; shot k5_dos_dedos
echo "== cuadrado"; inp boton_SQUARE 8; shot k6_cuadrado
echo "== START"; inp boton_START 6; shot k7_start
$A app close NEOS00001 >/dev/null; sleep 3
$A fs move $D/game.log $D/log_$TAG.txt >/dev/null; $A fs download $D/log_$TAG.txt $OUT/log_$TAG.txt >/dev/null
echo "== listo"
