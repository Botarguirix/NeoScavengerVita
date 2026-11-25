// flash.h — subconjunto de la API de Flash que usa NEO Scavenger (Flixel + código propio).
// Solo lo que el juego pide; el backend real (vitaGL/SDL2) rellenará BitmapData, TextField, Sound, etc.
// Todo esto son STUBS con la forma correcta: lo que no hace nada útil todavía llama a as3::notImplemented
// únicamente si se ejecuta algo que de verdad lo necesita.
#pragma once
#include <functional>
#include "as3rt.h"
#include "pixalloc.h"
#include "lz4lite.h"
#include "persist.h"
#include "audio.h"
#include "png.h"
#include "font.h"
#include <sstream>

namespace flash {
using as3::Any;
using as3::Array;
using as3::Function;
using as3::Object;
using as3::String;

#define FLASH_CONST(name, value) static inline const as3::String name = as3::String(value)

// Eventos asincronos (carga de archivos...): se ejecutan en pumpPending(), una vez por frame, como hace Flash.
inline std::vector<std::function<void()>>& pendingQueue() { static std::vector<std::function<void()>> q; return q; }
inline void post(std::function<void()> f) { pendingQueue().push_back(std::move(f)); }
inline size_t pumpPending() {
    size_t n = 0;
    while (!pendingQueue().empty()) {
        auto batch = std::move(pendingQueue());
        pendingQueue().clear();
        for (auto& f : batch) { f(); n++; }
    }
    return n;
}

// ------------------------------------------------------------ geometría
struct Point : Object {
    double x = 0, y = 0;
    void __construct(double x_ = 0, double y_ = 0) { x = x_; y = y_; }
    double get_length() const { return std::sqrt(x * x + y * y); }
    Point* clone() const { Point* p = new Point(); p->x = x; p->y = y; return p; }
    Point* add(Point* o) const { Point* p = new Point(); p->x = x + o->x; p->y = y + o->y; return p; }
    static double distance(Point* a, Point* b) { return std::hypot(a->x - b->x, a->y - b->y); }
};
struct Rectangle : Object {
    double x = 0, y = 0, width = 0, height = 0;
    void __construct(double x_ = 0, double y_ = 0, double w = 0, double h = 0) { x = x_; y = y_; width = w; height = h; }
    double get_right() const { return x + width; }
    double get_bottom() const { return y + height; }
    double get_left() const { return x; }
    double get_top() const { return y; }
    Rectangle* clone() const { Rectangle* r = new Rectangle(); r->x = x; r->y = y; r->width = width; r->height = height; return r; }
    bool contains(double px, double py) const { return px >= x && py >= y && px < x + width && py < y + height; }
    bool intersects(Rectangle* o) const { return x < o->x + o->width && o->x < x + width && y < o->y + o->height && o->y < y + height; }
};
struct Matrix : Object {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    void __construct(double a_ = 1, double b_ = 0, double c_ = 0, double d_ = 1, double tx_ = 0, double ty_ = 0) { a = a_; b = b_; c = c_; d = d_; tx = tx_; ty = ty_; }
    void identity() { a = 1; b = 0; c = 0; d = 1; tx = 0; ty = 0; }
    void translate(double dx, double dy) { tx += dx; ty += dy; }
    void scale(double sx, double sy) { a *= sx; b *= sy; c *= sx; d *= sy; tx *= sx; ty *= sy; }
    void rotate(double q) { double ca = std::cos(q), sa = std::sin(q); double na = a * ca - b * sa, nb = a * sa + b * ca, nc = c * ca - d * sa, nd = c * sa + d * ca, ntx = tx * ca - ty * sa, nty = tx * sa + ty * ca; a = na; b = nb; c = nc; d = nd; tx = ntx; ty = nty; }
};
struct ColorTransform : Object {
    double redMultiplier = 1, greenMultiplier = 1, blueMultiplier = 1, alphaMultiplier = 1;
    double redOffset = 0, greenOffset = 0, blueOffset = 0, alphaOffset = 0;
    void __construct(double rm = 1, double gm = 1, double bm = 1, double am = 1, double ro = 0, double go = 0, double bo = 0, double ao = 0) {
        redMultiplier = rm; greenMultiplier = gm; blueMultiplier = bm; alphaMultiplier = am; redOffset = ro; greenOffset = go; blueOffset = bo; alphaOffset = ao;
    }
};
struct Transform : Object { ColorTransform* colorTransform = nullptr; Matrix* matrix = nullptr; void __trace() override { Object::__trace(); as3::gc::mark(colorTransform); as3::gc::mark(matrix); } };

// ------------------------------------------------------------ eventos
struct Event : Object {
    FLASH_CONST(ENTER_FRAME, "enterFrame"); FLASH_CONST(ADDED_TO_STAGE, "addedToStage"); FLASH_CONST(REMOVED_FROM_STAGE, "removedFromStage");
    FLASH_CONST(COMPLETE, "complete"); FLASH_CONST(ACTIVATE, "activate"); FLASH_CONST(DEACTIVATE, "deactivate"); FLASH_CONST(RESIZE, "resize");
    FLASH_CONST(CLOSE, "close"); FLASH_CONST(OPEN, "open"); FLASH_CONST(SELECT, "select"); FLASH_CONST(CANCEL, "cancel"); FLASH_CONST(CHANGE, "change");
    FLASH_CONST(ID3, "id3"); FLASH_CONST(SOUND_COMPLETE, "soundComplete"); FLASH_CONST(INIT, "init"); FLASH_CONST(MOUSE_LEAVE, "mouseLeave"); FLASH_CONST(FULLSCREEN, "fullScreen");
    String type; Object* target = nullptr; Object* currentTarget = nullptr;
    void __trace() override { Object::__trace(); as3::gc::mark(target); as3::gc::mark(currentTarget); }
    void __construct(const String& t = String(""), bool = false, bool = false) { type = t; }
    void stopPropagation() {} void preventDefault() {}
};
struct ErrorEvent : Event { String text; double errorID = 0; FLASH_CONST(ERROR, "error"); };
struct IOErrorEvent : ErrorEvent { FLASH_CONST(IO_ERROR, "ioError"); };
struct SecurityErrorEvent : ErrorEvent { FLASH_CONST(SECURITY_ERROR, "securityError"); };
struct NetStatusEvent : Event { FLASH_CONST(NET_STATUS, "netStatus"); Object* info = nullptr; void __trace() override { Event::__trace(); as3::gc::mark(info); } };
struct MouseEvent : Event {
    FLASH_CONST(CLICK, "click"); FLASH_CONST(MOUSE_DOWN, "mouseDown"); FLASH_CONST(MOUSE_UP, "mouseUp"); FLASH_CONST(MOUSE_MOVE, "mouseMove");
    FLASH_CONST(MOUSE_OVER, "mouseOver"); FLASH_CONST(MOUSE_OUT, "mouseOut"); FLASH_CONST(MOUSE_WHEEL, "mouseWheel");
    FLASH_CONST(RIGHT_CLICK, "rightClick"); FLASH_CONST(RIGHT_MOUSE_DOWN, "rightMouseDown"); FLASH_CONST(RIGHT_MOUSE_UP, "rightMouseUp");
    FLASH_CONST(MIDDLE_CLICK, "middleClick"); FLASH_CONST(MIDDLE_MOUSE_DOWN, "middleMouseDown"); FLASH_CONST(MIDDLE_MOUSE_UP, "middleMouseUp");
    FLASH_CONST(ROLL_OVER, "rollOver"); FLASH_CONST(ROLL_OUT, "rollOut"); FLASH_CONST(DOUBLE_CLICK, "doubleClick");
    double stageX = 0, stageY = 0, localX = 0, localY = 0, delta = 0; bool ctrlKey = false, shiftKey = false, altKey = false, buttonDown = false;
};
struct KeyboardEvent : Event {
    FLASH_CONST(KEY_DOWN, "keyDown"); FLASH_CONST(KEY_UP, "keyUp");
    uint32_t keyCode = 0, charCode = 0, keyLocation = 0; bool ctrlKey = false, shiftKey = false, altKey = false;
};
struct EventDispatcher : Object {
    std::vector<std::pair<std::string, Function>> __listeners;
    void __trace() override { Object::__trace(); for (auto& l : __listeners) as3::gc::mark(l.second); }
    void __construct() {}
    void addEventListener(const String& type, const Function& f, bool = false, int32_t = 0, bool = false) { __listeners.emplace_back(type.str(), f); }
    void removeEventListener(const String& type, const Function& f, bool = false) {
        for (size_t i = 0; i < __listeners.size(); i++) if (__listeners[i].first == type.str() && __listeners[i].second == f) { __listeners.erase(__listeners.begin() + i); break; }
    }
    bool dispatchEvent(Event* e) {
        if (!e->target) e->target = this;
        e->currentTarget = this;
        auto copy = __listeners;
        for (auto& l : copy) if (l.first == e->type.str()) l.second.call({Any(e)});
        return true;
    }
    bool willTrigger(const String& type) { return hasEventListener(type); }
    bool hasEventListener(const String& type) { for (auto& l : __listeners) if (l.first == type.str()) return true; return false; }
};

// ------------------------------------------------------------ display
struct Graphics : Object {
    // Solo lo que se puede dibujar sin rasterizador vectorial: rectangulos y circulos rellenos (los usa la interfaz)
    struct Cmd { int kind; double x, y, w, h; uint32_t argb; };  // kind 0 = rect, 1 = circulo (x,y centro, w radio)
    std::vector<Cmd> cmds;
    uint32_t fill = 0; bool filling = false;
    void clear() { cmds.clear(); filling = false; }
    void lineStyle(double = 0, double = 0, double = 1) {}
    void beginFill(double c = 0, double a = 1) { fill = ((uint32_t)std::lround(std::max(0.0, std::min(1.0, a)) * 255) << 24) | ((uint32_t)c & 0xFFFFFF); filling = true; }
    void endFill() { filling = false; }
    void drawRect(double x, double y, double w, double h) { if (filling) cmds.push_back({0, x, y, w, h, fill}); }
    void drawCircle(double x, double y, double r) { if (filling) cmds.push_back({1, x, y, r, r, fill}); }
    void moveTo(double, double) {}
    void lineTo(double, double) {}
};
struct BmpStats { size_t created = 0, decoded = 0, bytes = 0, freed = 0; std::map<std::pair<int,int>, size_t> sizes; std::map<std::string, size_t> who; std::vector<struct BitmapData*> all; std::map<std::string, size_t> cp; };
inline BmpStats& bmpStats() { static BmpStats s; return s; }
struct BitmapData : Object {
    // Bitmap de un solo color (relleno completo y sin escrituras despues): copiarlo es un simple relleno.
    // Lo usa FlxCamera.fill cada fotograma (rellena _fill y lo copia entero sobre la camara).
    bool uniform = false; uint32_t uniformColor = 0;
    bool uniformPending = false;  // px todavia no refleja uniformColor (relleno perezoso): ensure() lo escribe
    // Version: cambia en cada escritura (la cache de texturas de la GPU sabe asi si hay que volver a subirlo).
    uint32_t version = 1;
    // Modo diferido (bufer de la camara de Flixel): copyPixels/fillRect/draw sobre este bitmap no tocan pixeles, se
    // anotan como ordenes que la GPU ejecuta al final del fotograma. Cualquier lectura de pixeles lo "aplana" antes
    // (las reproduce por software), asi el resultado es identico al de Flash.
    struct Cmd {
        enum Kind : uint8_t { Blit, Rect, Draw } kind;
        bool merge = false;
        struct BitmapData* src = nullptr;
        int32_t sx = 0, sy = 0, w = 0, h = 0;   // rectangulo origen (Blit) o destino (Rect)
        int32_t dx = 0, dy = 0;                 // destino (Blit)
        uint32_t color = 0;                     // Rect
        double ma = 1, mb = 0, mc = 0, md = 1, mtx = 0, mty = 0;  // Draw: matriz
        bool hasCt = false; double ct[8] = {1, 1, 1, 1, 0, 0, 0, 0};  // Draw: color (mult r,g,b,a; offset r,g,b,a)
        bool smoothing = false;
    };
    bool deferred = false, replaying = false;
    bool cmdsFromClear = false;   // la lista empieza con un relleno completo opaco (no hace falta el contenido previo)
    std::vector<Cmd> cmds;
    uint32_t deferRefs = 0;       // ordenes pendientes (de algun bufer diferido) que leen este bitmap
    void __trace() override { Object::__trace(); for (auto& k : cmds) if (k.src) as3::gc::mark((Object*)k.src); }
    void flushDeferred();         // reproduce las ordenes por software y vacia la lista
    void dropDeferred();          // vacia la lista sin reproducirla (la GPU ya la dibujo)
    void touched();               // se va a escribir: version nueva; si otro bufer la tiene pendiente, se aplana antes
    int32_t width = 0, height = 0; bool transparent = true; std::vector<uint32_t, pixmem::Alloc<uint32_t>> px;
    std::string sourcePath;  // si no esta vacia y px esta vacio, los pixeles se decodifican del PNG al primer acceso
    // Imagen del paquete ya descomprimida: de donde salio y la version al descomprimirla. Si no se ha escrito desde
    // entonces (version == decodedVersion) sus pixeles se pueden soltar y volver a leer del paquete (evictCleanPakImages)
    std::string pakPath; uint32_t decodedVersion = 0;
    bool sharedNoDispose = false;  // lo usan varios duenos (p. ej. _pixels y framePixels de un FlxSprite): ownedSet no lo libera
    void ensure() {
        if (deferred && !replaying && !cmds.empty()) flushDeferred();
        if (uniformPending) { uniformPending = false; std::fill(px.begin(), px.end(), uniformColor); }
        if (!px.empty() || sourcePath.empty()) return;
        if (platform::pakImage(sourcePath)) {  // imagen ya decodificada: se descomprime directamente en la zona de pixeles
            size_t n = (size_t)width * (size_t)height;
            px.reserve(n + 8); px.resize(n);  // 8 pixeles de margen: decompressFast escribe hasta 32 bytes de mas
            bool ok = platform::readImagePixels(sourcePath, (uint8_t*)px.data(), [](const uint8_t* s, size_t sl, uint8_t* d, size_t dl) { return lz4lite::decompressFast(s, sl, d, dl); });
            if (ok) { bmpStats().decoded++; bmpStats().all.push_back(this); bmpStats().bytes += px.size() * 4;
                      pakPath = sourcePath; decodedVersion = version; sourcePath.clear(); return; }
            platform::log("[imagenes]", "no se pudo leer %s del paquete", sourcePath.c_str());
            px.assign(n, 0); sourcePath.clear(); return;
        }
        std::string bytes; png::Image img;
        if (platform::readFile(sourcePath, bytes) && png::decode(bytes, img)) { px.assign(img.argb.begin(), img.argb.end()); std::vector<uint32_t>().swap(img.argb); bmpStats().decoded++; bmpStats().all.push_back(this); bmpStats().bytes += px.size() * 4; }
        else { platform::log("[imagenes]", "no se pudo decodificar %s", sourcePath.c_str()); px.assign((size_t)width * (size_t)height, 0); }
        sourcePath.clear();
    }
    void __construct(double w = 0, double h = 0, bool t = true, double fill = 0xFFFFFFFF) { uniform = true; uniformColor = (uint32_t)fill | (t ? 0u : 0xFF000000u); width = (int32_t)w; height = (int32_t)h; transparent = t;
        // Relleno perezoso: se reserva sin escribir y ensure() rellena solo si alguien lee o escribe una parte. Muchos
        // bitmaps nuevos se sobrescriben enteros (la copia de fotograma de FlxSprite.resetHelpers): asi no se recorren dos veces
        px.resize((size_t)w * (size_t)h); uniformPending = !px.empty(); bmpStats().created++; bmpStats().bytes += px.size() * 4; bmpStats().all.push_back(this); bmpStats().sizes[{width, height}]++;
#ifdef AS3_TRACE
        { auto& st = as3::traceStack(); std::string k = std::to_string(width) + "x" + std::to_string(height);
          for (size_t i = st.size(), n = 0; i-- > 0 && n < 3; n++) k += std::string(" < ") + st[i];
          bmpStats().who[k] += px.size() * 4; }
#endif
    }
    Rectangle* get_rect() { Rectangle* r = new Rectangle(); r->width = width; r->height = height; return r; }
    BitmapData* clone() { BitmapData* b = new BitmapData(); b->width = width; b->height = height; b->transparent = transparent; ensure(); b->px = px; b->uniform = uniform; b->uniformColor = uniformColor; return b; }
    uint32_t getPixel32(double x, double y) { ensure(); return (x >= 0 && y >= 0 && x < width && y < height) ? px[(size_t)y * width + (size_t)x] : 0; }
    uint32_t getPixel(double x, double y) { return getPixel32(x, y) & 0xFFFFFF; }
    void setPixel32(double x, double y, double c) { uniform = false; touched(); ensure(); if (x >= 0 && y >= 0 && x < width && y < height) px[(size_t)y * width + (size_t)x] = (uint32_t)c; }
    void setPixel(double x, double y, double c) { setPixel32(x, y, (uint32_t)c | 0xFF000000u); }
    void fillRect(Rectangle* r, double c) {
        if (!r) return;
        if (deferred && !replaying) {
            uint32_t v = (uint32_t)c; if (!transparent) v |= 0xFF000000u;
            int x0 = std::max(0, (int)r->x), y0 = std::max(0, (int)r->y);
            int x1 = std::min(width, (int)(r->x + r->width)), y1 = std::min(height, (int)(r->y + r->height));
            if (x0 >= x1 || y0 >= y1) return;
            bool whole = x0 == 0 && y0 == 0 && x1 == width && y1 == height;
            if (whole) { dropDeferred(); cmdsFromClear = true; }  // sin mezcla: sustituye todo el contenido anterior
            Cmd k; k.kind = Cmd::Rect; k.merge = false; k.sx = x0; k.sy = y0; k.w = x1 - x0; k.h = y1 - y0; k.color = v;
            cmds.push_back(k); version++; uniform = false;
            return;
        }
        touched();
        uint32_t v = (uint32_t)c; if (!transparent) v |= 0xFF000000u;
        int x0 = std::max(0, (int)r->x), y0 = std::max(0, (int)r->y);
        int x1 = std::min(width, (int)(r->x + r->width)), y1 = std::min(height, (int)(r->y + r->height));
        if (x0 >= x1 || y0 >= y1) return;
        if (x0 == 0 && y0 == 0 && x1 == width && y1 == height) {  // relleno completo: perezoso
            if (px.size() != (size_t)width * (size_t)height) { px.assign((size_t)width * (size_t)height, v); sourcePath.clear(); uniformPending = false; }
            else uniformPending = true;
            uniform = true; uniformColor = v;
            return;
        }
        ensure();
        if (!(uniform && uniformColor == v)) uniform = false;
        for (int j = y0; j < y1; j++) std::fill_n(&px[(size_t)j * width + x0], x1 - x0, v);
    }
    // Mezcla "source-over" con alfa no premultiplicado (formato de la API de BitmapData)
    static uint32_t over(uint32_t d, uint32_t sc) {
        uint32_t sa = sc >> 24;
        if (sa == 255) return sc;
        if (sa == 0) return d;
        uint32_t da = d >> 24;
        if (da == 255) {  // destino opaco (camara, pantalla): sin divisiones (el Cortex-A9 no divide enteros por hardware)
            uint32_t ia = 255 - sa;
            auto mix = [&](int sh) { uint32_t v = ((sc >> sh) & 255) * sa + ((d >> sh) & 255) * ia; return ((v + 1 + (v >> 8)) >> 8) & 255; };
            return 0xFF000000u | mix(16) << 16 | mix(8) << 8 | mix(0);
        }
        uint32_t oa = sa + da * (255 - sa) / 255;
        if (oa == 0) return 0;
        auto ch = [&](int sh) { uint32_t a = (sc >> sh) & 255, b = (d >> sh) & 255; return ((a * sa + b * da * (255 - sa) / 255) / oa) & 255; };
        return oa << 24 | ch(16) << 16 | ch(8) << 8 | ch(0);
    }
    void plot(int x, int y, uint32_t c, bool blend) { if (uniformPending) ensure(); uniform = false; version++;
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        uint32_t& d = px[(size_t)y * width + x];
        d = blend ? over(d, c) : c;
        if (!transparent) d |= 0xFF000000u;
    }
    void copyPixels(BitmapData* src, Rectangle* r, Point* p, BitmapData* alphaB = nullptr, Point* alphaP = nullptr, bool merge = false) {
        if ((uintptr_t)this < 4096) { platform::log("[imagenes]", "copyPixels sobre un BitmapData nulo"); return; }
        if (!src || !r || !p) return;
        if (deferred && !replaying && !alphaB && src != this) {
            if (src->deferred && !src->cmds.empty()) src->flushDeferred();
            int32_t sx0 = (int32_t)std::floor(r->x), sy0 = (int32_t)std::floor(r->y), w = (int32_t)r->width, h = (int32_t)r->height;
            int32_t dx0 = (int32_t)std::floor(p->x), dy0 = (int32_t)std::floor(p->y);
            if (w <= 0 || h <= 0) return;
            if (src->uniform) {  // bitmap de un solo color: relleno (FlxCamera.fill y sus efectos)
                uint32_t c = src->uniformColor | (src->transparent ? 0u : 0xFF000000u);
                uint32_t a = c >> 24;
                if (merge && a == 0) return;
                int x0 = std::max(0, dx0), y0 = std::max(0, dy0), x1 = std::min(width, dx0 + w), y1 = std::min(height, dy0 + h);
                if (x0 >= x1 || y0 >= y1) return;
                bool whole = x0 == 0 && y0 == 0 && x1 == width && y1 == height;
                if (whole && (!merge || a == 255)) { dropDeferred(); cmdsFromClear = true; }
                Cmd k; k.kind = Cmd::Rect; k.merge = merge && a != 255; k.sx = x0; k.sy = y0; k.w = x1 - x0; k.h = y1 - y0; k.color = c;
                cmds.push_back(k); version++; uniform = false;
                return;
            }
            Cmd k; k.kind = Cmd::Blit; k.merge = merge; k.src = src; k.sx = sx0; k.sy = sy0; k.w = w; k.h = h; k.dx = dx0; k.dy = dy0;
            src->deferRefs++;
            cmds.push_back(k); version++; uniform = false;
            return;
        }
        touched();
        if (uniformPending && !merge && !alphaB && src != this && !src->uniform) {  // la copia tapa todo el destino: sobra el relleno
            int sx = (int)std::floor(r->x), sy = (int)std::floor(r->y), dx = (int)std::floor(p->x), dy = (int)std::floor(p->y);
            if (dx == 0 && dy == 0 && sx >= 0 && sy >= 0 && (int)r->width >= width && (int)r->height >= height &&
                sx + width <= src->width && sy + height <= src->height && src->transparent == transparent)
                uniformPending = false;
        }
        ensure();
        if (!(src->uniform && !alphaB && src != this)) src->ensure();  // origen de un solo color: el atajo de abajo no lee sus pixeles
        if (alphaB) alphaB->ensure();
        int sx0 = (int)std::floor(r->x), sy0 = (int)std::floor(r->y), w = (int)r->width, h = (int)r->height;
        int dx0 = (int)std::floor(p->x), dy0 = (int)std::floor(p->y);
        int ax0 = alphaP ? (int)alphaP->x : 0, ay0 = alphaP ? (int)alphaP->y : 0;
        // recorte unico del rectangulo contra origen y destino (antes se comprobaba en cada pixel)
        int i0 = std::max({0, -sx0, -dx0}), i1 = std::min({w, src->width - sx0, width - dx0});
        int j0 = std::max({0, -sy0, -dy0}), j1 = std::min({h, src->height - sy0, height - dy0});
        if (i0 >= i1 || j0 >= j1) return;
#ifdef AS3_TRACE
        { auto& st = as3::traceStack(); std::string k = "copyPixels";
          for (size_t q = st.size(), n = 0; q-- > 0 && n < 3; n++) k += std::string(" < ") + st[q];
          bmpStats().cp[k] += (size_t)(i1 - i0) * (j1 - j0); }
#endif
        if (src->px.size() < (size_t)src->width * src->height || px.size() < (size_t)width * height) {
            platform::log("[imagenes]", "copyPixels: bitmap con menos pixeles que su tamano (origen %dx%d/%zu, destino %dx%d/%zu)",
                          src->width, src->height, src->px.size(), width, height, px.size());
            return;
        }
        const uint32_t sop = src->transparent ? 0u : 0xFF000000u, dop = transparent ? 0u : 0xFF000000u;
        if (src->uniform && !alphaB && src != this) {  // copia de un bitmap de un solo color = relleno
            uint32_t c = src->uniformColor | sop;
            uint32_t a = c >> 24;
            bool whole = dx0 + i0 == 0 && dy0 + j0 == 0 && i1 - i0 == width && j1 - j0 == height;
            if (!merge || a == 255) {
                uint32_t v = c | dop;
                for (int j = j0; j < j1; j++) std::fill_n(px.data() + (size_t)(dy0 + j) * width + (dx0 + i0), i1 - i0, v);
                if (whole) { uniform = true; uniformColor = v; } else uniform = false;
                return;
            }
            if (a == 0) return;  // mezclar transparente no cambia nada
            uniform = false;
            for (int j = j0; j < j1; j++) { uint32_t* d = px.data() + (size_t)(dy0 + j) * width + (dx0 + i0); for (int q = 0; q < i1 - i0; q++) d[q] = over(d[q], c) | dop; }
            return;
        }
        uniform = false;
        for (int j = j0; j < j1; j++) {
            // punteros a la primera columna visible: nunca se forman indices fuera del vector
            const uint32_t* s = src->px.data() + ((size_t)(sy0 + j) * src->width + (size_t)(sx0 + i0));
            uint32_t* d = px.data() + ((size_t)(dy0 + j) * width + (size_t)(dx0 + i0));
            const int n = i1 - i0;
            if (alphaB) {
                for (int q = 0; q < n; q++) {
                    int ax = ax0 + i0 + q, ay = ay0 + j;
                    uint32_t am = (ax >= 0 && ay >= 0 && ax < alphaB->width && ay < alphaB->height) ? alphaB->px[(size_t)ay * alphaB->width + ax] >> 24 : 0;
                    uint32_t c = s[q] | sop;
                    c = (c & 0xFFFFFF) | (((c >> 24) * am / 255) << 24);
                    d[q] = over(d[q], c) | dop;
                }
            } else if (!merge || sop) {
                // copia directa (sin mezcla, o el origen es opaco: el resultado es el mismo)
                if (!sop && !dop) std::memcpy(d, s, (size_t)n * 4);
                else for (int q = 0; q < n; q++) d[q] = s[q] | sop | dop;
            } else {
                for (int q = 0; q < n; q++) {
                    uint32_t c = s[q];
                    uint32_t a = c >> 24;
                    if (a == 255) d[q] = c;
                    else if (a) d[q] = over(d[q], c) | dop;
                }
            }
        }
    }
    void draw(Object* source, Matrix* m = nullptr, ColorTransform* ct = nullptr, const String& blend = String(), Rectangle* clip = nullptr, bool smoothing = false);
    // Como en Flash, un bitmap liberado queda sin pixeles (ancho y alto 0, asi ningun blit lee fuera)
    void dispose() { uniform = false; uniformPending = false; touched(); if (!cmds.empty()) dropDeferred(); bmpStats().freed += px.size() * 4; std::vector<uint32_t, pixmem::Alloc<uint32_t>>().swap(px); sourcePath.clear(); width = height = 0; }
    // BitmapData.hitTest de Flash: pixel con alfa >= umbral bajo un punto, dentro de un rectangulo o solapando otro bitmap
    bool hitTest(Point* first, double thr, Object* second, Point* secondPt = nullptr, double thr2 = 1) {
        ensure();
        double fx = first ? first->x : 0, fy = first ? first->y : 0;
        uint32_t t1 = (uint32_t)std::max(0.0, thr);
        auto alphaAt = [&](BitmapData* b, int x, int y) -> uint32_t {
            if (x < 0 || y < 0 || x >= b->width || y >= b->height) return 0;
            return b->transparent ? b->px[(size_t)y * b->width + x] >> 24 : 255;
        };
        if (Point* p = dynamic_cast<Point*>(second)) {
            int x = (int)std::floor(p->x - fx), y = (int)std::floor(p->y - fy);
            return x >= 0 && y >= 0 && x < width && y < height && alphaAt(this, x, y) >= t1;
        }
        if (Rectangle* r = dynamic_cast<Rectangle*>(second)) {
            int x0 = std::max(0, (int)std::floor(r->x - fx)), y0 = std::max(0, (int)std::floor(r->y - fy));
            int x1 = std::min(width, (int)std::ceil(r->x + r->width - fx)), y1 = std::min(height, (int)std::ceil(r->y + r->height - fy));
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (alphaAt(this, x, y) >= t1) return true;
            return false;
        }
        BitmapData* o = dynamic_cast<BitmapData*>(second);
        if (!o) return false;
        o->ensure();
        double sx = secondPt ? secondPt->x : 0, sy = secondPt ? secondPt->y : 0;
        uint32_t t2 = (uint32_t)std::max(0.0, thr2);
        int x0 = std::max(0, (int)std::floor(sx - fx)), y0 = std::max(0, (int)std::floor(sy - fy));
        int x1 = std::min(width, (int)std::ceil(sx - fx + o->width)), y1 = std::min(height, (int)std::ceil(sy - fy + o->height));
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++)
            if (alphaAt(this, x, y) >= t1 && alphaAt(o, (int)(x + fx - sx), (int)(y + fy - sy)) >= t2) return true;
        return false;
    }
    void lock() {} void unlock(Rectangle* = nullptr) {}
    static uint32_t applyCT(uint32_t c, const ColorTransform* ct) {
        if (!ct) return c;
        auto f = [](double v) { return (uint32_t)std::max(0.0, std::min(255.0, v)); };
        uint32_t a = f((c >> 24) * ct->alphaMultiplier + ct->alphaOffset);
        uint32_t r = f(((c >> 16) & 255) * ct->redMultiplier + ct->redOffset);
        uint32_t g = f(((c >> 8) & 255) * ct->greenMultiplier + ct->greenOffset);
        uint32_t b = f((c & 255) * ct->blueMultiplier + ct->blueOffset);
        return a << 24 | r << 16 | g << 8 | b;
    }
    void colorTransform(Rectangle* r, ColorTransform* ct) { ensure(); uniform = false; touched();
        ensure();
        int x0 = std::max(0, (int)r->x), y0 = std::max(0, (int)r->y), x1 = std::min(width, (int)(r->x + r->width)), y1 = std::min(height, (int)(r->y + r->height));
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { uint32_t& d = px[(size_t)y * width + x]; d = applyCT(d, ct); }
    }
    void scroll(double dx, double dy) { ensure(); uniform = false; touched();
        ensure();
        std::vector<uint32_t> old(px.begin(), px.end());
        int ix = (int)dx, iy = (int)dy;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            int sx = x - ix, sy = y - iy;
            if (sx >= 0 && sy >= 0 && sx < width && sy < height) px[(size_t)y * width + x] = old[(size_t)sy * width + sx];
        }
    }
    template <class... A> void perlinNoise(A...) { as3::notImplemented("BitmapData.perlinNoise"); }
    template <class... A> void applyFilter(A...) {}
    template <class... A> void merge(A...) {}
    template <class... A> void threshold(A...) {}
    template <class... A> void paletteMap(A...) {}
};
struct DisplayObject : EventDispatcher {
    double x = 0, y = 0, width = 0, height = 0, alpha = 1, rotation = 0, scaleX = 1, scaleY = 1, mouseX = 0, mouseY = 0;
    bool visible = true, mouseEnabled = true, __isStage = false;
    String name;
    struct DisplayObjectContainer* parent = nullptr;
    struct Stage* stage = nullptr;
    struct LoaderInfo* loaderInfo = nullptr;
    Transform* transform = nullptr;
    DisplayObject* root = nullptr;
    Rectangle* scrollRect = nullptr;
    Any filters;
    void __trace() override { EventDispatcher::__trace(); as3::gc::mark((Object*)parent); as3::gc::mark((Object*)stage); as3::gc::mark((Object*)loaderInfo); as3::gc::mark(transform); as3::gc::mark(root); as3::gc::mark(scrollRect); as3::gc::mark(filters); }
    Rectangle* getBounds(Object*) { return new Rectangle(); }
    Point* localToGlobal(Point* p) { return p; }
    Point* globalToLocal(Point* p) { return p; }
    Point* get_mousePos() { return new Point(); }
    Any __get(const std::string& n) override;
    void __set(const std::string& n, const Any& v) override;
};
struct InteractiveObject : DisplayObject { bool tabEnabled = false; bool mouseChildren = true; bool doubleClickEnabled = false; };
struct DisplayObjectContainer : InteractiveObject {
    std::vector<DisplayObject*> __children;
    void __trace() override { DisplayObject::__trace(); for (auto* c : __children) as3::gc::mark(c); }
    // Al entrar en la pantalla, el objeto y sus hijos reciben ADDED_TO_STAGE (como en Flash)
    static void attachStage(DisplayObject* c, Stage* st);
    static void detachStage(DisplayObject* c);
    DisplayObject* addChild(DisplayObject* c) {
        if (c->parent) c->parent->removeChild(c);
        __children.push_back(c); c->parent = this; if (stage || __isStage) attachStage(c, stageOrSelf()); return c;
    }
    DisplayObject* addChildAt(DisplayObject* c, double i) {
        if (c->parent) c->parent->removeChild(c);
        __children.insert(__children.begin() + std::min((size_t)i, __children.size()), c); c->parent = this;
        if (stage || __isStage) attachStage(c, stageOrSelf()); return c;
    }
    DisplayObject* removeChild(DisplayObject* c) {
        auto it = std::find(__children.begin(), __children.end(), c);
        if (it != __children.end()) { __children.erase(it); detachStage(c); }
        c->parent = nullptr; return c;
    }
    DisplayObject* removeChildAt(double i) { DisplayObject* c = __children[(size_t)i]; return removeChild(c); }
    Stage* stageOrSelf();
    DisplayObject* getChildAt(double i) { return __children[(size_t)i]; }
    int32_t get_numChildren() { return (int32_t)__children.size(); }
    bool contains(DisplayObject* c) { return std::find(__children.begin(), __children.end(), c) != __children.end(); }
    int32_t getChildIndex(DisplayObject* c) { auto it = std::find(__children.begin(), __children.end(), c); return it == __children.end() ? -1 : (int32_t)(it - __children.begin()); }
};
struct Sprite : DisplayObjectContainer {
    Graphics* graphics = new Graphics();
    void __trace() override { DisplayObjectContainer::__trace(); as3::gc::mark(graphics); }
    bool buttonMode = false, useHandCursor = false;
    void startDrag() {} void stopDrag() {}
};
struct MovieClip : Sprite {
    int32_t currentFrame = 1, totalFrames = 1, framesLoaded = 1;
    void stop() {} void play() {} void gotoAndStop(const Any&) {} void gotoAndPlay(const Any&) {} void nextFrame() {} void prevFrame() {}
};
struct Bitmap : DisplayObject {
    BitmapData* bitmapData = nullptr; bool smoothing = false; String pixelSnapping;
    void __trace() override { DisplayObject::__trace(); as3::gc::mark(bitmapData); }
    void __construct(BitmapData* d = nullptr, const String& = String(), bool s = false) { bitmapData = d; smoothing = s; if (d) { width = d->width; height = d->height; } }
    Any __get(const std::string& n) override { if (n == "bitmapData") return Any((Object*)bitmapData); if (n == "smoothing") return Any(smoothing); return DisplayObject::__get(n); }
    void __loadEmbed(const char* src) override {
        std::string path = platform::embedPath(src), bytes;
        int w = 0, h = 0;
        if (platform::imageSize(path, w, h) || (platform::readFile(path, bytes) && png::header(bytes, w, h))) {
            BitmapData* bd = new BitmapData();
            bd->width = w; bd->height = h; bd->sourcePath = path;
            bitmapData = bd; width = w; height = h;
        } else platform::log("[assets]", "imagen incrustada no encontrada: %s (%s)", src, path.c_str());
    }
};
struct Stage : DisplayObjectContainer {
    Stage() { __isStage = true; }
    String scaleMode, align, displayState, quality; int32_t stageWidth = 960, stageHeight = 544, frameRate = 30;
    // Tamano nativo del SWF. Con NO_SCALE, Flash coloca el origen del escenario segun align
    // ("" = centrado; contiene L/R/T/B = pegado a ese borde) cuando la ventana no mide lo mismo que el SWF.
    int32_t swfWidth = 0, swfHeight = 0;
    double originX() const {
        if (!swfWidth) return 0; std::string a = align.str();
        if (a.find('L') != std::string::npos) return 0;
        if (a.find('R') != std::string::npos) return stageWidth - swfWidth;
        return (stageWidth - swfWidth) / 2.0;
    }
    double originY() const {
        if (!swfHeight) return 0; std::string a = align.str();
        if (a.find('T') != std::string::npos) return 0;
        if (a.find('B') != std::string::npos) return stageHeight - swfHeight;
        return (stageHeight - swfHeight) / 2.0;
    }
    Object* focus = nullptr; bool stageFocusRect = false; bool showDefaultContextMenu = true;
    void __trace() override { DisplayObjectContainer::__trace(); as3::gc::mark(focus); }
};
inline Stage* DisplayObjectContainer::stageOrSelf() { Stage* s = dynamic_cast<Stage*>((Object*)this); return s ? s : stage; }
inline void DisplayObjectContainer::attachStage(DisplayObject* c, Stage* st) {
    bool was = c->stage != nullptr;
    c->stage = st;
    if (!c->root) c->root = (st && st->__children.size()) ? st->__children[0] : nullptr;
    if (!was) { Event* e = new Event(); e->type = Event::ADDED_TO_STAGE; c->dispatchEvent(e); }
    if (DisplayObjectContainer* k = dynamic_cast<DisplayObjectContainer*>((Object*)c)) {
        auto copy = k->__children;
        for (auto* ch : copy) attachStage(ch, st);
    }
}
inline void DisplayObjectContainer::detachStage(DisplayObject* c) {
    if (c->stage) { Event* e = new Event(); e->type = Event::REMOVED_FROM_STAGE; c->dispatchEvent(e); }
    c->stage = nullptr;
    if (DisplayObjectContainer* k = dynamic_cast<DisplayObjectContainer*>((Object*)c)) {
        auto copy = k->__children;
        for (auto* ch : copy) detachStage(ch);
    }
}
// ---------------------------------------------------------------- entrada (la inyecta el backend: raton en PC, tactil/mando en Vita)
// Posicion del raton en coordenadas del escenario: cada objeto recibe su mouseX/mouseY local, como en Flash.
inline void setMouseLocal(DisplayObject* d, double gx, double gy) {
    double sx = d->scaleX ? d->scaleX : 1, sy = d->scaleY ? d->scaleY : 1;
    d->mouseX = (gx - d->x) / sx;
    d->mouseY = (gy - d->y) / sy;
    if (DisplayObjectContainer* k = dynamic_cast<DisplayObjectContainer*>((Object*)d))
        for (auto* ch : k->__children) if (ch) setMouseLocal(ch, d->mouseX, d->mouseY);
}
// x, y en pixeles de la ventana; se pasan a coordenadas del escenario (origen segun align)
inline void injectMouseMove(Stage* st, double x, double y) {
    x -= st->originX(); y -= st->originY();
    st->mouseX = x; st->mouseY = y;
    for (auto* ch : st->__children) if (ch) setMouseLocal(ch, x, y);
    MouseEvent* e = new MouseEvent(); e->type = MouseEvent::MOUSE_MOVE; e->stageX = x; e->stageY = y;
    st->dispatchEvent(e);
}
inline void injectMouseButton(Stage* st, bool down, bool right = false) {
    MouseEvent* e = new MouseEvent();
    e->type = right ? (down ? MouseEvent::RIGHT_MOUSE_DOWN : MouseEvent::RIGHT_MOUSE_UP) : (down ? MouseEvent::MOUSE_DOWN : MouseEvent::MOUSE_UP);
    e->stageX = st->mouseX; e->stageY = st->mouseY; e->buttonDown = down;
    st->dispatchEvent(e);
    if (!down) {
        MouseEvent* c = new MouseEvent();
        c->type = right ? MouseEvent::RIGHT_CLICK : MouseEvent::CLICK; c->stageX = st->mouseX; c->stageY = st->mouseY;
        st->dispatchEvent(c);
    }
}
inline void injectWheel(Stage* st, double delta) {
    MouseEvent* e = new MouseEvent(); e->type = MouseEvent::MOUSE_WHEEL; e->delta = delta; e->stageX = st->mouseX; e->stageY = st->mouseY;
    st->dispatchEvent(e);
}
inline void injectKey(Stage* st, uint32_t keyCode, bool down, uint32_t charCode = 0) {
    KeyboardEvent* e = new KeyboardEvent();
    e->type = down ? KeyboardEvent::KEY_DOWN : KeyboardEvent::KEY_UP; e->keyCode = keyCode; e->charCode = charCode;
    st->dispatchEvent(e);
}

