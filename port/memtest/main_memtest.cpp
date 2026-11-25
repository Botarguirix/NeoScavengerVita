// main_memtest.cpp — VPK pequeno de pruebas de memoria, carga de imagenes y dibujo con vitaGL (NEOS00002).
// No lleva datos del juego: lee el data.pak que ya esta en ux0:data/neoscavvita/ (copia legal del usuario).
// Escribe los resultados en ux0:data/neoscavvita/memtest.log. Fases (el color de fondo indica la fase en curso):
//   1 lectura del paquete           (azul)      MB/s de fread
//   2 descompresion LZ4 de todo      (verde)     MB/s, por grupo (base / x2_)
//   3 imagenes base residentes       (amarillo)  malloc por imagen vs una zona propia: tiempo, fallos, memoria
//   4 x2_ a media resolucion          (naranja)   coste de reducir al cargar y memoria resultante
//   5 texturas vitaGL                 (morado)    BGRA8 vs RGBA4444 vs DXT5: tiempo, VRAM, RAM
//   6 dibujo a 60 fps                 (escena)    N quads por fotograma: una llamada por quad vs por lotes
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <vitaGL.h>
#include <malloc.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "lz4lite.h"

extern "C" int sceAppMgrGrowMemory(unsigned int size, int killMiniApps);
extern "C" {
unsigned int _newlib_heap_size_user = 208u * 1024u * 1024u;  // igual que el juego
unsigned int sceLibcHeapSize = 4u * 1024u * 1024u;
}

static const char* kDir = "ux0:data/neoscavvita/";
static FILE* gLog = nullptr;
static void logf(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    if (gLog) { vfprintf(gLog, fmt, a); fflush(gLog); }
    va_end(a);
}
static double nowMs() { return sceKernelGetProcessTimeWide() / 1000.0; }
static const double MB = 1048576.0;

static void mem(const char* tag) {
    SceKernelFreeMemorySizeInfo info{}; info.size = sizeof info; sceKernelGetFreeMemorySize(&info);
    struct mallinfo mi = mallinfo();
    logf("[memoria] %-28s heap usado %6.1f MB (de %u) | libre usuario %5.1f MB, CDRAM %5.1f MB | vitaGL VRAM libre %5.1f MB, RAM libre %5.1f MB\n",
         tag, mi.uordblks / MB, _newlib_heap_size_user >> 20, info.size_user / MB, info.size_cdram / MB,
         vglMemFree(VGL_MEM_VRAM) / MB, vglMemFree(VGL_MEM_RAM) / MB);
}
static void phaseColor(float r, float g, float b) {  // fondo de la fase (para ver el progreso en una captura)
    for (int i = 0; i < 2; i++) { glClearColor(r, g, b, 1); glClear(GL_COLOR_BUFFER_BIT); vglSwapBuffers(GL_FALSE); }
}

// ---------------------------------------------------------------- indice del paquete (NSDPAK02, ver runtime/platform.h)
struct Entry { std::string rel; uint8_t type; uint64_t off, stored; uint32_t w, h, raw; bool x2; };
static std::vector<Entry> gIdx;
static FILE* gPak = nullptr;
static bool openPak() {
    std::string p = std::string(kDir) + "data.pak";
    gPak = fopen(p.c_str(), "rb");
    if (!gPak) { logf("ERROR: no se pudo abrir %s\n", p.c_str()); return false; }
    char magic[8]; fread(magic, 1, 8, gPak);
    if (memcmp(magic, "NSDPAK02", 8)) { logf("ERROR: %s no es NSDPAK02\n", p.c_str()); return false; }
    uint32_t n = 0; fread(&n, 4, 1, gPak);
    gIdx.resize(n);
    for (auto& e : gIdx) {
        uint16_t l = 0; fread(&l, 2, 1, gPak);
        e.rel.resize(l); fread(&e.rel[0], 1, l, gPak);
        fread(&e.type, 1, 1, gPak); fread(&e.off, 8, 1, gPak); fread(&e.stored, 8, 1, gPak);
        fread(&e.w, 4, 1, gPak); fread(&e.h, 4, 1, gPak); fread(&e.raw, 4, 1, gPak);
        e.x2 = e.rel.find("/x2_") != std::string::npos;
    }
    return true;
}
static std::vector<uint8_t> gComp;
// Lee y descomprime una imagen en dst (al menos w*h*4 + 32 bytes: decompressFast escribe hasta 32 de mas)
static bool loadImage(const Entry& e, uint8_t* dst, double* readMs = nullptr, double* decMs = nullptr) {
    double t0 = nowMs();
    if (gComp.size() < e.stored) gComp.resize(e.stored);
    fseek(gPak, (long)e.off, SEEK_SET);
    if (fread(gComp.data(), 1, e.stored, gPak) != e.stored) return false;
    double t1 = nowMs();
    bool ok = e.stored == e.raw ? (memcpy(dst, gComp.data(), e.raw), true) : lz4lite::decompressFast(gComp.data(), e.stored, dst, e.raw);
    double t2 = nowMs();
    if (readMs) *readMs += t1 - t0;
    if (decMs) *decMs += t2 - t1;
    return ok;
}

