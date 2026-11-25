"""Traduce direcciones de un log ([SEH] ...) a funciones con nm. Uso: python direcciones.py <exe> <log>"""
import bisect, re, subprocess, sys
exe, log = sys.argv[1], sys.argv[2]
txt = open(log, encoding="utf-8", errors="replace").read()
m = re.search(r"base ([0-9a-fA-F]+)", txt)
base = int(m.group(1), 16)
syms = []
for l in subprocess.run(["nm", "-C", "--numeric-sort", "--defined-only", exe], capture_output=True, text=True, errors="replace").stdout.split("\n"):
    p = l.split(" ", 2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
for kind, a in re.findall(r"\[SEH\] (acceso invalido en|pila) ([0-9a-fA-F]+)", txt):
    va = int(a, 16) - base + 0x140000000
    i = bisect.bisect_right(addrs, va) - 1
    print(f"{kind:22s} {syms[i][1][:160] if i >= 0 else '?'}")
