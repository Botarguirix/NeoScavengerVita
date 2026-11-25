// modmgr.h — soporte de mods de NEO Scavenger (formato original) y de NexusMods. Independiente de la plataforma
// (PC y Vita); solo usa la libreria estandar y zlib.
//
// Formato de mods del juego original (ver SampleModFolder/_readme.txt):
//   * getmods.php lista los mods:  nRows=2&strModName0=SampleMod&strModURL0=SampleModFolder&strModName1=0&strModURL1=SampleModFolder/0
//   * cada carpeta de mod aporta  data/*.xml  (contenido NUEVO, se añade) y/o es el mod especial "0" (SUSTITUYE datos
//     originales). Tambien puede traer imagenes (getimages.php).
// En la Vita no hay servidor ni getmods.php editable por el usuario: el gestor detecta las carpetas de
// <datos>/mods/<Mod>/ y construye la lista; un mod descargado como .zip de NexusMods se instala con installZip().
#pragma once
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#define MM_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MM_MKDIR(p) mkdir(p, 0777)
#endif

namespace modmgr {

// ------------------------------------------------------------------ utilidades de archivos
inline bool fileExists(const std::string& p) { std::ifstream f(p, std::ios::binary); return (bool)f; }
inline bool readFile(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
inline bool mkdirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && cur.back() != ':') MM_MKDIR(cur.c_str());
        }
        if (i < path.size()) cur += path[i];
    }
    return true;
}
inline std::string dirOf(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? "" : p.substr(0, s); }

// ------------------------------------------------------------------ lector ZIP (sin ZIP64; metodos 0 y 8)
struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t csize = 0, usize = 0, offset = 0;
    bool isDir() const { return !name.empty() && name.back() == '/'; }
};
class ZipReader {
public:
    bool open(const std::string& path, std::string* err = nullptr) {
        if (!readFile(path, data_)) { if (err) *err = "no se puede leer " + path; return false; }
        return parse(err);
    }
    bool openMemory(std::string bytes, std::string* err = nullptr) { data_ = std::move(bytes); return parse(err); }
    const std::vector<ZipEntry>& entries() const { return entries_; }
    // extrae una entrada a memoria; false si esta danada o supera `maxBytes`
    bool extract(const ZipEntry& e, std::string& out, size_t maxBytes = 64u << 20, std::string* err = nullptr) const {
        auto fail = [&](const char* m) { if (err) *err = m; return false; };
        if (e.usize > maxBytes) return fail("archivo demasiado grande");
        if ((uint64_t)e.offset + 30 > data_.size()) return fail("cabecera local fuera de rango");
        const unsigned char* p = (const unsigned char*)data_.data() + e.offset;
        if (u32(p) != 0x04034b50) return fail("cabecera local invalida");
        size_t start = (size_t)e.offset + 30 + u16(p + 26) + u16(p + 28);
        if (start + e.csize > data_.size()) return fail("datos fuera de rango");
        if (e.method == 0) { out.assign(data_, start, e.usize); return out.size() == e.usize; }
        if (e.method != 8) return fail("metodo de compresion no soportado (solo almacenado y deflate)");
        out.resize(e.usize);
        z_stream zs{};
        if (inflateInit2(&zs, -15) != Z_OK) return fail("zlib no inicializa");
        zs.next_in = (Bytef*)data_.data() + start; zs.avail_in = e.csize;
        zs.next_out = (Bytef*)&out[0]; zs.avail_out = e.usize;
        int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != e.usize) return fail("datos comprimidos danados");
        return true;
    }
private:
    std::string data_;
    std::vector<ZipEntry> entries_;
    static uint16_t u16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
    static uint32_t u32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
    bool parse(std::string* err) {
        entries_.clear();
        auto fail = [&](const char* m) { if (err) *err = m; return false; };
        if (data_.size() < 22) return fail("no es un zip");
        const unsigned char* b = (const unsigned char*)data_.data();
        long eocd = -1;
        for (long i = (long)data_.size() - 22; i >= 0 && i >= (long)data_.size() - 22 - 65535; i--)
            if (u32(b + i) == 0x06054b50) { eocd = i; break; }
        if (eocd < 0) return fail("no es un zip (sin directorio central)");
        uint16_t n = u16(b + eocd + 10);
        uint32_t cdOff = u32(b + eocd + 16);
        if (n == 0xFFFF || cdOff == 0xFFFFFFFFu) return fail("zip64 no soportado");
        size_t p = cdOff;
        for (uint16_t i = 0; i < n; i++) {
            if (p + 46 > data_.size() || u32(b + p) != 0x02014b50) return fail("directorio central danado");
            ZipEntry e;
            e.method = u16(b + p + 10); e.csize = u32(b + p + 20); e.usize = u32(b + p + 24); e.offset = u32(b + p + 42);
            uint16_t nl = u16(b + p + 28), xl = u16(b + p + 30), cl = u16(b + p + 32);
            if (p + 46 + nl > data_.size()) return fail("nombre fuera de rango");
            e.name.assign(data_, p + 46, nl);
            std::replace(e.name.begin(), e.name.end(), '\\', '/');
            entries_.push_back(std::move(e));
            p += 46 + nl + xl + cl;
        }
        return true;
    }
};

