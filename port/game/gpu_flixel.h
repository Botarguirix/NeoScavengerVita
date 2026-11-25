// gpu_flixel.h — ejecuta con vitaGL las ordenes de dibujo que el runtime anota en el bufer de la camara de Flixel
// (modo diferido de BitmapData). Cada BitmapData de origen se sube una vez como textura BGRA y solo se vuelve a
// subir si cambia su version. Asi los blits de Flixel los hace la GPU en lugar de la CPU.
#pragma once
#include <vitaGL.h>
#include <cstring>
#include <unordered_map>
#include "all.h"

namespace gpu {

struct Tex { GLuint id = 0; uint32_t version = 0; int w = 0, h = 0; uint32_t* mem = nullptr; uint32_t lastFrame = 0; };
struct Stats { size_t uploads = 0, uploadBytes = 0, textures = 0, texBytes = 0, cmds = 0, quads = 0, batches = 0; };
inline std::unordered_map<flash::BitmapData*, Tex>& cache() { static std::unordered_map<flash::BitmapData*, Tex> c; return c; }
inline Stats& stats() { static Stats s; return s; }
inline uint32_t& frameNo() { static uint32_t f = 0; return f; }

inline void freeTex(Tex& t) {
    if (t.id) { glDeleteTextures(1, &t.id); stats().texBytes -= (size_t)t.w * t.h * 4; stats().textures--; }
    t.id = 0;
}

// El recolector borra bitmaps: sus texturas se liberan antes del barrido
inline bool installGcHook() {
    as3::gc::preSweep().push_back([] {
        auto& c = cache();
        for (auto it = c.begin(); it != c.end();) {
            if (it->first->__gcMark != as3::gc::epoch()) { freeTex(it->second); it = c.erase(it); } else ++it;
        }
    });
    return true;
}

inline Tex* texFor(flash::BitmapData* b) {
    if (!b || b->width <= 0 || b->height <= 0) return nullptr;
    Tex& t = cache()[b];
    if (t.id && (t.w != b->width || t.h != b->height)) freeTex(t);
    if (!t.id) {
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, b->width, b->height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        t.mem = (uint32_t*)vglGetTexDataPointer(GL_TEXTURE_2D);
        t.w = b->width; t.h = b->height; t.version = 0;
        stats().textures++; stats().texBytes += (size_t)t.w * t.h * 4;
    } else glBindTexture(GL_TEXTURE_2D, t.id);
    if (t.version != b->version) {
        b->ensure();
        if (b->px.size() >= (size_t)t.w * t.h) {
            if (t.mem) {
                // vitaGL alinea el paso de fila de las texturas a 8 pixeles: se copia fila a fila
                const int stride = (t.w + 7) & ~7;
                const uint32_t* s = b->px.data();
                for (int y = 0; y < t.h; y++) {
                    uint32_t* d = t.mem + (size_t)y * stride;
                    const uint32_t* r = s + (size_t)y * t.w;
                    if (b->transparent) std::memcpy(d, r, (size_t)t.w * 4);
                    else for (int x = 0; x < t.w; x++) d[x] = r[x] | 0xFF000000u;
                }
            } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.w, t.h, GL_BGRA, GL_UNSIGNED_BYTE, b->px.data());
        }
        t.version = b->version;
        stats().uploads++; stats().uploadBytes += (size_t)t.w * t.h * 4;
    }
    t.lastFrame = frameNo();
    return &t;
}

