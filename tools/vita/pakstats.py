# pakstats.py — estadisticas de las imagenes de un data.pak (NSDPAK02): tamanos, memoria, cuantas superan la pantalla.
# Uso: python tools/vita/pakstats.py work/gameroot_pak/data.pak [N_mayores]
import struct, sys, collections

def read_index(path):
    with open(path, 'rb') as f:
        assert f.read(8) == b'NSDPAK02'
        n, = struct.unpack('<I', f.read(4))
        out = []
        for _ in range(n):
            l, = struct.unpack('<H', f.read(2))
            rel = f.read(l).decode('utf-8', 'replace')
            t, = struct.unpack('<B', f.read(1))
            off, stored, w, h, raw = struct.unpack('<QQIII', f.read(28))
            out.append((rel, t, stored, w, h, raw))
        return out

def main():
    items = read_index(sys.argv[1])
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    imgs = [i for i in items if i[1] == 1]
    MB = 1048576.0
    tot_raw = sum(i[5] for i in imgs); tot_st = sum(i[2] for i in imgs)
    print(f"archivos {len(items)}, imagenes {len(imgs)}: ARGB {tot_raw/MB:.1f} MB, en paquete {tot_st/MB:.1f} MB")
    buckets = collections.OrderedDict([('<=64', 0), ('<=256', 0), ('<=512', 0), ('<=960x544', 0), ('>pantalla', 0), ('>2048', 0)])
    mem = collections.defaultdict(int)
    for rel, t, st, w, h, raw in imgs:
        m = max(w, h)
        k = '>2048' if m > 2048 else '>pantalla' if (w > 960 or h > 544) else '<=960x544' if m > 512 else '<=512' if m > 256 else '<=256' if m > 64 else '<=64'
        buckets[k] += 1; mem[k] += raw
    for k, v in buckets.items(): print(f"  {k:>10}: {v:5d} imagenes, {mem[k]/MB:7.1f} MB ARGB")
    # memoria si se limitan a la pantalla (escala proporcional) y a la mitad
    def capped(w, h, cw, ch):
        s = min(1.0, cw / w, ch / h); return max(1, int(w * s)) * max(1, int(h * s)) * 4
    for cw, ch, name in [(960, 544, 'pantalla'), (480, 272, 'media pantalla')]:
        print(f"  limitadas a {name}: {sum(capped(w, h, cw, ch) for _, _, _, w, h, _ in imgs)/MB:.1f} MB ARGB")
    print(f"  en RGBA4444/RGB565 (2 B/px): {tot_raw/2/MB:.1f} MB;  en DXT5 (1 B/px): {tot_raw/4/MB:.1f} MB")
    print(f"las {top} mayores:")
    for rel, t, st, w, h, raw in sorted(imgs, key=lambda i: -i[5])[:top]:
        print(f"  {w:5d}x{h:<5d} {raw/MB:6.1f} MB  {rel}")
    # por carpeta
    per = collections.defaultdict(lambda: [0, 0])
    for rel, t, st, w, h, raw in imgs:
        d = rel.rsplit('/', 1)[0] if '/' in rel else '.'
        per[d][0] += 1; per[d][1] += raw
    print("por carpeta (ARGB):")
    for d, (c, r) in sorted(per.items(), key=lambda kv: -kv[1][1])[:15]:
        print(f"  {r/MB:7.1f} MB  {c:5d}  {d}")

main()