// ------------------------------------------------------------------ getmods.php del juego original
struct ModRef { std::string name, url; };
inline std::vector<ModRef> parseGetMods(const std::string& text) {
    std::map<std::string, std::string> kv;
    std::string cur;
    std::vector<std::string> parts;
    for (char c : text) { if (c == '&' || c == '\n' || c == '\r') { if (!cur.empty()) parts.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) parts.push_back(cur);
    for (auto& p : parts) { size_t e = p.find('='); if (e != std::string::npos) kv[p.substr(0, e)] = p.substr(e + 1); }
    std::vector<ModRef> out;
    int n = kv.count("nRows") ? std::atoi(kv["nRows"].c_str()) : 0;
    for (int i = 0; i < n; i++) {
        auto a = kv.find("strModName" + std::to_string(i)), b = kv.find("strModURL" + std::to_string(i));
        if (a != kv.end() && b != kv.end()) out.push_back({a->second, b->second});
    }
    return out;
}

// ------------------------------------------------------------------ validacion
// XML minimo bien formado: etiquetas balanceadas (para rechazar mods rotos antes de que cuelguen el arranque).
inline bool xmlWellFormed(const std::string& s, std::string* err = nullptr) {
    std::vector<std::string> st;
    size_t i = 0, n = s.size();
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    while (i < n) {
        if (s[i] != '<') { i++; continue; }
        if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i); if (e == std::string::npos) return fail("comentario sin cerrar"); i = e + 3; continue; }
        if (s.compare(i, 9, "<![CDATA[") == 0) { size_t e = s.find("]]>", i); if (e == std::string::npos) return fail("CDATA sin cerrar"); i = e + 3; continue; }
        if (s[i + 1] == '?' || s[i + 1] == '!') { size_t e = s.find('>', i); if (e == std::string::npos) return fail("declaracion sin cerrar"); i = e + 1; continue; }
        size_t e = s.find('>', i);
        if (e == std::string::npos) return fail("etiqueta sin cerrar");
        if (s[i + 1] == '/') {
            std::string name = s.substr(i + 2, e - i - 2);
            while (!name.empty() && std::isspace((unsigned char)name.back())) name.pop_back();
            if (st.empty() || st.back() != name) return fail("etiqueta de cierre inesperada </" + name + ">");
            st.pop_back();
        } else if (s[e - 1] != '/') {
            size_t j = i + 1;
            while (j < e && !std::isspace((unsigned char)s[j])) j++;
            st.push_back(s.substr(i + 1, j - i - 1));
        }
        i = e + 1;
    }
    if (!st.empty()) return fail("falta cerrar <" + st.back() + ">");
    return true;
}

// ------------------------------------------------------------------ gestor
struct ModInfo {
    std::string name;       // nombre visible = nombre de la carpeta
    std::string path;       // ruta de la carpeta (con '/' final)
    bool enabled = true;
    bool isOverride = false;  // carpeta "0" o con nombre especial de sustitucion
    std::string error;      // si no es valido, por que
    std::vector<std::string> xmlFiles, imageFiles;
};

struct InstallResult {
    bool ok = false;
    std::string modName, message;
    int files = 0;
    size_t bytes = 0;
};

class ModManager {
public:
    explicit ModManager(std::string modsDir) : dir_(std::move(modsDir)) { if (!dir_.empty() && dir_.back() != '/') dir_ += '/'; }
    const std::string& dir() const { return dir_; }

    // Instala un .zip (NexusMods): detecta la raiz del mod, protege contra rutas peligrosas y limita el tamaño.
    InstallResult installZip(const std::string& zipPath, const std::string& forcedName = "") {
        InstallResult r;
        ZipReader z;
        std::string err;
        if (!z.open(zipPath, &err)) { r.message = err; return r; }
        // raiz del mod = carpeta mas superficial que contenga data/ o getimages.php o getmods.php
        std::string root;
        bool found = false;
        size_t best = (size_t)-1;
        for (auto& e : z.entries()) {
            std::string n = e.name;
            for (const char* key : {"data/", "getimages.php", "getmods.php"}) {
                size_t pos = (n.compare(0, strlen(key), key) == 0) ? 0 : n.find(std::string("/") + key);
                if (pos == std::string::npos) continue;
                std::string prefix = pos == 0 ? "" : n.substr(0, pos + 1);
                if (prefix.size() < best) { best = prefix.size(); root = prefix; found = true; }
            }
        }
        if (!found) { r.message = "el zip no parece un mod de NEO Scavenger (no hay data/, getimages.php ni getmods.php)"; return r; }
        std::string name = forcedName;
        if (name.empty()) {
            name = root.empty() ? baseName(zipPath) : root.substr(0, root.size() - 1);
            size_t s = name.rfind('/');
            if (s != std::string::npos) name = name.substr(s + 1);
        }
        name = sanitize(name);
        if (name.empty()) { r.message = "nombre de mod invalido"; return r; }
        const size_t kMaxTotal = 256u << 20;
        std::string modRoot = dir_ + name + "/";
        for (auto& e : z.entries()) {
            if (e.name.compare(0, root.size(), root) != 0) continue;
            std::string rel = e.name.substr(root.size());
            if (rel.empty() || e.isDir()) continue;
            if (!safeRel(rel)) { r.message = "ruta peligrosa en el zip: " + e.name; return r; }
            std::string data;
            if (!z.extract(e, data, 64u << 20, &err)) { r.message = e.name + ": " + err; return r; }
            r.bytes += data.size();
            if (r.bytes > kMaxTotal) { r.message = "el mod supera el limite de 256 MB"; return r; }
            mkdirs(dirOf(modRoot + rel));
            std::ofstream f(modRoot + rel, std::ios::binary);
            if (!f) { r.message = "no se puede escribir " + modRoot + rel; return r; }
            f.write(data.data(), (std::streamsize)data.size());
            r.files++;
        }
        r.ok = true; r.modName = name;
        r.message = "instalado " + name + " (" + std::to_string(r.files) + " archivos)";
        return r;
    }

