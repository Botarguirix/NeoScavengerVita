// NEO Scavenger Vita — esqueleto 00.02 (20-11-2026)
// Objetivo: validar en la consola lo básico antes de enchufar el C++ traducido:
//   - arranque con vitaGL (pipeline fija, sin compilador de shaders en ejecución)
//   - lectura de img2.pak (NSVPAK02) y subida de texturas a la GPU, en un hilo del núcleo 2
//   - cap de 30 fps con vsync
//   - táctil frontal y mandos
//   - log con etiquetas de cuello de botella, memoria y tiempos de frame
// No es el juego: dibuja un fondo desplazable, la hoja de hexágonos y un cursor táctil.
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <vitaGL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Memoria del proceso: con DMR el juego dispone de más RAM; reservamos un heap grande para newlib.
extern "C" {
unsigned int _newlib_heap_size_user = 64u * 1024u * 1024u;  // se afinará con la sonda de RAM
unsigned int sceLibcHeapSize = 8u * 1024u * 1024u;
}

static const char* kDir = "ux0:data/neoscavvita";
static FILE* gLog = nullptr;

static void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    sceClibPrintf("%s\n", buf);
    if (gLog) {
        fprintf(gLog, "%s\n", buf);
        fflush(gLog);
    }
}

static uint64_t nowUs() { return sceKernelGetProcessTimeWide(); }

// ---------------------------------------------------------------- NSVPAK02
struct PakEntry {
    std::string name;
    uint32_t off, size;
    uint16_t w, h;
    uint8_t fmt;
};
struct Pak {
    FILE* f = nullptr;
    std::vector<PakEntry> e;
    bool open(const char* path) {
        f = fopen(path, "rb");
        if (!f) return false;
        char magic[8];
        uint32_t n = 0, start = 0;
        if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "NSVPAK02", 8) != 0) return false;
        fread(&n, 4, 1, f);
        fread(&start, 4, 1, f);
        for (uint32_t i = 0; i < n; i++) {
            uint16_t l;
            fread(&l, 2, 1, f);
            PakEntry p;
            p.name.resize(l);
            fread(&p.name[0], 1, l, f);
            fread(&p.off, 4, 1, f);
            fread(&p.size, 4, 1, f);
            fread(&p.w, 2, 1, f);
            fread(&p.h, 2, 1, f);
            fread(&p.fmt, 1, 1, f);
            fseek(f, 3, SEEK_CUR);
            e.push_back(std::move(p));
        }
        return true;
    }
    const PakEntry* find(const std::string& n) const {
        for (auto& p : e)
            if (p.name == n) return &p;
        return nullptr;
    }
};

// ---------------------------------------------------------------- texturas
struct Tex {
    const PakEntry* ent = nullptr;
    std::vector<uint8_t> pixels;  // lo rellena el hilo cargador
    GLuint id = 0;
    std::atomic<int> state{0};  // 0 pendiente, 1 leída (lista para subir), 2 en GPU
    uint32_t readUs = 0;
};

static Pak gPak;
static std::vector<Tex*> gTex;
static std::atomic<bool> gStopLoader{false};

// Hilo cargador en el núcleo 2: solo lee del disco (la subida a GL debe ocurrir en el hilo de dibujo).
static int loaderThread(SceSize, void*) {
    for (Tex* t : gTex) {
        if (gStopLoader) break;
        uint64_t t0 = nowUs();
        t->pixels.resize(t->ent->size);
        fseek(gPak.f, t->ent->off, SEEK_SET);
        size_t got = fread(t->pixels.data(), 1, t->ent->size, gPak.f);
        t->readUs = (uint32_t)(nowUs() - t0);
        if (got != t->ent->size) {
            logf("[error] lectura incompleta de %s (%u de %u)", t->ent->name.c_str(), (unsigned)got, t->ent->size);
            t->pixels.clear();
        }
        t->state = 1;
    }
    return 0;
}

