// main_game_vita.cpp — NEO Scavenger en la Vita: el codigo original traducido (as3cpp) + runtime + backend vitaGL.
// Primer esqueleto jugable (fase B del ROADMAP):
//   - escenario de 960x544 (tamano real de la pantalla): el propio juego elige su diseno 800x450
//     y su opcion "Stretch" (SHOW_ALL) lo ajusta a la pantalla, igual que en una ventana de ese tamano en PC
//   - el escenario se dibuja por software en un BitmapData y se sube como textura cada fotograma
//   - entrada minima: tactil frontal = raton (toque = clic, arrastrar = arrastrar), stick izquierdo = cursor con
//     zona muerta, X = clic, START = Esc (controles completos: fase C, docs/CONTROLES_VITA.md)
//   - 30 fps bloqueados por vcount; log con [perf], [memoria] y etiquetas de cuello de botella
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <vitaGL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <malloc.h>
#include "all.h"
#include "../src/ajustes_vita.h"
#include "../src/camaras.h"
#include "gpu_flixel.h"
#include "controles.h"

extern "C" int sceAppMgrGrowMemory(unsigned int size, int killMiniApps);  // SceAppMgr (lo habilita el plugin DMR)
size_t fastallocFreeBytes();  // runtime/fastalloc.cpp
void as3_init_statics();
void as3_trace_statics();

extern "C" {
// Heap de newlib: el juego no tiene recolector, asi que necesita la mayor parte de la RAM (con DMR).
// Medido en la consola: ~232 MB libres al arrancar; el heap se reserva antes que vitaGL, que se queda con el resto
unsigned int _newlib_heap_size_user = 208u * 1024u * 1024u;
unsigned int sceLibcHeapSize = 4u * 1024u * 1024u;
// Pila grande: el codigo traducido y el dibujo de la lista de visualizacion son recursivos
unsigned int sceUserMainThreadStackSize = 4u * 1024u * 1024u;
}

static const char* kDir = "ux0:data/neoscavvita/";
static const int W = 960, H = 544;
// Zonas muertas de los sticks (docs/CONTROLES_VITA.md): ajustables entre 15 % y 40 % segun el desgaste de cada consola
static const float kStickDead = 0.28f, kStickOuter = 0.93f;

static double nowMs() { return sceKernelGetProcessTimeWide() / 1000.0; }

static void memReport(const char* tag) {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof info;
    sceKernelGetFreeMemorySize(&info);
    struct mallinfo mi = mallinfo();
    auto& b = flash::bmpStats();
    platform::logf("[memoria] %s: heap usado %.1f MB de %u MB | libre usuario %.1f MB, CDRAM %.1f MB | vitaGL VRAM libre %.1f MB | "
                "bitmaps %u creados, %u PNG, %.1f MB pixeles (%.1f MB liberados)\n",
                tag, mi.uordblks / 1048576.0, _newlib_heap_size_user >> 20, info.size_user / 1048576.0, info.size_cdram / 1048576.0,
                vglMemFree(VGL_MEM_VRAM) / 1048576.0, b.created, b.decoded, b.bytes / 1048576.0, b.freed / 1048576.0);
    std::fflush(stdout);
}

// ---------------------------------------------------------------- audio
// Hilo de audio en el nucleo 2: mezcla 1024 muestras estereo a 44,1 kHz por bloque (23 ms) y las envia al puerto BGM.
static std::atomic<double> gAudioMixMs{0};  // media movil del coste de mezclar un bloque (para el log)
static int audioThread(SceSize, void*) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024, 44100, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) { platform::logf("[audio] no se pudo abrir el puerto de audio: 0x%08x\n", (unsigned)port); return 0; }
    int vols[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vols);
    platform::logf("[audio] puerto BGM abierto (44100 Hz, bloques de 1024)\n");
    static int16_t buf[2][1024 * 2];
    int k = 0;
    for (;;) {
        uint64_t t0 = sceKernelGetProcessTimeWide();
        audio::mixer().mix(buf[k], 1024);
        double ms = (sceKernelGetProcessTimeWide() - t0) / 1000.0;
        gAudioMixMs = gAudioMixMs * 0.95 + ms * 0.05;
        sceAudioOutOutput(port, buf[k]);
        k ^= 1;
    }
    return 0;
}