    // Busca mods ya instalados: cualquier carpeta de <mods>/ con data/, getimages.php o getmods.php.
    // `folders` lo da la plataforma (listar un directorio es distinto en Vita y PC).
    std::vector<ModInfo> scan(const std::vector<std::string>& folders) {
        std::vector<ModInfo> mods;
        std::set<std::string> off = readOrder(order_);
        for (auto& f : folders) {
            ModInfo m;
            m.name = f; m.path = dir_ + f + "/";
            m.isOverride = (f == "0");
            m.enabled = !off.count(f);
            mods.push_back(m);
        }
        // orden: modorder.txt manda; el resto por nombre
        std::stable_sort(mods.begin(), mods.end(), [&](const ModInfo& a, const ModInfo& b) {
            auto ia = std::find(order_.begin(), order_.end(), a.name), ib = std::find(order_.begin(), order_.end(), b.name);
            if (ia != order_.end() && ib != order_.end()) return ia < ib;
            if (ia != order_.end()) return true;
            if (ib != order_.end()) return false;
            return a.name < b.name;
        });
        return mods;
    }
    // modorder.txt:  una linea por mod, en orden de carga. Prefijo '-' = desactivado.
    void loadOrder() { std::string t; order_.clear(); if (readFile(dir_ + "modorder.txt", t)) parseOrder(t); }
    bool saveOrder(const std::vector<ModInfo>& mods) {
        std::ofstream f(dir_ + "modorder.txt");
        if (!f) return false;
        for (auto& m : mods) f << (m.enabled ? "" : "-") << m.name << "\n";
        return true;
    }
    // Valida los XML de un mod; si alguno esta roto, el mod queda desactivado con el motivo.
    static bool validate(ModInfo& m, const std::vector<std::string>& xmlRelPaths) {
        for (auto& rel : xmlRelPaths) {
            std::string t, e;
            if (!readFile(m.path + rel, t)) continue;
            if (!xmlWellFormed(t, &e)) { m.enabled = false; m.error = rel + ": " + e; return false; }
        }
        return true;
    }
    // Lista que el juego espera (equivalente al getmods.php): primero los mods con contenido, el "0" de sustitucion al final de cada uno
    static std::vector<ModRef> toGetMods(const std::vector<ModInfo>& mods) {
        std::vector<ModRef> out;
        for (auto& m : mods) if (m.enabled) out.push_back({m.name, m.path});
        return out;
    }

private:
    std::string dir_;
    std::vector<std::string> order_;
    static std::string baseName(const std::string& p) {
        std::string n = p;
        size_t s = n.find_last_of("/\\");
        if (s != std::string::npos) n = n.substr(s + 1);
        size_t d = n.rfind('.');
        if (d != std::string::npos) n = n.substr(0, d);
        return n;
    }
    static std::string sanitize(const std::string& s) {
        std::string r;
        for (char c : s) if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ' || c == '.') r += c;
        while (!r.empty() && (r.back() == '.' || r.back() == ' ')) r.pop_back();
        if (r == "." || r == "..") r.clear();
        return r;
    }
    static bool safeRel(const std::string& rel) {
        if (rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos || rel.find("//") != std::string::npos) return false;
        std::stringstream ss(rel); std::string seg;
        while (std::getline(ss, seg, '/')) if (seg == ".." ) return false;
        return true;
    }
    void parseOrder(const std::string& t) {
        std::stringstream ss(t); std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            bool dis = line[0] == '-';
            order_.push_back(dis ? line.substr(1) : line);
            if (dis) disabled_.insert(line.substr(1));
        }
    }
    std::set<std::string> readOrder(const std::vector<std::string>&) { return disabled_; }
    std::set<std::string> disabled_;
};

}  // namespace modmgr
