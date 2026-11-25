#!/usr/bin/env bash
# Prueba de controles en la Vita (dentro de WSL): llega al mapa con toques y prueba mando y panel trasero.
# Uso: probar_controles.sh <etiqueta>
set -u
TAG="$1"
A=~/wsl-agent.sh
OUT=/mnt/d/Proyectos/NeoScavengerVita/work
D=ux0:data/neoscavvita
C=/mnt/d/Proyectos/NeoScavengerVita/tools/vita/controles
tap() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 250 --coordinate-space screenshot --width 960 --height 544 >/dev/null; sleep "${3:-4}"; }
shot() { $A screen capture --output "$OUT/shot_${TAG}_$1.jpg" >/dev/null; }
inp() { $A input submit "$C/$1.json" | tail -c 120; echo; sleep "${2:-2}"; }
$A screen on >/dev/null; $A app launch NEOS00001 >/dev/null; sleep 75; $A screen on >/dev/null
tap 470 127 20; tap 112 34 3; tap 185 58 3; tap 480 188 8; tap 480 188 25
tap 95 265 4; tap 480 300 6
for i in 1 2 3 4 5; do tap 755 273 20; done
shot c0_mapa
echo "== deriva (stick izq. al 25 %: no debe mover el cursor)"; inp deriva 2; shot c1_deriva
echo "== stick izquierdo a fondo"; inp stick_izq 2; shot c2_stick_izq
echo "== stick derecho (mover mapa)"; inp stick_der 2; shot c3_stick_der
echo "== panel trasero (arrastrar mapa)"; inp trasero_arrastre 2; shot c4_trasero
echo "== L (inventario)"; inp boton_L 6; shot c5_L
echo "== circulo (volver)"; inp boton_O 6; shot c6_O
echo "== R (fin de turno)"; inp boton_R 20; shot c7_R
$A app close NEOS00001 >/dev/null; sleep 3
$A fs move "$D/game.log" "$D/log_$TAG.txt" >/dev/null
$A fs download "$D/log_$TAG.txt" "$OUT/log_$TAG.txt" >/dev/null
echo "== listo"
