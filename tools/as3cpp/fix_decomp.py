#!/usr/bin/env python3
"""fix_decomp.py — corrige fallos conocidos del decompilador (JPEXS) antes de traducir.

Uso: python fix_decomp.py <carpeta_scripts_entrada> <carpeta_scripts_salida>

Cada arreglo está documentado: son errores del DECOMPILADOR (no del juego) que se detectaron al compilar
el C++ generado. El resto del código pasa sin tocar.
"""
import re
import shutil
import sys
from pathlib import Path

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(src, dst)

FIXES = []


def fix(name):
    def deco(fn):
        FIXES.append((name, fn))
        return fn
    return deco


@fix("length huérfano: el decompilador pierde `var length:uint = members.length` (Flixel overlaps/collide)")
def orphan_length(text):
    pat = re.compile(r"(?P<arr>_loc\d+_) = \((?P<g>\w+) as FlxGroup\)\.members;(?P<mid>\s*)while\((?P<i>_loc\d+_) < length\)")
    return pat.subn(lambda m: f"{m['arr']} = ({m['g']} as FlxGroup).members;{m['mid']}while({m['i']} < {m['arr']}.length)", text)


total = {}
for path in dst.rglob("*.as"):
    text = path.read_text(encoding="utf-8")
    new = text
    for name, fn in FIXES:
        new, n = fn(new)
        if n:
            total[name] = total.get(name, 0) + n
    if new != text:
        path.write_text(new, encoding="utf-8")
for name, n in total.items():
    print(f"{n:3d} x {name}")