// Un fotograma de Flash: ENTER_FRAME a todo el arbol de objetos (el escenario primero)
inline void enterFrameAll(DisplayObject* d) {
    if (d && d->__isStage && as3::fixedStepMs() > 0) as3::virtualNowMs() += as3::fixedStepMs();
    Event* e = new Event(); e->type = Event::ENTER_FRAME;
    d->dispatchEvent(e);
    if (DisplayObjectContainer* k = dynamic_cast<DisplayObjectContainer*>((Object*)d)) {
        auto copy = k->__children;
        for (auto* ch : copy) enterFrameAll(ch);
    }
}
struct URLRequest;
struct Loader;
inline Any DisplayObject::__get(const std::string& n) {
    if (n == "x") return Any(x); if (n == "y") return Any(y);
    if (n == "width") return Any(width); if (n == "height") return Any(height);
    if (n == "alpha") return Any(alpha); if (n == "rotation") return Any(rotation);
    if (n == "scaleX") return Any(scaleX); if (n == "scaleY") return Any(scaleY);
    if (n == "visible") return Any(visible); if (n == "name") return Any(name);
    if (n == "mouseX") return Any(mouseX); if (n == "mouseY") return Any(mouseY);
    if (n == "parent") return Any((Object*)parent); if (n == "stage") return Any((Object*)stage);
    if (n == "root") return Any((Object*)root); if (n == "loaderInfo") return Any((Object*)loaderInfo);
    return Object::__get(n);
}
inline void DisplayObject::__set(const std::string& n, const Any& v) {
    if (n == "x") x = as3::toNumber(v); else if (n == "y") y = as3::toNumber(v);
    else if (n == "width") width = as3::toNumber(v); else if (n == "height") height = as3::toNumber(v);
    else if (n == "alpha") alpha = as3::toNumber(v); else if (n == "rotation") rotation = as3::toNumber(v);
    else if (n == "scaleX") scaleX = as3::toNumber(v); else if (n == "scaleY") scaleY = as3::toNumber(v);
    else if (n == "visible") visible = as3::truthy(v); else if (n == "name") name = as3::toString(v);
    else Object::__set(n, v);
}
struct LoaderInfo : EventDispatcher {
    String url; DisplayObject* content = nullptr; Object* applicationDomain = nullptr; Object* parameters = nullptr; double bytesLoaded = 0, bytesTotal = 0;
    Loader* loader = nullptr;
    Any __get(const std::string& n) override;
    void __trace() override { EventDispatcher::__trace(); as3::gc::mark(content); as3::gc::mark(applicationDomain); as3::gc::mark(parameters); as3::gc::mark((Object*)loader); }
};
struct Loader : DisplayObjectContainer {
    LoaderInfo* contentLoaderInfo = nullptr;
    DisplayObject* content = nullptr;
    void __trace() override { DisplayObjectContainer::__trace(); as3::gc::mark(contentLoaderInfo); as3::gc::mark(content); }
    void __construct() { contentLoaderInfo = new LoaderInfo(); contentLoaderInfo->loader = this; }
    static std::string localPath(const std::string& url) {
        std::string u = url;
        size_t sc = u.find("://");
        if (sc != std::string::npos) {
            size_t k = u.find("neoscavenger/");
            u = k != std::string::npos ? u.substr(k + 13) : u.substr(u.find('/', sc + 3) == std::string::npos ? u.size() : u.find('/', sc + 3) + 1);
        }
        size_t q = u.find('?');
        return q == std::string::npos ? u : u.substr(0, q);
    }
    // Lee la cabecera del PNG ahora (ancho/alto) y deja los pixeles para cuando se necesiten.
    void load(URLRequest* req, Object* = nullptr);
    void loadBytes(Object*) {}
    void unload() { content = nullptr; }
    Any __get(const std::string& n) override {
        if (n == "contentLoaderInfo") return Any((Object*)contentLoaderInfo);
        if (n == "content") return Any((Object*)content);
        return DisplayObjectContainer::__get(n);
    }
};
inline Any LoaderInfo::__get(const std::string& n) {
    if (n == "loader") return Any(loader);
    if (n == "content") return Any(content);
    if (n == "url") return Any(url);
    return Object::__get(n);
}

