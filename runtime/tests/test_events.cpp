// Prueba de eventos y lista de visualizacion de flash.h (sin el resto del juego): compila en segundos.
#include <cstdio>
#include "as3rt.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FALLO linea %d: %s\n", __LINE__, #c); ++fails; } } while (0)

struct Owner : flash::MovieClip {
    int clicks = 0, added = 0, frames = 0;
    void onClick(flash::MouseEvent*) { clicks++; }
    void onAdded(flash::Event*) { added++; }
    void onFrame(flash::Event*) { frames++; }
};

int main() {
    as3::installCrashHandler();
    flash::Stage* st = new flash::Stage();
    Owner* o = new Owner();
    flash::Sprite* s = new flash::Sprite();
    // add / remove con metodos enlazados (el juego usa `as3::bind(this, &Clase::metodo)`)
    s->addEventListener(flash::MouseEvent::CLICK, as3::bind(o, &Owner::onClick));
    flash::MouseEvent* e = new flash::MouseEvent();
    e->type = flash::MouseEvent::CLICK;
    s->dispatchEvent(e);
    CHECK(o->clicks == 1);
    s->removeEventListener(flash::MouseEvent::CLICK, as3::bind(o, &Owner::onClick));
    flash::MouseEvent* e2 = new flash::MouseEvent();
    e2->type = flash::MouseEvent::CLICK;
    s->dispatchEvent(e2);
    CHECK(o->clicks == 1);  // ya no escucha
    // lista de visualizacion
    st->addChild(o);
    CHECK(o->stage == st);
    o->addEventListener(flash::Event::ADDED_TO_STAGE, as3::bind(o, &Owner::onAdded));
    o->addEventListener(flash::Event::ENTER_FRAME, as3::bind(o, &Owner::onFrame));
    o->addChild(s);
    CHECK(s->stage == st && s->parent == o);
    flash::enterFrameAll(st);
    flash::enterFrameAll(st);
    CHECK(o->frames == 2);
    o->removeChild(s);
    CHECK(s->parent == nullptr && s->stage == nullptr);
    std::printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
