// font.h — fuentes del juego (DefineFont3 del SWF) y rasterizado de glifos por software.
// Las fuentes del juego son de pixel, dibujadas como contornos; se rasterizan con supermuestreo 4x4 y regla de
// devanado no nulo, y cada glifo se guarda en cache por tamaño.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "platform.h"

namespace font {

struct Pt { double x, y; };
struct Glyph {
    uint16_t code = 0;
    double advance = 0;                      // en unidades EM (20480 = 1 em)
    std::vector<std::vector<Pt>> contours;   // poligonos ya aplanados, en unidades EM, y hacia abajo, base en 0
};
struct Bitmap {  // glifo rasterizado
    int w = 0, h = 0, left = 0, top = 0;     // top: desplazamiento desde la linea base (negativo = por encima)
    double advance = 0;                      // en pixeles
    std::vector<uint8_t> alpha;
};

class BitReader {
public:
    BitReader(const uint8_t* d, size_t n, size_t pos) : d_(d), n_(n), byte_(pos) {}
    uint32_t ub(int bits) {
        uint32_t v = 0;
        for (int i = 0; i < bits; i++) {
            if (byte_ >= n_) return v;
            v = (v << 1) | ((d_[byte_] >> (7 - bit_)) & 1);
            if (++bit_ == 8) { bit_ = 0; byte_++; }
        }
        return v;
    }
    int32_t sb(int bits) {
        if (bits == 0) return 0;
        uint32_t v = ub(bits);
        if (v & (1u << (bits - 1))) v |= ~0u << bits;
        return (int32_t)v;
    }
    void align() { if (bit_) { bit_ = 0; byte_++; } }
    size_t pos() const { return byte_; }
private:
    const uint8_t* d_; size_t n_, byte_; int bit_ = 0;
};

class Font {
public:
    std::string name;
    double ascent = 0, descent = 0, leading = 0;  // unidades EM
    std::vector<Glyph> glyphs;
    std::map<uint16_t, int> byCode;

    static constexpr double EM = 20480.0;

    bool load(const std::string& bytes) {
        const uint8_t* d = (const uint8_t*)bytes.data();
        size_t n = bytes.size();
        if (n < 8) return false;
        uint8_t flags = d[2];
        bool hasLayout = flags & 0x80, wideOffsets = flags & 0x08, wideCodes = flags & 0x04;
        size_t p = 4;
        uint8_t nlen = d[p++];
        name.assign((const char*)d + p, nlen);
        while (!name.empty() && name.back() == 0) name.pop_back();
        p += nlen;
        uint16_t ng = (uint16_t)(d[p] | d[p + 1] << 8);
        p += 2;
        size_t tableStart = p;
        auto rd = [&](size_t at, bool wide) -> uint32_t {
            return wide ? (uint32_t)(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | d[at + 3] << 24) : (uint32_t)(d[at] | d[at + 1] << 8);
        };
        int ow = wideOffsets ? 4 : 2;
        std::vector<uint32_t> offs(ng);
        for (int i = 0; i < ng; i++) offs[i] = rd(tableStart + (size_t)i * ow, wideOffsets);
        uint32_t codeOff = ng ? rd(tableStart + (size_t)ng * ow, wideOffsets) : 0;
        glyphs.resize(ng);
        for (int i = 0; i < ng; i++) parseShape(d, n, tableStart + offs[i], glyphs[i]);
        size_t cp = tableStart + codeOff;
        for (int i = 0; i < ng; i++) {
            glyphs[i].code = wideCodes ? (uint16_t)(d[cp] | d[cp + 1] << 8) : d[cp];
            cp += wideCodes ? 2 : 1;
            byCode[glyphs[i].code] = i;
        }
        if (hasLayout && cp + 6 <= n) {
            ascent = (uint16_t)(d[cp] | d[cp + 1] << 8);
            descent = (uint16_t)(d[cp + 2] | d[cp + 3] << 8);
            leading = (int16_t)(d[cp + 4] | d[cp + 5] << 8);
            cp += 6;
            for (int i = 0; i < ng && cp + 2 <= n; i++, cp += 2) glyphs[i].advance = (int16_t)(d[cp] | d[cp + 1] << 8);
        }
        return true;
    }

    const Glyph* glyph(uint32_t code) const {
        auto it = byCode.find((uint16_t)code);
        return it == byCode.end() ? nullptr : &glyphs[it->second];
    }

