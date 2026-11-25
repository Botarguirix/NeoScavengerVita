#!/usr/bin/env python3
"""Equivalente no interactivo de `vita_agent.py bootstrap` (que exige terminal con curses).

Hace lo mismo que el asistente: lee ur0:tai/config.txt por FTP (solo lectura), avisa de
plugins incompatibles y, si no hay bloqueos, guarda config.json + identidad del PC.
No escribe nada en la Vita.

Uso (dentro de WSL):  python bootstrap_noninteractive.py <ip> <directorio_config> [nombre_agente]
"""
import sys
sys.path.insert(0, 'client')
import bootstrap as b
from pathlib import Path

host, folder = sys.argv[1], Path(sys.argv[2])
name = sys.argv[3] if len(sys.argv) > 3 else 'Claude'
assert name in b.AGENT_NAMES, f'nombre de agente no válido: {name}'
text = b.read_vita_config(host)
res = b.inspect_plugins(text)
print('Plugins con aviso:', [(f['plugin'], f['severity']) for f in res['findings']] or 'ninguno')
print('NoLockScreen activo:', res['nolockscreen_enabled'])
print('--- secciones y plugins activos en config.txt:')
sec = None
for line in text.splitlines():
    l = line.split('#', 1)[0].strip()
    if l:
        print(('  ' if not l.startswith('*') else '') + l)
if any(f['severity'] == 'incompatible' for f in res['findings']):
    sys.exit('BLOQUEADO: hay plugins incompatibles; desactívalos y reinicia la Vita.')
cfg = b.save_setup(folder, host, name)
print('Guardado:', cfg)
