// main_pc.cpp — arnes de PC del juego traducido.
// Hito 1.5/2: arranca como lo hace el SWF original (Preloader = clase de documento), le da fotogramas
// (ENTER_FRAME + eventos asincronos) y observa la carga de datos de DataHandler.
// Los backends reales (ventana, entrada, audio) llegan en los hitos 3-5.
#include <cstdio>
#include <functional>
#include <set>
#include <map>
#include <algorithm>
#include <unordered_set>
#include "all.h"
#include <windows.h>
#include <psapi.h>
#include "sampler.h"
#include "../src/ajustes_vita.h"
#include "../src/camaras.h"

static std::vector<int16_t>& wavBuf() { static std::vector<int16_t> w; return w; }
static void writeWav(const char* path) {  // mezcla del juego durante el guion (verificacion del audio)
    auto& w = wavBuf();
    FILE* f = std::fopen(path, "wb"); if (!f) return;
    uint32_t data = (uint32_t)(w.size() * 2), riff = 36 + data, fmt = 16, rate = 44100, brate = 44100 * 4;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fmt, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f); std::fwrite(&brate, 4, 1, f);
    std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    std::fwrite(w.data(), 2, w.size(), f); std::fclose(f);
    size_t loud = 0; for (int16_t x : w) if (x > 1000 || x < -1000) loud++;
    std::printf("[audio] WAV %s: %.1f s, muestras con sonido: %zu\n", path, w.size() / 2 / 44100.0, loud);
}
static void memPoint(const char* tag) {
    PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    auto& b = flash::bmpStats();
    std::printf("[memoria] %-22s privada %.1f MB | bitmaps: %zu creados, %zu PNG decodificados, %.1f MB de pixeles\n", tag,
                pmc.PagefileUsage / 1048576.0, b.created, b.decoded, b.bytes / 1048576.0);
    // Desglose de los pixeles VIVOS (tras recolectar): imagenes del paquete vs bitmaps creados por el codigo, por tamano
    if (FlxG::camera) std::printf("[camaras] FlxG.camera %p %dx%d buffer %dx%d deferred=%d | lista:", (void*)FlxG::camera, (int)FlxG::camera->width, (int)FlxG::camera->height,
                                  FlxG::camera->buffer ? FlxG::camera->buffer->width : -1, FlxG::camera->buffer ? FlxG::camera->buffer->height : -1, FlxG::camera->buffer ? (int)FlxG::camera->buffer->deferred : -1);
    for (FlxCamera* c : camaras::todas()) std::printf(" %p %dx%d", (void*)c, c->buffer ? c->buffer->width : -1, c->buffer ? c->buffer->height : -1);
    std::printf("\n");
    as3::gc::trackParents() = true; as3::gc::parents().clear();
    if (as3::gc::enabled()) as3::gc::collect();
    as3::gc::trackParents() = false;
    {   // quien retiene los bitmaps grandes: cadena de dueños (clase) hasta la raiz
        int shown1360 = 0, shown242 = 0;
        for (as3::Object* o : as3::gc::heap()) {
            auto* bd = dynamic_cast<flash::BitmapData*>(o);
            if (!bd || bd->px.empty()) continue;
            bool big = bd->width == 1360 && bd->height == 768, item = bd->width == 242 && bd->height == 282;
            if (!(big && shown1360 < 4) && !(item && shown242 < 2)) continue;
            (big ? shown1360 : shown242)++;
            std::string chain;
            as3::Object* p = o;
            for (int d = 0; d < 14 && p; d++) {
                auto it = as3::gc::parents().find(p);
                if (it == as3::gc::parents().end()) { chain += " < (raiz)"; break; }
                p = it->second;
                chain += " < "; chain += p ? p->__className().str() : std::string("(raiz/estatico)");
            }
            std::printf("[dueños] %dx%d%s\n", bd->width, bd->height, chain.c_str());
        }
    }
    std::unordered_set<flash::BitmapData*> fromPak(b.all.begin(), b.all.end());
    size_t pakB = 0, pakN = 0, newB = 0, newN = 0, camB = 0;
    std::map<std::pair<int,int>, std::pair<size_t,size_t>> bySize;
    for (as3::Object* o : as3::gc::heap()) {
        auto* bd = dynamic_cast<flash::BitmapData*>(o);
        if (!bd || bd->px.empty()) continue;
        size_t bytes = bd->px.size() * 4;
        if (bd->deferred) camB += bytes;
        else if (fromPak.count(bd)) { pakB += bytes; pakN++; }
        else { newB += bytes; newN++; auto& s = bySize[{bd->width, bd->height}]; s.first++; s.second += bytes; }
    }
    std::printf("[memoria]   vivos: del paquete %zu (%.1f MB) | creados por el codigo %zu (%.1f MB) | camaras %.1f MB\n",
                pakN, pakB / 1048576.0, newN, newB / 1048576.0, camB / 1048576.0);
    std::vector<std::pair<size_t, std::pair<int,int>>> v;
    for (auto& kv : bySize) v.push_back({kv.second.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    for (size_t i = 0; i < v.size() && i < 12; i++)
        std::printf("[memoria]     %4dx%-4d x%-4zu %.1f MB\n", v[i].second.first, v[i].second.second, bySize[v[i].second].first, v[i].first / 1048576.0);
}

void as3_trace_statics();  // generado: marca los estaticos para el recolector
void as3_init_statics();  // generado por as3cpp (statics.cpp): estaticos de todas las clases en orden de dependencias

static LONG WINAPI sehDump(EXCEPTION_POINTERS* ep) {
    static int once = 0; if (once++) return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(stderr, "[SEH] codigo %08lx\n", ep->ExceptionRecord->ExceptionCode);
    if (ep->ExceptionRecord->ExceptionCode == 0xC00000FDu) return EXCEPTION_CONTINUE_SEARCH;  // desbordamiento de pila: sin margen para mas
    std::fprintf(stderr, "[SEH] acceso invalido en %p (base %p), direccion leida %p\n", ep->ExceptionRecord->ExceptionAddress,
                 (void*)GetModuleHandleA(nullptr), (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    // pila nativa aproximada: direcciones de retorno en la pila (se traducen con nm)
    uint64_t* sp = (uint64_t*)ep->ContextRecord->Rsp;
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    int shown = 0;
    for (int i = 0; i < 4096 && shown < 25; i++) {
        uint64_t v = sp[i];
        if (v > base && v < base + 0x4000000) { std::fprintf(stderr, "[SEH] pila %llx\n", (unsigned long long)v); shown++; }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char** argv) {
    AddVectoredExceptionHandler(1, sehDump);
    setvbuf(stdout, nullptr, _IONBF, 0);
    as3::installCrashHandler();
#ifdef AS3_TRACE
    as3::crashHook() = []() {
        const std::string& d = DataHandler::m_strDebug.str();
        std::fprintf(stderr, "[ultimos mensajes del juego]%s\n", d.substr(d.size() > 600 ? d.size() - 600 : 0).c_str());
    };
#endif
    platform::setDataRoot(argc > 1 ? argv[1] : "D:/Proyectos/NeoScavengerVita/work/gameroot");
    if (getenv("NEOS_AJUSTES")) ajustes::omitirImagenesFondo();
    int maxFrames = argc > 2 ? std::atoi(argv[2]) : 20000;
    std::printf("[arranque] raiz de datos: %s\n", platform::dataRoot().c_str());
    if (!getenv("NEOS_REALTIME")) as3::fixedStepMs() = 1000.0 / 30.0;  // reloj fijo a 30 fps (capturas reproducibles)
    as3_init_statics();
    if (getenv("NEOS_STRETCH")) {
        // Opcion "Stretch" del propio juego (GUIEscMenu.Stretch): SHOW_ALL ajusta su diseno a la pantalla
        flash::SharedObject::onCreate = [](const std::string& n, flash::SharedObject* so) {
            if (n == "flixel") so->data->__set("res", as3::Any(as3::objectOf({{"strStretchOverride", as3::Any(as3::String("showAll"))}})));
        };
    }
    std::printf("[arranque] estaticos listos\n");
    try {
        Preloader* pre = new Preloader();
        flash::Stage* st = new flash::Stage();
        st->stageWidth = getenv("NEOS_W") ? atoi(getenv("NEOS_W")) : 1360; st->stageHeight = getenv("NEOS_H") ? atoi(getenv("NEOS_H")) : 768; st->frameRate = 30;
        st->swfWidth = 1360; st->swfHeight = 768;  // tamano nativo del SWF de NEO Scavenger
        // Como en Flash: stage.loaderInfo es el del SWF principal; sin FlashVars, parameters es un objeto vacio
        st->loaderInfo = new flash::LoaderInfo();
        st->loaderInfo->parameters = as3::make<as3::Object>();
        st->loaderInfo->url = as3::String("file:///NEOScavenger.swf");
        pre->loaderInfo = st->loaderInfo;
        pre->root = pre;
        st->root = pre;
        st->stage = st;
        pre->stage = st;
        pre->loaderInfo = new flash::LoaderInfo();
        pre->loaderInfo->url = as3::String("file:///NEOScavenger.swf");
        pre->__construct();
        st->addChild(pre);  // el Preloader es la raiz de la lista de visualizacion
        if (!getenv("NEOS_NOGC")) {  // recolector entre fotogramas (NEOS_NOGC=1 lo desactiva para comparar)
            as3::gc::enabled() = true;
            as3::gc::rootFns().push_back([st, pre] { as3_trace_statics(); as3::gc::mark(st); as3::gc::mark(pre); });
        }
        std::printf("[arranque] Preloader construido\n");
        int frame = 0;
        double tLoad0 = platform::nowMs();
        if (getenv("NEOS_PROFLOAD")) { sampler::start(); sampler::on = true; }
        for (; frame < maxFrames && !DataHandler::bLoadingComplete; frame++) {
            flash::pumpPending();
            flash::enterFrameAll(st); flash::gcAfterFrame(30);
            static size_t shown = 0;
            const std::string& dbg = DataHandler::m_strDebug.str();
            if (dbg.size() > shown) { std::printf("[juego]%s\n", dbg.substr(shown).c_str()); shown = dbg.size(); }
            if (frame % 2000 == 0)
                std::printf("[carga] fotograma %d, %.1f%%, siguiente=%d, pasos pendientes=%u\n", frame, DataHandler::m_fPercentLoaded * 100.0,
                            (int)DataHandler::m_bNextStep, DataHandler::aLoadList ? DataHandler::aLoadList->get_length() : 0u);
        }
        std::printf("[carga] terminada=%d en %d fotogramas (%.1f%%)\n", (int)DataHandler::bLoadingComplete, frame,
                    DataHandler::m_fPercentLoaded * 100.0);
        if (getenv("NEOS_PROFLOAD")) sampler::dump("perfil_muestras.txt");
        std::printf("[carga] tiempo de carga en PC: %.0f ms, excepciones AS3 lanzadas: %zu\n", platform::nowMs() - tLoad0, as3::thrownCount());
        memPoint("datos cargados");
        if (DataHandler::bLoadingComplete) {
            // Resumen de lo cargado (para compararlo con el original: ver ROADMAP, hito 2)
            DataSet* ds = DataHandler::GetDataSet(DataHandler::m_strBasePrefix);
            auto dict = [](const char* nombre, as3::Dictionary* d) { std::printf("[datos] %-26s %zu\n", nombre, d ? d->order.size() : 0); };
            auto arr = [](const char* nombre, as3::Array* a) { std::printf("[datos] %-26s %u\n", nombre, a ? a->get_length() : 0u); };
            auto vec = [](const char* nombre, auto* v) { std::printf("[datos] %-26s %u\n", nombre, v ? v->get_length() : 0u); };
            dict("objetos (items)", ds->m_dictItems);
            {  // objetos distintos (el diccionario puede guardar el mismo objeto con varias claves)
                std::set<void*> uniq; size_t nulos = 0;
                for (auto& k : ds->m_dictItems->order) { as3::Any v = ds->m_dictItems->get(k); if (v.k == as3::Any::Obj) uniq.insert(v.o); else nulos++; }
                std::printf("[datos] %-26s %zu (valores nulos: %zu)\n", "objetos distintos", uniq.size(), nulos);
                size_t nn = 0; for (auto& x : ds->m_aConditions->v) if (x.k == as3::Any::Obj) nn++;
                std::printf("[datos] %-26s %zu de %u posiciones\n", "condiciones no vacias", nn, ds->m_aConditions->get_length());
            }
            vec("tablas de tesoro", ds->m_vTreasures);
            arr("encuentros", ds->m_aEncounters);
            arr("disparadores", ds->m_aEncounterTriggers);
            dict("ingredientes", ds->m_dictIngredients);
            vec("recetas", ds->m_vRecipes);
            dict("movimientos de combate", ds->m_dictMoves);
            arr("modos de ataque", ds->m_aAttackModes);
            arr("condiciones", ds->m_aConditions);
            arr("criaturas", ds->m_aCreatures);
            vec("tabla de criaturas", ds->m_vCreatureTable);
            vec("condiciones de campamento", ds->m_vCampConds);
            vec("lugares DMC", ds->m_vDMCPlaces);
            vec("titulares", ds->m_vHeadlines);
            arr("tipos de hexagono", ds->m_aHexTypes);
            arr("tipos de contenedor", ds->m_aContainerTypes);
            dict("facciones", ds->m_dictFactions);
            dict("mapas", ds->m_dictMaps);
            dict("imagenes", ds->m_dictIMG);
            dict("hexagonos prohibidos", ds->m_dictForbiddenHexes);
            dict("variables del juego", ds->m_dictGameVars);
            dict("perfiles de carga", ds->m_dictChargeProfiles);
            // Tras la carga: el Preloader anade el juego (FlxGame) a la pantalla y arranca MenuState
            int extra = argc > 3 ? std::atoi(argv[3]) : 300;
            for (int f = 0; f < extra; f++) {
                flash::pumpPending();
                flash::enterFrameAll(st); flash::gcAfterFrame(30);
                if (f % 50 == 0) {
                    FlxState* cur = FlxG::get_state();
                    std::printf("[juego] fotograma %d tras la carga, estado actual: %s\n", f,
                                cur ? cur->__className().str().c_str() : "(ninguno)");
                }
                static size_t shown2 = 0;
                const std::string& dbg2 = DataHandler::m_strDebug.str();
                if (dbg2.size() > shown2) { std::printf("[juego]%s\n", dbg2.substr(shown2).c_str()); shown2 = dbg2.size(); }
            }
            memPoint("menu (300 fotogramas)");
            // Captura del escenario completo (lista de visualizacion dibujada por software) a PNG
            flash::BitmapData* shot = as3::make<flash::BitmapData>(st->stageWidth, st->stageHeight, false, (double)0xFF000000u);
            flash::renderStage(st, shot);
            std::string pngData = png::encode(shot->width, shot->height, shot->px);
            std::string out = platform::dataRoot() + "../captura_menu.png";
            FILE* fo = std::fopen(out.c_str(), "wb");
            if (fo) { std::fwrite(pngData.data(), 1, pngData.size(), fo); std::fclose(fo); }
            std::printf("[captura] %s (%dx%d)\n", out.c_str(), shot->width, shot->height);
            // Diagnostico: buffer de la camara de Flixel y su lugar en la lista de visualizacion
            if (FlxG::camera && FlxG::camera->buffer) {
                flash::BitmapData* b = FlxG::camera->buffer;
                b->ensure();
                size_t opaque = 0; for (uint32_t c : b->px) if ((c >> 24) && (c & 0xFFFFFF)) opaque++;
                std::printf("[camara] buffer %dx%d, pixeles con color: %zu, bitmap en pantalla: %s, sprite padre: %s\n", b->width, b->height, opaque,
                            FlxG::camera->_flashBitmap && FlxG::camera->_flashBitmap->parent ? "si" : "no",
                            FlxG::camera->_flashSprite && FlxG::camera->_flashSprite->parent ? "si" : "no");
                std::string pb = png::encode(b->width, b->height, b->px);
                std::string o2 = platform::dataRoot() + "../captura_camara.png";
                FILE* f2 = std::fopen(o2.c_str(), "wb");
                if (f2) { std::fwrite(pb.data(), 1, pb.size(), f2); std::fclose(f2); }
            } else std::printf("[camara] sin camara o sin buffer\n");
            // Diagnostico: arbol de la lista de visualizacion
            std::function<void(flash::DisplayObject*, int)> tree = [&](flash::DisplayObject* d, int depth) {
                if (depth > 6) return;
                flash::Bitmap* bm = dynamic_cast<flash::Bitmap*>(d);
                std::printf("[arbol] %*s%s vis=%d alpha=%.2f pos=(%.0f,%.0f) escala=(%.2f,%.2f)%s\n", depth * 2, "", d->__className().str().c_str(),
                            (int)d->visible, d->alpha, d->x, d->y, d->scaleX, d->scaleY,
                            bm ? (bm->bitmapData ? (" bitmap " + std::to_string(bm->bitmapData->width) + "x" + std::to_string(bm->bitmapData->height)).c_str() : " bitmap NULO") : "");
                if (auto* c = dynamic_cast<flash::DisplayObjectContainer*>(d)) for (auto* ch : c->__children) tree(ch, depth + 1);
            };
            tree(st, 0);

            // ---- Guion de entrada: pulsar "NEW GAME" con el raton y capturar la pantalla siguiente
            auto save = [&](const char* name) {
                flash::BitmapData* b2 = as3::make<flash::BitmapData>(st->stageWidth, st->stageHeight, false, (double)0xFF000000u);
                flash::renderStage(st, b2);
                std::string d = png::encode(b2->width, b2->height, b2->px);
                std::string o = platform::dataRoot() + "../" + name;
                FILE* f = std::fopen(o.c_str(), "wb");
                if (f) { std::fwrite(d.data(), 1, d.size(), f); std::fclose(f); }
                std::printf("[captura] %s\n", o.c_str());
            };
            auto run = [&](int n) {
                for (int i = 0; i < n; i++) {
                    flash::pumpPending(); flash::enterFrameAll(st);
                    {   // audio en vacio (sin altavoz): el mezclador avanza 1/30 s para que los sonidos terminen igual que en la consola
                        static int16_t abuf[1470 * 2];
                        flash::audioUpdate(44100); audio::mixer().mix(abuf, 1470);
                        if (getenv("NEOS_WAV")) { auto& w = wavBuf(); w.insert(w.end(), abuf, abuf + 1470 * 2); }
                    }
                    if (getenv("NEOS_AJUSTES")) ajustes::aplicar();
                    if (getenv("NEOS_DEFER")) camaras::activarDiferido();
                    // NEOS_DROP=1: como en la Vita, las ordenes de dibujo de la camara no se reproducen (las ejecuta la GPU); solo mide la logica
                    if (getenv("NEOS_DROP")) camaras::descartarOrdenes();
                    flash::gcAfterFrame(30);
                    if (getenv("NEOS_SLOWMS")) Sleep(atoi(getenv("NEOS_SLOWMS")));
                }
                FlxState* cur = FlxG::get_state();
                std::printf("[guion] estado: %s\n", cur ? cur->__className().str().c_str() : "-");
            };
            flash::injectMouseMove(st, getenv("NEOS_CX") ? atof(getenv("NEOS_CX")) : 752, getenv("NEOS_CY") ? atof(getenv("NEOS_CY")) : 186); run(5);
            flash::injectMouseButton(st, true); run(3);
            flash::injectMouseButton(st, false); run(90);
            save("captura_newgame.png");
            memPoint("partida nueva");
#include "_who.inc"
            {   // banco de tiempos (como proxy de la Vita): logica y dibujo por fotograma en la pantalla actual
                flash::BitmapData* fb = as3::make<flash::BitmapData>(st->stageWidth, st->stageHeight, false, (double)0xFF000000u); fb->__gcPinned = true;
                double lg = 0, dr = 0; int N = getenv("NEOS_PROF") ? 600 : 120;
                if (getenv("NEOS_PROF")) { sampler::start(); sampler::on = true; }
                for (int i = 0; i < N; i++) {
                    double a = platform::nowMs(); if (getenv("NEOS_DEFER")) camaras::activarDiferido(); flash::pumpPending(); flash::enterFrameAll(st); flash::gcAfterFrame(30);
                    double b = platform::nowMs(); std::fill(fb->px.begin(), fb->px.end(), 0xFF000000u); flash::renderStage(st, fb);
                    double c = platform::nowMs(); lg += b - a; dr += c - b;
                }
                if (getenv("NEOS_PROF")) sampler::dump("perfil_muestras.txt");
                std::printf("[perf-pc] %d fotogramas: logica %.2f ms, dibujo %.2f ms por fotograma\n", N, lg / N, dr / N);
            }
            // ---- Guion de prueba (A3): NEOS_SCRIPT=<archivo>, una orden por linea, coordenadas de pantalla (960x544):
            //   click x y | move x y | down | up | rclick x y | key codigo | wait fotogramas | shot nombre | estado | memoria | # comentario
            if (const char* sp = getenv("NEOS_SCRIPT")) {
                FILE* sf = std::fopen(sp, "r");
                if (!sf) std::printf("[guion] no se puede abrir %s\n", sp);
                char line[256]; int ln = 0;
                while (sf && std::fgets(line, sizeof line, sf)) {
                    ln++;
                    char cmd[32] = {0}, arg[200] = {0}; double a = 0, b = 0;
                    if (std::sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#') continue;
                    std::string c = cmd;
                    try {
                        if (c == "click" && std::sscanf(line, "%*s %lf %lf", &a, &b) == 2) {
                            flash::injectMouseMove(st, a, b); run(2); flash::injectMouseButton(st, true); run(2); flash::injectMouseButton(st, false); run(2);
                        } else if (c == "rclick" && std::sscanf(line, "%*s %lf %lf", &a, &b) == 2) {
                            flash::injectMouseMove(st, a, b); run(2); flash::injectMouseButton(st, true, true); run(2); flash::injectMouseButton(st, false, true); run(2);
                        } else if (c == "move" && std::sscanf(line, "%*s %lf %lf", &a, &b) == 2) { flash::injectMouseMove(st, a, b); run(1); }
                        else if (c == "down") { flash::injectMouseButton(st, true); run(1); }
                        else if (c == "up") { flash::injectMouseButton(st, false); run(1); }
                        else if (c == "key" && std::sscanf(line, "%*s %lf", &a) == 1) { flash::injectKey(st, (uint32_t)a, true); run(2); flash::injectKey(st, (uint32_t)a, false); run(2); }
                        else if (c == "wait" && std::sscanf(line, "%*s %lf", &a) == 1) run((int)a);
                        else if (c == "shot" && std::sscanf(line, "%*s %199s", arg) == 1) save((std::string("guion_") + arg + ".png").c_str());
                        else if (c == "estado") run(0);
                        else if (c == "perfilclick" && std::sscanf(line, "%*s %lf %lf", &a, &b) == 2) {  // perfil de lo que provoca un clic
                            flash::injectMouseMove(st, a, b); run(2);
                            sampler::start(); sampler::on = true;
                            double t0 = platform::nowMs();
                            flash::injectMouseButton(st, true); run(2); flash::injectMouseButton(st, false); run(30);
                            sampler::dump("perfil_muestras.txt");
                            std::printf("[guion] perfilclick: %.0f ms en 34 fotogramas\n", platform::nowMs() - t0);
                        }
                        else if (c == "perfil") {  // perfil de la logica durante N fotogramas (sin dibujar): perfil <fotogramas>
                            sscanf(line, "%*s %lf", &a); flash::bmpStats().cp.clear(); sampler::start(); sampler::on = true;
                            double t0 = platform::nowMs(); run((int)a);
                            sampler::dump("perfil_muestras.txt");
                            std::printf("[guion] perfil: %.0f fotogramas, %.2f ms por fotograma de logica\n", a, (platform::nowMs() - t0) / a);
                        }
                        else if (c == "memoria") { memPoint("guion");
#include "_vivos.inc"
                        }
                        else if (c == "movimientos" && PlayState::m_objInstance && PlayState::m_objInstance->txtMovesLeft) {
                            std::printf("[guion] texto de movimientos: '%s' (m_fMovesLeft=%g)\n", PlayState::m_objInstance->txtMovesLeft->get_text().str().c_str(),
                                        PlayState::m_objInstance->sprPlayer ? PlayState::m_objInstance->sprPlayer->m_fMovesLeft : -1.0);
                        }
                        else if (c == "mapa" && MapUtils::tmapHexes && MapUtils::tmapHexes->_tileObjects) {
                            size_t cnt[4] = {0, 0, 0, 0};
                            for (auto& x : MapUtils::tmapHexes->_tileObjects->v)
                                if (auto* t = as3::cast<FlxHexTile>(x)) cnt[std::min<uint32_t>(t->nExploredState, 3)]++;
                            std::printf("[guion] mapa: casillas visibles %zu, recordadas %zu, sin explorar %zu, otras %zu\n", cnt[0], cnt[1], cnt[2], cnt[3]);
                            auto* hm = MapUtils::tmapHexes;
                            std::printf("[guion] mapa: hoja %dx%d, casilla %dx%d\n", hm->_tiles ? hm->_tiles->width : -1, hm->_tiles ? hm->_tiles->height : -1, (int)hm->_tileWidth, (int)hm->_tileHeight);
                        }
                        else if (c == "keydown" && std::sscanf(line, "%*s %lf", &a) == 1) { flash::injectKey(st, (uint32_t)a, true); run(1); }
                        else if (c == "keyup" && std::sscanf(line, "%*s %lf", &a) == 1) { flash::injectKey(st, (uint32_t)a, false); run(1); }
                        else if (c == "guardar") { bool ok = DataHandler::SaveGame(as3::Function()); std::printf("[guion] guardar partida: %s\n", ok ? "ok" : "FALLO"); run(5); }
                        else if (c == "redibujar" && MapUtils::tmapHexes) { MapUtils::tmapHexes->setDirty(true); run(2); }
                        else if (c == "raton") {  // diagnostico: posicion del raton para Flixel y objeto bajo el cursor en el inventario
                            GUIInventory* inv = PlayState::m_objInstance ? PlayState::m_objInstance->grpInventoryUI : nullptr;
                            std::printf("[guion] raton: escenario (%.0f,%.0f) | inventario ptMouse=(%.1f,%.1f) sobre=%s arrastrando=%s\n",
                                        st->mouseX, st->mouseY, inv && inv->ptMouse ? inv->ptMouse->x : -1.0, inv && inv->ptMouse ? inv->ptMouse->y : -1.0,
                                        inv && inv->objMouseOverItem ? inv->objMouseOverItem->get_IDString().str().c_str() : "-",
                                        inv && inv->objDragging ? inv->objDragging->get_IDString().str().c_str() : "-");
                        }
                        else if (c == "combate") {  // diagnostico del combate: opciones disponibles
                            GUIInventory* inv = PlayState::m_objInstance ? PlayState::m_objInstance->grpInventoryUI : nullptr;
                            CombatPair* cp = inv && inv->grpBattleLayer ? inv->grpBattleLayer->get_CurrentCombatPair() : nullptr;
                            ItemInstance* av = inv && inv->grpAvailEncounterSlot ? inv->grpAvailEncounterSlot->SocketedItem() : nullptr;
                            std::printf("[guion] combate: par=%p, opciones del par=%u, objetos en la caja de opciones=%u\n", (void*)cp,
                                        cp && cp->vAllItems ? cp->vAllItems->get_length() : 0u, av && av->vItems ? av->vItems->get_length() : 0u);
                            if (cp) std::printf("[guion]   movimientos del par=%u\n", cp->vAllMoves ? cp->vAllMoves->get_length() : 0u);
                            if (cp) {  // por que se descarta cada movimiento: sentencia donde sale IsAvailable (ver lines.txt)
                                DataSet* ds = DataHandler::GetDataSet(DataHandler::m_strBasePrefix);
                                for (auto& key : ds->m_dictMoves->order) {
                                    BattleMove* mv = as3::cast<BattleMove>(ds->m_dictMoves->get(key));
                                    if (!mv) continue;
                                    bool ok = mv->IsAvailable(cp->m_objBattle, cp);
                                    std::printf("[guion]   mov %-24s %s (sale en #%d) precond nosotros:", as3::toString(key).str().c_str(), ok ? "SI" : "no", as3::lastExitLine());
                                    for (int v : mv->m_vUsPreConditions->v) std::printf(" %d", v);
                                    std::printf("\n");
                                }
                                std::printf("[guion]   cola de condiciones activa=%d:", (int)cp->sprUs->m_bCondQueue);
                                for (auto& k : cp->sprUs->m_dictCondQueue->order) std::printf(" %s=%s", as3::toString(k).str().c_str(), as3::toString(cp->sprUs->m_dictCondQueue->get(k)).str().c_str());
                                std::printf("\n");
                                std::printf("[guion]   condiciones del jugador:");
                                for (auto& x : as3::eachValue(cp->sprUs->aCurrentStates)) if (auto* pc = as3::cast<PlayerCondition>(x)) std::printf(" %u", pc->m_nID);
                                std::printf("\n");
                                {
                                }
                            }
                            if (cp && cp->vAllItems) for (auto* it : cp->vAllItems->v) if (it) std::printf("[guion]   opcion %s\n", it->get_IDString().str().c_str());
                        }
                        else std::printf("[guion] linea %d: orden no reconocida: %s", ln, line);
                    } catch (const as3::Thrown& t) {
                        std::printf("[guion] linea %d: excepcion AS3: %s\n", ln, as3::toString(t.value).str().c_str());
                        t.print();
                    }
                    const std::string& dbg3 = DataHandler::m_strDebug.str();
                    static size_t shown3 = dbg3.size();
                    if (dbg3.size() > shown3) { std::printf("[juego]%s\n", dbg3.substr(shown3).c_str()); shown3 = dbg3.size(); }
                }
                if (sf) std::fclose(sf);
                std::printf("[guion] terminado (%d lineas)\n", ln);
                if (getenv("NEOS_WAV")) writeWav(getenv("NEOS_WAV"));
            }
            {
                std::vector<std::pair<size_t, std::pair<int,int>>> v;
                for (auto& kv : flash::bmpStats().sizes) v.push_back({kv.second * kv.first.first * kv.first.second * 4, kv.first});
                std::sort(v.rbegin(), v.rend());
                for (size_t i = 0; i < v.size() && i < 12; i++)
                    std::printf("[memoria] bitmaps %5dx%-5d x%-5zu = %.1f MB\n", v[i].second.first, v[i].second.second,
                                flash::bmpStats().sizes[v[i].second], v[i].first / 1048576.0);
                std::printf("[memoria] liberado por dispose: %.1f MB\n", flash::bmpStats().freed / 1048576.0);
            }
        }
    } catch (const as3::Thrown& t) {
        std::printf("[arranque] excepcion AS3: %s\n", as3::toString(t.value).str().c_str());
        t.print();
        return 2;
    }
    return 0;
}
