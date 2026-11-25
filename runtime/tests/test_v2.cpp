// Prueba v2: ejecuta CODIGO TRADUCIDO de verdad (FlxPoint, ChargeProfile, Weather, GameStats) y el runtime de AS3.
// Los valores esperados salen de la semantica de AS3 (calculados a mano) o de Python (fechas).
// Compilar con tools/as3cpp/run_tests.sh
#include <cmath>
#include <cstdio>
#include "all.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FALLO linea %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    // ---- codigo traducido
    FlxPoint* p = as3::make<FlxPoint>();
    CHECK(p->x == 0 && p->y == 0);                       // parametros por defecto = 0
    FlxPoint* q = as3::make<FlxPoint>(3.5, -2);
    CHECK(p->copyFrom(q) == p && p->x == 3.5 && p->y == -2);
    CHECK(p->make(1, 2)->copyTo(q) == q && q->x == 1 && q->y == 2);
    flash::Point* fp = as3::make<flash::Point>(7, 8);
    CHECK(p->copyFromFlash(fp)->x == 7);

    ChargeProfile* c = as3::make<ChargeProfile>();
    CHECK(std::isnan(c->m_fPerUse));                     // Number sin asignar = NaN
    CHECK(c->DISCHARGE_HEX == 3u);
    c->m_fPerUse = 0.5; c->m_fPerHour = 0.25; c->m_fPerHourEquipped = 1; c->m_fPerHex = 0.1;
    CHECK(c->GetQuantity(4, 2, 3, true) == 0.5 * 4 + 0.25 * 2 + 1.0 * 2 + 0.1 * 3);
    CHECK(c->GetQuantity(4, 2, 3, false) == 0.5 * 4 + 0.25 * 2 + 0.1 * 3);

    GameStats* g = as3::make<GameStats>();
    g->m_strLog = "x";
    CHECK(!g->m_strLog.isNull());
    g->destroy();
    CHECK(g->m_strLog.isNull() && g->m_nMorality == 0);

    as3::Date* d0 = as3::make<as3::Date>();
    Weather* w = as3::make<Weather>(-5.5, 0.3, 0.6, true, false, d0);
    CHECK(w->fTemp == -5.5 && w->bPrecip && !w->bOvercast && w->objDate == d0);
    w->destroy();
    CHECK(w->objDate == nullptr);

    // ---- semantica de AS3 en el runtime
    CHECK(as3::toInt(4294967296.0 + 5) == 5);            // ToInt32 con desbordamiento
    CHECK(as3::toInt(2147483648.0) == -2147483647 - 1);
    CHECK(as3::toInt(-1.9) == -1);                       // trunca hacia 0
    CHECK(as3::toUint(-1) == 4294967295u);
    CHECK(as3::toString(as3::add(as3::String("5"), 3)).str() == "53");        // String + Number
    CHECK(as3::add(5, 3) == 8.0);
    CHECK(as3::toNumber(as3::add(as3::Any(5), as3::Any(3))) == 8);
    CHECK(as3::toString(as3::Any(as3::add(as3::Any(as3::String("a")), as3::Any(1)))).str() == "a1");
    CHECK(as3::eq(as3::Any(), as3::Any(nullptr)));                            // undefined == null
    CHECK(!as3::strictEq(as3::Any(), as3::Any(nullptr)));                     // undefined !== null
    CHECK(as3::eq(as3::Any(as3::String("5")), as3::Any(5)));                  // "5" == 5
    CHECK(!as3::strictEq(as3::Any(as3::String("5")), as3::Any(5)));           // "5" !== 5
    CHECK(!as3::truthy(as3::NaN) && !as3::truthy(as3::String("")) && as3::truthy(as3::String("0")));
    CHECK(as3::numToString(0.1 + 0.2) == "0.30000000000000004");
    CHECK(as3::numToString(1e21) == "1e+21" && as3::numToString(100) == "100" && as3::numToString(-0.5) == "-0.5");

    as3::Array* a = as3::make<as3::Array>();
    a->push(3, 1, 2);
    CHECK(a->get_length() == 3 && as3::at(a, 0).n == 3);
    a->sort(as3::Any(), as3::Array::NUMERIC);
    CHECK(as3::at(a, 0).n == 1 && as3::at(a, 2).n == 3);
    CHECK(a->join(as3::String("-")).str() == "1-2-3");
    as3::Array* sp = a->splice(1, 1);
    CHECK(sp->get_length() == 1 && a->get_length() == 2);

    as3::Vector<int32_t>* v = as3::make<as3::Vector<int32_t>>();
    v->push(5, 6, 7);
    CHECK(v->indexOf(6) == 1 && as3::at(v, 2) == 7);
    bool threw = false;
    try { as3::at(v, 10); } catch (const as3::Thrown&) { threw = true; }
    CHECK(threw);                                         // RangeError #1125 fuera de rango

    as3::Dictionary* dd = as3::make<as3::Dictionary>();
    dd->set(as3::Any(as3::String("k")), as3::Any(42));
    CHECK(as3::toNumber(as3::at(dd, as3::Any(as3::String("k")))) == 42 && dd->has(as3::Any(as3::String("k"))));

    as3::String s("Hola Mundo");
    CHECK(s.substr(0, 4).str() == "Hola" && s.indexOf(as3::String("Mundo")) == 5 && s.toUpperCase().str() == "HOLA MUNDO");
    CHECK(s.split(as3::String(" "))->get_length() == 2);
    CHECK(s.replace(as3::regex("o", "g"), as3::String("0")).str() == "H0la Mund0");

    // ---- Date (AS3): referencia calculada con Python (UTC)
    as3::Date* dt = as3::make<as3::Date>(2026.0, 10.0, 20.0);            // 20-nov-2026
    CHECK(dt->getTime() == 1795132800000.0);
    CHECK(dt->getFullYear() == 2026 && dt->getMonth() == 10 && dt->getDate() == 20 && dt->getDay() == 5);  // viernes
    dt->month = 12;                                                       // desborda: ene 2027
    CHECK(dt->getTime() == 1800403200000.0);   // 20-ene-2027 (month=12, date=20)
    CHECK(dt->getFullYear() == 2027 && dt->getMonth() == 0);
    dt->setHours(36);                                                     // +1 dia 12 h
    CHECK(dt->getDate() == 21 && dt->getHours() == 12);  // 20-ene + 36 h

    std::printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
