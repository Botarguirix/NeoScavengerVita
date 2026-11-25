"""Empaqueta la carpeta de datos del juego (copia legal del usuario, ya preparada) en un solo archivo NSDPAK01.

Uso: python -I mkdatapak.py <carpeta_datos> <salida.pak>

Formato (little endian):
  "NSDPAK01" | u32 n | n x (u16 len_ruta, ruta utf-8 con '/', u64 offset, u64 tamano) | datos
El runtime (platform.h) lo lee si existe <raiz>/data.pak; si no, usa los archivos sueltos.
"""
import os
import struct
import sys

src, out = sys.argv[1], sys.argv[2]
files = []
for root, _dirs, names in os.walk(src):
    for n in sorted(names):
        p = os.path.join(root, n)
        rel = os.path.relpath(p, src).replace("\\", "/")
        files.append((rel, p))
files.sort()
index_size = 8 + 4 + sum(2 + len(r.encode("utf-8")) + 16 for r, _ in files)
off = index_size
entries = []
for rel, p in files:
    sz = os.path.getsize(p)
    entries.append((rel, off, sz))
    off += sz
with open(out, "wb") as f:
    f.write(b"NSDPAK01")
    f.write(struct.pack("<I", len(entries)))
    for rel, o, sz in entries:
        b = rel.encode("utf-8")
        f.write(struct.pack("<H", len(b)) + b + struct.pack("<QQ", o, sz))
    for (rel, p), (_r, o, sz) in zip(files, entries):
        assert f.tell() == o
        with open(p, "rb") as g:
            f.write(g.read())
print(f"{len(entries)} archivos, {off / 1048576:.1f} MB -> {out}")
