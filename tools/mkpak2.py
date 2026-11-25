#!/usr/bin/env python3
"""mkpak2.py — crea img2.pak (formato NSVPAK02) a partir de la carpeta img/ del juego.

Evolución del NSVPAK01 del port anterior (que guardaba los PNG tal cual y obligaba a
decodificarlos en la Vita: ~12,9 ms cada uno). Aquí las imágenes ya vienen decodificadas.

Formato (little endian):
  8   'NSVPAK02'
  u32 cantidad
  u32 offset_tabla_nombres_fin (= inicio de datos, alineado a 16)
  por entrada: u16 len, nombre, u32 offset, u32 tamaño, u16 ancho, u16 alto, u8 formato, u8 pad[3]
  datos (cada entrada alineada a 16)
Formatos: 0 = RGBA8 sin comprimir (esqueleto). Reservados: 1 = DXT5, 2 = RGBA8+LZ4, 3 = DXT5+LZ4.

Uso: python mkpak2.py <carpeta_img> <salida.pak> [nombre.png ...]    (sin nombres = todas)
"""
import struct
import sys
from pathlib import Path
from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
names = sys.argv[3:] or sorted(p.name for p in src.glob("*.png"))
entries = []
for n in names:
    im = Image.open(src / n).convert("RGBA")
    entries.append((n, im.width, im.height, im.tobytes()))
table = b""
for n, w, h, _ in entries:
    nb = n.encode()
    table += struct.pack("<H", len(nb)) + nb + b"\0" * 8 + struct.pack("<HHB3x", w, h, 0)
head = 16 + len(table)
start = (head + 15) & ~15
off, rows = start, []
for n, w, h, data in entries:
    rows.append((off, len(data)))
    off = (off + len(data) + 15) & ~15
table = b""
for (n, w, h, data), (o, s) in zip(entries, rows):
    nb = n.encode()
    table += struct.pack("<H", len(nb)) + nb + struct.pack("<II", o, s) + struct.pack("<HHB3x", w, h, 0)
assert 16 + len(table) == head
with open(out, "wb") as f:
    f.write(b"NSVPAK02" + struct.pack("<II", len(entries), start) + table)
    f.write(b"\0" * (start - 16 - len(table)))
    for (n, w, h, data), (o, s) in zip(entries, rows):
        assert f.tell() == o
        f.write(data)
        f.write(b"\0" * (((o + s + 15) & ~15) - (o + s)))
print(f"{out}: {len(entries)} imágenes, {out.stat().st_size/1048576:.1f} MB")
