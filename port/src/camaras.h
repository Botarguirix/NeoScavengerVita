// camaras.h — acceso a las camaras de Flixel desde los arneses (PC y Vita).
#pragma once
#include "all.h"
#include <vector>

namespace camaras {

inline std::vector<FlxCamera*> todas() {
    std::vector<FlxCamera*> out;
    if (FlxG::cameras)
        for (auto& x : FlxG::cameras->v)
            if (FlxCamera* c = as3::cast<FlxCamera>(x)) out.push_back(c);
    return out;
}

// Activa el modo diferido en el bufer de cada camara (llamar cada fotograma antes de la logica del juego)
inline void activarDiferido() {
    std::vector<flash::BitmapData*> b;
    for (FlxCamera* c : todas()) if (c->buffer) b.push_back(c->buffer);
    flash::setDeferredSet(b);
}

// Para medir solo la logica (PC): descarta las ordenes de dibujo de todas las camaras sin reproducirlas
inline void descartarOrdenes() {
    for (FlxCamera* c : todas()) if (c->buffer) c->buffer->dropDeferred();
}

}  // namespace camaras
