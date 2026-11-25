// png.h — decodificador PNG minimo y autonomo (zlib): todos los tipos de color y profundidades de bit (1-16),
// paletas con tRNS e interlazado Adam7. Salida: pixeles 0xAARRGGBB (formato de BitmapData de Flash, alfa sin premultiplicar).
// El juego lee pixeles de PNG en su logica (p. ej. el mapa del mundo sale de MapMiniMichigan.png), asi que no basta con dibujarlos.
#pragma once
#include <zlib.h>

#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace png {

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> argb;
};

inline uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Solo la cabecera (rapido): ancho y alto
inline bool header(const std::string& d, int& w, int& h) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 33 || std::memcmp(d.data(), sig, 8) != 0) return false;
    const unsigned char* p = (const unsigned char*)d.data();
    if (std::memcmp(p + 12, "IHDR", 4) != 0) return false;
    w = (int)be32(p + 16);
    h = (int)be32(p + 20);
    return w > 0 && h > 0;
}

inline bool decode(const std::string& d, Image& out, std::string* err = nullptr) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    int W = 0, H = 0;
    if (!header(d, W, H)) return fail("no es un PNG");
    const unsigned char* b = (const unsigned char*)d.data();
    int depth = b[24], ctype = b[25], interlace = b[28];
    if (b[26] != 0 || b[27] != 0) return fail("metodo PNG no soportado");
    int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels) return fail("tipo de color desconocido");
    int bpp = channels * depth;  // bits por pixel
    std::vector<uint32_t> palette;
    std::vector<unsigned char> trns;
    std::string idat;
    size_t pos = 33;
    while (pos + 12 <= d.size()) {
        uint32_t len = be32(b + pos);
        if (pos + 12 + (size_t)len > d.size()) return fail("fragmento truncado");
        const unsigned char* c = b + pos + 4;
        const unsigned char* data = b + pos + 8;
        if (!std::memcmp(c, "PLTE", 4)) { for (uint32_t i = 0; i + 2 < len; i += 3) palette.push_back(0xFF000000u | (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | data[i + 2]); }
        else if (!std::memcmp(c, "tRNS", 4)) trns.assign(data, data + len);
        else if (!std::memcmp(c, "IDAT", 4)) idat.append((const char*)data, len);
        else if (!std::memcmp(c, "IEND", 4)) break;
        pos += 12 + (size_t)len;
    }
    // tamaño descomprimido esperado
    static const int ax[7] = {0, 4, 0, 2, 0, 1, 0}, ay[7] = {0, 0, 4, 0, 2, 0, 1}, dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    struct Pass { int x0, y0, sx, sy, pw, ph; size_t rowBytes; };
    std::vector<Pass> passes;
    if (!interlace) passes.push_back({0, 0, 1, 1, W, H, ((size_t)W * bpp + 7) / 8});
    else for (int i = 0; i < 7; i++) {
        int pw = (W - ax[i] + dx[i] - 1) / dx[i], ph = (H - ay[i] + dy[i] - 1) / dy[i];
        if (W <= ax[i] || H <= ay[i]) pw = ph = 0;
        passes.push_back({ax[i], ay[i], dx[i], dy[i], pw, ph, pw ? ((size_t)pw * bpp + 7) / 8 : 0});
    }
    size_t need = 0;
    for (auto& p : passes) need += p.ph ? (size_t)p.ph * (p.rowBytes + 1) : 0;
    std::vector<unsigned char> raw(need);
    uLongf destLen = (uLongf)need;
    int rc = uncompress(raw.data(), &destLen, (const Bytef*)idat.data(), (uLong)idat.size());
    if (rc != Z_OK && !(rc == Z_BUF_ERROR && destLen == need)) return fail("datos de imagen danados");
    if (destLen < need) return fail("datos de imagen incompletos");

    out.w = W; out.h = H;
    out.argb.assign((size_t)W * H, 0);
    size_t off = 0;
    int bytesPP = std::max(1, bpp / 8);
    for (auto& ps : passes) {
        if (!ps.ph) continue;
        std::vector<unsigned char> prev(ps.rowBytes, 0), cur(ps.rowBytes);
        for (int y = 0; y < ps.ph; y++) {
            int ft = raw[off++];
            std::memcpy(cur.data(), &raw[off], ps.rowBytes);
            off += ps.rowBytes;
            for (size_t i = 0; i < ps.rowBytes; i++) {
                int a = i >= (size_t)bytesPP ? cur[i - bytesPP] : 0, up = prev[i], c = i >= (size_t)bytesPP ? prev[i - bytesPP] : 0, v = cur[i];
                switch (ft) {
                    case 0: break;
                    case 1: v += a; break;
                    case 2: v += up; break;
                    case 3: v += (a + up) / 2; break;
                    case 4: { int p = a + up - c, pa = std::abs(p - a), pb = std::abs(p - up), pc = std::abs(p - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? up : c); break; }
                    default: return fail("filtro PNG desconocido");
                }
                cur[i] = (unsigned char)v;
            }
            for (int x = 0; x < ps.pw; x++) {
                auto sample = [&](int ch) -> uint32_t {  // muestra `ch` del pixel x, llevada a 0..65535 segun profundidad
                    size_t bit = (size_t)x * bpp + (size_t)ch * depth;
                    if (depth == 8) return cur[bit / 8];
                    if (depth == 16) return (uint32_t)cur[bit / 8] << 8 | cur[bit / 8 + 1];
                    return (cur[bit / 8] >> (8 - depth - (bit % 8))) & ((1u << depth) - 1);
                };
                auto to8 = [&](uint32_t s) -> uint32_t {
                    if (depth == 8) return s;
                    if (depth == 16) return s >> 8;
                    return s * 255 / ((1u << depth) - 1);
                };
                uint32_t r, g, bl, a = 255;
                if (ctype == 0) { uint32_t s = sample(0); r = g = bl = to8(s); if (trns.size() >= 2 && s == (uint32_t)((trns[0] << 8) | trns[1])) a = 0; }
                else if (ctype == 2) {
                    uint32_t sr = sample(0), sg = sample(1), sb = sample(2);
                    r = to8(sr); g = to8(sg); bl = to8(sb);
                    if (trns.size() >= 6 && sr == (uint32_t)((trns[0] << 8) | trns[1]) && sg == (uint32_t)((trns[2] << 8) | trns[3]) && sb == (uint32_t)((trns[4] << 8) | trns[5])) a = 0;
                } else if (ctype == 3) {
                    uint32_t idx = sample(0);
                    uint32_t col = idx < palette.size() ? palette[idx] : 0xFF000000u;
                    r = (col >> 16) & 255; g = (col >> 8) & 255; bl = col & 255;
                    a = idx < trns.size() ? trns[idx] : 255;
                } else if (ctype == 4) { r = g = bl = to8(sample(0)); a = to8(sample(1)); }
                else { r = to8(sample(0)); g = to8(sample(1)); bl = to8(sample(2)); a = to8(sample(3)); }
                int px = ps.x0 + x * ps.sx, py = ps.y0 + y * ps.sy;
                out.argb[(size_t)py * W + px] = a << 24 | r << 16 | g << 8 | bl;
            }
            prev = cur;
        }
    }
    return true;
}