// ---------------------------------------------------------------- perfil en la consola (solo con -DAS3_TRACE)
// Un hilo en el nucleo 1 mira cada ~1 ms que funcion AS3 esta en la cima de la pila del hilo del juego. Si un fotograma
// dura mas de 1 s, al terminar se escriben en el log las funciones donde se fue el tiempo.
static std::atomic<uint32_t> gFrameId{0};
static std::atomic<uint64_t> gFrameStartUs{0};
#ifdef AS3_TRACE
static int profSampler(SceSize, void*) {
    std::unordered_map<std::string, uint32_t> hist;
    uint32_t lastId = 0; uint32_t total = 0;
    for (;;) {
        sceKernelDelayThread(1000);
        uint32_t id = gFrameId.load();
        if (id != lastId) {
            if (total > 1000) {
                std::vector<std::pair<uint32_t, std::string>> v;
                for (auto& kv : hist) v.push_back({kv.second, kv.first});
                std::sort(v.rbegin(), v.rend());
                platform::logf("[perfil] fotograma de %u ms (fotograma %u). Funciones AS3 en la cima de la pila:\n", total, lastId);
                for (size_t i = 0; i < v.size() && i < 20; i++) platform::logf("[perfil] %5.1f%%  %s\n", 100.0 * v[i].first / total, v[i].second.c_str());
            }
            hist.clear(); total = 0; lastId = id;
        }
        uint64_t now = sceKernelGetProcessTimeWide();
        if (now - gFrameStartUs.load() < 1000000ull) continue;  // solo fotogramas de mas de 1 s
        auto& st = as3::traceStack();
        size_t n = st.size();
        if (n == 0) continue;
        std::string k = st[n - 1] ? st[n - 1] : "?";
        if (n > 1 && st[n - 2]) { k += " < "; k += st[n - 2]; }
        if (n > 2 && st[n - 3]) { k += " < "; k += st[n - 3]; }
        hist[k] += 1; total += 1;  // ~1 muestra por ms
    }
    return 0;
}
#endif

