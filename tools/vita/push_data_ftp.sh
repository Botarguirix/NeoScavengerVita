#!/usr/bin/env bash
# Sube los datos del juego (copia legal del usuario, ya preparada en work/gameroot) a ux0:data/neoscavvita/
# por el FTP de VitaShell (pulsar SELECT en VitaShell). No usa el agente ni WSL.
# Uso: tools/vita/push_data_ftp.sh <ip_vita> [carpeta_origen]
# Solo sube archivos que falten o cambien de tamano (se puede relanzar si se corta).
set -u
IP="${1:?falta la IP de la Vita (p. ej. 192.168.1.32)}"
SRC="${2:-$(cd "$(dirname "$0")/../.." && pwd)/work/gameroot}"
BASE="ftp://$IP:1337/ux0:/data/neoscavvita"
cd "$SRC" || exit 1
total=$(find . -type f | wc -l); n=0; subidos=0; fallos=0
t0=$(date +%s)
while IFS= read -r f; do
  n=$((n + 1)); rel="${f#./}"
  remote_size=$(curl -s --max-time 20 -I "$BASE/$rel" 2>/dev/null | tr -d '\r' | awk '/Content-Length/ {print $2}')
  local_size=$(stat -c %s "$f")
  if [ "$remote_size" = "$local_size" ]; then continue; fi
  if curl -s --max-time 300 --ftp-create-dirs -T "$f" "$BASE/$rel"; then subidos=$((subidos + 1)); else fallos=$((fallos + 1)); echo "FALLO $rel"; fi
  [ $((n % 100)) -eq 0 ] && echo "[datos] $n de $total revisados, $subidos subidos, $fallos fallos"
done < <(find . -type f | sort)
echo "[datos] terminado: $total archivos, $subidos subidos, $fallos fallos, $(( $(date +%s) - t0 )) s"
[ "$fallos" -eq 0 ]
