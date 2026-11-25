"""Traduce perfil_muestras.txt (sampler.h) a funciones con nm. Uso: python perfil.py <exe> <muestras> [n]"""
import bisect, subprocess, sys
exe, mf = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
lines = open(mf).read().split("\n")
base = int(lines[0].split()[1], 16)
mods = [(l.split()[0][4:], int(l.split()[1])) for l in lines[1:] if l.startswith("mod:")]
samples = [(int(a, 16), int(n)) for a, n in (l.split() for l in lines[1:] if l.strip() and not l.startswith("mod:"))]
syms = []
out = subprocess.run(["nm", "-C", "--numeric-sort", "--defined-only", exe], capture_output=True, text=True, errors="replace").stdout
for l in out.split("\n"):
    p = l.split(" ", 2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
# imagen preferida de PE en x64: 0x140000000
pref = 0x140000000
addrs = [a for a, _ in syms]
tot = {}
total = 0
for a, n in samples:
    va = a - base + pref
    i = bisect.bisect_right(addrs, va) - 1
    name = syms[i][1] if i >= 0 else "?"
    tot[name] = tot.get(name, 0) + n
    total += n
for m, n in mods:
    tot["[DLL] " + m] = tot.get("[DLL] " + m, 0) + n
    total += n
print(f"muestras: {total}")
for name, n in sorted(tot.items(), key=lambda x: -x[1])[:top]:
    print(f"{100.0 * n / total:6.2f}%  {name[:150]}")
