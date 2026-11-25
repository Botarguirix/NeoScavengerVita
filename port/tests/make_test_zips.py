"""Genera zips de prueba para port/tests/test_mods.cpp.  Uso: python make_test_zips.py <salida> <SampleModFolder> """
import sys
import zipfile
from pathlib import Path

out, sample = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
items = (sample / "data" / "itemtypes.xml").read_bytes()
treasure = (sample / "0" / "data" / "treasuretable.xml").read_bytes()
getmods = (sample / "getmods.php").read_bytes()

# A) como suelen venir en NexusMods: una carpeta raiz con el mod dentro
with zipfile.ZipFile(out / "FancyBag-1.0.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("FancyBag/data/itemtypes.xml", items)
    z.writestr("FancyBag/getimages.php", (sample / "getimages.php").read_bytes())
    z.writestr("FancyBag/readme.txt", "mod de prueba")
# B) plano: data/ en la raiz del zip (sin carpeta)
with zipfile.ZipFile(out / "FlatMod.zip", "w", zipfile.ZIP_STORED) as z:
    z.writestr("data/treasuretable.xml", treasure)
# C) dos niveles de carpeta (Mod/Mod/data) y ademas un getmods.php del autor
with zipfile.ZipFile(out / "Nested-2.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Download/Nested/data/itemtypes.xml", items)
    z.writestr("Download/Nested/getmods.php", getmods)
# D) malicioso: intenta escapar de la carpeta
with zipfile.ZipFile(out / "Evil.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Evil/data/itemtypes.xml", items)
    z.writestr("Evil/data/../../../escape.txt", "no deberia salir")
# E) no es un mod
with zipfile.ZipFile(out / "NotAMod.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("readme.txt", "solo un texto")
# F) corrupto: cabecera valida, datos truncados
raw = (out / "FancyBag-1.0.zip").read_bytes()
(out / "Broken.zip").write_bytes(raw[: len(raw) // 2])
# G) XML roto dentro de un mod valido
with zipfile.ZipFile(out / "BadXml.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("BadXml/data/itemtypes.xml", items[: len(items) // 2])
print("zips en", out)
