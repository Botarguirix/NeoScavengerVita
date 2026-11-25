// platform.h — capa de plataforma del runtime: ruta de datos, lectura de archivos, hora y log.
// PC y Vita comparten esta interfaz; cada backend fija la ruta de datos al arrancar.
#pragma once
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

namespace platform {

// Carpeta que contiene `data/`, `img/`, `OST/` y los mods:  PC = carpeta de Steam; Vita = ux0:data/neoscavvita/
inline std::string& dataRoot() { static std::string r = "./"; return r; }

// Paquete de datos NSDPAK02 (tools/vita/mkdatapak.cpp): un solo archivo en lugar de miles (en la Vita abrir archivos es
// lento). Las imagenes van ya decodificadas (ARGB 32 bits) y comprimidas con LZ4: no se decodifica PNG en la consola.
// Formato (little endian): "NSDPAK02" | u32 n | n x (u16 len, ruta, u8 tipo, u64 offset, u64 guardado, u32 ancho, u32 alto,
// u32 tamano_sin_comprimir) | datos.  tipo 0 = archivo tal cual; tipo 1 = imagen (alto*ancho*4 bytes, LZ4 si guardado < sin comprimir).
struct PakEntry { uint64_t off = 0, stored = 0; uint32_t w = 0, h = 0, raw = 0; uint8_t type = 0; };
struct DataPak {
    FILE* f = nullptr;   // indice (y lectura en PC)
#ifdef __vita__
    SceUID fd = -1;      // datos: sceIoPread directo. Medido en la Vita: fread 2,9 MB/s, sceIoRead 10,6-13,6 MB/s
#endif
    std::unordered_map<std::string, PakEntry> idx;
    bool open(const std::string& path) {
        f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::setvbuf(f, nullptr, _IOFBF, 256 * 1024);
        char magic[8]; uint32_t n = 0;
        if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "NSDPAK02", 8) != 0 || std::fread(&n, 4, 1, f) != 1) { std::fclose(f); f = nullptr; return false; }
        for (uint32_t i = 0; i < n; i++) {
            uint16_t l = 0; std::fread(&l, 2, 1, f);
            std::string p(l, '\0'); std::fread(&p[0], 1, l, f);
            PakEntry e; std::fread(&e.type, 1, 1, f);
            std::fread(&e.off, 8, 1, f); std::fread(&e.stored, 8, 1, f);
            std::fread(&e.w, 4, 1, f); std::fread(&e.h, 4, 1, f); std::fread(&e.raw, 4, 1, f);
            idx[p] = e;
        }
#ifdef __vita__
        fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
#endif
        return true;
    }
    // Lee n bytes desde off (posicional, sin buferes de newlib)
    bool read(uint64_t off, void* dst, size_t n) {
        if (n == 0) return true;
#ifdef __vita__
        if (fd >= 0) {
            uint8_t* d = (uint8_t*)dst; size_t got = 0;
            while (got < n) {
                int r = sceIoPread(fd, d + got, (SceSize)std::min<size_t>(n - got, 4u << 20), (SceOff)(off + got));
                if (r <= 0) return false;
                got += (size_t)r;
            }
            return true;
        }
#endif
        std::fseek(f, (long)off, SEEK_SET);
        return std::fread(dst, 1, n, f) == n;
    }
};
inline DataPak& dataPak() { static DataPak p; return p; }
inline void setDataRoot(std::string r) {
    if (!r.empty() && r.back() != '/' && r.back() != '\\') r += '/';
    dataRoot() = std::move(r);
    dataPak().open(dataRoot() + "data.pak");
}