// ------------------------------------------------------------ constantes de enumeración
struct StageAlign : Object { FLASH_CONST(TOP_LEFT, "TL"); FLASH_CONST(TOP, "T"); FLASH_CONST(LEFT, "L"); };
struct StageScaleMode : Object { FLASH_CONST(NO_SCALE, "noScale"); FLASH_CONST(SHOW_ALL, "showAll"); FLASH_CONST(EXACT_FIT, "exactFit"); FLASH_CONST(NO_BORDER, "noBorder"); };
struct StageDisplayState : Object { FLASH_CONST(NORMAL, "normal"); FLASH_CONST(FULL_SCREEN, "fullScreen"); FLASH_CONST(FULL_SCREEN_INTERACTIVE, "fullScreenInteractive"); };
struct BlendMode : Object { FLASH_CONST(NORMAL, "normal"); FLASH_CONST(ADD, "add"); FLASH_CONST(MULTIPLY, "multiply"); FLASH_CONST(SCREEN, "screen");
    FLASH_CONST(DIFFERENCE, "difference"); FLASH_CONST(SUBTRACT, "subtract"); FLASH_CONST(LAYER, "layer"); FLASH_CONST(ALPHA, "alpha");
    FLASH_CONST(ERASE, "erase"); FLASH_CONST(INVERT, "invert"); FLASH_CONST(OVERLAY, "overlay"); FLASH_CONST(DARKEN, "darken"); FLASH_CONST(LIGHTEN, "lighten"); };
