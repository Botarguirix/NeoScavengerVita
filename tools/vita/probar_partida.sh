#!/usr/bin/env bash
# Prueba en la Vita (se ejecuta DENTRO de WSL): mismo recorrido que tests/guiones/02_despertar.txt, con toques reales:
# NEW GAME -> habilidades -> despertar -> elegir "saltar por la ventana" -> mapa -> mover un hexagono -> fin de turno.
# Captura la pantalla en cada paso y descarga el log. Uso: probar_partida.sh <etiqueta> [segundos_de_carga]
set -u
TAG="$1"; LOAD="${2:-75}"
A=~/wsl-agent.sh
OUT=/mnt/d/Proyectos/NeoScavengerVita/work
D=ux0:data/neoscavvita
tap() { $A touch swipe --panel front --from "$1" "$2" --to "$1" "$2" --duration-ms 250 --coordinate-space screenshot --width 960 --height 544 >/dev/null; sleep "${3:-4}"; }
shot() { $A screen capture --output "$OUT/shot_${TAG}_$1.jpg" >/dev/null; }
echo "== lanzar"; $A app launch NEOS00001 >/dev/null
sleep "$LOAD"; shot 1_menu
echo "== NEW GAME";           tap 470 127 20; shot 2_habilidades
echo "== habilidades";        tap 112 34 3; tap 185 58 3; tap 480 188 8; tap 480 188 25; shot 3_despertar
echo "== elegir respuesta";   tap 95 265 4; tap 480 300 6; shot 4_respuesta
echo "== confirmar x3";       tap 755 273 20; shot 5_encuentro2; tap 755 273 20; shot 6_encuentro3; tap 755 273 20; tap 755 273 20; tap 755 273 25; shot 7_mapa
echo "== mover y fin de turno"; tap 680 190 15; shot 8_mover; tap 920 15 25; shot 9_fin_turno
echo "== cerrar"; $A app close NEOS00001 >/dev/null
sleep 3
$A fs move "$D/game.log" "$D/log_$TAG.txt" >/dev/null
$A fs download "$D/log_$TAG.txt" "$OUT/log_$TAG.txt" >/dev/null
echo "== listo"