// ---------------------------------------------------------------- fases
static void fase1_lectura() {
    phaseColor(0.1f, 0.2f, 0.6f);
    logf("\n== 1 lectura del paquete\n");
    const size_t limit = 12u << 20;  // 12 MB por prueba (la tarjeta puede ser lenta)
    for (size_t chunk : {64u * 1024u, 1024u * 1024u}) {
        std::vector<uint8_t> buf(chunk);
        fseek(gPak, 0, SEEK_SET);
        double t0 = nowMs(); size_t tot = 0, r;
        while (tot < limit && (r = fread(buf.data(), 1, chunk, gPak)) > 0) tot += r;
        double ms = nowMs() - t0;
        logf("  fread       bloques de %4u KB: %.1f MB en %.0f ms = %.1f MB/s\n", (unsigned)(chunk / 1024), tot / MB, ms, tot / MB / (ms / 1000));
    }
    // sceIoRead directo con bufer alineado (sin newlib), desde el principio y desde la mitad (sin cache)
    std::string p = std::string(kDir) + "data.pak";
    for (size_t chunk : {256u * 1024u, 4u * 1024u * 1024u}) for (int half = 0; half < 2; half++) {
        void* buf = memalign(64, chunk);
        SceUID fd = sceIoOpen(p.c_str(), SCE_O_RDONLY, 0);
        SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, half ? sz / 2 : 0, SCE_SEEK_SET);
        double t0 = nowMs(); size_t tot = 0; int r;
        while (tot < limit && (r = sceIoRead(fd, buf, chunk)) > 0) tot += r;
        double ms = nowMs() - t0;
        sceIoClose(fd); free(buf);
        logf("  sceIoRead   bloques de %4u KB (%s): %.1f MB en %.0f ms = %.1f MB/s\n", (unsigned)(chunk / 1024), half ? "mitad, sin cache" : "inicio, repetido",
             tot / MB, ms, tot / MB / (ms / 1000));
    }
}

static void fase2_descompresion() {
    phaseColor(0.1f, 0.5f, 0.2f);
    logf("\n== 2 descompresion de TODAS las imagenes (sin guardarlas)\n");
    size_t maxRaw = 0; for (auto& e : gIdx) if (e.type == 1) maxRaw = std::max<size_t>(maxRaw, e.raw);
    std::vector<uint8_t> dst(maxRaw + 64);
    for (int g = 0; g < 2; g++) {
        double rd = 0, dc = 0; size_t n = 0, raw = 0, st = 0, fails = 0;
        for (auto& e : gIdx) if (e.type == 1 && e.x2 == (g == 1)) {
            if (!loadImage(e, dst.data(), &rd, &dc)) fails++;
            n++; raw += e.raw; st += e.stored;
        }
        logf("  %-5s %4u imagenes: %.1f MB en paquete -> %.1f MB ARGB | lectura %.0f ms, LZ4 %.0f ms (%.0f MB/s) | fallos %u\n",
             g ? "x2_" : "base", (unsigned)n, st / MB, raw / MB, rd, dc, raw / MB / (dc / 1000), (unsigned)fails);
    }
}

// Reduce a la mitad (promedio 2x2, ARGB con alfa premultiplicado solo para el promedio de color)
static void halve(const uint32_t* s, int w, int h, uint32_t* d) {
    int W2 = std::max(1, w / 2), H2 = std::max(1, h / 2);
    for (int y = 0; y < H2; y++) for (int x = 0; x < W2; x++) {
        uint32_t a = 0, r = 0, g = 0, b = 0;
        for (int k = 0; k < 4; k++) {
            int sx = std::min(w - 1, x * 2 + (k & 1)), sy = std::min(h - 1, y * 2 + (k >> 1));
            uint32_t p = s[(size_t)sy * w + sx], pa = p >> 24;
            a += pa; r += ((p >> 16) & 255) * pa; g += ((p >> 8) & 255) * pa; b += (p & 255) * pa;
        }
        d[(size_t)y * W2 + x] = a ? ((a / 4) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a) : 0;
    }
}