// "file:///data/x.xml", "./img/a.png", "data\\x.xml" -> "data/x.xml" (clave del paquete)
inline std::string pakKey(const std::string& rel) {
    std::string k = rel;
    if (k.compare(0, 8, "file:///") == 0) k = k.substr(8);
    for (auto& c : k) if (c == '\\') c = '/';
    while (k.compare(0, 2, "./") == 0) k = k.substr(2);
    while (!k.empty() && k[0] == '/') k = k.substr(1);
    const std::string& root = dataRoot();
    if (k.compare(0, root.size(), root) == 0) k = k.substr(root.size());
    return k;
}
inline std::string resolve(const std::string& rel) {
    // ya es absoluta: /..., C:/..., ux0:..., ur0:... (dispositivo terminado en ':' antes de la primera barra)
    if (!rel.empty() && rel[0] == '/') return rel;
    size_t colon = rel.find(':'), slash = rel.find('/');
    if (colon != std::string::npos && colon > 0 && (slash == std::string::npos || colon < slash) && rel.compare(0, 7, "file://") != 0) return rel;
    return dataRoot() + rel;
}
inline bool readFile(const std::string& rel, std::string& out) {
    DataPak& pk = dataPak();
    if (pk.f) {
        auto it = pk.idx.find(pakKey(rel));
        if (it != pk.idx.end() && it->second.type == 0) {
            out.resize((size_t)it->second.stored);
            return out.empty() || pk.read(it->second.off, &out[0], out.size());
        }
        if (std::getenv("NEOS_PAKDEBUG")) std::printf("[pak] no esta en el paquete: '%s' (clave '%s')\n", rel.c_str(), pakKey(rel).c_str());
    }
    // fopen/fread: en la Vita std::ifstream no lee rutas de dispositivo (ux0:...)
    FILE* f = std::fopen(resolve(rel).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = n == 0 || std::fread(&out[0], 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    return ok;
}
inline bool fileExists(const std::string& rel) {
    if (dataPak().f && dataPak().idx.count(pakKey(rel))) return true;  // (imagenes incluidas)
    FILE* f = std::fopen(resolve(rel).c_str(), "rb"); if (f) std::fclose(f); return f != nullptr;
}
inline bool writeFile(const std::string& rel, const std::string& data) {
    std::ofstream f(resolve(rel), std::ios::binary);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

// ---- guardado: carpeta <datos>/save/ (se crea si no existe) y escritura segura (temporal + renombrar: si la consola se
// apaga a mitad, queda la partida anterior completa)
inline std::string saveDir() {
    std::string d = dataRoot() + "save/";
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0777);
#endif
    return d;
}
inline bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) { std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// ---- imagenes: del paquete (ya decodificadas) o, si no estan, del PNG suelto (lo decodifica el llamador)
inline const PakEntry* pakImage(const std::string& rel) {
    DataPak& pk = dataPak();
    if (!pk.f) return nullptr;
    auto it = pk.idx.find(pakKey(rel));
    return (it != pk.idx.end() && it->second.type == 1) ? &it->second : nullptr;
}
// Imagenes que el port no muestra (prefijos de la clave del paquete): se sustituyen por 1x1 transparente. Lo usa
// ajustes_vita.h para el fondo animado del titulo, que el usuario pidio quitar: sus 11 capas de 1360 px ocupaban
// ~64 MB de la zona de pixeles (medido en la Vita: 63-67 de 72 MB ya en el menu, y el menu de pausa las vuelve a crear).
inline std::vector<std::string>& omittedImages() { static std::vector<std::string> v; return v; }
inline bool isOmittedImage(const std::string& rel) {
    if (omittedImages().empty()) return false;
    std::string k = pakKey(rel);
    for (auto& p : omittedImages()) if (k.compare(0, p.size(), p) == 0) return true;
    return false;
}
// Tamano de una imagen sin leer sus pixeles (del indice del paquete)
inline bool imageSize(const std::string& rel, int& w, int& h) {
    if (const PakEntry* e = pakImage(rel)) {
        if (isOmittedImage(rel)) { w = h = 1; return true; }
        w = (int)e->w; h = (int)e->h; return true;
    }
    return false;
}
// Lee los pixeles de una imagen del paquete en dst (capacidad >= w*h*4 + slack). Devuelve false si no esta en el paquete.
template <class Decompress>
inline bool readImagePixels(const std::string& rel, uint8_t* dst, Decompress&& dec) {
    const PakEntry* e = pakImage(rel);
    if (!e) return false;
    if (isOmittedImage(rel)) { std::memset(dst, 0, 4); return true; }
    DataPak& pk = dataPak();
    if (e->stored == e->raw) return pk.read(e->off, dst, e->raw);
    static std::vector<uint8_t> tmp;
    tmp.resize((size_t)e->stored);
    if (!pk.read(e->off, tmp.data(), tmp.size())) return false;
    return dec(tmp.data(), tmp.size(), dst, (size_t)e->raw);
}

// "/_assets/189_nombre.bin" -> "assets/189.bin"  (el prefijo numerico es el id del recurso en el SWF)
inline std::string embedPath(const char* src) {
    std::string s = src ? src : "";
    size_t slash = s.rfind('/');
    std::string base = slash == std::string::npos ? s : s.substr(slash + 1);
    size_t us = base.find('_'), dot = base.rfind('.');
    if (us == std::string::npos || dot == std::string::npos) return "assets/" + base;
    return "assets/" + base.substr(0, us) + base.substr(dot);
}

inline double nowMs() {
    static const auto t0 = std::chrono::steady_clock::now();
    return (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count() / 1000.0;
}

// Log con la etiqueta de cuello de botella del proyecto: [CPU-bound], [GPU-bound], [VMEM-bound], [RAM-bound], [bandwidth-bound]
// o categorias normales ([datos], [mods], [arranque]...). En la Vita se redirige a port.log.
// Archivo de log: en la Vita printf no se puede redirigir, asi que log() y logf() escriben en logFile() si esta fijado
inline FILE*& logFile() { static FILE* f = nullptr; return f; }
inline void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void logf(const char* fmt, ...) {
    FILE* out = logFile() ? logFile() : stdout;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(out, fmt, ap);
    va_end(ap);
    std::fflush(out);
}
inline void log(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
inline void log(const char* tag, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    FILE* out = logFile() ? logFile() : stdout;
    std::fprintf(out, "%s %s\n", tag, buf);
    std::fflush(out);
}

}  // namespace platform