struct AntiAliasType : Object { FLASH_CONST(ADVANCED, "advanced"); FLASH_CONST(NORMAL, "normal"); };
struct GridFitType : Object { FLASH_CONST(PIXEL, "pixel"); FLASH_CONST(NONE, "none"); FLASH_CONST(SUBPIXEL, "subpixel"); };
struct TextFieldAutoSize : Object { FLASH_CONST(NONE, "none"); FLASH_CONST(LEFT, "left"); FLASH_CONST(CENTER, "center"); FLASH_CONST(RIGHT, "right"); };
struct TextFieldType : Object { FLASH_CONST(DYNAMIC, "dynamic"); FLASH_CONST(INPUT, "input"); };
// newlib (Vita) define BIG_ENDIAN/LITTLE_ENDIAN como macros: chocan con las constantes de Flash
#ifdef BIG_ENDIAN
#undef BIG_ENDIAN
#endif
#ifdef LITTLE_ENDIAN
#undef LITTLE_ENDIAN
#endif
struct Endian : Object { FLASH_CONST(BIG_ENDIAN, "bigEndian"); FLASH_CONST(LITTLE_ENDIAN, "littleEndian"); };
struct URLRequestMethod : Object { FLASH_CONST(GET, "GET"); FLASH_CONST(POST, "POST"); };
struct URLLoaderDataFormat : Object { FLASH_CONST(TEXT, "text"); FLASH_CONST(BINARY, "binary"); FLASH_CONST(VARIABLES, "variables"); };
struct ClipboardFormats : Object { FLASH_CONST(TEXT_FORMAT, "air:text"); };
struct SharedObjectFlushStatus : Object { FLASH_CONST(FLUSHED, "flushed"); FLASH_CONST(PENDING, "pending"); };