static void uploadReady(uint32_t budgetUs) {
    uint64_t t0 = nowUs();
    for (Tex* t : gTex) {
        if (t->state != 1) continue;
        if (nowUs() - t0 > budgetUs) break;  // repartir la subida entre frames
        uint64_t u0 = nowUs();
        glGenTextures(1, &t->id);
        glBindTexture(GL_TEXTURE_2D, t->id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->ent->w, t->ent->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        std::vector<uint8_t>().swap(t->pixels);  // liberar la copia en RAM
        t->state = 2;
        logf("[tex] %-28s %4dx%-4d lectura %5.1f ms, subida GL %5.1f ms", t->ent->name.c_str(), t->ent->w, t->ent->h,
             t->readUs / 1000.0, (nowUs() - u0) / 1000.0);
    }
}

static void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    const GLfloat vtx[] = {x, y, x + w, y, x, y + h, x + w, y + h};
    const GLfloat uv[] = {u0, v0, u1, v0, u0, v1, u1, v1};
    glVertexPointer(2, GL_FLOAT, 0, vtx);
    glTexCoordPointer(2, GL_FLOAT, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void solid(float x, float y, float w, float h, float r, float g, float b, float a) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    const GLfloat vtx[] = {x, y, x + w, y, x, y + h, x + w, y + h};
    glVertexPointer(2, GL_FLOAT, 0, vtx);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glColor4f(1, 1, 1, 1);
    glEnable(GL_TEXTURE_2D);
}

static void memReport(const char* tag) {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof info;
    sceKernelGetFreeMemorySize(&info);
    logf("[memoria] %s: libre usuario %.1f MB, CDRAM %.1f MB, phycont %.1f MB | vitaGL VRAM libre %.1f MB, RAM %.1f MB",
         tag, info.size_user / 1048576.0, info.size_cdram / 1048576.0, info.size_phycont / 1048576.0,
         vglMemFree(VGL_MEM_VRAM) / 1048576.0, vglMemFree(VGL_MEM_RAM) / 1048576.0);
}

int main() {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    char path[256];
    snprintf(path, sizeof path, "%s/port.log", kDir);
    gLog = fopen(path, "w");
    logf("NEO Scavenger Vita esqueleto 00.02");
    logf("[reloj] CPU %d MHz, GPU %d MHz, bus %d MHz (los sube el usuario con PSVshell)", scePowerGetArmClockFrequency(),
         scePowerGetGpuClockFrequency(), scePowerGetBusClockFrequency());

    // Sonda de RAM: mayor bloque USER_RW que se puede reservar (con DMR debería superar los 256 MB).
    {
        unsigned best = 0;
        for (unsigned mb = 384; mb >= 32; mb -= 16) {
            SceUID id = sceKernelAllocMemBlock("probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, mb * 1024 * 1024, nullptr);
            if (id >= 0) {
                best = mb;
                sceKernelFreeMemBlock(id);
                break;
            }
        }
        logf("[memoria] sonda: mayor bloque USER_RW reservable ~%u MB (aparte del heap newlib de 64 MB ya tomado)", best);
        memReport("antes de vglInit");
    }
    vglInitExtended(0, 960, 544, 24 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    vglWaitVblankStart(GL_TRUE);
    memReport("tras vglInit");

    snprintf(path, sizeof path, "%s/img2.pak", kDir);
    if (!gPak.open(path)) {
        logf("[error] no se puede abrir %s (NSVPAK02). Copia el pak al directorio de datos.", path);
        sceKernelDelayThread(3 * 1000 * 1000);
        return 1;
    }
    logf("[pak] %u imágenes", (unsigned)gPak.e.size());
    for (auto& p : gPak.e) {
        Tex* t = new Tex;
        t->ent = &p;
        gTex.push_back(t);
    }
    // 0x40000000 << 2 = afinidad al núcleo 2 (CapUnlocker habilita el 3.º/4.º)
    // SCE_KERNEL_CPU_MASK_USER_2 = 0x40000 (núcleos de usuario: 0x10000, 0x20000, 0x40000)
    SceUID th = sceKernelCreateThread("loader", loaderThread, 0x10000100, 0x10000, 0, 0x40000, nullptr);
    if (th < 0) {
        logf("[error] no se pudo crear el hilo cargador en el núcleo 2: 0x%08x", (unsigned)th);
    } else {
        int rc = sceKernelStartThread(th, 0, nullptr);
        logf("[hilos] cargador en núcleo 2: id 0x%x, inicio rc=0x%08x", (unsigned)th, (unsigned)rc);
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto texOf = [&](const char* n) -> Tex* {
        for (Tex* t : gTex)
            if (t->ent->name == n && t->state == 2) return t;
        return nullptr;
    };

    unsigned lastVcount = sceDisplayGetVcount();
    float scroll = 0, cx = 480, cy = 272;
    uint64_t statStart = nowUs(), lastFrame = statStart;
    int frames = 0, slow = 0;
    double maxFrameMs = 0, sumCpuMs = 0;
    bool memDone = false;
    const double dt = 1.0 / 30.0;  // delta fijo: el cap de 30 fps está bloqueado
    for (;;) {
        uint64_t f0 = nowUs();
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;
        SceTouchData touch{};
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        if (touch.reportNum > 0) {
            cx = touch.report[0].x * 960.0f / 1920.0f;
            cy = touch.report[0].y * 544.0f / 1088.0f;
        }
        float lx = (pad.lx - 128) / 128.0f, ly = (pad.ly - 128) / 128.0f;
        if (fabsf(lx) > 0.15f) cx += lx * 300 * dt;
        if (fabsf(ly) > 0.15f) cy += ly * 300 * dt;
        scroll += 40.0f * dt;

        uploadReady(4000);  // máx. 4 ms por frame subiendo texturas

        glClearColor(0.05f, 0.07f, 0.08f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColor4f(1, 1, 1, 1);
        if (Tex* t = texOf("titleScrollBg.png")) {
            glBindTexture(GL_TEXTURE_2D, t->id);
            float u = fmodf(scroll / t->ent->w, 1.0f);
            quad(0, 0, 960, 544, u, 0, u + 960.0f / t->ent->w, 1);
        }
        if (Tex* t = texOf("titleScrollMid.png")) {
            glBindTexture(GL_TEXTURE_2D, t->id);
            float u = fmodf(scroll * 2 / t->ent->w, 1.0f);
            quad(0, 544 - 407 * 0.8f, 960, 407 * 0.8f, u, 0, u + 960.0f / t->ent->w / 0.8f * 0.8f, 1);
        }
        if (Tex* t = texOf("titleScrollGrass.png")) {
            glBindTexture(GL_TEXTURE_2D, t->id);
            float u = fmodf(scroll * 4 / t->ent->w, 1.0f);
            quad(0, 544 - 476 * 0.5f, 960, 476 * 0.5f, u, 0, u + 960.0f / t->ent->w, 1);
        }
        if (Tex* t = texOf("HexSheetSummerDay.png")) {
            glBindTexture(GL_TEXTURE_2D, t->id);
            quad(10, 10, 325, 114, 0, 0, 1, 1);  // hoja completa a 1/4 de tamaño
        }
        // cursor táctil / stick
        solid(cx - 12, cy - 12, 24, 24, 1.0f, 0.3f, 0.1f, 0.9f);

        // barra de coste de frame: verde < 25 ms, amarillo < 33, rojo > 33
        double cpuMs = (nowUs() - f0) / 1000.0;
        solid(0, 538, (float)std::min(960.0, cpuMs * 28.8), 6, cpuMs < 25 ? 0.2f : cpuMs < 33 ? 0.9f : 1.0f,
              cpuMs < 33 ? 0.9f : 0.1f, 0.1f, 1);
        sumCpuMs += cpuMs;

        vglSwapBuffers(GL_FALSE);
        // Cap de 30 fps: esperar a que pasen 2 vblanks desde el frame anterior (vglSwapBuffers no bloquea).
        while ((unsigned)(sceDisplayGetVcount() - lastVcount) < 2) sceDisplayWaitVblankStart();
        lastVcount = sceDisplayGetVcount();

        uint64_t now = nowUs();
        double frameMs = (now - lastFrame) / 1000.0;
        lastFrame = now;
        frames++;
        maxFrameMs = std::max(maxFrameMs, frameMs);
        if (frameMs > 40.0) slow++;
        if (now - statStart >= 1000000) {
            double fps = frames * 1e6 / (now - statStart);
            const char* tag = (slow == 0) ? "ok" : (sumCpuMs / frames > 25 ? "[CPU-bound] hilo principal: dibujo/subida de texturas" : "[GPU-bound] posible límite de relleno o espera de vblank");
            logf("[perf] %.1f fps, frame máx %.1f ms, %d frames lentos, CPU hilo principal medio %.1f ms (%s)", fps,
                 maxFrameMs, slow, sumCpuMs / frames, tag);
            frames = 0;
            slow = 0;
            maxFrameMs = 0;
            sumCpuMs = 0;
            statStart = now;
            bool allDone = true;
            for (Tex* t : gTex) allDone &= (t->state == 2);
            if (allDone && !memDone) {
                memReport("todas las texturas en GPU");
                memDone = true;
            }
        }
    }
    gStopLoader = true;
    sceKernelWaitThreadEnd(th, nullptr, nullptr);
    logf("fin");
    if (gLog) fclose(gLog);
    sceKernelExitProcess(0);
    return 0;
}
