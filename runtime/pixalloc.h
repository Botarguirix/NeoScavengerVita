// pixalloc.h — asignador de los pixeles de BitmapData en una zona de memoria propia.
// En la Vita el heap de newlib se reserva antes de main y no puede crecer; la memoria extra que da DMR
// (sceAppMgrGrowMemory) se reserva despues como bloque y los pixeles (la mayor parte de la memoria del juego) van ahi.
// Sin zona (PC, o si no hay DMR) o si se llena, se usa el operator new normal.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <new>

namespace pixmem {

struct Arena {
    uint8_t* base = nullptr;
    size_t size = 0, used = 0, peak = 0, fallbacks = 0;
    std::map<size_t, size_t> freeList;  // desplazamiento -> longitud de cada hueco libre (ordenado, para fusionar)
    std::map<size_t, size_t> live;      // desplazamiento -> longitud de cada bloque en uso
};
inline Arena& arena() { static Arena a; return a; }

inline void init(void* base, size_t size) {
    Arena& a = arena();
    a.base = (uint8_t*)base; a.size = size; a.used = 0;
    a.freeList.clear(); a.live.clear();
    if (size) a.freeList[0] = size;
}

inline void* alloc(size_t n) {
    Arena& a = arena();
    n = (n + 15) & ~(size_t)15;
    if (a.base && n) {
        for (auto it = a.freeList.begin(); it != a.freeList.end(); ++it) {  // primer hueco que quepa
            if (it->second < n) continue;
            size_t off = it->first, len = it->second;
            a.freeList.erase(it);
            if (len > n) a.freeList[off + n] = len - n;
            a.live[off] = n;
            a.used += n; if (a.used > a.peak) a.peak = a.used;
            return a.base + off;
        }
    }
    if (a.base) a.fallbacks++;
    return ::operator new(n);
}

inline void free(void* p) {
    Arena& a = arena();
    uint8_t* q = (uint8_t*)p;
    if (a.base && q >= a.base && q < a.base + a.size) {
        size_t off = (size_t)(q - a.base);
        auto lv = a.live.find(off);
        if (lv == a.live.end()) return;
        size_t len = lv->second;
        a.live.erase(lv);
        a.used -= len;
        auto next = a.freeList.lower_bound(off);
        if (next != a.freeList.end() && off + len == next->first) { len += next->second; next = a.freeList.erase(next); }
        if (next != a.freeList.begin()) {
            auto prev = std::prev(next);
            if (prev->first + prev->second == off) { prev->second += len; return; }
        }
        a.freeList[off] = len;
        return;
    }
    ::operator delete(p);
}

template <class T> struct Alloc {
    using value_type = T;
    Alloc() = default;
    template <class U> Alloc(const Alloc<U>&) {}
    T* allocate(size_t n) { return (T*)pixmem::alloc(n * sizeof(T)); }
    void deallocate(T* p, size_t) { pixmem::free(p); }
    // resize() sin valor deja los pixeles sin inicializar (el constructor de BitmapData los rellena de forma perezosa:
    // uniformPending); con valor, los construye normalmente
    template <class U> void construct(U* p) noexcept { ::new ((void*)p) U; }
    template <class U, class V> void construct(U* p, V&& v) { ::new ((void*)p) U(static_cast<V&&>(v)); }
    template <class U> bool operator==(const Alloc<U>&) const { return true; }
    template <class U> bool operator!=(const Alloc<U>&) const { return false; }
};

}  // namespace pixmem
