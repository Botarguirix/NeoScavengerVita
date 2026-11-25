// persist.h — guardado de SharedObject (partidas, opciones) en archivo.
// Flash serializaba SharedObject.data en AMF3: los objetos de clases sin alias registrado vuelven como Object generico
// con sus campos publicos, los Vector.<int/uint/Number/String/Boolean> conservan su tipo y los Dictionary siguen siendo
// Dictionary. Este formato propio reproduce eso (incluidas las referencias compartidas y los ciclos).
// Formato: "NSSO0001" + valor. Etiquetas: 0 undefined, 1 null, 2 false, 3 true, 4 numero (double), 5 cadena,
// 6 referencia (u32), 7 Array, 8 Object, 9 Dictionary, 10 Vector (u8 tipo), 11 Date (ms).
#pragma once
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "as3rt.h"

namespace persist {

struct Writer {
    std::string out;
    std::unordered_map<as3::Object*, uint32_t> refs;
    void u8(uint8_t v) { out.push_back((char)v); }
    void u32(uint32_t v) { out.append((const char*)&v, 4); }
    void f64(double v) { out.append((const char*)&v, 8); }
    void str(const std::string& s) { u32((uint32_t)s.size()); out.append(s); }
    void val(const as3::Any& a);
    void obj(as3::Object* o);
};

inline void Writer::val(const as3::Any& a) {
    switch (a.k) {
        case as3::Any::Undef: u8(0); return;
        case as3::Any::Null: u8(1); return;
        case as3::Any::Bool: u8(a.b ? 3 : 2); return;
        case as3::Any::Num: u8(4); f64(a.n); return;
        case as3::Any::Str: if (a.s.isNull()) { u8(1); return; } u8(5); str(a.s.str()); return;
        case as3::Any::Obj: obj(a.o); return;
        default: u8(1); return;  // funciones: no se guardan (AMF tampoco)
    }
}

inline void Writer::obj(as3::Object* o) {
    if (!o) { u8(1); return; }
    auto it = refs.find(o);
    if (it != refs.end()) { u8(6); u32(it->second); return; }
    if (dynamic_cast<as3::Class*>(o)) { u8(1); return; }
    uint32_t id = (uint32_t)refs.size();
    refs[o] = id;
    if (auto* d = dynamic_cast<as3::Date*>(o)) { u8(11); f64(d->getTime()); return; }
    if (auto* arr = dynamic_cast<as3::Array*>(o)) {
        u8(7); u32((uint32_t)arr->v.size());
        for (auto& x : arr->v) val(x);
        return;
    }
    if (auto* dict = dynamic_cast<as3::Dictionary*>(o)) {
        u8(9); u32((uint32_t)dict->order.size());
        for (auto& k : dict->order) { val(k); val(dict->get(k)); }
        return;
    }
    if (auto* vb = dynamic_cast<as3::VectorBase*>(o)) {
        int kind = vb->__vecKind();
        size_t n = vb->__vecLen();
        u8(10); u8((uint8_t)kind); u32((uint32_t)n);
        for (size_t i = 0; i < n; i++) {
            as3::Any e = vb->__vecGet(i);
            switch (kind) {
                case as3::VectorBase::KInt: case as3::VectorBase::KUint: case as3::VectorBase::KNum: f64(as3::toNumber(e)); break;
                case as3::VectorBase::KStr: str(as3::toString(e).str()); break;
                case as3::VectorBase::KBool: u8(as3::truthy(e) ? 1 : 0); break;
                default: val(e);
            }
        }
        return;
    }
    // objeto generico o instancia de clase: campos publicos + propiedades dinamicas
    std::vector<std::string> names;
    o->__fields(names);
    size_t fixed = names.size();
    if (o->__props) for (auto& kv : *o->__props) if (std::find(names.begin(), names.end(), kv.first) == names.end()) names.push_back(kv.first);
    u8(8); u32((uint32_t)names.size());
    for (size_t i = 0; i < names.size(); i++) {
        str(names[i]);
        if (i < fixed) val(o->__get(names[i]));
        else val((*o->__props)[names[i]]);
    }
}

struct Reader {
    const std::string& in;
    size_t pos = 0;
    bool bad = false;
    std::vector<as3::Object*> refs;
    explicit Reader(const std::string& s) : in(s) {}
    bool need(size_t n) { if (pos + n > in.size()) { bad = true; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return (uint8_t)in[pos++]; }
    uint32_t u32() { uint32_t v = 0; if (!need(4)) return 0; std::memcpy(&v, in.data() + pos, 4); pos += 4; return v; }
    double f64() { double v = 0; if (!need(8)) return 0; std::memcpy(&v, in.data() + pos, 8); pos += 8; return v; }
    std::string str() { uint32_t n = u32(); if (!need(n)) return std::string(); std::string s = in.substr(pos, n); pos += n; return s; }
    as3::Any val();
};

inline as3::Any Reader::val() {
    if (bad) return as3::Any();
    uint8_t t = u8();
    switch (t) {
        case 0: return as3::Any();
        case 1: return as3::Any(nullptr);
        case 2: return as3::Any(false);
        case 3: return as3::Any(true);
        case 4: return as3::Any(f64());
        case 5: return as3::Any(as3::String(str()));
        case 6: { uint32_t id = u32(); return id < refs.size() ? as3::Any(refs[id]) : as3::Any(nullptr); }
        case 11: { auto* d = as3::make<as3::Date>(); d->setTime(f64()); refs.push_back(d); return as3::Any((as3::Object*)d); }
        case 7: {
            auto* a = as3::make<as3::Array>(); refs.push_back(a);
            uint32_t n = u32();
            for (uint32_t i = 0; i < n && !bad; i++) a->v.push_back(val());
            return as3::Any((as3::Object*)a);
        }
        case 9: {
            auto* d = as3::make<as3::Dictionary>(); refs.push_back(d);
            uint32_t n = u32();
            for (uint32_t i = 0; i < n && !bad; i++) { as3::Any k = val(); as3::Any v = val(); d->set(k, v); }
            return as3::Any((as3::Object*)d);
        }
        case 10: {
            uint8_t kind = u8(); uint32_t n = u32();
            as3::Object* vec = nullptr;
            switch (kind) {
                case as3::VectorBase::KInt: vec = as3::make<as3::Vector<int32_t>>(); break;
                case as3::VectorBase::KUint: vec = as3::make<as3::Vector<uint32_t>>(); break;
                case as3::VectorBase::KNum: vec = as3::make<as3::Vector<double>>(); break;
                case as3::VectorBase::KStr: vec = as3::make<as3::Vector<as3::String>>(); break;
                case as3::VectorBase::KBool: vec = as3::make<as3::Vector<bool>>(); break;
                default: vec = as3::make<as3::Array>(); break;  // Vector.<Clase> sin alias: AMF devuelve objetos genericos
            }
            refs.push_back(vec);
            auto* vb = dynamic_cast<as3::VectorBase*>(vec);
            auto* arr = dynamic_cast<as3::Array*>(vec);
            for (uint32_t i = 0; i < n && !bad; i++) {
                as3::Any e;
                switch (kind) {
                    case as3::VectorBase::KInt: case as3::VectorBase::KUint: case as3::VectorBase::KNum: e = as3::Any(f64()); break;
                    case as3::VectorBase::KStr: e = as3::Any(as3::String(str())); break;
                    case as3::VectorBase::KBool: e = as3::Any(u8() != 0); break;
                    default: e = val();
                }
                if (vb) vb->__vecPush(e); else if (arr) arr->v.push_back(e);
            }
            return as3::Any(vec);
        }
        case 8: {
            auto* o = as3::make<as3::Object>(); refs.push_back(o);
            uint32_t n = u32();
            for (uint32_t i = 0; i < n && !bad; i++) { std::string k = str(); as3::Any v = val(); o->__set(k, v); }
            return as3::Any(o);
        }
        default: bad = true; return as3::Any();
    }
}

inline std::string save(as3::Object* data) {
    Writer w; w.out = "NSSO0001"; w.obj(data); return w.out;
}
inline as3::Object* load(const std::string& bytes) {
    if (bytes.size() < 9 || bytes.compare(0, 8, "NSSO0001") != 0) return nullptr;
    Reader r(bytes); r.pos = 8;
    as3::Any v = r.val();
    if (r.bad || v.k != as3::Any::Obj) return nullptr;
    return v.o;
}

}  // namespace persist
