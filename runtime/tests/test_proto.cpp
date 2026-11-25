// Prueba del prototipo: las clases traducidas compilan y dan los mismos
// resultados que la semántica de AS3 (valores calculados a mano a partir del código).
#include <cmath>
#include <cstdio>
#include "ChargeProfile.h"
#include "FlxPoint.h"
#include "GameStats.h"
#include "Weather.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALLO linea %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    // FlxPoint: valores por defecto y encadenado (return this)
    FlxPoint* p = new FlxPoint();
    CHECK(p->x == 0 && p->y == 0);
    FlxPoint* q = new FlxPoint(3.5, -2);
    CHECK(p->copyFrom(q) == p && p->x == 3.5 && p->y == -2);
    CHECK(p->make(1, 2)->copyTo(q) == q && q->x == 1 && q->y == 2);
    flash::Point fp(7, 8);
    CHECK(p->copyFromFlash(&fp)->x == 7);

    // ChargeProfile: Number sin asignar es NaN en AS3; const de instancia
    ChargeProfile* c = new ChargeProfile();
    CHECK(std::isnan(c->m_fPerUse));
    CHECK(c->DISCHARGE_HEX == 3u);
    c->m_fPerUse = 0.5; c->m_fPerHour = 0.25; c->m_fPerHourEquipped = 1; c->m_fPerHex = 0.1;
    // 0.5*4 + 0.25*2 + 1*2 (equipado) + 0.1*3
    CHECK(c->GetQuantity(4, 2, 3, true) == 0.5 * 4 + 0.25 * 2 + 1.0 * 2 + 0.1 * 3);
    CHECK(c->GetQuantity(4, 2, 3, false) == 0.5 * 4 + 0.25 * 2 + 0.1 * 3);

    // GameStats: String null por defecto; destroy() vuelve a null
    GameStats* g = new GameStats();
    g->m_strLog = "x";
    CHECK(!g->m_strLog.isNull());
    g->destroy();
    CHECK(g->m_strLog.isNull() && g->m_nMorality == 0);

    // Weather: constructor con Date
    as3::Date* d = new as3::Date();
    Weather* w = new Weather(-5.5, 0.3, 0.6, true, false, d);
    CHECK(w->fTemp == -5.5 && w->bPrecip && !w->bOvercast && w->objDate == d);
    w->destroy();
    CHECK(w->objDate == nullptr);

    std::printf(fails ? "%d fallos\n" : "OK: todas las comprobaciones pasan\n", fails);
    return fails ? 1 : 0;
}
