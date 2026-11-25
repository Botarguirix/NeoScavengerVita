// ajustes_vita.h — cambios propios del port (no del juego), pedidos para la Vita. Se aplican desde fuera del codigo
// traducido, cada fotograma (coste minimo), sin tocar las reglas.
#pragma once
#include "all.h"

namespace ajustes {

// Fondo animado del titulo (11 capas de 1360 px que se desplazan): en la Vita costaba ~280 ms por fotograma en el menu.
// Por peticion del usuario se quita: el grupo del fondo del menu de titulo/escape no se dibuja ni se actualiza.
inline bool& sinFondoTitulo() { static bool v = true; return v; }
// Llamar antes de cargar datos: las capas del fondo ni se decodifican (1x1 transparente en su lugar, ~64 MB menos)
inline void omitirImagenesFondo() { if (sinFondoTitulo()) platform::omittedImages().push_back("img/titleScroll"); }

inline void aplicar() {
    if (!sinFondoTitulo()) return;
    FlxState* s = FlxG::get_state();
    GUIEscMenu* m = nullptr;
    if (MenuState* ms = dynamic_cast<MenuState*>(s)) m = ms->m_grpMenu;
    else if (PlayState::m_objInstance && s == (FlxState*)PlayState::m_objInstance) m = PlayState::m_objInstance->grpHelp;
    if (m && m->m_grpBG && (m->m_grpBG->visible || m->m_grpBG->active)) {
        m->m_grpBG->visible = false;
        m->m_grpBG->active = false;
    }
}

}  // namespace ajustes