// ------------------------------------------------------------ texto y fuentes
struct Font : Object { String fontName; };
struct TextFormat : Object {
    String font, align, url, target; double size = 12, color = 0, leading = 0, letterSpacing = 0, leftMargin = 0, rightMargin = 0, indent = 0;
    bool bold = false, italic = false, underline = false;
    void __construct(const Any& f = Any(), const Any& s = Any(), const Any& c = Any(), const Any& b = Any(), const Any& i = Any(), const Any& u = Any(),
                     const Any& url_ = Any(), const Any& tg = Any(), const Any& al = Any(), const Any& lm = Any(), const Any& rm = Any(),
                     const Any& ind = Any(), const Any& ld = Any()) {
        if (!f.isNullish()) font = as3::toString(f);
        if (!s.isNullish()) size = as3::toNumber(s);
        if (!c.isNullish()) color = as3::toNumber(c);
        bold = as3::truthy(b); italic = as3::truthy(i); underline = as3::truthy(u);
        if (!al.isNullish()) align = as3::toString(al);
        if (!lm.isNullish()) leftMargin = as3::toNumber(lm);
        if (!rm.isNullish()) rightMargin = as3::toNumber(rm);
        if (!ind.isNullish()) indent = as3::toNumber(ind);
        if (!ld.isNullish()) leading = as3::toNumber(ld);
    }
};
struct TextLineMetrics : Object { double x = 0, width = 0, height = 0, ascent = 0, descent = 0, leading = 0; };
// Texto con las fuentes reales del juego (DefineFont3 del SWF): mismas medidas que Flash para avance, ascenso y linea.
// Margen de 2 px como en Flash. Sin HTML ni varios formatos por tramo (el juego usa un formato por campo).
struct TextField : InteractiveObject {
    String text, htmlText, autoSize, antiAliasType, gridFitType, type; bool multiline = false, wordWrap = false, embedFonts = false, selectable = true, border = false, background = false;
    double textColor = 0, backgroundColor = 0, borderColor = 0, sharpness = 0, thickness = 0, maxChars = 0;
    TextFormat* defaultTextFormat = nullptr;
    void __trace() override { DisplayObject::__trace(); as3::gc::mark(defaultTextFormat); }
    font::Font* fnt() const { return font::get(defaultTextFormat ? defaultTextFormat->font.str() : std::string("system")); }
    double fontSize() const { return defaultTextFormat && defaultTextFormat->size > 0 ? defaultTextFormat->size : 12; }
    double scale() const { return fontSize() / font::Font::EM; }
    double charAdv(unsigned char c) const {
        font::Font* f = fnt();
        const font::Glyph* g = f ? f->glyph(c) : nullptr;
        double ls = defaultTextFormat ? defaultTextFormat->letterSpacing : 0;
        return (g ? g->advance * scale() : fontSize() * 0.5) + ls;
    }
    double strWidth(const std::string& t) const { double w = 0; for (char c : t) w += charAdv((unsigned char)c); return w; }
    double ascentPx() const { font::Font* f = fnt(); return f ? f->ascent * scale() : fontSize(); }
    double descentPx() const { font::Font* f = fnt(); return f ? f->descent * scale() : fontSize() * 0.25; }
    double lineHeight() const {
        font::Font* f = fnt();
        double extra = defaultTextFormat ? defaultTextFormat->leading : 0;
        return (f ? (f->ascent + f->descent + f->leading) * scale() : fontSize() * 1.25) + extra;
    }
    std::vector<std::string> layoutLines() const {
        std::vector<std::string> out;
        const std::string& t = text.str();
        double avail = width - 4;
        size_t i = 0;
        while (i <= t.size()) {
            size_t e = t.find_first_of("\r\n", i);
            std::string para = t.substr(i, e == std::string::npos ? std::string::npos : e - i);
            if (!wordWrap || width <= 0 || strWidth(para) <= avail) out.push_back(para);
            else {
                std::string cur, w;
                std::stringstream ss(para);
                while (ss >> w) {
                    std::string cand = cur.empty() ? w : cur + " " + w;
                    if (!cur.empty() && strWidth(cand) > avail) { out.push_back(cur); cur = w; }
                    else cur = cand;
                }
                out.push_back(cur);
            }
            if (e == std::string::npos) break;
            i = e + (t[e] == '\r' && e + 1 < t.size() && t[e + 1] == '\n' ? 2 : 1);
        }
        return out;
    }
    double get_numLines() const { return (double)layoutLines().size(); }
    double get_textWidth() const { double w = 0; for (auto& l : layoutLines()) w = std::max(w, strWidth(l)); return w; }
    double get_textHeight() const { return layoutLines().size() * lineHeight(); }
    double get_maxScrollV() const { return 1; }
    double get_length() const { return (double)text.str().size(); }
    void setTextFormat(TextFormat* f, double = -1, double = -1) { if (f) defaultTextFormat = f; }
    TextFormat* getTextFormat(double = -1, double = -1) { return defaultTextFormat; }
    void appendText(const String& s) { text = String(text.str() + s.str()); }
    TextLineMetrics* getLineMetrics(double i) {
        auto lines = layoutLines();
        TextLineMetrics* m = new TextLineMetrics();
        std::string l = (i >= 0 && i < lines.size()) ? lines[(size_t)i] : std::string();
        m->width = strWidth(l); m->ascent = ascentPx(); m->descent = descentPx();
        m->leading = lineHeight() - ascentPx() - descentPx(); m->height = lineHeight(); m->x = 2;
        return m;
    }
    double scrollV = 1;
};

