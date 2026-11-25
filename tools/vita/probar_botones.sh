#!/usr/bin/env bash
# Pulsa L y R en el menu del juego y descarga el log (registro [mando]) para ver si llegan los botones.
T=bt_$(date +%H%M%S); A=~/wsl-agent.sh; C=/mnt/d/Proyectos/NeoScavengerVita/tools/vita/controles; D=ux0:data/neoscavvita
$A app launch NEOS00001 >/dev/null; sleep 70
$A screen on | tail -c 80; echo; sleep 1
$A input submit $C/boton_L.json | tail -c 80; echo; sleep 2
$A input submit $C/boton_R.json | tail -c 80; echo; sleep 2
$A input submit $C/boton_X.json | tail -c 80; echo; sleep 2
$A input submit $C/boton_C.json | tail -c 80; echo; sleep 2
$A app close NEOS00001 >/dev/null; sleep 3
$A fs move $D/game.log $D/log_$T.txt >/dev/null
$A fs download $D/log_$T.txt /mnt/d/Proyectos/NeoScavengerVita/work/log_bt.txt >/dev/null; echo fin
