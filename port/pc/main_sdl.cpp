// main_sdl.cpp — NEO Scavenger en una ventana de PC (A4 del ROADMAP): el juego traducido + runtime, con raton y teclado.
// Misma configuracion que la Vita: escenario 960x544 (el juego usa su diseno 800x450 con su opcion "Stretch"),
// 30 fps fijos y recolector entre fotogramas. Sirve para probar a mano sin la consola.
// Uso: neoscav_sdl.exe [carpeta_de_datos] [escala_de_ventana]
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include "all.h"
#include "../src/ajustes_vita.h"

void as3_init_statics();
void as3_trace_statics();

static const int W = 960, H = 544;

// SDL -> codigo de tecla de Flash (Keyboard.*): letras y numeros coinciden con ASCII en mayusculas
static uint32_t flashKey(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return (uint32_t)(k - SDLK_a + 'A');
    if (k >= SDLK_0 && k <= SDLK_9) return (uint32_t)k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return 112u + (uint32_t)(k - SDLK_F1);
    switch (k) {
        case SDLK_ESCAPE: return 27; case SDLK_RETURN: return 13; case SDLK_SPACE: return 32; case SDLK_TAB: return 9;
        case SDLK_BACKSPACE: return 8; case SDLK_DELETE: return 46; case SDLK_LEFT: return 37; case SDLK_UP: return 38;
        case SDLK_RIGHT: return 39; case SDLK_DOWN: return 40; case SDLK_LSHIFT: case SDLK_RSHIFT: return 16;
        case SDLK_LCTRL: case SDLK_RCTRL: return 17; case SDLK_LALT: case SDLK_RALT: return 18;
        case SDLK_PAGEUP: return 33; case SDLK_PAGEDOWN: return 34; case SDLK_HOME: return 36; case SDLK_END: return 35;
        default: return 0;
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    platform::setDataRoot(argc > 1 ? argv[1] : "D:/Proyectos/NeoScavengerVita/work/gameroot");
    int scale = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("NEO Scavenger (port, 960x544)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * scale, H * scale, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);

    as3_init_statics();
    flash::SharedObject::onCreate = [](const std::string& n, flash::SharedObject* so) {
        if (n == "flixel") so->data->__set("res", as3::Any(as3::objectOf({{"strStretchOverride", as3::Any(as3::String("showAll"))}})));
    };
    flash::Stage* st = new flash::Stage();
    Preloader* pre = new Preloader();
    try {
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
    } catch (const as3::Thrown& t) {
        std::printf("[arranque] excepcion AS3: %s\n", as3::toString(t.value).str().c_str());
        return 2;
    }
    as3::gc::enabled() = !getenv("NEOS_NOGC");
    as3::gc::rootFns().push_back([st, pre] { as3_trace_statics(); as3::gc::mark(st); as3::gc::mark(pre); });
    flash::BitmapData* frame = as3::make<flash::BitmapData>(W, H, false, (double)0xFF000000u);
    frame->__gcPinned = true;

    bool quit = false;
    uint32_t next = SDL_GetTicks();
    double sumLogic = 0, sumDraw = 0; int frames = 0; uint32_t statT = SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        try {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) quit = true;
                else if (e.type == SDL_MOUSEMOTION) flash::injectMouseMove(st, e.motion.x, e.motion.y);
                else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                    flash::injectMouseMove(st, e.button.x, e.button.y);
                    if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
                        flash::injectMouseButton(st, e.type == SDL_MOUSEBUTTONDOWN, e.button.button == SDL_BUTTON_RIGHT);
                } else if (e.type == SDL_MOUSEWHEEL) flash::injectWheel(st, e.wheel.y * 3);
                else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                    if (uint32_t k = flashKey(e.key.keysym.sym)) if (!e.key.repeat) flash::injectKey(st, k, e.type == SDL_KEYDOWN);
                }
            }
            double t0 = platform::nowMs();
            flash::pumpPending();
            flash::enterFrameAll(st);
            ajustes::aplicar();
            flash::gcAfterFrame(30);
            double t1 = platform::nowMs();
            std::fill(frame->px.begin(), frame->px.end(), 0xFF000000u);
            flash::renderStage(st, frame);
            sumLogic += t1 - t0; sumDraw += platform::nowMs() - t1; frames++;
        } catch (const as3::Thrown& t) {
            std::printf("[juego] excepcion AS3 sin capturar: %s\n", as3::toString(t.value).str().c_str());
        }
        const std::string& dbg = DataHandler::m_strDebug.str();
        static size_t shown = 0;
        if (dbg.size() > shown) { std::printf("[juego]%s\n", dbg.substr(shown).c_str()); shown = dbg.size(); }
        SDL_UpdateTexture(tex, nullptr, frame->px.data(), W * 4);  // ARGB8888 = 0xAARRGGBB, igual que BitmapData
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        if (SDL_GetTicks() - statT >= 5000) {
            std::printf("[perf] logica %.2f ms, dibujo %.2f ms por fotograma | recolector: %zu pasadas, ultima %.1f ms, %zu objetos vivos\n",
                        sumLogic / frames, sumDraw / frames, as3::gc::stats().collections, as3::gc::stats().lastMs, as3::gc::stats().lastLive);
            sumLogic = sumDraw = 0; frames = 0; statT = SDL_GetTicks();
        }
        next += 33;  // 30 fps
        uint32_t now = SDL_GetTicks();
        if ((int32_t)(next - now) > 0) SDL_Delay(next - now); else next = now;
    }
    SDL_Quit();
    return 0;
}