int main() {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    std::string logPath = std::string(kDir) + "game.log";
    // En la Vita freopen(stdout) no redirige: se reasigna stdout/stderr de newlib a un archivo propio, sin bufer
    if (FILE* lf = std::fopen(logPath.c_str(), "w")) { setvbuf(lf, nullptr, _IONBF, 0); stdout = lf; stderr = lf; platform::logFile() = lf; }
    {   // sonda: mayor bloque USER_RW que aun se puede reservar (aparte del heap de newlib ya tomado)
        unsigned best = 0;
        for (unsigned mb = 384; mb >= 16; mb -= 8) {
            SceUID id = sceKernelAllocMemBlock("sonda", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, mb * 1024 * 1024, nullptr);
            if (id >= 0) { best = mb; sceKernelFreeMemBlock(id); break; }
        }
        platform::logf("[memoria] sonda: heap newlib %u MB tomado; mayor bloque extra reservable ~%u MB\n", _newlib_heap_size_user >> 20, best);
    }
    platform::logf("NEO Scavenger Vita — esqueleto jugable 00.03\n");
    platform::logf("[reloj] CPU %d MHz, GPU %d MHz, bus %d MHz (los sube el usuario con PSVshell; avisar de calor y desgaste)\n",
                scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetBusClockFrequency());
    memReport("arranque");

    // DMR: ampliar la memoria de la aplicacion (hasta ~381 MiB) ANTES de vitaGL, que se queda con la RAM libre.
    // Sin el plugin la llamada falla y se sigue con la memoria normal.
    int grownMB = 0;
    for (int mb = 128; mb >= 16; mb /= 2) if (sceAppMgrGrowMemory((unsigned)mb << 20, 1) >= 0) { grownMB = mb; break; }
    // Zona para los pixeles de BitmapData: el mayor bloque reservable menos 20 MB para vitaGL
    {
        unsigned best = 0;
        for (unsigned mb = 384; mb >= 16; mb -= 4) {
            SceUID id = sceKernelAllocMemBlock("sonda2", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, mb * 1024 * 1024, nullptr);
            if (id >= 0) { best = mb; sceKernelFreeMemBlock(id); break; }
        }
        unsigned arenaMB = best > 36 ? best - 20 : 0;
        void* base = nullptr;
        if (arenaMB) {
            SceUID id = sceKernelAllocMemBlock("pixeles", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, arenaMB * 1024 * 1024, nullptr);
            if (id >= 0 && sceKernelGetMemBlockBase(id, &base) >= 0) pixmem::init(base, (size_t)arenaMB << 20);
            else arenaMB = 0;
        }
        platform::logf("[memoria] DMR: %s (+%d MB) | mayor bloque libre %u MB | zona de pixeles %u MB\n",
                       grownMB ? "ampliada" : "sin ampliar", grownMB, best, arenaMB);
    }
    vglInitExtended(0, W, H, 2 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    vglWaitVblankStart(GL_TRUE);
    memReport("tras vglInit");

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, W, H, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);  // BGRA = 0xAARRGGBB en memoria
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

    platform::setDataRoot(kDir);
    ajustes::omitirImagenesFondo();
#ifdef AS3_TRACE
    as3::traceStack().reserve(8192);  // sin realojar: el muestreador la lee desde otro hilo
    {   SceUID ps = sceKernelCreateThread("perfil", profSampler, 0x10000100, 0x10000, 0, 0x20000, nullptr);
        if (ps >= 0) sceKernelStartThread(ps, 0, nullptr); }
#endif
    {   // audio en el nucleo 2 (el hilo del juego solo sincroniza volumenes y eventos)
        audio::mixer().outHz = 44100;
        SceUID ath = sceKernelCreateThread("audio", audioThread, 0x10000100 - 16, 0x10000, 0, 0x40000, nullptr);
        if (ath >= 0) sceKernelStartThread(ath, 0, nullptr);
        else platform::logf("[audio] no se pudo crear el hilo: 0x%08x\n", (unsigned)ath);
    }
    double t0 = nowMs();
    as3_init_statics();
    platform::logf("[arranque] estaticos listos en %.0f ms\n", nowMs() - t0);
    // Opcion "Stretch" del propio juego activada por defecto: ajusta su diseno a la pantalla de la Vita
    flash::SharedObject::onCreate = [](const std::string& n, flash::SharedObject* so) {
        if (n == "flixel") so->data->__set("res", as3::Any(as3::objectOf({{"strStretchOverride", as3::Any(as3::String("showAll"))}})));
    };

    flash::Stage* st = nullptr;
    try {
        Preloader* pre = new Preloader();
        st = new flash::Stage();
        st->stageWidth = W; st->stageHeight = H; st->frameRate = 30;
        st->swfWidth = 1360; st->swfHeight = 768;
        st->loaderInfo = new flash::LoaderInfo();
        st->loaderInfo->parameters = as3::make<as3::Object>();
        st->loaderInfo->url = as3::String("file:///NEOScavenger.swf");
        pre->root = pre; st->root = pre; st->stage = st; pre->stage = st;
        pre->loaderInfo = new flash::LoaderInfo();
        pre->loaderInfo->url = as3::String("file:///NEOScavenger.swf");
        pre->__construct();
        st->addChild(pre);
        // recolector entre fotogramas: sin el, el juego no cabe en la memoria de la Vita
        as3::gc::enabled() = true;
        // La pasada cuesta ~0,75 s en la consola: se espacia (casi todo lo que se crea ocurre en transiciones de pantalla)
        flash::gcMinNewObjects() = 80000;
        flash::gcMinNewPixelBytes() = 48u << 20;
        flash::gcPressure() = []() {  // heap de newlib casi lleno
            struct mallinfo mi = mallinfo();
            size_t used = (size_t)mi.uordblks - std::min((size_t)mi.uordblks, fastallocFreeBytes());  // sin las listas libres
            return used > (size_t)_newlib_heap_size_user / 10 * 8;
        };
        as3::gc::rootFns().push_back([st, pre] { as3_trace_statics(); as3::gc::mark(st); as3::gc::mark(pre); });
    } catch (const as3::Thrown& t) {
        platform::logf("[arranque] excepcion AS3: %s\n", as3::toString(t.value).str().c_str());
        std::fflush(stdout);
        sceKernelExitProcess(1);
    }

    flash::BitmapData* frame = as3::make<flash::BitmapData>(W, H, true, 0.0);  // capa del escenario (transparente)
    frame->__gcPinned = true;
    // Se escribe directamente en la memoria de la textura (glTexSubImage2D costaba ~66 ms por fotograma en la consola)
    glBindTexture(GL_TEXTURE_2D, tex);
    uint32_t* texMem = (uint32_t*)vglGetTexDataPointer(GL_TEXTURE_2D);
    // Textura de la camara de Flixel (su bufer de 1360x768 se copia tal cual y la GPU lo escala): sustituye el
    // compositor por software (~55 ms) y la conversion de color (~22 ms) medidos en la consola
    GLuint camTex = 0; int camW = 0, camH = 0; uint32_t* camMem = nullptr;
    auto quadAt = [](float x, float y, float w, float h) {
        const GLfloat vtx[] = {x, y, x + w, y, x, y + h, x + w, y + h};
        const GLfloat uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
        glVertexPointer(2, GL_FLOAT, 0, vtx);
        glTexCoordPointer(2, GL_FLOAT, 0, uv);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    unsigned lastVcount = sceDisplayGetVcount();
    double statStart = nowMs(), lastFrame = statStart;
    double sumLogic = 0, sumDraw = 0, sumUpload = 0, maxFrame = 0;
    int frames = 0, slow = 0, totalFrames = 0;
    bool loadedReported = false;
    float cx = W / 2, cy = H / 2;
    bool touchDown = false, crossDown = false, startDown = false;
    // Toque: primero se mueve el cursor y la pulsacion llega 2 fotogramas despues (los botones de Flixel necesitan
    // ver el raton encima antes de recibir el clic). Si el dedo se levanta antes, la suelta tambien se retrasa.
    // Tactil frontal: toque corto = clic; mover el dedo = arrastrar (boton pulsado); mantener quieto 0,5 s = clic derecho
    // (el menu contextual del propio juego: Coger/Soltar, Usar, Destruir...). El cursor se mueve al tocar y la pulsacion
    // llega despues (los botones de Flixel necesitan ver el raton encima antes del clic).
    enum { T_LIBRE, T_ESPERA, T_PULSADO, T_SOLTAR, T_LARGO } tst = T_LIBRE;
    int tWait = 0, tHeld = 0; float tx0 = 0, ty0 = 0; bool tRight = false;
    size_t shownDbg = 0;

    for (;;) {
        double f0 = nowMs();
        gFrameId++; gFrameStartUs = sceKernelGetProcessTimeWide();
        // ---------------- entrada
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        {   // la entrada emulada (agente de pruebas, Remote Play) solo aparece en las variantes Ext: se combinan
            SceCtrlData ext{};
            if (sceCtrlPeekBufferPositiveExt2(0, &ext, 1) >= 0) {
                pad.buttons |= ext.buttons & ~0x00010000u;
                auto far = [](uint8_t v) { return v < 128 ? 128 - v : v - 128; };
                if (far(ext.lx) + far(ext.ly) > far(pad.lx) + far(pad.ly)) { pad.lx = ext.lx; pad.ly = ext.ly; }
                if (far(ext.rx) + far(ext.ry) > far(pad.rx) + far(pad.ry)) { pad.rx = ext.rx; pad.ry = ext.ry; }
            }
        }
        SceTouchData touch{};
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        SceTouchData backTouch{};
        sceTouchPeek(SCE_TOUCH_PORT_BACK, &backTouch, 1);
        try {
            bool t = touch.reportNum > 0;
            if (t) {
                cx = touch.report[0].x * 0.5f;
                cy = touch.report[0].y * 0.5f;
                flash::injectMouseMove(st, cx, cy);
            }
            {   // diagnostico: cambios del tactil (numero de dedos) y pulsaciones enviadas al juego
                static int lastN = -1;
                if (touch.reportNum != lastN) { platform::logf("[tactil] dedos=%d en (%.0f,%.0f) estado=%d fotograma=%d\n", touch.reportNum, cx, cy, (int)tst, totalFrames); lastN = touch.reportNum; }
            }
            if (tst != T_LIBRE && FlxG::mouse) platform::logf("[tactil] flixel: raton (%d,%d) mundo (%.0f,%.0f) estado_boton=%d\n", FlxG::mouse->screenX, FlxG::mouse->screenY, FlxG::mouse->x, FlxG::mouse->y, FlxG::mouse->_current);
            switch (tst) {
                case T_LIBRE: if (t) { tst = T_ESPERA; tHeld = 0; tx0 = cx; ty0 = cy; } break;
                case T_ESPERA: {
                    tHeld++;
                    float mv = std::fabs(cx - tx0) + std::fabs(cy - ty0);
                    if (!t) {  // toque corto: clic
                        flash::injectMouseButton(st, true); tst = T_SOLTAR; tWait = 2; tRight = false;
                    } else if (mv > 12) {  // el dedo se mueve: arrastrar con el boton pulsado
                        flash::injectMouseButton(st, true); tst = T_PULSADO;
                    } else if (tHeld >= 15) {  // 0,5 s quieto: clic derecho (menu contextual del juego)
                        flash::injectMouseButton(st, true, true); tst = T_SOLTAR; tWait = 2; tRight = true;
                        platform::logf("[tactil] pulsacion larga en (%.0f,%.0f): clic derecho\n", cx, cy);
                    }
                    break;
                }
                case T_PULSADO: if (!t) { flash::injectMouseButton(st, false); tst = T_LIBRE; } break;
                case T_SOLTAR:
                    if (--tWait <= 0) { flash::injectMouseButton(st, false, tRight); tst = tRight ? T_LARGO : T_LIBRE; }
                    break;
                case T_LARGO: if (!t) tst = T_LIBRE; break;  // esperar a que se levante el dedo
            }
            // stick izquierdo = cursor. Zona muerta radial AGRESIVA (28 %): los sticks de la Vita se desgastan y sin ella
            // el cursor se va solo. Se reescala el recorrido util (28 %-93 %) a 0-1 y la zona exterior (>93 %) cuenta como
            // inclinacion maxima, para que un stick gastado alcance la velocidad tope. Curva cuadratica: fino al inclinar poco.
            float lx = (pad.lx - 128) / 127.0f, ly = (pad.ly - 128) / 127.0f;
            float mag = std::sqrt(lx * lx + ly * ly);
            if (mag > kStickDead) {
                float m = std::min(1.0f, (mag - kStickDead) / (kStickOuter - kStickDead));
                float v = m * m * 600.0f / 30.0f;  // hasta 600 px/s
                cx = std::max(0.0f, std::min((float)W - 1, cx + lx / mag * v));
                cy = std::max(0.0f, std::min((float)H - 1, cy + ly / mag * v));
                flash::injectMouseMove(st, cx, cy);
            }
            bool c = pad.buttons & SCE_CTRL_CROSS;
            if (c != crossDown) { flash::injectMouseButton(st, c); crossDown = c; }
            // resto del mando y panel trasero (atajos del propio juego, mover el mapa, cursor fino)
            {
                float zoom = (FlxG::camera && FlxG::camera->_flashSprite) ? (float)FlxG::camera->_flashSprite->scaleX : 1.0f;
                controles::actualizar(st, pad, backTouch, cx, cy, W, H, zoom > 0 ? zoom : 1.0f);
            }
            (void)startDown;

            // ---------------- logica del juego (un fotograma de Flash)
            camaras::activarDiferido();  // los blits de Flixel de todas las camaras se graban para la GPU
            flash::pumpPending();
            flash::enterFrameAll(st);
            flash::audioUpdate(44100);
            ajustes::aplicar();
            if (flash::gcAfterFrame(60) && as3::gc::stats().lastMs > 20)
                platform::logf("[CPU-bound] recolector: %.1f ms (%u objetos vivos, %u liberados)\n", as3::gc::stats().lastMs, as3::gc::stats().lastLive, as3::gc::stats().lastFreed);
        } catch (const as3::Thrown& t) {
            platform::logf("[juego] excepcion AS3 sin capturar (fotograma %d): %s\n", totalFrames, as3::toString(t.value).str().c_str());
        }
        const std::string& dbg = DataHandler::m_strDebug.str();
        if (dbg.size() > shownDbg) { platform::logf("[juego]%s\n", dbg.substr(shownDbg).c_str()); shownDbg = dbg.size(); }
        if (!loadedReported && DataHandler::bLoadingComplete) {
            platform::logf("[carga] datos al 100%% en %.1f s (fotograma %d)\n", (nowMs() - t0) / 1000.0, totalFrames);
            memReport("datos cargados");
            loadedReported = true;
        }
        double f1 = nowMs();

        // ---------------- dibujo: las ordenes de dibujo de TODAS las camaras de Flixel las ejecuta la GPU, dentro de la
        // lista de visualizacion (cursor, bandeja de sonido, etc. como quads). Sin ordenes (pantallas de carga): por software.
        auto cams = camaras::todas();
        gpu::CamMap gpuCams;
        for (FlxCamera* c : cams)
            if (c->buffer && c->_flashSprite && c->buffer->deferred && c->buffer->cmdsFromClear && !c->buffer->cmds.empty()) gpuCams[c->_flashSprite] = c->buffer;
        double f2 = nowMs();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!gpuCams.empty()) {
            flash::Affine org; org.tx = st->originX(); org.ty = st->originY();
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            static int sinDibujar = -1;
            int sk = gpu::drawDisplayList(st, org, 1.0, gpuCams);
            for (auto& kv : gpuCams) kv.second->dropDeferred();
            if (sk != sinDibujar) { platform::logf("[gpu] camaras por GPU: %u | objetos del escenario sin dibujar: %d\n", (unsigned)gpuCams.size(), sk); sinDibujar = sk; }
        } else {
            std::fill(frame->px.begin(), frame->px.end(), 0u);
            flash::renderStage(st, frame);  // leer los buferes aplana sus ordenes por software
            glBindTexture(GL_TEXTURE_2D, tex);
            if (texMem) std::memcpy(texMem, frame->px.data(), (size_t)W * H * 4);
            else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, frame->px.data());
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            quadAt(0, 0, (float)W, (float)H);
        }
        double f3 = nowMs();
        vglSwapBuffers(GL_FALSE);
        while ((unsigned)(sceDisplayGetVcount() - lastVcount) < 2) sceDisplayWaitVblankStart();
        lastVcount = sceDisplayGetVcount();

        // ---------------- medicion
        double now = nowMs();
        double fm = now - lastFrame;
        lastFrame = now;
        frames++; totalFrames++;
        sumLogic += f1 - f0; sumDraw += f2 - f1; sumUpload += f3 - f2;
        maxFrame = std::max(maxFrame, fm);
        if (fm > 40) slow++;
        if (now - statStart >= 2000) {
            double fps = frames * 1000.0 / (now - statStart);
            double lg = sumLogic / frames, dr = sumDraw / frames, up = sumUpload / frames;
            const char* tag = (lg + dr + up) < 30 ? "ok" : (dr > lg ? "[CPU-bound] dibujo por software" : "[CPU-bound] logica del juego");
            FlxState* cur = FlxG::get_state();
            platform::logf("[perf] %.1f fps, frame max %.1f ms, %d lentos | logica %.1f ms, dibujo %.1f ms, subida %.1f ms (%s) | estado %s\n",
                        fps, maxFrame, slow, lg, dr, up, tag, cur ? cur->__className().str().c_str() : "-");
            {
                auto& gs = gpu::stats(); auto& ds = flash::deferStats();
                platform::logf("[audio] mezcla %.2f ms por bloque de 23 ms (otro nucleo), %u voces\n", gAudioMixMs.load(), (unsigned)audio::mixer().voices.size());
                platform::logf("[gpu] %u texturas (%.1f MB VRAM), %u subidas (%.1f MB), %u ordenes, %u quads | aplanados por software %u (por origen %u)\n",
                               (unsigned)gs.textures, gs.texBytes / 1048576.0, (unsigned)gs.uploads, gs.uploadBytes / 1048576.0, (unsigned)gs.cmds, (unsigned)gs.quads, (unsigned)ds.flushes, (unsigned)ds.srcFlushes);
                gs.uploads = gs.uploadBytes = gs.cmds = gs.quads = 0; ds.flushes = ds.srcFlushes = 0;
            }
            if (totalFrames % 300 < 60) {
                memReport("periodico");
                auto& pa = pixmem::arena();
                platform::logf("[memoria] zona de pixeles: %.1f de %.1f MB (pico %.1f), %u bloques, %u huecos, %u fuera de la zona | recolector: %u pasadas, ultima %.1f ms, %u objetos vivos\n",
                               pa.used / 1048576.0, pa.size / 1048576.0, pa.peak / 1048576.0, (unsigned)pa.live.size(), (unsigned)pa.freeList.size(), (unsigned)pa.fallbacks,
                               (unsigned)as3::gc::stats().collections, as3::gc::stats().lastMs, (unsigned)as3::gc::stats().lastLive);
            }
            frames = 0; slow = 0; maxFrame = 0; sumLogic = sumDraw = sumUpload = 0; statStart = now;
        }
        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_START)) break;  // salida de emergencia
    }
    platform::logf("fin\n");
    std::fflush(stdout);
    sceKernelExitProcess(0);
    return 0;
}