static void fase3_residentes() {
    phaseColor(0.6f, 0.6f, 0.1f);
    logf("\n== 3 imagenes base residentes en memoria (lo que el juego tendria cargado si las usara todas)\n");
    mem("antes");
    // a) malloc por imagen (como ahora con el heap de newlib)
    std::vector<uint8_t*> keep; size_t bytes = 0, fails = 0; double t0 = nowMs();
    for (auto& e : gIdx) if (e.type == 1 && !e.x2) {
        uint8_t* p = (uint8_t*)malloc(e.raw + 32);
        if (!p) { fails++; continue; }
        loadImage(e, p); keep.push_back(p); bytes += e.raw;
    }
    logf("  a) malloc por imagen: %u imagenes, %.1f MB en %.0f ms, %u sin memoria\n", (unsigned)keep.size(), bytes / MB, nowMs() - t0, (unsigned)fails);
    mem("con base en malloc");
    for (auto* p : keep) free(p);
    keep.clear();
    mem("tras liberar");
    // b) una zona propia (bloque del sistema, como la zona de pixeles del juego), asignacion lineal
    unsigned best = 0;
    for (unsigned mb = 384; mb >= 16; mb -= 4) {
        SceUID id = sceKernelAllocMemBlock("sonda", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, mb << 20, nullptr);
        if (id >= 0) { best = mb; sceKernelFreeMemBlock(id); break; }
    }
    logf("  mayor bloque del sistema reservable ahora: %u MB\n", best);
    if (best >= 32) {
        unsigned zoneMB = best - 16;
        SceUID id = sceKernelAllocMemBlock("zona", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, zoneMB << 20, nullptr);
        void* base = nullptr; sceKernelGetMemBlockBase(id, &base);
        size_t used = 0, n = 0, out = 0; t0 = nowMs();
        for (auto& e : gIdx) if (e.type == 1 && !e.x2) {
            size_t need = ((size_t)e.raw + 32 + 63) & ~(size_t)63;
            if (used + need > ((size_t)zoneMB << 20)) { out++; continue; }
            loadImage(e, (uint8_t*)base + used); used += need; n++;
        }
        logf("  b) zona propia de %u MB: %u imagenes, %.1f MB en %.0f ms, %u no caben\n", zoneMB, (unsigned)n, used / MB, nowMs() - t0, (unsigned)out);
        sceKernelFreeMemBlock(id);
    }
}

static void fase4_x2_mitad() {
    phaseColor(0.8f, 0.4f, 0.1f);
    logf("\n== 4 imagenes x2_ reducidas a la mitad al cargar\n");
    size_t maxRaw = 0; for (auto& e : gIdx) if (e.type == 1) maxRaw = std::max<size_t>(maxRaw, e.raw);
    std::vector<uint8_t> tmp(maxRaw + 64);
    std::vector<uint32_t> half(maxRaw / 4 + 16);
    double ld = 0, hv = 0; size_t n = 0, full = 0, red = 0;
    for (auto& e : gIdx) if (e.type == 1 && e.x2) {
        double t0 = nowMs(); loadImage(e, tmp.data()); double t1 = nowMs();
        halve((const uint32_t*)tmp.data(), e.w, e.h, half.data()); double t2 = nowMs();
        ld += t1 - t0; hv += t2 - t1; n++; full += e.raw; red += (size_t)std::max(1u, e.w / 2) * std::max(1u, e.h / 2) * 4;
    }
    logf("  %u imagenes: %.1f MB -> %.1f MB | carga %.0f ms, reduccion %.0f ms (%.2f ms por imagen)\n", (unsigned)n, full / MB, red / MB, ld, hv, n ? hv / n : 0);
    logf("  (alternativa sin coste en la consola: guardarlas ya reducidas en el paquete con mkdatapak)\n");
}