// Quad con textura: rectangulo origen (sx,sy,w,h) del bitmap -> destino con la matriz afin (a,b,c,d,tx,ty) del bufer
inline void texQuad(const Tex& t, float sx, float sy, float w, float h, const float m[6]) {
    float u0 = sx / t.w, v0 = sy / t.h, u1 = (sx + w) / t.w, v1 = (sy + h) / t.h;
    auto X = [&](float x, float y) { return m[0] * x + m[2] * y + m[4]; };
    auto Y = [&](float x, float y) { return m[1] * x + m[3] * y + m[5]; };
    const GLfloat vtx[] = {X(0, 0), Y(0, 0), X(w, 0), Y(w, 0), X(0, h), Y(0, h), X(w, h), Y(w, h)};
    const GLfloat uv[] = {u0, v0, u1, v0, u0, v1, u1, v1};
    glVertexPointer(2, GL_FLOAT, 0, vtx);
    glTexCoordPointer(2, GL_FLOAT, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    stats().quads++;
}

// Dibujo por lotes: los cuadros consecutivos con la misma textura van en UNA llamada (posicion, coordenadas de textura y
// color por vertice). Los rectangulos de color usan una textura blanca de 1x1, asi no cortan el lote. Se respeta el
// orden: el lote se vacia al cambiar de textura. Prueba NEOS00002 en la Vita: CPU 20,9 -> 3,2 ms con 5000 cuadros.
struct Batch {
    GLuint tex = 0;
    std::vector<GLfloat> v, uv;
    std::vector<GLubyte> c;
    size_t quads = 0;
    void flush() {
        if (!quads) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, v.data());
        glTexCoordPointer(2, GL_FLOAT, 0, uv.data());
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, c.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(quads * 6));
        glDisableClientState(GL_COLOR_ARRAY);
        stats().batches++;
        v.clear(); uv.clear(); c.clear(); quads = 0;
    }
    // esquinas p[0..3] = (0,0) (w,0) (0,h) (w,h) ya transformadas; t[0..3] lo mismo en coordenadas de textura
    void quad(GLuint t, const float* px, const float* py, float u0, float v0, float u1, float v1, const GLubyte col[4]) {
        if (t != tex) { flush(); tex = t; }
        const int idx[6] = {0, 1, 2, 1, 3, 2};
        const float us[4] = {u0, u1, u0, u1}, vs[4] = {v0, v0, v1, v1};
        for (int i : idx) {
            v.push_back(px[i]); v.push_back(py[i]);
            uv.push_back(us[i]); uv.push_back(vs[i]);
            c.insert(c.end(), col, col + 4);
        }
        quads++; stats().quads++;
    }
};
inline GLuint whiteTex() {
    static GLuint id = 0;
    if (!id) {
        const uint32_t px = 0xFFFFFFFFu;
        glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return id;
}
inline GLubyte clamp255(float f) { return (GLubyte)(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f); }

// Dibuja las ordenes de un bufer. M = matriz del sprite de la camara (posicion y zoom); (bx,by) = desplazamiento del
// bitmap dentro del sprite (centrado). Las coordenadas de las ordenes son las del bufer.
inline void drawCommands(flash::BitmapData& buf, const flash::Affine& M, float bx, float by, double alpha) {
    static bool hook = installGcHook(); (void)hook;
    static Batch b;
    const GLuint white = whiteTex();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    const GLfloat mm[16] = {(GLfloat)M.a, (GLfloat)M.b, 0, 0, (GLfloat)M.c, (GLfloat)M.d, 0, 0, 0, 0, 1, 0, (GLfloat)M.tx, (GLfloat)M.ty, 0, 1};
    glMultMatrixf(mm);
    glTranslatef(bx, by, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    const float ga = (float)alpha;
    for (auto& k : buf.cmds) {
        stats().cmds++;
        if (k.kind == flash::BitmapData::Cmd::Rect) {
            const GLubyte col[4] = {(GLubyte)((k.color >> 16) & 255), (GLubyte)((k.color >> 8) & 255), (GLubyte)(k.color & 255), (GLubyte)((k.color >> 24) * ga)};
            float x0 = (float)k.sx, y0 = (float)k.sy, x1 = (float)(k.sx + k.w), y1 = (float)(k.sy + k.h);
            const float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1};
            b.quad(white, px, py, 0.5f, 0.5f, 0.5f, 0.5f, col);
            continue;
        }
        Tex* t = texFor(k.src);  // puede subir la textura (cambia la textura enlazada: el lote enlaza la suya al vaciarse)
        if (!t) continue;
        if (k.kind == flash::BitmapData::Cmd::Blit) {
            int sx = k.sx, sy = k.sy, w = k.w, h = k.h, dx = k.dx, dy = k.dy;
            if (sx < 0) { dx -= sx; w += sx; sx = 0; }
            if (sy < 0) { dy -= sy; h += sy; sy = 0; }
            if (sx + w > t->w) w = t->w - sx;
            if (sy + h > t->h) h = t->h - sy;
            if (w <= 0 || h <= 0) continue;
            const GLubyte col[4] = {255, 255, 255, clamp255(ga)};
            const float px[4] = {(float)dx, (float)(dx + w), (float)dx, (float)(dx + w)};
            const float py[4] = {(float)dy, (float)dy, (float)(dy + h), (float)(dy + h)};
            b.quad(t->id, px, py, (float)sx / t->w, (float)sy / t->h, (float)(sx + w) / t->w, (float)(sy + h) / t->h, col);
        } else {
            const float m[6] = {(float)k.ma, (float)k.mb, (float)k.mc, (float)k.md, (float)k.mtx, (float)k.mty};
            const float w = (float)t->w, h = (float)t->h;
            const float px[4] = {m[4], m[0] * w + m[4], m[2] * h + m[4], m[0] * w + m[2] * h + m[4]};
            const float py[4] = {m[5], m[1] * w + m[5], m[3] * h + m[5], m[1] * w + m[3] * h + m[5]};
            GLubyte col[4] = {255, 255, 255, clamp255(ga)};
            if (k.hasCt) { col[0] = clamp255((float)k.ct[0]); col[1] = clamp255((float)k.ct[1]); col[2] = clamp255((float)k.ct[2]); col[3] = clamp255((float)k.ct[3] * ga); }
            b.quad(t->id, px, py, 0, 0, 1, 1, col);
        }
    }
    b.flush();
    glColor4f(1, 1, 1, 1);
    glPopMatrix();
}

// Lista de visualizacion como quads. 'gpuCams' asocia el sprite de una camara de Flixel con su bufer en modo diferido:
// en lugar del bitmap (que no tiene pixeles al dia) se ejecutan sus ordenes. Devuelve cuantos objetos con contenido que
// no se sabe dibujar (texto, graficos no rectangulares) se han saltado.
using CamMap = std::unordered_map<flash::DisplayObject*, flash::BitmapData*>;
inline void fillRectGl(const flash::Affine& M, double x, double y, double w, double h, uint32_t argb, double alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4ub((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, (GLubyte)((argb >> 24) * alpha));
    auto X = [&](double px, double py) { return (GLfloat)(M.a * px + M.c * py + M.tx); };
    auto Y = [&](double px, double py) { return (GLfloat)(M.b * px + M.d * py + M.ty); };
    const GLfloat vtx[] = {X(x, y), Y(x, y), X(x + w, y), Y(x + w, y), X(x, y + h), Y(x, y + h), X(x + w, y + h), Y(x + w, y + h)};
    glVertexPointer(2, GL_FLOAT, 0, vtx);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glColor4f(1, 1, 1, 1);
    glEnable(GL_TEXTURE_2D);
}
inline int drawDisplayList(flash::DisplayObject* d, const flash::Affine& parent, double alpha, const CamMap& cams) {
    if (!d || !d->visible || alpha <= 0) return 0;
    flash::Affine M = d->__isStage ? parent : flash::affineThen(flash::localAffine(d), parent);
    double a = alpha * (d->__isStage ? 1.0 : d->alpha);
    if (a <= 0) return 0;
    auto it = cams.find(d);
    if (it != cams.end()) {
        flash::BitmapData* buf = it->second;
        float bx = 0, by = 0;
        if (auto* sp = dynamic_cast<flash::DisplayObjectContainer*>(d))
            for (auto* ch : sp->__children) if (dynamic_cast<flash::Bitmap*>(ch)) { bx = (float)ch->x; by = (float)ch->y; break; }
        drawCommands(*buf, M, bx, by, a);
        return 0;
    }
    int skipped = 0;
    if (auto* bm = dynamic_cast<flash::Bitmap*>(d)) {
        if (Tex* t = texFor(bm->bitmapData)) {
            const float m[6] = {(float)M.a, (float)M.b, (float)M.c, (float)M.d, (float)M.tx, (float)M.ty};
            glColor4f(1, 1, 1, (float)a);
            texQuad(*t, 0, 0, (float)t->w, (float)t->h, m);
            glColor4f(1, 1, 1, 1);
        }
        return 0;
    }
    if (dynamic_cast<flash::TextField*>(d)) return 1;
    if (auto* c = dynamic_cast<flash::DisplayObjectContainer*>(d)) {
        if (auto* sp = dynamic_cast<flash::Sprite*>(d)) {
            if (sp->graphics) for (auto& g : sp->graphics->cmds) {
                if (g.kind == 0) fillRectGl(M, g.x, g.y, g.w, g.h, g.argb, a);
                else skipped++;
            }
        }
        for (auto* ch : c->__children) skipped += drawDisplayList(ch, M, a, cams);
    }
    return skipped;
}

}  // namespace gpu