    // Glifo rasterizado a `size` pixeles por em (cache)
    const Bitmap& raster(uint32_t code, double size) {
        uint64_t key = ((uint64_t)code << 32) | (uint32_t)(size * 64);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        Bitmap& b = cache_[key];
        const Glyph* g = glyph(code);
        double sc = size / EM;
        if (!g) { b.advance = size * 0.5; return b; }
        b.advance = g->advance * sc;
        double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
        for (auto& c : g->contours) for (auto& q : c) { minx = std::min(minx, q.x); maxx = std::max(maxx, q.x); miny = std::min(miny, q.y); maxy = std::max(maxy, q.y); }
        if (maxx < minx) return b;
        b.left = (int)std::floor(minx * sc); b.top = (int)std::floor(miny * sc);
        b.w = (int)std::ceil(maxx * sc) - b.left; b.h = (int)std::ceil(maxy * sc) - b.top;
        if (b.w <= 0 || b.h <= 0) { b.w = b.h = 0; return b; }
        const int SS = 4;
        std::vector<int> cover((size_t)b.w * b.h, 0);
        std::vector<std::pair<double, int>> xs;
        for (int sy = 0; sy < b.h * SS; sy++) {
            double y = (b.top + (sy + 0.5) / SS) / sc;  // centro de la sub-fila, en unidades EM
            xs.clear();
            for (auto& c : g->contours) {
                for (size_t i = 0; i < c.size(); i++) {
                    const Pt& a = c[i]; const Pt& z = c[(i + 1) % c.size()];
                    if ((a.y <= y) == (z.y <= y)) continue;
                    double x = a.x + (y - a.y) * (z.x - a.x) / (z.y - a.y);
                    xs.push_back({x * sc - b.left, z.y > a.y ? 1 : -1});
                }
            }
            std::sort(xs.begin(), xs.end());
            int wind = 0;
            for (size_t i = 0; i + 1 < xs.size(); i++) {
                wind += xs[i].second;
                if (wind == 0) continue;
                int x0 = (int)std::ceil(xs[i].first * SS - 0.5), x1 = (int)std::ceil(xs[i + 1].first * SS - 0.5);
                for (int sx = std::max(0, x0); sx < std::min(b.w * SS, x1); sx++) cover[(size_t)(sy / SS) * b.w + sx / SS]++;
            }
        }
        b.alpha.resize(cover.size());
        for (size_t i = 0; i < cover.size(); i++) b.alpha[i] = (uint8_t)std::min(255, cover[i] * 255 / (SS * SS));
        return b;
    }

private:
    std::map<uint64_t, Bitmap> cache_;

    static void parseShape(const uint8_t* d, size_t n, size_t at, Glyph& g) {
        if (at >= n) return;
        BitReader br(d, n, at);
        int fillBits = br.ub(4), lineBits = br.ub(4);
        double x = 0, y = 0;
        std::vector<Pt> cur;
        auto flush = [&]() { if (cur.size() >= 3) g.contours.push_back(cur); cur.clear(); };
        for (int guard = 0; guard < 100000; guard++) {
            if (br.ub(1) == 0) {
                uint32_t fl = br.ub(5);
                if (fl == 0) break;
                if (fl & 1) { int mb = br.ub(5); x = br.sb(mb); y = br.sb(mb); flush(); cur.push_back({x, y}); }
                if (fl & 2) br.ub(fillBits);
                if (fl & 4) br.ub(fillBits);
                if (fl & 8) br.ub(lineBits);
                if (fl & 16) break;  // estilos nuevos: no aparecen en fuentes
            } else if (br.ub(1)) {  // recta
                int nb = br.ub(4) + 2;
                double dx = 0, dy = 0;
                if (br.ub(1)) { dx = br.sb(nb); dy = br.sb(nb); }
                else if (br.ub(1)) dy = br.sb(nb); else dx = br.sb(nb);
                if (cur.empty()) cur.push_back({x, y});
                x += dx; y += dy; cur.push_back({x, y});
            } else {  // curva cuadratica: se aplana en 6 tramos
                int nb = br.ub(4) + 2;
                double cx = x + br.sb(nb), cy = y + br.sb(nb);
                double ax = cx + br.sb(nb), ay = cy + br.sb(nb);
                if (cur.empty()) cur.push_back({x, y});
                for (int k = 1; k <= 6; k++) {
                    double t = k / 6.0, u = 1 - t;
                    cur.push_back({u * u * x + 2 * u * t * cx + t * t * ax, u * u * y + 2 * u * t * cy + t * t * ay});
                }
                x = ax; y = ay;
            }
        }
        flush();
    }
};

// Registro de fuentes por nombre (sin distinguir mayusculas); se cargan de <datos>/fonts/<nombre>.df3
inline Font* get(const std::string& rawName) {
    static std::map<std::string, Font*> fonts;
    std::string n;
    for (char c : rawName) n += (char)std::tolower((unsigned char)c);
    if (n.empty()) n = "system";
    auto it = fonts.find(n);
    if (it != fonts.end()) return it->second;
    std::string bytes;
    Font* f = nullptr;
    if (platform::readFile("fonts/" + n + ".df3", bytes)) { f = new Font(); if (!f->load(bytes)) { delete f; f = nullptr; } }
    if (!f && n != "system") f = get("system");
    fonts[n] = f;
    return f;
}

}  // namespace font