// ---------------------------------------------------------------- BitmapData::draw (por software)
// Matriz afin de Flash: x' = a*x + c*y + tx ; y' = b*x + d*y + ty
struct Affine { double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0; };
inline Affine affineThen(const Affine& A, const Affine& B) {  // primero A, despues B
    return {A.a * B.a + A.b * B.c, A.a * B.b + A.b * B.d, A.c * B.a + A.d * B.c, A.c * B.b + A.d * B.d,
            A.tx * B.a + A.ty * B.c + B.tx, A.tx * B.b + A.ty * B.d + B.ty};
}
inline Affine localAffine(const DisplayObject* o) {
    double r = o->rotation * 3.14159265358979323846 / 180.0, cs = std::cos(r), sn = std::sin(r);
    return {o->scaleX * cs, o->scaleX * sn, -o->scaleY * sn, o->scaleY * cs, o->x, o->y};
}
struct DrawCtx { BitmapData* dst; Rectangle* clip; bool smoothing; };
inline bool inClip(const DrawCtx& k, int x, int y) {
    return !k.clip || (x >= k.clip->x && y >= k.clip->y && x < k.clip->x + k.clip->width && y < k.clip->y + k.clip->height);
}
inline void drawBitmapAff(const DrawCtx& k, BitmapData* src, const Affine& M, const ColorTransform* ct) {
    if (!src) return;
    src->ensure();
    BitmapData* dst = k.dst;
    double det = M.a * M.d - M.b * M.c;
    if (std::fabs(det) < 1e-12) return;
    // caja destino
    double xs[4] = {M.tx, M.a * src->width + M.tx, M.c * src->height + M.tx, M.a * src->width + M.c * src->height + M.tx};
    double ys[4] = {M.ty, M.b * src->width + M.ty, M.d * src->height + M.ty, M.b * src->width + M.d * src->height + M.ty};
    int x0 = std::max(0, (int)std::floor(*std::min_element(xs, xs + 4))), x1 = std::min(dst->width, (int)std::ceil(*std::max_element(xs, xs + 4)));
    int y0 = std::max(0, (int)std::floor(*std::min_element(ys, ys + 4))), y1 = std::min(dst->height, (int)std::ceil(*std::max_element(ys, ys + 4)));
    // inversa
    double ia = M.d / det, ib = -M.b / det, ic = -M.c / det, id = M.a / det;
    bool ctId = !ct || (ct->alphaMultiplier == 1 && ct->redMultiplier == 1 && ct->greenMultiplier == 1 && ct->blueMultiplier == 1 &&
                        ct->alphaOffset == 0 && ct->redOffset == 0 && ct->greenOffset == 0 && ct->blueOffset == 0);
    if (M.b == 0 && M.c == 0) {
        // camino rapido: escala/traslacion sin rotacion (camara de Flixel, sprites escalados)
        if (k.clip) {
            x0 = std::max(x0, (int)std::ceil(k.clip->x - 0.5)); x1 = std::min(x1, (int)std::ceil(k.clip->x + k.clip->width - 0.5));
            y0 = std::max(y0, (int)std::ceil(k.clip->y - 0.5)); y1 = std::min(y1, (int)std::ceil(k.clip->y + k.clip->height - 0.5));
        }
        if (x0 >= x1 || y0 >= y1) return;
        static thread_local std::vector<int> cols;
        cols.resize(x1 - x0);
        for (int x = x0; x < x1; x++) cols[x - x0] = (int)std::floor(ia * (x + 0.5 - M.tx));
        const uint32_t opaq = src->transparent ? 0u : 0xFF000000u;
        const uint32_t dfix = dst->transparent ? 0u : 0xFF000000u;
        for (int y = y0; y < y1; y++) {
            int sy = (int)std::floor(id * (y + 0.5 - M.ty));
            if (sy < 0 || sy >= src->height) continue;
            const uint32_t* srow = &src->px[(size_t)sy * src->width];
            uint32_t* drow = &dst->px[(size_t)y * dst->width];
            for (int x = x0; x < x1; x++) {
                int sx = cols[x - x0];
                if ((unsigned)sx >= (unsigned)src->width) continue;
                uint32_t c = srow[sx] | opaq;
                if (!ctId) c = BitmapData::applyCT(c, ct);
                uint32_t& dd = drow[x];
                dd = BitmapData::over(dd, c) | dfix;
            }
        }
        return;
    }
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        if (!inClip(k, x, y)) continue;
        double px_ = x + 0.5 - M.tx, py_ = y + 0.5 - M.ty;
        int sx = (int)std::floor(ia * px_ + ic * py_), sy = (int)std::floor(ib * px_ + id * py_);
        if (sx < 0 || sy < 0 || sx >= src->width || sy >= src->height) continue;
        uint32_t c = src->px[(size_t)sy * src->width + sx];
        if (!src->transparent) c |= 0xFF000000u;
        dst->plot(x, y, BitmapData::applyCT(c, ct), true);
    }
}
inline void drawText(const DrawCtx& k, TextField* tf, const Affine& M, const ColorTransform* ct) {
    font::Font* f = tf->fnt();
    if (!f || tf->text.str().empty()) return;
    double sz = tf->fontSize() * std::sqrt(std::fabs(M.a * M.d - M.b * M.c));
    double s1 = std::sqrt(std::fabs(M.a * M.d - M.b * M.c));
    uint32_t col = (uint32_t)(tf->defaultTextFormat ? tf->defaultTextFormat->color : tf->textColor) & 0xFFFFFF;
    std::string align = tf->defaultTextFormat ? tf->defaultTextFormat->align.str() : "";
    auto lines = tf->layoutLines();
    double lh = tf->lineHeight(), asc = tf->ascentPx();
    for (size_t li = 0; li < lines.size(); li++) {
        double lw = tf->strWidth(lines[li]);
        double x = 2;
        if (align == "center") x = std::max(2.0, (tf->width - lw) / 2);
        else if (align == "right") x = std::max(2.0, tf->width - 2 - lw);
        double base = 2 + asc + li * lh;
        for (char ch : lines[li]) {
            const font::Bitmap& g = f->raster((unsigned char)ch, sz);
            // origen del glifo en el destino
            double ox = M.a * x + M.c * base + M.tx, oy = M.b * x + M.d * base + M.ty;
            int gx = (int)std::lround(ox) + g.left, gy = (int)std::lround(oy) + g.top;
            for (int j = 0; j < g.h; j++) for (int i = 0; i < g.w; i++) {
                uint32_t a = g.alpha[(size_t)j * g.w + i];
                if (!a || !inClip(k, gx + i, gy + j)) continue;
                k.dst->plot(gx + i, gy + j, BitmapData::applyCT(a << 24 | col, ct), true);
            }
            x += tf->charAdv((unsigned char)ch);
            (void)s1;
        }
    }
}
inline void drawGraphics(const DrawCtx& k, Graphics* g, const Affine& M, const ColorTransform* ct) {
    if (!g) return;
    for (auto& c : g->cmds) {
        double x0 = c.kind ? c.x - c.w : c.x, y0 = c.kind ? c.y - c.h : c.y, w = c.kind ? c.w * 2 : c.w, h = c.kind ? c.h * 2 : c.h;
        int X0 = (int)std::floor(M.a * x0 + M.tx), Y0 = (int)std::floor(M.d * y0 + M.ty);
        int X1 = (int)std::ceil(M.a * (x0 + w) + M.tx), Y1 = (int)std::ceil(M.d * (y0 + h) + M.ty);
        for (int y = std::max(0, Y0); y < std::min(k.dst->height, Y1); y++) for (int x = std::max(0, X0); x < std::min(k.dst->width, X1); x++) {
            if (!inClip(k, x, y)) continue;
            if (c.kind == 1) { double ux = (x + 0.5 - M.tx) / M.a - c.x, uy = (y + 0.5 - M.ty) / M.d - c.y; if (ux * ux + uy * uy > c.w * c.w) continue; }
            k.dst->plot(x, y, BitmapData::applyCT(c.argb, ct), true);
        }
    }
}
inline ColorTransform* mulCT(const ColorTransform* a, double alpha) {
    ColorTransform* r = new ColorTransform();
    if (a) *r = *a;
    r->alphaMultiplier *= alpha;
    return r;
}
inline void drawObject(const DrawCtx& k, Object* o, const Affine& M, const ColorTransform* ct, bool top) {
    if (!o) return;
    if (BitmapData* b = dynamic_cast<BitmapData*>(o)) { drawBitmapAff(k, b, M, ct); return; }
    DisplayObject* d = dynamic_cast<DisplayObject*>(o);
    if (!d) return;
    if (!top && !d->visible) return;
    if (Bitmap* bm = dynamic_cast<Bitmap*>(o)) { drawBitmapAff(k, bm->bitmapData, M, ct); return; }
    if (TextField* tf = dynamic_cast<TextField*>(o)) { drawText(k, tf, M, ct); return; }
    if (Sprite* sp = dynamic_cast<Sprite*>(o)) drawGraphics(k, sp->graphics, M, ct);
    if (DisplayObjectContainer* c = dynamic_cast<DisplayObjectContainer*>(o)) {
        for (DisplayObject* ch : c->__children) {
            if (!ch || !ch->visible) continue;
            ColorTransform* cct = ch->alpha < 1 ? mulCT(ct, ch->alpha) : const_cast<ColorTransform*>(ct);
            drawObject(k, ch, affineThen(localAffine(ch), M), cct, false);
        }
    }
}
inline void BitmapData::draw(Object* source, Matrix* m, ColorTransform* ct, const String& blend, Rectangle* clip, bool smoothing) {
    if (deferred && !replaying && !clip && blend.str().empty()) {
        BitmapData* sb = dynamic_cast<BitmapData*>(source);
        if (!sb) if (Bitmap* bm = dynamic_cast<Bitmap*>(source)) sb = bm->bitmapData;
        if (sb && sb != this) {
            if (sb->deferred && !sb->cmds.empty()) sb->flushDeferred();
            Cmd k; k.kind = Cmd::Draw; k.merge = true; k.src = sb; k.w = sb->width; k.h = sb->height; k.smoothing = smoothing;
            if (m) { k.ma = m->a; k.mb = m->b; k.mc = m->c; k.md = m->d; k.mtx = m->tx; k.mty = m->ty; }
            if (ct) { k.hasCt = true; double v[8] = {ct->redMultiplier, ct->greenMultiplier, ct->blueMultiplier, ct->alphaMultiplier,
                                                    ct->redOffset, ct->greenOffset, ct->blueOffset, ct->alphaOffset};
                      for (int i = 0; i < 8; i++) k.ct[i] = v[i]; }
            sb->deferRefs++;
            cmds.push_back(k); version++; uniform = false;
            return;
        }
        if (!cmds.empty()) flushDeferred();
    }
    uniform = false; touched();
    ensure();
    Affine M;
    if (m) M = {m->a, m->b, m->c, m->d, m->tx, m->ty};
    DrawCtx k{this, clip, smoothing};
    drawObject(k, source, M, ct, true);
}

// ------------------------------------------------------------ sonido
struct SoundTransform : Object { double volume = 1, pan = 0; void __construct(double v = 1, double p = 0) { volume = v; pan = p; } };
struct SoundChannel : EventDispatcher {
    double position = 0, leftPeak = 0, rightPeak = 0; SoundTransform* soundTransform = nullptr;
    std::shared_ptr<audio::Voice> voice;   // voz del mezclador (vacia si no se pudo reproducir)
    double startMs = 0; bool completeSent = false;
    void __trace() override { EventDispatcher::__trace(); as3::gc::mark((Object*)soundTransform); }
    void stop() { if (voice) voice->stop = true; }
};
// Canales que estan sonando: el juego no tiene por que guardar el canal (Flash lo mantiene vivo mientras suena)
inline std::vector<SoundChannel*>& activeChannels() { static std::vector<SoundChannel*> v; return v; }
struct Sound : EventDispatcher {
    void __trace() override;
    double length = 0; double bytesLoaded = 0; Object* id3 = new Object();
    std::string mp3Path;      // MP3 incrustado (assets/<id>.mp3)
    bool lengthKnown = false;
    template <class... A> void __construct(A...) {}
    void __loadEmbed(const char* src) override { mp3Path = platform::embedPath(src); }
    double get_length() {
        if (!lengthKnown && !mp3Path.empty()) { lengthKnown = true; length = audio::lengthMs(audio::load(mp3Path, platform::readFile)); }
        return length;
    }
    SoundChannel* play(double startMs = 0, int32_t loops = 0, SoundTransform* st = nullptr) {
        SoundChannel* ch = new SoundChannel();
        ch->soundTransform = st;
        ch->startMs = startMs;
        audio::Bytes b = mp3Path.empty() ? nullptr : audio::load(mp3Path, platform::readFile);
        if (getenv("NEOS_AUDIOLOG")) platform::log("[audio]", "play %s desde %.0f ms, bucles %d: %s", mp3Path.c_str(), startMs, (int)loops, b ? "ok" : "NO ENCONTRADO");
        if (!b) { ch->completeSent = true; return ch; }
        if (!lengthKnown) { lengthKnown = true; length = audio::lengthMs(b); }
        auto v = std::make_shared<audio::Voice>();
        v->mp3 = b; v->skipMs = startMs; v->loops = loops;
        if (st) { v->vol = (float)st->volume; v->pan = (float)st->pan; }
        ch->voice = v;
        audio::mixer().add(v);
        activeChannels().push_back(ch);
        return ch;
    }
    void close() {}
    void load(Object*, Object* = nullptr) {}
};
struct SoundMixer : Object {
    static void stopAll() { for (auto* c : activeChannels()) c->stop(); }
    static inline SoundTransform* soundTransform = nullptr;
};
// Cada fotograma (hilo del juego): volumen/panoramica de cada canal desde su soundTransform, posicion, y SOUND_COMPLETE
// cuando el mezclador termina una voz. Coste: unas decenas de canales como mucho.
inline void audioUpdate(int outHz) {
    auto& v = activeChannels();
    if (SoundMixer::soundTransform) audio::mixer().master = (float)SoundMixer::soundTransform->volume;
    for (size_t i = 0; i < v.size();) {
        SoundChannel* c = v[i];
        if (c->voice) {
            if (c->soundTransform) { c->voice->vol = (float)c->soundTransform->volume; c->voice->pan = (float)c->soundTransform->pan; }
            c->position = c->startMs + 1000.0 * (double)c->voice->playedFrames.load() / outHz;
        }
        if (!c->voice || c->voice->done) {
            v.erase(v.begin() + i);
            if (c->voice && !c->voice->stop && !c->completeSent) {
                c->completeSent = true;
                Event* e = new Event(); e->type = Event::SOUND_COMPLETE;
                c->dispatchEvent(e);
            }
            continue;
        }
        i++;
    }
}
inline bool __audioGcHook = (as3::gc::rootFns().push_back([] { for (auto* c : activeChannels()) as3::gc::mark((as3::Object*)c); }), true);

