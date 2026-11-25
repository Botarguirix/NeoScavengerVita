#!/usr/bin/env python3
"""extract_fonts.py — extrae las fuentes DefineFont3 (etiqueta 75) del SWF del juego como <nombre>.df3 (cuerpo crudo de la etiqueta).

Uso: python extract_fonts.py <game.swf descomprimido> <carpeta_salida>
"""
import struct
import sys
from pathlib import Path

swf = Path(sys.argv[1]).read_bytes()
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
assert swf[:3] == b"FWS", "se espera un SWF sin comprimir (FWS)"
nbits = swf[8] >> 3
pos = 8 + (5 + nbits * 4 + 7) // 8 + 4
found = 0
while pos < len(swf):
    h = struct.unpack("<H", swf[pos:pos + 2])[0]
    tag, ln = h >> 6, h & 63
    pos += 2
    if ln == 63:
        ln = struct.unpack("<I", swf[pos:pos + 4])[0]
        pos += 4
    body = swf[pos:pos + ln]
    pos += ln
    if tag == 75:  # DefineFont3
        nlen = body[4]
        name = body[5:5 + nlen].decode("latin-1").rstrip("\0")
        (out / f"{name}.df3").write_bytes(body)
        print(f"{name}.df3  {len(body)} bytes  id={struct.unpack('<H', body[:2])[0]}  glifos={struct.unpack('<H', body[5 + nlen:7 + nlen])[0]}")
        found += 1
print(found, "fuentes")
