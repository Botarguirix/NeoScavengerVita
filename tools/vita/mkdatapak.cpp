// mkdatapak.cpp — empaqueta los datos del juego (copia legal del usuario, ya preparados en una carpeta) en data.pak
// (formato NSDPAK02, ver runtime/platform.h). Los PNG se decodifican aqui, una vez, y se guardan como ARGB comprimido
// con LZ4: la Vita no decodifica PNG ni lee el archivo entero para conocer su tamano.
//
// Compilar: g++ -std=c++17 -O2 -I runtime tools/vita/mkdatapak.cpp -o mkdatapak.exe
// Uso:      mkdatapak <carpeta_datos> <salida.pak>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "png.h"
#include "lz4lite.h"

namespace fs = std::filesystem;

struct Item {
    std::string rel;
    uint8_t type = 0;
    uint32_t w = 0, h = 0, raw = 0;
    std::vector<uint8_t> data;  // lo que se guarda
};

static std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "uso: mkdatapak <carpeta_datos> <salida.pak>\n"); return 1; }
    fs::path root = argv[1];
    std::vector<Item> items;
    size_t pngCount = 0, pngRaw = 0, pngStored = 0, pngFail = 0;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        Item it;
        it.rel = fs::relative(e.path(), root).generic_string();
        std::vector<uint8_t> bytes = readAll(e.path());
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        bool done = false;
        if (ext == ".png") {
            png::Image img;
            std::string sb(bytes.begin(), bytes.end());
            if (png::decode(sb, img) && img.w > 0 && img.h > 0 && img.argb.size() >= (size_t)img.w * img.h) {
                it.type = 1; it.w = (uint32_t)img.w; it.h = (uint32_t)img.h; it.raw = it.w * it.h * 4;
                std::vector<uint8_t> comp;
                lz4lite::compress((const uint8_t*)img.argb.data(), it.raw, comp);
                if (comp.size() < it.raw) it.data = std::move(comp);
                else it.data.assign((const uint8_t*)img.argb.data(), (const uint8_t*)img.argb.data() + it.raw);
                pngCount++; pngRaw += it.raw; pngStored += it.data.size();
                done = true;
            } else { pngFail++; std::fprintf(stderr, "AVISO: no se pudo decodificar %s (se guarda tal cual)\n", it.rel.c_str()); }
        }
        if (!done) { it.type = 0; it.raw = (uint32_t)bytes.size(); it.data = std::move(bytes); }
        items.push_back(std::move(it));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.rel < b.rel; });

    size_t index = 8 + 4;
    for (auto& it : items) index += 2 + it.rel.size() + 1 + 8 + 8 + 4 + 4 + 4;
    FILE* f = std::fopen(argv[2], "wb");
    if (!f) { std::fprintf(stderr, "no se puede escribir %s\n", argv[2]); return 1; }
    std::fwrite("NSDPAK02", 1, 8, f);
    uint32_t n = (uint32_t)items.size();
    std::fwrite(&n, 4, 1, f);
    uint64_t off = index;
    for (auto& it : items) {
        uint16_t l = (uint16_t)it.rel.size();
        uint64_t stored = it.data.size();
        std::fwrite(&l, 2, 1, f); std::fwrite(it.rel.data(), 1, l, f);
        std::fwrite(&it.type, 1, 1, f);
        std::fwrite(&off, 8, 1, f); std::fwrite(&stored, 8, 1, f);
        std::fwrite(&it.w, 4, 1, f); std::fwrite(&it.h, 4, 1, f); std::fwrite(&it.raw, 4, 1, f);
        off += stored;
    }
    for (auto& it : items) std::fwrite(it.data.data(), 1, it.data.size(), f);
    std::fclose(f);
    std::printf("%zu archivos, %.1f MB -> %s\n", items.size(), off / 1048576.0, argv[2]);
    std::printf("imagenes: %zu (sin comprimir %.1f MB, guardadas %.1f MB = %.0f%%), fallos %zu\n", pngCount, pngRaw / 1048576.0,
                pngStored / 1048576.0, pngRaw ? 100.0 * pngStored / pngRaw : 0.0, pngFail);
    return 0;
}