// ------------------------------------------------------------ red y sistema
struct URLVariables : Object {
    template <class... A> void __construct(A...) {}
    static std::string urlDecode(const std::string& s) {
        std::string r;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '+') r += ' ';
            else if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) { r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else r += s[i];
        }
        return r;
    }
    void decode(const std::string& text) {  // "a=b&c=d" -> propiedades (todas texto, como en AS3)
        size_t i = 0;
        while (i <= text.size()) {
            size_t amp = text.find('&', i);
            std::string part = text.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
            while (!part.empty() && (part.back() == '\r' || part.back() == '\n')) part.pop_back();
            while (!part.empty() && (part[0] == '\r' || part[0] == '\n')) part.erase(part.begin());
            size_t eq = part.find('=');
            if (eq != std::string::npos) __set(urlDecode(part.substr(0, eq)), Any(String(urlDecode(part.substr(eq + 1)))));
            if (amp == std::string::npos) break;
            i = amp + 1;
        }
    }
};
struct URLRequest : Object { String url, method, contentType; Any data; void __trace() override { Object::__trace(); as3::gc::mark(data); } void __construct(const String& u = String()) { url = u; } };
inline void Loader::load(URLRequest* req, Object*) {
    std::string path = localPath(req->url.str());
    Loader* self = this;
    platform::log("[imagenes]", "cargar %s", path.c_str());
    post([self, path]() {
        std::string bytes; int w = 0, h = 0;
        if (!platform::imageSize(path, w, h) && (!platform::readFile(path, bytes) || !png::header(bytes, w, h))) {
            IOErrorEvent* e = new IOErrorEvent();
            e->type = IOErrorEvent::IO_ERROR; e->errorID = 2035; e->text = String("Error #2035: URL no encontrada: " + path);
            self->contentLoaderInfo->dispatchEvent(e);
            return;
        }
        BitmapData* bd = new BitmapData();
        bd->width = w; bd->height = h; bd->transparent = true; bd->sourcePath = path;
        Bitmap* bm = new Bitmap();
        bm->bitmapData = bd; bm->width = w; bm->height = h;
        self->content = bm;
        self->contentLoaderInfo->content = bm;
        self->contentLoaderInfo->url = String(path);
        self->contentLoaderInfo->bytesLoaded = self->contentLoaderInfo->bytesTotal = (double)bytes.size();
        Event* ev = new Event(); ev->type = Event::COMPLETE;
        self->contentLoaderInfo->dispatchEvent(ev);
    });
}
struct URLLoader : EventDispatcher {
    String dataFormat = String("text");
    Any data;
    void __trace() override { EventDispatcher::__trace(); as3::gc::mark(data); }
    double bytesLoaded = 0, bytesTotal = 0;
    void __construct() {}
    void __construct(URLRequest* req) { if (req) load(req); }  // en AS3, new URLLoader(request) empieza a cargar
    // URL -> ruta local: se quita el esquema/servidor y la ruta del servidor original del juego
    static std::string localPath(const std::string& url) {
        std::string u = url;
        size_t sc = u.find("://");
        if (sc != std::string::npos) {
            size_t k = u.find("neoscavenger/");
            u = k != std::string::npos ? u.substr(k + 13) : u.substr(u.find('/', sc + 3) == std::string::npos ? u.size() : u.find('/', sc + 3) + 1);
        }
        size_t q = u.find('?');
        return q == std::string::npos ? u : u.substr(0, q);
    }
    void load(URLRequest* req) {
        std::string path = localPath(req->url.str());
        platform::log("[datos]", "leer %s", path.c_str());
        URLLoader* self = this;
        post([self, path]() {
            std::string bytes;
            if (!platform::readFile(path, bytes)) {
                platform::log("[datos]", "no se puede leer %s", path.c_str());
                IOErrorEvent* e = new IOErrorEvent();
                e->type = IOErrorEvent::IO_ERROR; e->errorID = 2032; e->text = String("Error #2032: error de secuencia: " + path);
                self->data = Any(e->text);
                self->dispatchEvent(e);
                return;
            }
            self->bytesLoaded = self->bytesTotal = (double)bytes.size();
            if (self->dataFormat.str() == "variables") { URLVariables* v = new URLVariables(); v->decode(bytes); self->data = Any(v); }
            else if (self->dataFormat.str() == "binary") { as3::ByteArray* b = new as3::ByteArray(); b->data.assign(bytes.begin(), bytes.end()); self->data = Any(b); }
            else self->data = Any(String(bytes));
            Event* ev = new Event(); ev->type = Event::COMPLETE;
            self->dispatchEvent(ev);
        });
    }
    void close() {}
    Any __get(const std::string& n) override {
        if (n == "data") return data;
        if (n == "dataFormat") return Any(dataFormat);
        if (n == "bytesLoaded") return Any(bytesLoaded);
        if (n == "bytesTotal") return Any(bytesTotal);
        return Object::__get(n);
    }
};
struct SecurityError : as3::Error {};
struct TypeError : as3::Error {};
struct FileFilter : Object { template <class... A> void __construct(A...) {} };
struct FileReference : EventDispatcher { String name; Object* data = nullptr; void __trace() override { EventDispatcher::__trace(); as3::gc::mark(data); } double size = 0; template <class... A> void __construct(A...) {} void save(const Any&, const String& = String()) {} void browse(Object* = nullptr) {} void load() {} };
struct SharedObject : EventDispatcher {
    SharedObject() { __gcPinned = true; }  // viven en el registro estatico de getLocal
    void __trace() override { EventDispatcher::__trace(); as3::gc::mark(data); }
    as3::Object* data = new as3::Object();
    // Un objeto por nombre, como en Flash. onCreate deja al port poner valores iniciales (p. ej. opciones de pantalla de la Vita).
    static inline std::function<void(const std::string&, SharedObject*)> onCreate;
    std::string name;
    // Se guarda en <datos>/save/<nombre>.sol (formato de persist.h). Si existe, se carga al pedirlo por primera vez.
    static std::string pathOf(const std::string& n) { return platform::saveDir() + n + ".sol"; }
    static SharedObject* getLocal(const String& n, const String& = String(), bool = false) {
        static std::map<std::string, SharedObject*> reg;
        SharedObject*& so = reg[n.str()];
        if (!so) {
            so = new SharedObject();
            so->name = n.str();
            std::string bytes;
            as3::Object* loaded = nullptr;
            if (platform::readFile(pathOf(so->name), bytes)) loaded = persist::load(bytes);
            if (loaded) { so->data = loaded; platform::log("[guardado]", "cargado %s (%u bytes)", so->name.c_str(), (unsigned)bytes.size()); }
            else if (onCreate) onCreate(n.str(), so);  // valores iniciales del port solo si no hay nada guardado
        }
        return so;
    }
    String flush(double = 0) {
        std::string bytes = persist::save(data);
        if (!platform::writeFileAtomic(pathOf(name), bytes)) {
            platform::log("[guardado]", "ERROR al guardar %s", name.c_str());
            return String("failed");
        }
        platform::log("[guardado]", "guardado %s (%u bytes)", name.c_str(), (unsigned)bytes.size());
        return SharedObjectFlushStatus::FLUSHED;
    }
    void clear() { data = new as3::Object(); std::remove(pathOf(name).c_str()); }
};
struct System : Object { static void disposeXML(const Any&) {} static void setClipboard(const String& s) { platform::log("[juego]", "%s", s.str().c_str()); } static inline double totalMemory = 0; static void gc() {} static void exit(double) {} };
struct Capabilities : Object { static inline String os = String("Windows"); static inline String version = String("WIN 11,0,0,0"); static inline String playerType = String("StandAlone"); };
struct Security : Object { static void loadPolicyFile(const String&) {} static void allowDomain(const String&) {} static inline String sandboxType = String("localTrusted"); };
struct ApplicationDomain : Object { static ApplicationDomain* currentDomain; bool hasDefinition(const String&) { return false; } Any getDefinition(const String&) { as3::notImplemented("ApplicationDomain.getDefinition"); } };
inline ApplicationDomain* ApplicationDomain::currentDomain = new ApplicationDomain();
struct Clipboard : Object { static Clipboard* generalClipboard; void clear() {} bool setData(const String&, const Any&, bool = true) { return true; } Any getData(const String&) { return Any(); } };
inline Clipboard* Clipboard::generalClipboard = new Clipboard();
struct MouseCursorData : Object { Object* data = nullptr; Point* hotSpot = nullptr; void __trace() override { Object::__trace(); as3::gc::mark(data); as3::gc::mark((Object*)hotSpot); } double frameRate = 0; };
struct Mouse : Object {
    static inline bool supportsNativeCursor = false;
    static inline String cursor = String("auto");
    static void show() {}
    static void hide() {}
    static void registerCursor(const String&, MouseCursorData*) {}
    static void unregisterCursor(const String&) {}
};

// ------------------------------------------------------------ assets embebidos de Flex (mx.core): se reemplazan, no se traducen
struct BitmapAsset : Bitmap { template <class... A> void __construct(A...) {} };
struct FlexBitmap : BitmapAsset {};
struct ByteArrayAsset : as3::ByteArray { template <class... A> void __construct(A...) {} };
struct SoundAsset : Sound {};
struct FontAsset : Font {};