// Codifica pixeles 0xAARRGGBB a PNG RGBA (para capturas y pruebas).
template <class V> std::string encode(int W, int H, const V& argb) {
    std::string raw;
    raw.reserve((size_t)H * (W * 4 + 1));
    for (int y = 0; y < H; y++) {
        raw += '\0';
        for (int x = 0; x < W; x++) {
            uint32_t c = argb[(size_t)y * W + x];
            raw += (char)((c >> 16) & 255); raw += (char)((c >> 8) & 255); raw += (char)(c & 255); raw += (char)(c >> 24);
        }
    }
    uLongf zl = compressBound((uLong)raw.size());
    std::string z(zl, '\0');
    compress2((Bytef*)&z[0], &zl, (const Bytef*)raw.data(), (uLong)raw.size(), 6);
    z.resize(zl);
    auto be = [](uint32_t v) { std::string s(4, '\0'); s[0] = (char)(v >> 24); s[1] = (char)(v >> 16); s[2] = (char)(v >> 8); s[3] = (char)v; return s; };
    auto chunk = [&](const char* type, const std::string& data) {
        std::string c = type + data;
        return be((uint32_t)data.size()) + c + be((uint32_t)crc32(0, (const Bytef*)c.data(), (uInt)c.size()));
    };
    std::string ihdr = be((uint32_t)W) + be((uint32_t)H) + std::string("\x08\x06\x00\x00\x00", 5);
    return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", z) + chunk("IEND", "");
}

}  // namespace png
