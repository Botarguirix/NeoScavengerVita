// controles.h — mando y panel trasero de la Vita (docs/CONTROLES_VITA.md). Todo se traduce a lo que el juego ya entiende:
// sus propios atajos de teclado (PlayState.KeyHandler) o la misma funcion que usa el teclado (MoveCamera). No se
// inventan reglas. El tactil frontal y el stick izquierdo (cursor) estan en main_game_vita.cpp.
#pragma once
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <cmath>
#include "all.h"

namespace controles {

// Zonas muertas: las mismas que el cursor (radial agresiva)
constexpr float kDead = 0.28f, kOuter = 0.93f;

// Codigos de tecla de Flash (flash.ui.Keyboard)
enum : uint32_t { K_TAB = 9, K_SPACE = 32, K_LEFT = 37, K_UP = 38, K_RIGHT = 39, K_DOWN = 40, K_E = 69, K_I = 73, K_F1 = 112 };

struct Estado {
    uint32_t prevButtons = 0;
    bool arrowDown[4] = {false, false, false, false};  // izquierda, arriba, derecha, abajo (stick derecho)
    int dpadHeld = 0;                                  // fotogramas con la cruceta pulsada (aceleracion del cursor fino)
    // panel trasero
    int backFingers = 0;
    float backX[2] = {0, 0}, backY[2] = {0, 0};
    bool backValid = false;  // el gesto empezo fuera del borde (15 %)
    int twoStart = -1;       // fotograma en que empezo un toque de dos dedos
    float twoMove = 0;       // movimiento acumulado en el toque de dos dedos
    int frame = 0;
};
inline Estado& estado() { static Estado e; return e; }

inline void tecla(flash::Stage* st, uint32_t code, bool down) { flash::injectKey(st, code, down); }

// Boton de la Vita -> tecla del juego (pulsar y soltar a la vez que el boton: el juego reacciona al soltar)
inline void botonTecla(flash::Stage* st, uint32_t buttons, uint32_t prev, uint32_t mask, uint32_t code) {
    bool now = buttons & mask, before = prev & mask;
    if (now != before) tecla(st, code, now);
}

// Panel trasero: coordenadas 0..1919 x 0..1087 (mitad de resolucion de pantalla). Zona muerta del 15 % en el borde.
inline bool dentroDelPanel(float x, float y) {
    // area activa del panel trasero (medida con el agente): x 0..1919, y 108..889
    const float y0 = 108, y1 = 889, h = y1 - y0;
    return x > 1920 * 0.15f && x < 1920 * 0.85f && y > y0 + h * 0.15f && y < y1 - h * 0.15f;
}

// zoom: escala de la camara de Flixel (pixeles de pantalla por pixel del mundo)
inline void actualizar(flash::Stage* st, const SceCtrlData& pad, const SceTouchData& back, float& cx, float& cy, int W, int H, float zoom) {
    Estado& e = estado();
    e.frame++;
    const uint32_t b = pad.buttons, p = e.prevButtons;
    if (b != p) platform::logf("[mando] botones %08x (antes %08x) fotograma %d\n", (unsigned)b, (unsigned)p, e.frame);

    // --- botones -> atajos del propio juego
    botonTecla(st, b, p, SCE_CTRL_LTRIGGER, K_I);     // L: inventario
    botonTecla(st, b, p, SCE_CTRL_RTRIGGER, K_SPACE); // R: fin de turno (o lo que ESPACIO haga en cada pantalla)
    botonTecla(st, b, p, SCE_CTRL_SQUARE, K_E);       // cuadrado: rebuscar / encuentro
    botonTecla(st, b, p, SCE_CTRL_CIRCLE, K_TAB);     // circulo: volver a la vista principal (cerrar paneles)
    botonTecla(st, b, p, SCE_CTRL_START, K_F1);       // START: menu del juego
    // triangulo: clic derecho donde esta el cursor (menu contextual del propio juego)
    if ((b & SCE_CTRL_TRIANGLE) != (p & SCE_CTRL_TRIANGLE)) flash::injectMouseButton(st, (b & SCE_CTRL_TRIANGLE) != 0, true);

    // --- stick derecho -> flechas (mover el mapa, con la velocidad y los limites del juego)
    float rx = (pad.rx - 128) / 127.0f, ry = (pad.ry - 128) / 127.0f;
    float rmag = std::sqrt(rx * rx + ry * ry);
    bool want[4] = {false, false, false, false};
    if (rmag > kDead) {
        float nx = rx / rmag, ny = ry / rmag;  // direccion; 8 sectores
        want[0] = nx < -0.38f; want[2] = nx > 0.38f;
        want[1] = ny < -0.38f; want[3] = ny > 0.38f;
    }
    const uint32_t arrows[4] = {K_LEFT, K_UP, K_RIGHT, K_DOWN};
    for (int i = 0; i < 4; i++) if (want[i] != e.arrowDown[i]) { tecla(st, arrows[i], want[i]); e.arrowDown[i] = want[i]; }

    // --- cruceta -> cursor fino (1 px; mantenida acelera a 4 px)
    int dx = (b & SCE_CTRL_RIGHT ? 1 : 0) - (b & SCE_CTRL_LEFT ? 1 : 0);
    int dy = (b & SCE_CTRL_DOWN ? 1 : 0) - (b & SCE_CTRL_UP ? 1 : 0);
    if (dx || dy) {
        e.dpadHeld++;
        float step = e.dpadHeld > 15 ? 4.0f : 1.0f;
        if (e.dpadHeld == 1 || e.dpadHeld > 8) {  // primer paso inmediato, luego repeticion
            cx = std::max(0.0f, std::min((float)W - 1, cx + dx * step));
            cy = std::max(0.0f, std::min((float)H - 1, cy + dy * step));
            flash::injectMouseMove(st, cx, cy);
        }
    } else e.dpadHeld = 0;

    // --- panel trasero
    int n = back.reportNum;
    float bx[2] = {0, 0}, by[2] = {0, 0};
    for (int i = 0; i < n && i < 2; i++) { bx[i] = back.report[i].x; by[i] = back.report[i].y; }
    if (n != e.backFingers) {  // empieza o cambia el gesto
        if (e.backFingers == 2 && n < 2 && e.twoStart >= 0 && e.frame - e.twoStart < 12 && e.twoMove < 40) {
            // toque breve con dos dedos: clic donde esta el cursor
            flash::injectMouseButton(st, true);
            flash::injectMouseButton(st, false);
        }
        e.backValid = n > 0 && dentroDelPanel(bx[0], by[0]);
        if (n > 0) platform::logf("[trasero] %d dedos en (%.0f,%.0f), %s\n", n, bx[0], by[0], e.backValid ? "valido" : "borde: ignorado");
        e.twoStart = n == 2 ? e.frame : -1;
        e.twoMove = 0;
    } else if (n == 1 && e.backValid) {
        // un dedo: arrastrar el mapa (solo en la vista del mapa), como mover la vista con el teclado
        PlayState* ps = PlayState::m_objInstance;
        if (ps && FlxG::get_state() == (FlxState*)ps && ps->m_nGameState == PlayState::GAMESTATE_GAMEREADY) {
            float ddx = (bx[0] - e.backX[0]) * 0.5f / zoom, ddy = (by[0] - e.backY[0]) * 0.5f / zoom;
            if (std::fabs(ddx) + std::fabs(ddy) > 0.5f) {
                FlxPoint* pt = as3::make<FlxPoint>(ddx, ddy);
                ps->MoveCamera(pt);
            }
        }
    } else if (n == 2 && e.backValid) {
        // dos dedos: cursor de precision (como un panel tactil), a media velocidad
        float mdx = ((bx[0] - e.backX[0]) + (bx[1] - e.backX[1])) * 0.25f * 0.5f;
        float mdy = ((by[0] - e.backY[0]) + (by[1] - e.backY[1])) * 0.25f * 0.5f;
        e.twoMove += std::fabs(mdx) + std::fabs(mdy);
        if (mdx != 0 || mdy != 0) {
            cx = std::max(0.0f, std::min((float)W - 1, cx + mdx));
            cy = std::max(0.0f, std::min((float)H - 1, cy + mdy));
            flash::injectMouseMove(st, cx, cy);
        }
    }
    e.backFingers = n;
    for (int i = 0; i < 2; i++) { e.backX[i] = bx[i]; e.backY[i] = by[i]; }
    e.prevButtons = b;
}

}  // namespace controles