// Dibuja el escenario completo en target (tamano de la ventana), aplicando el origen de align
inline void renderStage(Stage* st, BitmapData* target) {
    Matrix* m = new Matrix(); m->tx = st->originX(); m->ty = st->originY();
    target->draw(st, m);
}
// Asignacion a un campo BitmapData con dueno unico: libera el bitmap anterior (ver OWNED_BITMAP_FIELDS en as3cpp.py)
template <class V> inline void ownedSet(BitmapData*& slot, V&& v) {
    BitmapData* nv = v;
    if (slot && slot != nv && !slot->sharedNoDispose) slot->dispose();  // compartido: lo libera el recolector
    slot = nv;
}
// framePixels de FlxSprite (parche de tools/as3cpp: patch_frame_pixels). Fotograma = imagen entera y sin color:
// se comparte _pixels. Si no, bufer propio del tamano del fotograma (nunca el compartido: se escribiria en la imagen).
template <class W, class H, class FW, class FH>
inline void framePixelsSync(BitmapData*& fp, BitmapData* px, W w, H h, FW fw, FH fh, ColorTransform* ct) {
    int iw = (int)w, ih = (int)h;
    bool full = px && !ct && (int)fw == px->width && (int)fh == px->height && iw == px->width && ih == px->height && px->width > 0;
    if (full) {
        if (fp != px) { if (fp && !fp->sharedNoDispose) fp->dispose(); fp = px; }
        px->sharedNoDispose = true;
        return;
    }
    if (!fp || fp == px || fp->sharedNoDispose || fp->width != iw || fp->height != ih) {
        if (fp && fp != px && !fp->sharedNoDispose) fp->dispose();
        fp = new BitmapData(); fp->__construct(iw, ih);
    }
}
inline void Sound::__trace() { EventDispatcher::__trace(); as3::gc::mark(id3); }
// Recolector entre fotogramas: solo si no hay eventos asincronos pendientes (sus cierres guardan objetos que no se ven)
// Solo recolecta si desde la ultima pasada se crearon bastantes objetos o bastantes pixeles (la pasada cuesta
// segun los objetos VIVOS, asi que espaciarla es lo que evita tirones). everyN = separacion minima en fotogramas.
inline std::function<bool()>& gcPressure() { static std::function<bool()> f; return f; }
inline size_t& gcMinNewObjects() { static size_t v = 20000; return v; }
inline size_t& gcMinNewPixelBytes() { static size_t v = 24u << 20; return v; }
inline bool gcAfterFrame(int everyN) {
    static int n = 0;
    static size_t pixAtLast = 0;
    if (!as3::gc::enabled() || ++n < everyN || !pendingQueue().empty()) return false;
    size_t newObjs = as3::gc::heap().size() - std::min(as3::gc::heap().size(), as3::gc::stats().lastLive);
    size_t newPix = bmpStats().bytes - std::min(bmpStats().bytes, pixAtLast);
    // La comprobacion de presion puede ser cara (mallinfo recorre todo el heap): solo cada 30 fotogramas
    static int sinceCheck = 0; static bool presion = false;
    if (++sinceCheck >= 30) { sinceCheck = 0; presion = gcPressure() && gcPressure()(); }
    if (!presion && newObjs < gcMinNewObjects() && newPix < gcMinNewPixelBytes()) return false;
    pixAtLast = bmpStats().bytes;
    n = 0; presion = false;
    double t0 = platform::nowMs();
    as3::gc::collect();
    as3::gc::stats().lastMs = platform::nowMs() - t0;
    return true;
}
// La lista de diagnostico de bitmaps no es duena: se purga antes de borrar
inline std::vector<BitmapData*>& deferredTargets();
inline bool __bmpGcHook = (as3::gc::preSweep().push_back([] {
    { auto& t = deferredTargets(); size_t j = 0; for (auto* b : t) if (b->__gcMark == as3::gc::epoch()) t[j++] = b; t.resize(j); }
    auto& v = bmpStats().all; size_t k = 0;
    for (auto* b : v) if (b->__gcMark == as3::gc::epoch()) v[k++] = b;
    v.resize(k);
}), true);
// ---------------------------------------------------------------- modo diferido (ordenes de dibujo)
struct DeferStats { size_t flushes = 0, flushedCmds = 0, srcFlushes = 0, snapshots = 0; };
inline DeferStats& deferStats() { static DeferStats d; return d; }
constexpr size_t kSnapshotMaxBytes = 4 * 1024 * 1024;  // copia al escribir solo para bitmaps de hasta 4 MB
// Suelta los pixeles de imagenes del paquete que no se han modificado desde que se descomprimieron (las mas antiguas
// primero) hasta liberar `want` bytes. Vuelven a leerse del paquete (LZ4, ~1 ms) si la CPU las necesita; la GPU sigue
// dibujando su textura ya subida (la cache de texturas solo pide pixeles si cambia la version).
// El juego guarda TODAS sus imagenes en DataHandler.m_dictIMG: sin esto, cada imagen usada una vez ocupa memoria para
// siempre (medido en la Vita 25-11: 131 imagenes de objetos de 242x282 = 34 MB, y crece con cada accion).
struct EvictStats { size_t passes = 0, images = 0, bytes = 0; };
inline EvictStats& evictStats() { static EvictStats s; return s; }
inline size_t evictCleanPakImages(size_t want) {
    size_t freed = 0;
    for (BitmapData* b : bmpStats().all) {
        if (freed >= want) break;
        if (b->px.empty() || b->pakPath.empty() || b->version != b->decodedVersion || b->deferred || b->replaying ||
            !b->cmds.empty() || b->deferRefs || b->uniformPending) continue;
        freed += b->px.size() * 4;
        std::vector<uint32_t, pixmem::Alloc<uint32_t>>().swap(b->px);
        b->sourcePath = b->pakPath;
        evictStats().images++;
    }
    evictStats().passes++; evictStats().bytes += freed;
    return freed;
}
inline std::vector<BitmapData*>& deferredTargets() { static std::vector<BitmapData*> v; return v; }
inline void BitmapData::dropDeferred() {
    for (auto& k : cmds) if (k.src && k.src->deferRefs) k.src->deferRefs--;
    cmds.clear();
    cmdsFromClear = false;
}
inline void BitmapData::flushDeferred() {
    if (cmds.empty() || replaying) return;
    std::vector<Cmd> list; list.swap(cmds);
    for (auto& k : list) if (k.src && k.src->deferRefs) k.src->deferRefs--;
    cmdsFromClear = false;
    deferStats().flushes++; deferStats().flushedCmds += list.size();
    replaying = true;
    ensure();
    for (auto& k : list) {
        if (k.kind == Cmd::Rect) {
            if (!k.merge) { Rectangle r; r.x = k.sx; r.y = k.sy; r.width = k.w; r.height = k.h; uniform = false; fillRect(&r, (double)k.color); }
            else { uniform = false; for (int y = k.sy; y < k.sy + k.h; y++) { uint32_t* d = px.data() + (size_t)y * width + k.sx; for (int x = 0; x < k.w; x++) d[x] = over(d[x], k.color) | (transparent ? 0u : 0xFF000000u); } }
        } else if (k.kind == Cmd::Blit) {
            Rectangle r; r.x = k.sx; r.y = k.sy; r.width = k.w; r.height = k.h;
            Point pt; pt.x = k.dx; pt.y = k.dy;
            copyPixels(k.src, &r, &pt, nullptr, nullptr, k.merge);
        } else {
            Matrix mx; mx.a = k.ma; mx.b = k.mb; mx.c = k.mc; mx.d = k.md; mx.tx = k.mtx; mx.ty = k.mty;
            ColorTransform cto; if (k.hasCt) { cto.redMultiplier = k.ct[0]; cto.greenMultiplier = k.ct[1]; cto.blueMultiplier = k.ct[2]; cto.alphaMultiplier = k.ct[3];
                                               cto.redOffset = k.ct[4]; cto.greenOffset = k.ct[5]; cto.blueOffset = k.ct[6]; cto.alphaOffset = k.ct[7]; }
            draw(k.src, &mx, k.hasCt ? &cto : nullptr, String(), nullptr, k.smoothing);
        }
    }
    replaying = false;
    version++;
}
inline void BitmapData::touched() {
    version++;
    if (deferRefs) {  // una orden pendiente lee este bitmap: Flash copia en el momento, asi que las ordenes deben ver
        deferStats().srcFlushes++;  // el contenido de ANTES de esta escritura
        size_t bytes = (size_t)width * (size_t)height * 4;
        {
            const char* who = "?";
#ifdef AS3_TRACE
            auto& st = as3::traceStack(); std::string w; for (size_t i = st.size(), n = 0; i > 0 && n < 3; i--, n++) { if (n) w += " < "; w += st[i - 1]; }
            static std::string keep; keep = w; who = keep.c_str();
#endif
            if (bytes > 1024 * 1024) platform::logf("[diferido] origen pendiente %dx%d (%.1f MB) modificado por %s\n", width, height, bytes / 1048576.0, who);
        }
        // Caso raro (es a su vez un bufer diferido) o bitmap muy grande (la copia podria agotar la memoria): aplanar
        // Sin margen en la zona de pixeles (medido: 69,9 de 72 MB al mover un hexagono -> bad_alloc): tampoco se copia
        auto& pa = pixmem::arena();
        bool tight = pa.size && pa.used + bytes + (8u << 20) > pa.size;
        if (tight && !deferred && bytes <= kSnapshotMaxBytes) {  // hacer sitio soltando imagenes del paquete sin modificar
            evictCleanPakImages(bytes + (16u << 20));
            tight = pa.used + bytes + (8u << 20) > pa.size;
        }
        if (deferred || replaying || px.empty() || bytes > kSnapshotMaxBytes || tight) {
            for (BitmapData* t : deferredTargets()) if (t->deferred && !t->cmds.empty()) t->flushDeferred();
            return;
        }
        // Copia al escribir: una instantanea del bitmap pasa a ser el origen de las ordenes pendientes. Antes se
        // reproducian por software las camaras enteras (miles de ordenes): fotogramas de 5 a 13 s en la Vita.
        version--;  // la instantanea conserva la version vieja (la textura ya subida sigue valiendo para ella)
        BitmapData* snap = clone();
        snap->version = version;
        version++;
        uint32_t moved = 0;
        for (BitmapData* t : deferredTargets())
            for (auto& k : t->cmds) if (k.src == this) { k.src = snap; moved++; }
        snap->deferRefs = moved;
        deferRefs = 0;
        deferStats().snapshots++;
    }
}
// Activa el modo diferido en los buferes dados (las camaras de Flixel). Se llama cada fotograma: una camara puede
// cambiar de bufer (SetSize) o desaparecer. Los buferes que dejan de estar en la lista vuelven al modo normal.
inline void setDeferredSet(const std::vector<BitmapData*>& want) {
    auto& v = deferredTargets();
    for (size_t i = 0; i < v.size();) {
        bool keep = std::find(want.begin(), want.end(), v[i]) != want.end();
        if (!keep) { if (!v[i]->cmds.empty()) v[i]->flushDeferred(); v[i]->deferred = false; v.erase(v.begin() + i); } else i++;
    }
    for (BitmapData* b : want) if (b && !b->deferred) { b->ensure(); b->deferred = true; b->uniform = false; v.push_back(b); }
}
inline void setDeferred(BitmapData* b) { setDeferredSet(b ? std::vector<BitmapData*>{b} : std::vector<BitmapData*>{}); }
}  // namespace flash

namespace mx { namespace core { } }