// ---------------------------------------------------------------- texturas
struct Tx { GLuint id; int w, h; };
static void toRGBA4444(const uint32_t* s, int w, int h, uint16_t* d) {
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
        uint32_t p = s[i];
        d[i] = (uint16_t)((((p >> 20) & 15) << 12) | (((p >> 12) & 15) << 8) | (((p >> 4) & 15) << 4) | ((p >> 28) & 15));
    }
}
static void swapRB(uint32_t* p, size_t n) {  // ARGB (0xAARRGGBB) -> bytes RGBA
    for (size_t i = 0; i < n; i++) { uint32_t v = p[i]; p[i] = (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16); }
}
static std::vector<Tx> gScene;  // texturas para la fase 6

static void fase5_texturas() {
    phaseColor(0.5f, 0.2f, 0.6f);
    logf("\n== 5 texturas vitaGL con las imagenes base\n");
    size_t maxRaw = 0; for (auto& e : gIdx) if (e.type == 1 && !e.x2) maxRaw = std::max<size_t>(maxRaw, e.raw);
    std::vector<uint8_t> tmp(maxRaw + 64);
    std::vector<uint16_t> t16(maxRaw / 2 + 32);
    struct Mode { const char* name; int kind; };
    // (DXT5 comprimida en la consola ya medida: 33 s para las imagenes base, inviable; habria que hacerlo en mkdatapak)
    for (Mode m : {Mode{"BGRA8 (como ahora)", 0}, Mode{"RGBA4444 (2 B/px)", 1}}) {
        mem("antes");
        float vram0 = vglMemFree(VGL_MEM_VRAM) / MB, ram0 = vglMemFree(VGL_MEM_RAM) / MB;
        std::vector<Tx> tx; size_t bytes = 0, errs = 0; double tl = 0, tu = 0;
        for (auto& e : gIdx) if (e.type == 1 && !e.x2) {
            double t0 = nowMs(); loadImage(e, tmp.data()); double t1 = nowMs();
            GLuint id = 0; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
            if (m.kind == 0) glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, e.w, e.h, 0, GL_BGRA, GL_UNSIGNED_BYTE, tmp.data());
            else if (m.kind == 1) { toRGBA4444((const uint32_t*)tmp.data(), e.w, e.h, t16.data());
                                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, e.w, e.h, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, t16.data()); }
            else { swapRB((uint32_t*)tmp.data(), (size_t)e.w * e.h);
                   glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, e.w, e.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data()); }
            GLenum err = glGetError();
            tu += nowMs() - t1; tl += t1 - t0;
            if (err != GL_NO_ERROR) { errs++; glDeleteTextures(1, &id); if (errs < 4) logf("  error GL 0x%x en %s (%ux%u)\n", err, e.rel.c_str(), e.w, e.h); continue; }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            tx.push_back({id, (int)e.w, (int)e.h}); bytes += e.raw;
        }
        glFinish();
        float vram1 = vglMemFree(VGL_MEM_VRAM) / MB, ram1 = vglMemFree(VGL_MEM_RAM) / MB;
        logf("  %-32s %u texturas (%.1f MB ARGB de origen) | carga %.0f ms, subida %.0f ms | VRAM usada %.1f MB, RAM vitaGL usada %.1f MB | errores %u\n",
             m.name, (unsigned)tx.size(), bytes / MB, tl, tu, vram0 - vram1, ram0 - ram1, (unsigned)errs);
        mem("con texturas");
        // para la fase 6 se quedan hasta 64 texturas pequenas; el resto se libera antes de probar el siguiente formato
        for (auto& t : tx) {
            if (m.kind == 0 && gScene.size() < 64 && t.w <= 256 && t.h <= 256 && t.w >= 16) gScene.push_back(t);
            else glDeleteTextures(1, &t.id);
        }
        glFinish(); vglSwapBuffers(GL_FALSE); vglSwapBuffers(GL_FALSE);  // vitaGL libera la memoria de forma diferida
        mem("tras liberar texturas");
    }
}

