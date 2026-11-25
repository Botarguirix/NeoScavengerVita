// ajustes_vita.h — cambios propios del port (no del juego), pedidos para la Vita. Se aplican desde fuera del codigo
// traducido, cada fotograma (coste minimo), sin tocar las reglas.
#pragma once
#include "all.h"
#include "camaras.h"

namespace ajustes {

// Fondo animado del titulo (11 capas de 1360 px que se desplazan): en la Vita costaba ~280 ms por fotograma en el menu.
// Por peticion del usuario se quita: el grupo del fondo del menu de titulo/escape no se dibuja ni se actualiza.
inline bool& sinFondoTitulo() { static bool v = true; return v; }
// Llamar antes de cargar datos: las capas del fondo ni se decodifican (1x1 transparente en su lugar, ~64 MB menos)
inline void omitirImagenesFondo() { if (sinFondoTitulo()) platform::omittedImages().push_back("img/titleScroll"); }

// Barras de "letterbox": 4 sprites que comparten un bitmap negro de FlxG.width x FlxG.height (1360x768) y cada uno
// con su copia de fotograma: ~16 MB de la zona de pixeles (medido 25-11 con el rastreo de duenos del recolector).
// A 960x544 con el ajuste "Stretch" el diseno 800x450 ocupa toda la pantalla y las barras cubren ~2 px: se ocultan y
// se sustituyen por 1x1 para liberar la memoria.
inline void quitarLetterbox(GUILetterbox* lb) {
    if (!lb) return;
    if (lb->visible) lb->visible = false;
    for (FlxSprite* s : {lb->m_sprTitleFrameL, lb->m_sprTitleFrameR, lb->m_sprTitleFrameT, lb->m_sprTitleFrameB}) {
        if (!s || !s->get_pixels() || s->get_pixels()->width <= 1) continue;
        s->set_pixels(as3::make<flash::BitmapData>(1, 1, true, 4278190080.0));
    }
}

// La camara principal del juego es de FlxG.width x FlxG.height (1360x768) y su sprite "screen" guarda, ademas del
// bufer (que si se usa), una copia de fotograma del mismo tamano (4 MB) que nunca se dibuja: se cambia por 1x1.
inline void liberarCopiaPantallaCamaras() {
    for (FlxCamera* c : camaras::todas()) {
        FlxSprite* s = c->screen;
        if (s && s->framePixels && s->framePixels != c->buffer && s->framePixels->width > 1)
            flash::ownedSet(s->framePixels, as3::make<flash::BitmapData>(1, 1, true, 0));
    }
}

inline void aplicar() {
    liberarCopiaPantallaCamaras();
    FlxState* s = FlxG::get_state();
    if (MenuState* ms = dynamic_cast<MenuState*>(s)) quitarLetterbox(ms->m_grpLetterbox);
    else if (PlayState::m_objInstance && s == (FlxState*)PlayState::m_objInstance) quitarLetterbox(PlayState::m_objInstance->m_grpLetterbox);
    if (!sinFondoTitulo()) return;
    GUIEscMenu* m = nullptr;
    if (MenuState* ms = dynamic_cast<MenuState*>(s)) m = ms->m_grpMenu;
    else if (PlayState::m_objInstance && s == (FlxState*)PlayState::m_objInstance) m = PlayState::m_objInstance->grpHelp;
    if (m && m->m_grpBG && (m->m_grpBG->visible || m->m_grpBG->active)) {
        m->m_grpBG->visible = false;
        m->m_grpBG->active = false;
    }
}

}  // namespace ajustes
