// fastalloc.cpp — operator new/delete global con paginas por clase de tamano para bloques pequenos (<= 512 bytes).
// El codigo traducido crea muchisimos objetos, cadenas y vectores pequenos (la carga de datos son millones de
// reservas) y el malloc del sistema (con cerrojo en newlib) era la mayor parte del tiempo de carga.
//
// Paginas de 64 KB alineadas a 64 KB, cada una de una sola clase de tamano (16, 32 ... 512 bytes), con su propia lista
// de libres y un contador de bloques vivos. Liberar = buscar la pagina con una mascara de la direccion. Cuando una
// pagina se queda vacia se DEVUELVE al sistema: la memoria que deja la basura (p. ej. el XML tras la carga) vuelve a
// estar disponible para cualquier tamano (la primera version la retenia en listas por tamano y se agotaba el heap).
// Bloques grandes: malloc normal con una cabecera de 16 bytes marcada. Cerrojo de giro (el hilo de audio tambien reserva).
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {
constexpr size_t kStep = 16, kMax = 512, kClasses = kMax / kStep;
constexpr size_t kPage = 64 * 1024;
constexpr uint64_t kPageMagic = 0x4E53504147453031ull;  // "NSPAGE01"
constexpr uint64_t kBigMagic = 0x4E53424947424C4Bull;   // "NSBIGBLK"

struct FreeNode { FreeNode* next; };
struct Page {
    uint64_t magic;
    Page* next; Page* prev;   // lista de paginas con hueco de su clase
    FreeNode* free;
    char* bump; char* end;    // zona aun no usada
    uint32_t live, cls;
    bool listed;
    char pad[64 - 8 - 8 - 8 - 8 - 8 - 8 - 4 - 4 - 1 > 0 ? 64 - 8 - 8 - 8 - 8 - 8 - 8 - 4 - 4 - 1 : 1];
};
constexpr size_t kHeader = (sizeof(Page) + 15) & ~(size_t)15;

Page* gAvail[kClasses] = {};   // paginas con hueco
size_t gPagesInUse = 0;
std::atomic_flag gLock = ATOMIC_FLAG_INIT;
struct Guard { Guard() { while (gLock.test_and_set(std::memory_order_acquire)) {} } ~Guard() { gLock.clear(std::memory_order_release); } };

// Paginas de 64 KB alineadas sacadas de bloques de 2 MB reservados con malloc normal (memalign de newlib fallaba en la
// Vita). Las paginas vacias vuelven a un deposito comun: cualquier clase de tamano las reutiliza.
struct PoolPage { PoolPage* next; };
PoolPage* gPool = nullptr;
size_t gPoolPages = 0;
inline void* pageAlloc() {
    if (!gPool) {
        constexpr size_t kSuper = 2 * 1024 * 1024;
        char* raw = (char*)std::malloc(kSuper);
        if (!raw) return nullptr;
        uintptr_t a = ((uintptr_t)raw + kPage - 1) & ~(uintptr_t)(kPage - 1);
        for (; a + kPage <= (uintptr_t)raw + kSuper; a += kPage) { PoolPage* p = (PoolPage*)a; p->next = gPool; gPool = p; gPoolPages++; }
    }
    PoolPage* p = gPool; gPool = p->next; gPoolPages--;
    return p;
}
inline void pageFree(void* q) { PoolPage* p = (PoolPage*)q; p->next = gPool; gPool = p; gPoolPages++; }
inline void unlink(Page* pg) {
    if (!pg->listed) return;
    if (pg->prev) pg->prev->next = pg->next; else gAvail[pg->cls] = pg->next;
    if (pg->next) pg->next->prev = pg->prev;
    pg->next = pg->prev = nullptr; pg->listed = false;
}
inline void link(Page* pg) {
    if (pg->listed) return;
    pg->prev = nullptr; pg->next = gAvail[pg->cls];
    if (pg->next) pg->next->prev = pg;
    gAvail[pg->cls] = pg; pg->listed = true;
}

inline void* allocate(size_t n) {
    if (n == 0) n = 1;
    if (n > kMax) {
        uint64_t* p = (uint64_t*)std::malloc(n + 16);
        if (!p) throw std::bad_alloc();
        p[1] = kBigMagic;
        return p + 2;
    }
    size_t c = (n + kStep - 1) / kStep - 1, sz = (c + 1) * kStep;
    Guard g;
    Page* pg = gAvail[c];
    if (!pg) {
        pg = (Page*)pageAlloc();
        if (!pg) throw std::bad_alloc();
        pg->magic = kPageMagic; pg->free = nullptr; pg->bump = (char*)pg + kHeader; pg->end = (char*)pg + kPage;
        pg->live = 0; pg->cls = (uint32_t)c; pg->listed = false; pg->next = pg->prev = nullptr;
        link(pg);
        gPagesInUse++;
    }
    void* r;
    if (pg->free) { r = pg->free; pg->free = pg->free->next; }
    else { r = pg->bump; pg->bump += sz; }
    pg->live++;
    if (!pg->free && pg->bump + sz > pg->end) unlink(pg);  // llena
    return r;
}

inline void release(void* q) {
    if (!q) return;
    if (((uint64_t*)q)[-1] == kBigMagic) { ((uint64_t*)q)[-1] = 0; std::free((uint64_t*)q - 2); return; }
    Page* pg = (Page*)((uintptr_t)q & ~(uintptr_t)(kPage - 1));
    Guard g;
    FreeNode* f = (FreeNode*)q;
    f->next = pg->free; pg->free = f;
    if (--pg->live == 0) {  // pagina vacia: se devuelve al sistema (salvo si es la unica con hueco de su clase)
        unlink(pg);
        if (gAvail[pg->cls]) { gPagesInUse--; pageFree(pg); return; }
        link(pg);
        return;
    }
    link(pg);
}
}  // namespace

void* operator new(size_t n) { return allocate(n); }
void* operator new[](size_t n) { return allocate(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, size_t) noexcept { release(p); }
void operator delete[](void* p, size_t) noexcept { release(p); }

// Memoria en paginas de bloques pequenos (para el diagnostico; mallinfo ya cuenta bien lo devuelto al sistema)
size_t fastallocFreeBytes() { return gPoolPages * kPage; }  // paginas vacias en el deposito (mallinfo las cuenta como usadas)
size_t fastallocPages() { return gPagesInUse; }
