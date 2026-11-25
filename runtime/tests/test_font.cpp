// Dibuja una frase con cada fuente del juego a su tamaño habitual y a 2x, y la guarda como PNG para revisarla.
// Uso: test_font <raiz_datos> <salida.png>
#include <cstdio>

#include "font.h"
#include "png.h"

int main(int argc, char** argv) {
    platform::setDataRoot(argc > 1 ? argv[1] : "work/gameroot");
    const char* out = argc > 2 ? argv[2] : "work/test_font.png";
    const int W = 760, H = 230;
    std::vector<uint32_t> img((size_t)W * H, 0xFF202428);
    const char* names[] = {"pixelsupertiny", "pixeltiny", "pixelmid", "system"};
    double sizes[] = {8, 8, 8, 12};
    const char* text = "NEO Scavenger 0123456789 - Hunger: 45% / Wounds (bleeding)";
    int y = 6;
    for (int f = 0; f < 4; f++) {
        font::Font* fn = font::get(names[f]);
        if (!fn) { std::printf("no se pudo cargar %s\n", names[f]); continue; }
        for (double mult : {1.0, 2.0}) {
            double size = sizes[f] * mult;
            double asc = fn->ascent / font::Font::EM * size;
            double lh = (fn->ascent + fn->descent + fn->leading) / font::Font::EM * size;
            double x = 6, base = y + asc;
            for (const char* p = text; *p; p++) {
                const font::Bitmap& g = fn->raster((unsigned char)*p, size);
                int gx = (int)std::floor(x) + g.left, gy = (int)std::floor(base) + g.top;
                for (int j = 0; j < g.h; j++) for (int i = 0; i < g.w; i++) {
                    int px = gx + i, py = gy + j;
                    if (px < 0 || py < 0 || px >= W || py >= H) continue;
                    uint32_t a = g.alpha[(size_t)j * g.w + i];
                    uint32_t& d = img[(size_t)py * W + px];
                    auto mix = [&](int sh) { uint32_t dc = (d >> sh) & 255; return ((dc * (255 - a) + 235 * a) / 255) << sh; };
                    d = 0xFF000000u | mix(16) | mix(8) | mix(0);
                }
                x += g.advance;
            }
            std::printf("%-15s %4.0fpx  ascenso %.1f  linea %.1f  glifos %zu\n", names[f], size, asc, lh, fn->glyphs.size());
            y += (int)std::ceil(lh) + 6;
        }
    }
    std::string pngData = png::encode(W, H, img);
    FILE* fo = std::fopen(out, "wb");
    std::fwrite(pngData.data(), 1, pngData.size(), fo);
    std::fclose(fo);
    std::printf("guardado %s\n", out);
    return 0;
}