// ---------------------------------------------------------------- dibujo
static void fase6_dibujo() {
    logf("\n== 6 dibujo con vsync (objetivo 60 fps = 16,7 ms) | el juego dibuja ~4500-4900 quads por fotograma\n");
    // texturas pequenas (como los hexagonos y objetos): hasta 64 de <=256 px
    std::vector<Tx> pool;
    for (auto& t : gScene) if (t.w <= 256 && t.h <= 256 && t.w >= 16) { pool.push_back(t); if (pool.size() == 64) break; }
    if (pool.empty()) { logf("  sin texturas\n"); return; }
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::vector<GLfloat> vb, ub;
    for (int batched = 0; batched < 2; batched++)
    for (int n : {1000, 3000, 5000, 8000}) {
        const int frames = 60;
        double t0 = nowMs(), cpu = 0, worst = 0; unsigned seed = 1;
        for (int f = 0; f < frames; f++) {
            double c0 = nowMs();
            glClearColor(0.05f, 0.05f, 0.08f, 1); glClear(GL_COLOR_BUFFER_BIT);
            // n quads repartidos en grupos de la misma textura (como hexagonos u objetos repetidos)
            int per = n / (int)pool.size() + 1, done = 0;
            for (size_t ti = 0; ti < pool.size() && done < n; ti++) {
                const Tx& t = pool[ti]; glBindTexture(GL_TEXTURE_2D, t.id);
                int k = std::min(per, n - done);
                if (batched) { vb.resize((size_t)k * 12); ub.resize((size_t)k * 12); }
                for (int q = 0; q < k; q++) {
                    seed = seed * 1103515245u + 12345u;
                    float x = (float)((seed >> 8) % 900) + (f & 7), y = (float)((seed >> 18) % 500);
                    float w = 48, h = 48;
                    if (!batched) {
                        const GLfloat v[] = {x, y, x + w, y, x, y + h, x + w, y + h};
                        const GLfloat u[] = {0, 0, 1, 0, 0, 1, 1, 1};
                        glVertexPointer(2, GL_FLOAT, 0, v); glTexCoordPointer(2, GL_FLOAT, 0, u);
                        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    } else {
                        GLfloat* v = &vb[(size_t)q * 12]; GLfloat* u = &ub[(size_t)q * 12];
                        const GLfloat vv[] = {x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h};
                        const GLfloat uu[] = {0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1};
                        memcpy(v, vv, sizeof vv); memcpy(u, uu, sizeof uu);
                    }
                }
                if (batched && k > 0) { glVertexPointer(2, GL_FLOAT, 0, vb.data()); glTexCoordPointer(2, GL_FLOAT, 0, ub.data()); glDrawArrays(GL_TRIANGLES, 0, k * 6); }
                done += k;
            }
            double c1 = nowMs(); cpu += c1 - c0; worst = std::max(worst, c1 - c0);
            vglSwapBuffers(GL_FALSE);
        }
        double ms = (nowMs() - t0) / frames;
        logf("  %-22s %5d quads: %5.1f fps (%.1f ms/fotograma) | CPU enviando %.2f ms de media, peor %.2f ms\n",
             batched ? "por lotes (1 por textura)" : "1 llamada por quad", n, 1000.0 / ms, ms, cpu / frames, worst);
    }
}

int main() {
    std::string lp = std::string(kDir) + "memtest.log";
    gLog = fopen(lp.c_str(), "w");
    logf("NEOS prueba de memoria, carga y dibujo (NEOS00002)\n");
    logf("[reloj] CPU %d MHz, GPU %d MHz, bus %d MHz\n", scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetBusClockFrequency());
    mem("arranque");
    int grown = 0;
    for (int mb = 128; mb >= 16; mb /= 2) if (sceAppMgrGrowMemory((unsigned)mb << 20, 1) >= 0) { grown = mb; break; }
    logf("[memoria] DMR: %s (+%d MB)\n", grown ? "ampliada" : "sin ampliar", grown);
    vglInitExtended(0, 960, 544, 2 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    vglWaitVblankStart(GL_TRUE);
    mem("tras vglInit");
    if (openPak()) {
        size_t imgs = 0, raw = 0, big = 0, x2 = 0;
        for (auto& e : gIdx) if (e.type == 1) { imgs++; raw += e.raw; if (e.w > 960 || e.h > 544) big++; if (e.x2) x2++; }
        logf("paquete: %u archivos, %u imagenes (%.1f MB ARGB), %u x2_, %u mayores que la pantalla\n",
             (unsigned)gIdx.size(), (unsigned)imgs, raw / MB, (unsigned)x2, (unsigned)big);
        double t = nowMs();
        fase1_lectura();
        fase2_descompresion();
        fase3_residentes();
        // fase 4 ya medida: reducir x2_ en la consola cuesta 6,5 ms por imagen (6,9 s en total): se hara en mkdatapak
        fase5_texturas();
        fase6_dibujo();
        logf("\nFIN en %.1f s\n", (nowMs() - t) / 1000);
    }
    mem("final");
    phaseColor(0.1f, 0.6f, 0.1f);  // verde = terminado
    for (;;) { glClearColor(0.1f, 0.6f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT); vglSwapBuffers(GL_FALSE); }
    return 0;
}
