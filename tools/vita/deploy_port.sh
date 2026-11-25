#!/usr/bin/env bash
# Despliega un VPK en la Vita con vita-agent (se ejecuta DENTRO de WSL), lo lanza, captura y recoge el log.
# Uso: deploy_port.sh <vpk_en_/mnt/d/...> <etiqueta> [segundos_en_marcha]
# Reglas aprendidas en Valdis: nunca fs trash sobre rutas que se reutilizan; el VPK se sube con nombre único;
# el log se descarga con la app CERRADA (y movido a un nombre único).
set -u
VPK="$1"; TAG="$2"; WAIT="${3:-25}"
A=~/wsl-agent.sh
OUT=/mnt/d/Proyectos/NeoScavengerVita/work
D=ux0:data/neoscavvita
N="neoscavvita-$TAG.vpk"
echo "== subir $N";  $A fs upload --yes "$VPK" "$D/$N" | tail -c 160; echo
echo "== instalar";  $A app install --yes "$D/$N" --timeout 120 | tail -c 300; echo
echo "== lanzar";    $A app launch NEOS00001 | tail -c 160; echo
sleep "$WAIT"
echo "== captura";   $A screen capture --output "$OUT/shot_$TAG.jpg" | tail -c 120; echo
echo "== cerrar";    $A app close NEOS00001 | tail -c 120; echo
sleep 3
$A fs move "$D/${LOGNAME_VITA:-port.log}" "$D/log_$TAG.txt" | tail -c 100; echo
$A fs download "$D/log_$TAG.txt" "$OUT/log_$TAG.txt" | tail -c 140; echo
echo "== log"; cut -c1-200 "$OUT/log_$TAG.txt" | head -${LOGLINES:-40}
