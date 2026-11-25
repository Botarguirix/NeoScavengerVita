// as3rt.h — runtime de ActionScript 3 para el C++ generado por tools/as3cpp (v2, 20-11-2026).
// Semántica de AS3 donde importa para el juego 1:1: Number = double (NaN por defecto), int/uint con
// ToInt32, String nulable, == laxo y === estricto, + que concatena si hay String, Array/Vector/Dictionary,
// Object dinámico, funciones como valores, try/catch/throw/finally.
// Límites conocidos (anotados en el ROADMAP): String es UTF-8 con índices por byte (texto del juego en ASCII),
// sin recolector de basura (los objetos no se liberan; ver ROADMAP "memoria"), cierres capturan por copia.
#pragma once
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "platform.h"

// Rastreo de funciones (solo con -DAS3_TRACE, compilaciones de desarrollo): cada funcion traducida apunta su nombre;
// si el programa se cae (SIGSEGV) o lanza una excepcion AS3 sin capturar, se imprime la pila de nombres de AS3.
#ifdef AS3_TRACE
#include <csignal>
#include <exception>
namespace as3 {
inline int& curLine() { static int l = 0; return l; }
inline int& lastExitLine() { static int l = 0; return l; }  // ultima sentencia de la funcion que acaba de salir (diagnostico)
inline std::vector<const char*>& traceStack() { static std::vector<const char*> s; return s; }
struct FnScope {
    int saved;
    explicit FnScope(const char* n) : saved(curLine()) { traceStack().push_back(n); }
    ~FnScope() { lastExitLine() = curLine(); traceStack().pop_back(); curLine() = saved; }
};
inline void dumpTrace(const char* why) {
    std::fprintf(stderr, "\n[CRASH] %s. Ultima sentencia: #%d (ver lines.txt). Pila de AS3 (mas reciente primero):\n", why, curLine());
    auto& s = traceStack();
    int n = 0;
    for (auto it = s.rbegin(); it != s.rend() && n < 30; ++it, ++n) std::fprintf(stderr, "   %s\n", *it);
    std::fflush(stderr);
}
inline void (*&crashHook())() { static void (*h)() = nullptr; return h; }  // lo fija la aplicacion (p. ej. para volcar su propio log)
inline void crashHandler(int sig) { dumpTrace(sig == SIGSEGV ? "fallo de segmentacion" : sig == SIGABRT ? "abort (excepcion de C++ sin capturar o assert)" : "excepcion de coma flotante"); if (crashHook()) crashHook()(); std::_Exit(139); }
inline void installCrashHandler() {
    std::set_terminate([] {
        try { if (auto e = std::current_exception()) std::rethrow_exception(e); }
        catch (const std::exception& x) { std::fprintf(stderr, "[terminate] excepcion de C++: %s\n", x.what()); }
        catch (...) { std::fprintf(stderr, "[terminate] excepcion desconocida\n"); }
        std::abort();
    }); std::signal(SIGSEGV, crashHandler); std::signal(SIGABRT, crashHandler); std::signal(SIGFPE, crashHandler); }
}  // namespace as3
#define AS3_FN(n) ::as3::FnScope __as3fn(n)
#define AS3_L(n) (::as3::curLine() = (n))
#else
namespace as3 { inline void installCrashHandler() {} inline void dumpTrace(const char*) {} inline int& lastExitLine() { static int l = 0; return l; } }
#define AS3_FN(n)
#define AS3_L(n)
#endif

namespace as3 {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Infinity = std::numeric_limits<double>::infinity();

struct Undefined {};
inline constexpr Undefined undefined{};

struct Object;
class String;
struct Any;
struct Array;
struct Dictionary;
struct RegExp;
template <class T> struct Vector;
class Function;
template <class T> T fromAny(const Any& a);

// ============================================================ String
class String {
public:
    String() = default;
    String(std::nullptr_t) {}
    String(const char* s) : p_(s ? std::make_shared<const std::string>(s) : nullptr) {}
    String(std::string s) : p_(std::make_shared<const std::string>(std::move(s))) {}
    bool isNull() const { return !p_; }
    const std::string& str() const { static const std::string e; return p_ ? *p_ : e; }
    // AS3: la cadena null se muestra como "null" al concatenar; vacía es ""
    int32_t length() const { return (int32_t)str().size(); }
    bool operator==(const String& o) const { return (!p_ && !o.p_) || (p_ && o.p_ && *p_ == *o.p_); }
    bool operator!=(const String& o) const { return !(*this == o); }
    bool operator<(const String& o) const { return str() < o.str(); }
    // --- métodos de AS3 (implementados abajo, tras Array/RegExp)
    String charAt(double i) const;
    double charCodeAt(double i) const;
    int32_t indexOf(const String& s, double from = 0) const;
    int32_t lastIndexOf(const String& s, double from = 1e9) const;
    int32_t indexOf(const Any& s, double from = 0) const;
    int32_t lastIndexOf(const Any& s, double from = 1e9) const;
    String substr(double start, double len = 1e9) const;
    String substring(double a, double b = 1e9) const;
    String slice(double a, double b = 1e9) const;
    String toLowerCase() const;
    String toUpperCase() const;
    String toLocaleLowerCase() const { return toLowerCase(); }
    String toLocaleUpperCase() const { return toUpperCase(); }
    String concat(const String& s) const { return String(str() + s.str()); }
    String toString() const { return *this; }
    String valueOf() const { return *this; }
    int32_t localeCompare(const String& o) const { return str().compare(o.str()); }
    Array* split(const String& sep, double limit = 1e9) const;
    Array* split(RegExp* re, double limit = 1e9) const;
    String replace(const String& pat, const String& rep) const;
    String replace(RegExp* re, const String& rep) const;
    int32_t search(RegExp* re) const;
    Array* match(RegExp* re) const;
    static String fromCharCode(double c) { return String(std::string(1, (char)(int)c)); }
private:
    std::shared_ptr<const std::string> p_;
};

inline String operator""_as(const char* s, size_t n) { return String(std::string(s, n)); }

// ============================================================ Object
struct Any;
// ------------------------------------------------------------ recolector de basura (sin el, el juego no cabe en la Vita)
// Marcado y barrido ENTRE fotogramas: en ese momento no hay codigo AS3 en la pila, asi que las raices son conocidas
// (estaticos, escenario, objetos fijados y las que pase la aplicacion). Cada clase recorre sus campos en __trace()
// (el traductor lo genera; el runtime lo escribe a mano para Array, Vector, Dictionary, XML y los tipos de Flash).
namespace gc {
struct AllocInfo { void* p = nullptr; size_t n = 0; };
inline AllocInfo& lastAlloc() { static AllocInfo a; return a; }
inline std::vector<Object*>& heap() { static std::vector<Object*> h; return h; }
inline std::vector<Object*>& work() { static std::vector<Object*> w; return w; }
inline uint32_t& epoch() { static uint32_t e = 1; return e; }
}  // namespace gc

struct Object {
    // Solo los objetos creados con new entran en el recolector (los de pila o estaticos no)
    Object() {
        auto& a = gc::lastAlloc();
        if ((char*)this >= (char*)a.p && (char*)this < (char*)a.p + a.n) { gc::heap().push_back(this); a.p = nullptr; a.n = 0; }
    }
    Object(const Object&) : Object() {}
    Object& operator=(const Object&) { return *this; }
    static void* operator new(size_t n) { void* p = ::operator new(n); gc::lastAlloc() = {p, n}; return p; }
    static void operator delete(void* p) { ::operator delete(p); }
    uint32_t __gcMark = 0;
    bool __gcPinned = false;  // raiz permanente (clases, objetos de registros internos del runtime)
    virtual void __trace();   // marca los objetos a los que apunta
    virtual void __fields(std::vector<std::string>&) {}  // campos publicos (guardado, como AMF); los genera el traductor
    virtual ~Object() = default;
    void __construct() {}
    // propiedades dinámicas (Object como bolsa de datos, clases `dynamic`)
    std::unordered_map<std::string, Any>* __props = nullptr;
    virtual Any __get(const std::string& name);
    virtual void __set(const std::string& name, const Any& v);
    virtual bool __has(const std::string& name);
    virtual Any __call(const std::string& name, const std::vector<Any>& args);
    virtual String __className() const { return String("Object"); }
    virtual void __loadEmbed(const char*) {}  // clases con [Embed]: cargan sus datos desde assets/<id>.<ext>
    virtual String toString() { return String("[object Object]"); }
};

template <class T, class... A> T* make(A&&... a) {
    T* o = new T();
    o->__construct(std::forward<A>(a)...);
    return o;
}

// ============================================================ Function
struct FuncImpl;
class Function {
public:
    Function() = default;
    template <class F> Function(F f);  // lambda: Any(const std::vector<Any>&)
    Any call(const std::vector<Any>& a = {}) const;
    explicit operator bool() const { return (bool)p_; }
    Object* gcTarget() const;
    void setTarget(Object* o);
    // Dos referencias al mismo metodo del mismo objeto son iguales (como en AS3): se comparan por identidad (objeto + metodo)
    bool operator==(const Function& o) const;
    bool isNull() const { return !p_; }
    void setId(std::string id);
private:
    std::shared_ptr<FuncImpl> p_;
};

// ============================================================ Any
struct Any {
    enum Kind : uint8_t { Undef, Null, Num, Bool, Str, Obj, Func } k = Undef;
    double n = 0;
    bool b = false;
    Object* o = nullptr;
    String s;
    Function fn;

    Any() = default;
    Any(Undefined) {}
    Any(std::nullptr_t) : k(Null) {}
    Any(double d) : k(Num), n(d) {}
    Any(int32_t i) : k(Num), n(i) {}
    Any(uint32_t i) : k(Num), n(i) {}
    Any(bool v) : k(Bool), b(v) {}
    Any(const String& v) : k(v.isNull() ? Null : Str), s(v) {}
    Any(const char* v) : k(v ? Str : Null), s(v) {}
    Any(const Function& f) : k(f.isNull() ? Null : Func), fn(f) {}
    template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
    Any(T* p) : k(p ? Obj : Null), o(p) {}
    Any& operator++() { double v = toNumberSlow() + 1; *this = Any(v); return *this; }
    Any& operator--() { double v = toNumberSlow() - 1; *this = Any(v); return *this; }
    Any operator++(int) { Any old(toNumberSlow()); ++*this; return old; }
    Any operator--(int) { Any old(toNumberSlow()); --*this; return old; }
    double toNumberSlow() const;
    bool isUndefined() const { return k == Undef; }
    bool isNull() const { return k == Null; }
    bool isNullish() const { return k == Undef || k == Null; }
};

struct FuncImpl {
    std::function<Any(const std::vector<Any>&)> f;
    std::string id;  // identidad de un metodo enlazado (vacia en las funciones anonimas)
    Object* target = nullptr;  // objeto de un metodo enlazado (el recolector lo mantiene vivo)
};
inline Object* Function::gcTarget() const { return p_ ? p_->target : nullptr; }
inline void Function::setTarget(Object* o) { if (p_) p_->target = o; }

namespace gc {
// Diagnostico: quien mantiene vivo a quien (solo si trackParents(); lo usa el arnes de PC)
inline Object*& curTracer() { static Object* c = nullptr; return c; }
inline bool& trackParents() { static bool t = false; return t; }
inline std::unordered_map<Object*, Object*>& parents() { static std::unordered_map<Object*, Object*> p; return p; }
inline void markObj(Object* o) {
    if (!o || o->__gcMark == epoch()) return;
    o->__gcMark = epoch();
    if (trackParents()) parents()[o] = curTracer();
    work().push_back(o);
}
inline void mark(Object* o) { markObj(o); }
inline void mark(const Any& a) {
    if (a.k == Any::Obj) markObj(a.o);
    else if (a.k == Any::Func) markObj(a.fn.gcTarget());
}
inline void mark(const Function& f) { markObj(f.gcTarget()); }
inline void mark(const String&) {}
template <class T> void mark(T* p) {
    if constexpr (std::is_base_of<Object, T>::value) markObj(static_cast<Object*>(p));
    else if constexpr (std::is_polymorphic<T>::value) markObj(p ? dynamic_cast<Object*>(p) : nullptr);
}
template <class T> std::enable_if_t<std::is_arithmetic<T>::value || std::is_enum<T>::value> mark(const T&) {}
template <class T> void mark(const std::vector<T>& v) { for (auto& x : v) mark(x); }
// Raices de la aplicacion (estaticos generados, escenario...): se llaman en cada recoleccion
inline std::vector<std::function<void()>>& rootFns() { static std::vector<std::function<void()>> r; return r; }
inline bool& enabled() { static bool e = false; return e; }
struct Stats { size_t collections = 0, lastFreed = 0, lastLive = 0; double lastMs = 0; };
inline Stats& stats() { static Stats s; return s; }
// Antes de borrar: el runtime lo usa para quitar sus punteros sin dueno (listas de diagnostico)
inline std::vector<std::function<void()>>& preSweep() { static std::vector<std::function<void()>> r; return r; }
inline size_t collect() {
    epoch()++;
    auto& h = heap();
    for (Object* o : h) if (o->__gcPinned) markObj(o);
    for (auto& f : rootFns()) f();
    auto& w = work();
    while (!w.empty()) { Object* o = w.back(); w.pop_back(); curTracer() = o; o->__trace(); }
    curTracer() = nullptr;
    for (auto& f : preSweep()) f();
    size_t freed = 0, n = 0;
    std::vector<Object*> dead;
    for (Object* o : h) { if (o->__gcMark == epoch()) h[n++] = o; else dead.push_back(o); }
    h.resize(n);
    for (Object* o : dead) { delete o; freed++; }
    stats().collections++; stats().lastFreed = freed; stats().lastLive = n;
    return freed;
}
}  // namespace gc
inline bool Function::operator==(const Function& o) const {
    if (p_ == o.p_) return true;
    return p_ && o.p_ && !p_->id.empty() && p_->id == o.p_->id;
}
inline void Function::setId(std::string id) { if (p_) p_->id = std::move(id); }
template <class F> Function::Function(F f) : p_(std::make_shared<FuncImpl>()) { p_->f = std::move(f); }
inline Any Function::call(const std::vector<Any>& a) const { return p_ ? p_->f(a) : Any(); }

struct Class : Object {
    Class() { __gcPinned = true; }  // las clases viven en estaticos locales de classOf<T>()
    std::function<bool(Object*)> test;
    std::function<Object*(const std::vector<Any>&)> ctor;
    String name;
    String __className() const override { return String("Class"); }
};

inline size_t& thrownCount() { static size_t n = 0; return n; }  // excepciones AS3 lanzadas (diagnostico)
struct Thrown {
    Any value;
#ifdef AS3_TRACE
    std::vector<const char*> trace;  // pila de AS3 en el momento de lanzar (para depurar)
    int line = 0;
    explicit Thrown(Any v) : value(std::move(v)), trace(traceStack()), line(curLine()) { thrownCount()++; }
    void print() const {
        std::fprintf(stderr, "[excepcion AS3] sentencia #%d (ver lines.txt). Pila al lanzar (mas reciente primero):\n", line);
        int n = 0;
        for (auto it = trace.rbegin(); it != trace.rend() && n < 25; ++it, ++n) std::fprintf(stderr, "   %s\n", *it);
    }
#else
    explicit Thrown(Any v) : value(std::move(v)) { thrownCount()++; }
    void print() const {}
#endif
};

template <class F> struct ScopeExit {
    F f;
    ~ScopeExit() { f(); }
};
template <class F> ScopeExit<F> onScopeExit(F f) { return ScopeExit<F>{std::move(f)}; }

[[noreturn]] inline void notImplemented(const char* what) {
    std::fprintf(stderr, "[as3] no implementado: %s\n", what);
    std::abort();
}

// ============================================================ conversiones
inline std::string numToString(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    if (d == 0) return "0";
    if (d == std::floor(d) && std::fabs(d) < 1e21) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.0f", d);
        return buf;
    }
    char buf[40];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    std::string s(buf, r.ptr);
    size_t e = s.find('e');
    if (e != std::string::npos) {  // 1e-07 -> 1e-7, 1e+21 como en AS3
        std::string mant = s.substr(0, e), ex = s.substr(e + 1);
        char sign = '+';
        if (!ex.empty() && (ex[0] == '+' || ex[0] == '-')) { sign = ex[0]; ex = ex.substr(1); }
        while (ex.size() > 1 && ex[0] == '0') ex = ex.substr(1);
        s = mant + "e" + sign + ex;
    }
    return s;
}

inline double strToNumber(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    if (a == b) return 0;
    std::string t = s.substr(a, b - a);
    if (t == "Infinity" || t == "+Infinity") return Infinity;
    if (t == "-Infinity") return -Infinity;
    char* end = nullptr;
    double v = (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) ? (double)std::strtoull(t.c_str(), &end, 16)
                                                                             : std::strtod(t.c_str(), &end);
    return (end && *end == 0) ? v : NaN;
}

inline double toNumber(double d) { return d; }
inline double toNumber(int32_t i) { return i; }
inline double toNumber(uint32_t i) { return i; }
inline double toNumber(bool b) { return b ? 1 : 0; }
inline double toNumber(const String& s) { return s.isNull() ? 0 : strToNumber(s.str()); }
inline double toNumber(const Any& a) {
    switch (a.k) {
        case Any::Num: return a.n;
        case Any::Bool: return a.b ? 1 : 0;
        case Any::Str: return strToNumber(a.s.str());
        case Any::Null: return 0;
        default: return NaN;
    }
}
inline double toNumber(const Object*) { return NaN; }
inline double Any::toNumberSlow() const { return toNumber(*this); }

inline int32_t doubleToInt32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    if (d >= -2147483648.0 && d <= 2147483647.0) return (int32_t)d;
    double m = std::fmod(std::trunc(d), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (int32_t)(uint32_t)m;
}
template <class T> int32_t toInt(const T& v) { return doubleToInt32(toNumber(v)); }
inline int32_t toInt(int32_t v) { return v; }
inline int32_t toInt(uint32_t v) { return (int32_t)v; }
template <class T> uint32_t toUint(const T& v) { return (uint32_t)doubleToInt32(toNumber(v)); }
inline uint32_t toUint(uint32_t v) { return v; }

inline bool truthy(bool b) { return b; }
inline bool truthy(double d) { return d != 0 && !std::isnan(d); }
inline bool truthy(int32_t i) { return i != 0; }
inline bool truthy(uint32_t i) { return i != 0; }
inline bool truthy(const String& s) { return !s.isNull() && !s.str().empty(); }
inline bool truthy(const Object* o) { return o != nullptr; }
inline bool truthy(std::nullptr_t) { return false; }
inline bool truthy(const Function& f) { return (bool)f; }
inline bool truthy(const Any& a) {
    switch (a.k) {
        case Any::Num: return a.n != 0 && !std::isnan(a.n);
        case Any::Bool: return a.b;
        case Any::Str: return !a.s.str().empty();
        case Any::Obj: case Any::Func: return true;
        default: return false;
    }
}

String toString(const Any& a);
inline String toString(const String& s) { return s.isNull() ? String("null") : s; }
inline String toString(double d) { return String(numToString(d)); }
inline String toString(int32_t i) { return String(std::to_string(i)); }
inline String toString(uint32_t i) { return String(std::to_string(i)); }
inline String toString(bool b) { return String(b ? "true" : "false"); }
inline String toString(std::nullptr_t) { return String("null"); }
inline String toString(Object* o);

// ============================================================ igualdad y comparación
inline bool strictEqAny(const Any& a, const Any& b) {
    if (a.k != b.k) return false;
    switch (a.k) {
        case Any::Undef: case Any::Null: return true;
        case Any::Num: return a.n == b.n;
        case Any::Bool: return a.b == b.b;
        case Any::Str: return a.s == b.s;
        case Any::Obj: return a.o == b.o;
        case Any::Func: return a.fn == b.fn;
    }
    return false;
}
inline bool eqAny(const Any& a, const Any& b) {
    if (a.k == b.k) return strictEqAny(a, b);
    if (a.isNullish() && b.isNullish()) return true;
    if (a.isNullish() || b.isNullish()) return false;
    if (a.k == Any::Obj || b.k == Any::Obj || a.k == Any::Func || b.k == Any::Func) return false;
    return toNumber(a) == toNumber(b);
}
template <class A, class B> bool eq(const A& a, const B& b) { return eqAny(Any(a), Any(b)); }
template <class A, class B> bool strictEq(const A& a, const B& b) { return strictEqAny(Any(a), Any(b)); }

inline double cmpKey(const Any& a) { return toNumber(a); }
inline bool bothStr(const Any& a, const Any& b) { return a.k == Any::Str && b.k == Any::Str; }
template <class A, class B> bool cmp_lt(const A& a, const B& b) {
    Any x(a), y(b);
    return bothStr(x, y) ? x.s.str() < y.s.str() : toNumber(x) < toNumber(y);
}
template <class A, class B> bool cmp_gt(const A& a, const B& b) {
    Any x(a), y(b);
    return bothStr(x, y) ? x.s.str() > y.s.str() : toNumber(x) > toNumber(y);
}
template <class A, class B> bool cmp_le(const A& a, const B& b) {
    Any x(a), y(b);
    return bothStr(x, y) ? x.s.str() <= y.s.str() : toNumber(x) <= toNumber(y);
}
template <class A, class B> bool cmp_ge(const A& a, const B& b) {
    Any x(a), y(b);
    return bothStr(x, y) ? x.s.str() >= y.s.str() : toNumber(x) >= toNumber(y);
}

// ============================================================ aritmética y lógica
template <class T> constexpr bool isArith = std::is_arithmetic<T>::value;

template <class A, class B> auto add(const A& a, const B& b) {
    if constexpr (std::is_same<A, String>::value || std::is_same<B, String>::value) {
        return String(toString(a).str() + toString(b).str());
    } else if constexpr (isArith<A> && isArith<B>) {
        return (double)a + (double)b;
    } else {
        Any x(a), y(b);
        if (x.k == Any::Str || y.k == Any::Str) return Any(String(toString(x).str() + toString(y).str()));
        return Any(toNumber(x) + toNumber(y));
    }
}
// Metodos de Number/int/uint/Boolean (x.toString(), x.toFixed(2)...)
template <class T> String num_toString(T x, double radix = 10) {
    if constexpr (std::is_same<T, bool>::value) return String(x ? "true" : "false");
    else {
        double d = (double)x;
        int r = (int)radix;
        if (r == 10 || r < 2 || r > 36 || d != std::floor(d) || std::isinf(d)) return String(numToString(d));
        bool neg = d < 0; unsigned long long v = (unsigned long long)std::fabs(d);
        std::string out;
        do { out += "0123456789abcdefghijklmnopqrstuvwxyz"[v % r]; v /= r; } while (v);
        if (neg) out += '-';
        std::reverse(out.begin(), out.end());
        return String(out);
    }
}
template <class T> String num_toFixed(T x, double digits = 0) {
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*f", (int)digits, (double)x);
    return String(buf);
}
template <class T> String num_toPrecision(T x, double p = 0) {
    if (p <= 0) return String(numToString((double)x));
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*g", (int)p, (double)x);
    return String(buf);
}
template <class T> String num_toExponential(T x, double d = 6) {
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*e", (int)d, (double)x);
    return String(buf);
}
template <class T> T num_valueOf(T x) { return x; }
inline double fmod(double a, double b) { return std::fmod(a, b); }
template <class A, class B> double fmod(const A& a, const B& b) { return std::fmod(toNumber(a), toNumber(b)); }
template <class A, class B> int32_t shl(const A& a, const B& b) { return (int32_t)((uint32_t)toInt(a) << (toInt(b) & 31)); }
template <class A, class B> int32_t shr(const A& a, const B& b) { return toInt(a) >> (toInt(b) & 31); }
template <class A, class B> uint32_t ushr(const A& a, const B& b) { return (uint32_t)toInt(a) >> (toInt(b) & 31); }

template <class A, class F> auto land(const A& a, F f) { return truthy(a) ? f() : a; }
template <class A, class F> auto lor(const A& a, F f) { return truthy(a) ? a : f(); }

// ============================================================ Array
struct Array : Object {
    std::vector<Any> v;
    void __trace() override { Object::__trace(); gc::mark(v); }
    template <class... A> void __construct(A... a) {
        if constexpr (sizeof...(A) == 1) {
            Any first[] = {Any(a)...};
            if (first[0].k == Any::Num) { v.assign((size_t)first[0].n, Any()); return; }
            v.push_back(first[0]);
        } else {
            (v.push_back(Any(a)), ...);
        }
    }
    uint32_t get_length() const { return (uint32_t)v.size(); }
    uint32_t set_length(double n) { v.resize((size_t)n); return (uint32_t)n; }
    template <class... A> uint32_t push(A... a) { (v.push_back(Any(a)), ...); return (uint32_t)v.size(); }
    Any pop() { if (v.empty()) return Any(); Any r = v.back(); v.pop_back(); return r; }
    Any shift() { if (v.empty()) return Any(); Any r = v.front(); v.erase(v.begin()); return r; }
    template <class... A> uint32_t unshift(A... a) { std::vector<Any> t{Any(a)...}; v.insert(v.begin(), t.begin(), t.end()); return (uint32_t)v.size(); }
    template <class S, class D, class... A> Array* splice(S start_, D del_, A... items) {
        double start = toNumber(start_), del = toNumber(del_);
        Array* out = new Array();
        long n = (long)v.size(), s = start < 0 ? std::max(0L, n + (long)start) : std::min(n, (long)start);
        long d = std::max(0L, std::min((long)del, n - s));
        out->v.assign(v.begin() + s, v.begin() + s + d);
        v.erase(v.begin() + s, v.begin() + s + d);
        std::vector<Any> t{Any(items)...};
        v.insert(v.begin() + s, t.begin(), t.end());
        return out;
    }
    Array* slice(double a = 0, double b = 1e9) const {
        Array* out = new Array();
        long n = (long)v.size(), s = a < 0 ? std::max(0L, n + (long)a) : std::min(n, (long)a);
        long e = b < 0 ? std::max(0L, n + (long)b) : std::min(n, (long)b);
        if (e > s) out->v.assign(v.begin() + s, v.begin() + e);
        return out;
    }
    template <class... A> Array* concat(A... other) const {
        Array* out = new Array();
        out->v = v;
        auto add1 = [&](const Any& x) {
            if (x.k == Any::Obj) if (Array* ar = dynamic_cast<Array*>(x.o)) { out->v.insert(out->v.end(), ar->v.begin(), ar->v.end()); return; }
            out->v.push_back(x);
        };
        (add1(Any(other)), ...);
        return out;
    }
    String join(const String& sep = String(",")) const {
        std::string r;
        for (size_t i = 0; i < v.size(); i++) {
            if (i) r += sep.str();
            if (!v[i].isNullish()) r += ::as3::toString(v[i]).str();
        }
        return String(r);
    }
    int32_t indexOf(const Any& x, double from = 0) const {
        for (size_t i = from < 0 ? 0 : (size_t)from; i < v.size(); i++) if (strictEqAny(v[i], x)) return (int32_t)i;
        return -1;
    }
    int32_t lastIndexOf(const Any& x) const {
        for (long i = (long)v.size() - 1; i >= 0; i--) if (strictEqAny(v[i], x)) return (int32_t)i;
        return -1;
    }
    Array* reverse() { std::reverse(v.begin(), v.end()); return this; }
    Array* sort(const Any& cmp = Any(), double flags = 0);
    Array* sortOn(const Any& name, double flags = 0);
    Array* sort(double flags) { return sort(Any(), flags); }
    void forEach(const Function& f) { auto t = v; for (size_t i = 0; i < t.size(); i++) f.call({t[i], Any((double)i), Any(this)}); }
    Array* filter(const Function& f) { Array* o = new Array(); for (size_t i = 0; i < v.size(); i++) if (truthy(f.call({v[i], Any((double)i), Any(this)}))) o->v.push_back(v[i]); return o; }
    Array* map(const Function& f) { Array* o = new Array(); for (size_t i = 0; i < v.size(); i++) o->v.push_back(f.call({v[i], Any((double)i), Any(this)})); return o; }
    bool some(const Function& f) { for (size_t i = 0; i < v.size(); i++) if (truthy(f.call({v[i], Any((double)i), Any(this)}))) return true; return false; }
    bool every(const Function& f) { for (size_t i = 0; i < v.size(); i++) if (!truthy(f.call({v[i], Any((double)i), Any(this)}))) return false; return true; }
    String toString() { return join(); }
    Any __call(const std::string& name, const std::vector<Any>& a) override;
    Any __get(const std::string& name) override;
    String __className() const override { return String("Array"); }
    enum { CASEINSENSITIVE = 1, DESCENDING = 2, UNIQUESORT = 4, RETURNINDEXEDARRAY = 8, NUMERIC = 16 };
};

// ============================================================ Vector<T>
// Igualdad estricta entre un elemento y un valor de cualquier tipo (referencias: misma identidad aunque el tipo estatico difiera).
template <class A, class B> bool sameVal(const A& a, const B& b) {
    if constexpr (std::is_pointer<A>::value && std::is_pointer<B>::value) return (const void*)static_cast<const Object*>(a) == (const void*)static_cast<const Object*>(b);
    else if constexpr (std::is_same<A, B>::value) return a == b;
    else if constexpr (std::is_same<B, Any>::value) return strictEqAny(Any(a), b);
    else if constexpr (std::is_arithmetic<A>::value && std::is_arithmetic<B>::value) return (double)a == (double)b;
    else return strictEqAny(Any(a), Any(b));
}
// Conversión al insertar en un Vector.<T>: AS3 comprueba el tipo en ejecución (TypeError si no encaja).
template <class T, class V> T vcast(const V& x) {
    if constexpr (std::is_same<V, Any>::value) return fromAny<T>(x);
    else if constexpr (std::is_pointer<T>::value && std::is_pointer<V>::value && !std::is_convertible<V, T>::value) {
        T r = dynamic_cast<T>((Object*)x);
        if (x && !r) throw Thrown(Any(String("TypeError: Error #1034: Fallo de conversión de tipo")));
        return r;
    } else return (T)x;
}
// Acceso generico a un Vector.<T> (guardado): tipo de elemento, longitud, lectura y escritura como Any
struct VectorBase : Object {
    enum Kind { KObj = 0, KInt = 1, KUint = 2, KNum = 3, KStr = 4, KBool = 5 };
    virtual int __vecKind() const { return KObj; }
    virtual size_t __vecLen() const { return 0; }
    virtual Any __vecGet(size_t) const { return Any(); }
    virtual void __vecPush(const Any&) {}
};
template <class T> struct Vector : VectorBase {
    std::vector<T> v;
    int __vecKind() const override {
        if constexpr (std::is_same<T, int32_t>::value) return KInt;
        else if constexpr (std::is_same<T, uint32_t>::value) return KUint;
        else if constexpr (std::is_same<T, double>::value) return KNum;
        else if constexpr (std::is_same<T, String>::value) return KStr;
        else if constexpr (std::is_same<T, bool>::value) return KBool;
        else return KObj;
    }
    size_t __vecLen() const override { return v.size(); }
    Any __vecGet(size_t i) const override {
        if constexpr (std::is_pointer<T>::value) {
            using E = std::remove_pointer_t<T>;
            if constexpr (std::is_base_of<Object, E>::value) return Any((Object*)v[i]);
            else if constexpr (std::is_polymorphic<E>::value) return Any(v[i] ? dynamic_cast<Object*>(v[i]) : (Object*)nullptr);
            else return Any();
        } else if constexpr (std::is_same<T, bool>::value) return Any((bool)v[i]);
        else return Any(v[i]);
    }
    void __vecPush(const Any& a) override { v.push_back(fromAny<T>(a)); }
    void __trace() override { Object::__trace(); if constexpr (!std::is_arithmetic<T>::value) for (const auto& x : v) gc::mark(x); }
    bool fixed = false;
    void __construct(uint32_t len = 0, bool fixed_ = false) { v.assign(len, T()); fixed = fixed_; }
    uint32_t get_length() const { return (uint32_t)v.size(); }
    uint32_t set_length(double n) { v.resize((size_t)n, T()); return (uint32_t)n; }
    bool get_fixed() const { return fixed; }
    bool set_fixed(bool f) { fixed = f; return f; }
    template <class... A> uint32_t push(A... a) { (v.push_back(vcast<T>(a)), ...); return (uint32_t)v.size(); }
    T pop() { if (v.empty()) return T(); T r = v.back(); v.pop_back(); return r; }
    T shift() { if (v.empty()) return T(); T r = v.front(); v.erase(v.begin()); return r; }
    template <class... A> uint32_t unshift(A... a) { std::vector<T> t{vcast<T>(a)...}; v.insert(v.begin(), t.begin(), t.end()); return (uint32_t)v.size(); }
    template <class S, class D, class... A> Vector* splice(S start_, D del_, A... items) {
        double start = toNumber(start_), del = toNumber(del_);
        Vector* out = new Vector();
        long n = (long)v.size(), s = start < 0 ? std::max(0L, n + (long)start) : std::min(n, (long)start);
        long d = std::max(0L, std::min((long)del, n - s));
        out->v.assign(v.begin() + s, v.begin() + s + d);
        v.erase(v.begin() + s, v.begin() + s + d);
        std::vector<T> t{vcast<T>(items)...};
        v.insert(v.begin() + s, t.begin(), t.end());
        return out;
    }
    Vector* slice(double a = 0, double b = 1e9) const {
        Vector* out = new Vector();
        long n = (long)v.size(), s = a < 0 ? std::max(0L, n + (long)a) : std::min(n, (long)a);
        long e = b < 0 ? std::max(0L, n + (long)b) : std::min(n, (long)b);
        if (e > s) out->v.assign(v.begin() + s, v.begin() + e);
        return out;
    }
    template <class... A> Vector* concat(A*... o) const {
        Vector* r = new Vector();
        r->v = v;
        ((void)[&](auto* p) { for (auto& x : p->v) r->v.push_back(vcast<T>(x)); }(o), ...);
        return r;
    }
    // AS3: el argumento se convierte primero al tipo del vector (p. ej. un uint 0xFFCC0000 buscado en Vector.<int> = -3407872)
    template <class X> static auto asElem(const X& x) {
        if constexpr (std::is_arithmetic<T>::value && std::is_arithmetic<X>::value) return fromAny<T>(Any((double)x));
        else return x;
    }
    template <class X> int32_t indexOf(const X& x, double from = 0) const { auto xe = asElem(x); for (size_t i = from < 0 ? 0 : (size_t)from; i < v.size(); i++) if (sameVal(v[i], xe)) return (int32_t)i; return -1; }
    template <class X> int32_t lastIndexOf(const X& x) const { auto xe = asElem(x); for (long i = (long)v.size() - 1; i >= 0; i--) if (sameVal(v[i], xe)) return (int32_t)i; return -1; }
    Vector* reverse() { std::reverse(v.begin(), v.end()); return this; }
    String join(const String& sep = String(",")) const { std::string r; for (size_t i = 0; i < v.size(); i++) { if (i) r += sep.str(); r += ::as3::toString(v[i]).str(); } return String(r); }
    Vector* sort(const Any& cmp = Any());
    String toString() { return join(); }
    String __className() const override { return String("Vector"); }
    // Acceso dinamico (el juego hace datos[nombre].push(x) / .length sobre un valor sin tipo)
    Any __get(const std::string& n) override {
        if (n == "length") return Any((double)v.size());
        if (n == "fixed") return Any(fixed);
        if (!n.empty() && std::isdigit((unsigned char)n[0])) {  // vec[i] por la via dinamica
            size_t i = (size_t)std::strtoull(n.c_str(), nullptr, 10);
            if (i >= v.size()) throw Thrown(Any(String("RangeError: Error #1125: indice " + n + " fuera del vector de tamano " + std::to_string(v.size()))));
            if constexpr (std::is_same<T, bool>::value) return Any((bool)v[i]); else return Any(v[i]);
        }
        return Object::__get(n);
    }
    void __set(const std::string& n, const Any& val) override {
        if (n == "length") { v.resize((size_t)toNumber(val), T()); return; }
        if (!n.empty() && std::isdigit((unsigned char)n[0])) { setAt(this, (double)std::strtoull(n.c_str(), nullptr, 10), val); return; }
        Object::__set(n, val);
    }
    Any __call(const std::string& n, const std::vector<Any>& a) override {
        if (n == "push") { for (auto& x : a) v.push_back(fromAny<T>(x)); return Any((double)v.size()); }
        if (n == "pop") { if (v.empty()) return Any(); T r = v.back(); v.pop_back(); return Any(r); }
        if (n == "shift") { if (v.empty()) return Any(); T r = v.front(); v.erase(v.begin()); return Any(r); }
        if (n == "unshift") { std::vector<T> t; for (auto& x : a) t.push_back(fromAny<T>(x)); v.insert(v.begin(), t.begin(), t.end()); return Any((double)v.size()); }
        if (n == "indexOf" && !a.empty()) { T x = fromAny<T>(a[0]); return Any((double)indexOf(x)); }
        if (n == "join") return Any(a.empty() ? join() : join(::as3::toString(a[0])));
        if (n == "reverse") { reverse(); return Any(this); }
        return Object::__call(n, a);
    }
};

// ============================================================ Dictionary
struct AnyHash {
    size_t operator()(const Any& a) const {
        switch (a.k) {
            case Any::Num: return std::hash<double>()(a.n);
            case Any::Bool: return a.b;
            case Any::Str: return std::hash<std::string>()(a.s.str());
            case Any::Obj: return std::hash<const void*>()(a.o);
            default: return 0;
        }
    }
};
struct AnyEq {
    bool operator()(const Any& a, const Any& b) const { return strictEqAny(a, b); }
};
struct Dictionary : Object {
    std::unordered_map<Any, Any, AnyHash, AnyEq> m;
    std::vector<Any> order;
    void __trace() override { Object::__trace(); for (auto& kv : m) { gc::mark(kv.first); gc::mark(kv.second); } gc::mark(order); }  // las claves salen en orden de inserción (AS3 no lo garantiza; así es determinista)
    void __construct(bool = false) {}
    // AS3: las claves primitivas se comparan por su texto (dict[0] y dict["0"] son la misma entrada); los objetos por identidad
    static Any canon(const Any& k) {
        if (k.k == Any::Num || k.k == Any::Bool) return Any(::as3::toString(k));
        return k;
    }
    Any get(const Any& k) const { auto it = m.find(canon(k)); return it == m.end() ? Any() : it->second; }
    void set(const Any& k, const Any& v) { Any c = canon(k); auto it = m.find(c); if (it == m.end()) { order.push_back(k); m.emplace(c, v); } else it->second = v; }
    bool has(const Any& k) const { return m.count(canon(k)) > 0; }
    bool remove(const Any& k) {
        Any c = canon(k);
        if (!m.erase(c)) return false;
        for (size_t i = 0; i < order.size(); i++) if (strictEqAny(canon(order[i]), c)) { order.erase(order.begin() + i); break; }
        return true;
    }
    String __className() const override { return String("Dictionary"); }
};

// ============================================================ Object dinámico (propiedades)
inline Any Object::__get(const std::string& n) { if (__props) { auto it = __props->find(n); if (it != __props->end()) return it->second; } return Any(); }
inline void Object::__set(const std::string& n, const Any& v) { if (!__props) __props = new std::unordered_map<std::string, Any>(); (*__props)[n] = v; }
inline bool Object::__has(const std::string& n) { return __props && __props->count(n); }
inline void Object::__trace() {
    if (__props) for (auto& kv : *__props) gc::mark(kv.second);
}
inline Any Object::__call(const std::string& n, const std::vector<Any>& a) {
    // metodos de Object.prototype llamados de forma dinamica (p. ej. param1.hasOwnProperty("aCurrentStates"))
    if (n == "hasOwnProperty") return Any(!a.empty() && __has(::as3::toString(a[0]).str()));
    if (n == "toString") return Any(toString());
    Any f = __get(n);
    if (f.k == Any::Func) return f.fn.call(a);
    return Any();
}
inline String toString(Object* o) { return o ? o->toString() : String("null"); }

inline std::vector<std::string> propKeys(Object* o) {
    std::vector<std::string> r;
    if (o && o->__props) for (auto& kv : *o->__props) r.push_back(kv.first);
    std::sort(r.begin(), r.end());
    return r;
}

// ============================================================ RegExp
struct RegExp : Object {
    std::regex re;
    std::string source;
    bool global = false, ignoreCase = false;
    String __className() const override { return String("RegExp"); }
};
inline RegExp* regex(const char* pat, const char* flags) {
    RegExp* r = new RegExp();
    r->source = pat;
    for (const char* f = flags; *f; f++) { if (*f == 'g') r->global = true; if (*f == 'i') r->ignoreCase = true; }
    // El traductor crea la expresion en cada uso (como un literal /.../ de AS3); compilarla cuesta mucho, asi que se
    // compila una vez por patron y se copia (std::regex comparte su automata al copiarse)
    static std::unordered_map<std::string, std::regex> cache;
    std::string key = std::string(r->ignoreCase ? "i:" : "-:") + pat;
    auto it = cache.find(key);
    if (it == cache.end()) {
        auto fl = std::regex::ECMAScript;
        if (r->ignoreCase) fl |= std::regex::icase;
        it = cache.emplace(key, std::regex(pat, fl)).first;
    }
    r->re = it->second;
    return r;
}

// ============================================================ métodos de String (cuerpos)
inline String String::charAt(double i) const { return (i >= 0 && i < length()) ? String(std::string(1, str()[(size_t)i])) : String(""); }
inline double String::charCodeAt(double i) const { return (i >= 0 && i < length()) ? (unsigned char)str()[(size_t)i] : NaN; }
inline int32_t String::indexOf(const String& s, double from) const {
    size_t p = str().find(s.str(), from < 0 ? 0 : (size_t)from);
    return p == std::string::npos ? -1 : (int32_t)p;
}
inline int32_t String::lastIndexOf(const String& s, double from) const {
    size_t p = str().rfind(s.str(), from < 0 ? 0 : (size_t)from);
    return p == std::string::npos ? -1 : (int32_t)p;
}
inline int32_t String::indexOf(const Any& s, double from) const { return indexOf(::as3::toString(s), from); }
inline int32_t String::lastIndexOf(const Any& s, double from) const { return lastIndexOf(::as3::toString(s), from); }
inline String String::substr(double start, double len) const {
    double n = length();
    if (start < 0) start = std::max(0.0, n + start);
    if (start >= n || len <= 0) return String("");
    return String(str().substr((size_t)start, (size_t)std::min(len, n - start)));
}
inline String String::substring(double a, double b) const {
    double n = length();
    if (std::isnan(a) || a < 0) a = 0;
    if (std::isnan(b) || b < 0) b = 0;
    a = std::min(a, n); b = std::min(b, n);
    if (a > b) std::swap(a, b);
    return String(str().substr((size_t)a, (size_t)(b - a)));
}
inline String String::slice(double a, double b) const {
    double n = length();
    if (a < 0) a = std::max(0.0, n + a);
    if (b < 0) b = std::max(0.0, n + b);
    a = std::min(a, n); b = std::min(b, n);
    return b > a ? String(str().substr((size_t)a, (size_t)(b - a))) : String("");
}
inline String String::toLowerCase() const { std::string s = str(); for (auto& c : s) c = (char)std::tolower((unsigned char)c); return String(s); }
inline String String::toUpperCase() const { std::string s = str(); for (auto& c : s) c = (char)std::toupper((unsigned char)c); return String(s); }
inline Array* String::split(const String& sep, double limit) const {
    Array* a = new Array();
    const std::string& s = str();
    if (sep.isNull()) { a->v.push_back(Any(*this)); return a; }
    if (sep.str().empty()) { for (char c : s) { if (a->v.size() >= limit) break; a->v.push_back(Any(String(std::string(1, c)))); } return a; }
    size_t pos = 0;
    while (a->v.size() < limit) {
        size_t q = s.find(sep.str(), pos);
        if (q == std::string::npos) { a->v.push_back(Any(String(s.substr(pos)))); break; }
        a->v.push_back(Any(String(s.substr(pos, q - pos))));
        pos = q + sep.str().size();
    }
    return a;
}
inline Array* String::split(RegExp* re, double limit) const {
    Array* a = new Array();
    const std::string& s = str();
    auto it = std::sregex_iterator(s.begin(), s.end(), re->re), end = std::sregex_iterator();
    size_t last = 0;
    for (; it != end && a->v.size() < limit; ++it) {
        a->v.push_back(Any(String(s.substr(last, it->position() - last))));
        last = it->position() + it->length();
    }
    if (a->v.size() < limit) a->v.push_back(Any(String(s.substr(last))));
    return a;
}
inline String String::replace(const String& pat, const String& rep) const {
    size_t p = str().find(pat.str());
    if (p == std::string::npos) return *this;
    return String(str().substr(0, p) + rep.str() + str().substr(p + pat.str().size()));
}
inline String String::replace(RegExp* re, const String& rep) const {
    return String(std::regex_replace(str(), re->re, rep.str(),
                                     re->global ? std::regex_constants::format_default : std::regex_constants::format_first_only));
}
inline int32_t String::search(RegExp* re) const {
    std::smatch m;
    return std::regex_search(str(), m, re->re) ? (int32_t)m.position() : -1;
}
inline Array* String::match(RegExp* re) const {
    const std::string& s = str();
    std::smatch m;
    if (!re->global) {
        if (!std::regex_search(s, m, re->re)) return nullptr;
        Array* a = new Array();
        for (auto& g : m) a->v.push_back(Any(String(g.str())));
        return a;
    }
    Array* a = new Array();
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re->re); it != std::sregex_iterator(); ++it) a->v.push_back(Any(String(it->str())));
    return a->v.empty() ? nullptr : a;
}

// ============================================================ toString(Any) y acceso dinámico
inline String toString(const Any& a) {
    switch (a.k) {
        case Any::Undef: return String("undefined");
        case Any::Null: return String("null");
        case Any::Num: return String(numToString(a.n));
        case Any::Bool: return String(a.b ? "true" : "false");
        case Any::Str: return a.s;
        case Any::Obj: return a.o->toString();
        case Any::Func: return String("function Function() {}");
    }
    return String("");
}

inline Any Array::__get(const std::string& name) {
    if (name == "length") return Any((double)v.size());
    return Object::__get(name);
}
inline Any Array::__call(const std::string& n, const std::vector<Any>& a) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : Any(); };
    if (n == "push") { for (auto& x : a) v.push_back(x); return Any((double)v.size()); }
    if (n == "pop") return pop();
    if (n == "shift") return shift();
    if (n == "join") return Any(a.empty() ? join() : join(::as3::toString(a[0])));
    if (n == "indexOf") return Any((double)indexOf(arg(0)));
    if (n == "slice") return Any(slice(a.empty() ? 0 : toNumber(a[0]), a.size() < 2 ? 1e9 : toNumber(a[1])));
    if (n == "reverse") return Any(reverse());
    return Object::__call(n, a);
}

inline Array* Array::sort(const Any& cmp, double flags) {
    bool desc = (int)flags & DESCENDING, num = (int)flags & NUMERIC, ci = (int)flags & CASEINSENSITIVE;
    auto less = [&](const Any& x, const Any& y) {
        if (cmp.k == Any::Func) return toNumber(cmp.fn.call({x, y})) < 0;
        if (num) return toNumber(x) < toNumber(y);
        std::string a = ::as3::toString(x).str(), b = ::as3::toString(y).str();
        if (ci) { for (auto& c : a) c = (char)std::tolower((unsigned char)c); for (auto& c : b) c = (char)std::tolower((unsigned char)c); }
        return a < b;
    };
    // AS3: los elementos undefined van al final y nunca se pasan a la funcion de comparacion
    auto mid = std::stable_partition(v.begin(), v.end(), [](const Any& x) { return !x.isUndefined(); });
    std::stable_sort(v.begin(), mid, [&](const Any& x, const Any& y) { return desc ? less(y, x) : less(x, y); });
    return this;
}
inline Array* Array::sortOn(const Any& name, double flags) {
    std::string key = ::as3::toString(name).str();
    bool desc = (int)flags & DESCENDING, num = (int)flags & NUMERIC;
    std::stable_sort(v.begin(), v.end(), [&](const Any& x, const Any& y) {
        Any a = x.k == Any::Obj ? x.o->__get(key) : Any(), b = y.k == Any::Obj ? y.o->__get(key) : Any();
        bool l = num ? toNumber(a) < toNumber(b) : ::as3::toString(a).str() < ::as3::toString(b).str();
        bool g = num ? toNumber(b) < toNumber(a) : ::as3::toString(b).str() < ::as3::toString(a).str();
        return desc ? g : l;
    });
    return this;
}
template <class T> Vector<T>* Vector<T>::sort(const Any& cmp) {
    std::stable_sort(v.begin(), v.end(), [&](const T& x, const T& y) {
        if (cmp.k == Any::Func) return toNumber(cmp.fn.call({Any(x), Any(y)})) < 0;
        return x < y;
    });
    return this;
}

// ----- conversión Any -> tipos concretos
template <class T> T* cast(const Any& a) {
    if (a.k != Any::Obj) return nullptr;
    return dynamic_cast<T*>(a.o);
}
template <class T> T* cast(Object* o) { return dynamic_cast<T*>(o); }
template <class T, class U, class = std::enable_if_t<!std::is_same<U, Any>::value && !std::is_same<U, Object>::value>>
T* cast(U* o) { return dynamic_cast<T*>((Object*)o); }
inline Function castFn(const Any& a) { return a.k == Any::Func ? a.fn : Function(); }

template <class T> bool is(const Any& a) {
    if constexpr (std::is_same<T, double>::value || std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value) return a.k == Any::Num;
    else if constexpr (std::is_same<T, bool>::value) return a.k == Any::Bool;
    else if constexpr (std::is_same<T, String>::value) return a.k == Any::Str;
    else if constexpr (std::is_same<T, Function>::value) return a.k == Any::Func;
    else return a.k == Any::Obj && dynamic_cast<T*>(a.o) != nullptr;
}
template <class T, class U> bool is(U* p) { return p && dynamic_cast<T*>((Object*)p) != nullptr; }
template <class T> auto as(const Any& a) {
    if constexpr (std::is_same<T, double>::value || std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value || std::is_same<T, bool>::value || std::is_same<T, String>::value)
        return is<T>(a) ? a : Any(nullptr);
    else return cast<T>(a);
}
template <class T, class U> T* as(U* p) { return cast<T>(p); }
inline bool instanceOf(const Any& a, const Any&) { return a.k == Any::Obj; }

// ----- acceso por índice / clave y propiedades dinámicas
inline Any at(Array* a, double i) { return (i >= 0 && i < a->v.size()) ? a->v[(size_t)i] : Any(); }
template <class T> T at(Vector<T>* a, double i) {
    if (!(i >= 0 && i < a->v.size())) throw Thrown(Any(String("RangeError: Error #1125: indice " + numToString(i) + " fuera del vector de tamano " + std::to_string(a->v.size()))));
    return a->v[(size_t)i];
}
template <class T> T at(Vector<T>* a, const Any& k) { return at(a, toNumber(k)); }
inline Any at(Dictionary* d, const Any& k) { return d->get(k); }
inline Any at(Object* o, const Any& k);
inline Any at(const Any& a, const Any& k);
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value && !std::is_same<T, Array>::value && !std::is_same<T, Dictionary>::value>>
Any at(T* o, const Any& k) { return at(static_cast<Object*>(o), k); }
bool byteArrayAt(Object* o, const Any& k, Any& out);
bool byteArraySet(Object* o, const Any& k, const Any& v);
bool xmlListAt(Object* o, const Any& k, Any& out);
bool xmlListValues(Object* o, std::vector<Any>& out);
inline Any at(Array* a, const Any& k) { return k.k == Any::Num ? at(a, k.n) : a->__get(toString(k).str()); }
inline Any at(Object* o, const Any& k) {
    if (!o) throw Thrown(Any(String("TypeError: Error #1009")));
    { Any out; if (xmlListAt(o, k, out)) return out; if (byteArrayAt(o, k, out)) return out; }
    if (Array* a = dynamic_cast<Array*>(o)) return at(a, k);
    if (Dictionary* d = dynamic_cast<Dictionary*>(o)) return d->get(k);
    return o->__get(toString(k).str());
}
inline Any at(const Any& a, const Any& k) {
    if (a.k != Any::Obj) throw Thrown(Any(String("TypeError: Error #1010 (índice sobre valor no objeto)")));
    return at(a.o, k);
}
inline void setAt(Array* a, double i, const Any& v) { if (i >= 0) { if (i >= a->v.size()) a->v.resize((size_t)i + 1); a->v[(size_t)i] = v; } }
template <class T, class V> void setAt(Vector<T>* a, double i, const V& v);
inline void setAt(Dictionary* d, const Any& k, const Any& v) { d->set(k, v); }
inline void setAt(Object* o, const Any& k, const Any& v) {
    if (!o) throw Thrown(Any(String("TypeError: Error #1009")));
    if (byteArraySet(o, k, v)) return;
    if (Array* a = dynamic_cast<Array*>(o)) { if (k.k == Any::Num) setAt(a, k.n, v); else a->__set(toString(k).str(), v); return; }
    if (Dictionary* d = dynamic_cast<Dictionary*>(o)) { d->set(k, v); return; }
    o->__set(toString(k).str(), v);
}
inline void setAt(const Any& a, const Any& k, const Any& v) { if (a.k == Any::Obj) setAt(a.o, k, v); else throw Thrown(Any(String("TypeError: Error #1010"))); }
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value && !std::is_same<T, Array>::value && !std::is_same<T, Dictionary>::value>>
void setAt(T* o, const Any& k, const Any& v) { setAt(static_cast<Object*>(o), k, v); }

template <class T> T fromAny(const Any& a);
template <class T, class V> void setAt(Vector<T>* a, double i, const V& v) {
    if (!(i >= 0 && i <= a->v.size() && (i < a->v.size() || !a->fixed))) throw Thrown(Any(String("RangeError: Error #1125: escritura en indice " + numToString(i) + " del vector de tamano " + std::to_string(a->v.size()) + (a->fixed ? " (fijo)" : ""))));
    if (i == a->v.size()) a->v.push_back(T());
    if constexpr (std::is_same<V, Any>::value) a->v[(size_t)i] = fromAny<T>(v);
    else a->v[(size_t)i] = (T)v;
}

inline Any dyn(const Any& a, const char* name) {
    if (a.k == Any::Obj) return a.o->__get(name);
    if (a.k == Any::Str && std::strcmp(name, "length") == 0) return Any((double)a.s.length());
    if (a.isNullish()) throw Thrown(Any(String("TypeError: Error #1009: no se puede acceder a una propiedad de un objeto null")));
    return Any();
}
inline Any dyn(Object* o, const char* name) { return dyn(Any(o), name); }
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>> Any dyn(T* o, const char* name) { return dyn(Any((Object*)o), name); }
inline void setDyn(const Any& a, const char* name, const Any& v) {
    if (a.k == Any::Obj) a.o->__set(name, v);
    else throw Thrown(Any(String("TypeError: Error #1009")));
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>> void setDyn(T* o, const char* name, const Any& v) { setDyn(Any((Object*)o), name, v); }
inline Any callDyn(const Any& a, const char* name, const std::vector<Any>& args) {
    if (a.k == Any::Obj) return a.o->__call(name, args);
    if (a.k == Any::Str) {
        const String& s = a.s;
        std::string n = name;
        auto num = [&](size_t i, double d) { return i < args.size() ? toNumber(args[i]) : d; };
        if (n == "charAt") return Any(s.charAt(num(0, 0)));
        if (n == "indexOf") return Any((double)s.indexOf(toString(args.at(0)), num(1, 0)));
        if (n == "substr") return Any(s.substr(num(0, 0), num(1, 1e9)));
        if (n == "substring") return Any(s.substring(num(0, 0), num(1, 1e9)));
        if (n == "toLowerCase") return Any(s.toLowerCase());
        if (n == "toUpperCase") return Any(s.toUpperCase());
        if (n == "split") return Any(s.split(toString(args.at(0))));
        if (n == "toString") return a;
    }
    if (a.k == Any::Num || a.k == Any::Bool) {
        std::string n = name;
        double x = a.k == Any::Num ? a.n : (a.b ? 1 : 0);
        auto arg = [&](size_t i, double d) { return i < args.size() ? toNumber(args[i]) : d; };
        if (n == "toString") return a.k == Any::Bool ? Any(num_toString(a.b)) : Any(num_toString(x, arg(0, 10)));
        if (n == "toFixed") return Any(num_toFixed(x, arg(0, 0)));
        if (n == "toPrecision") return Any(num_toPrecision(x, arg(0, 0)));
        if (n == "valueOf") return a;
    }
    if (a.isNullish()) throw Thrown(Any(String(std::string("TypeError: Error #1006/1009: llamada a ") + name + " sobre null")));
    return Any();
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
Any callDyn(T* o, const char* name, const std::vector<Any>& args) { return callDyn(Any((Object*)o), name, args); }
inline Any invoke(const Function& f, const std::vector<Any>& a) { return f.call(a); }
inline Any invoke(const Any& f, const std::vector<Any>& a) {
    if (f.k != Any::Func) throw Thrown(Any(String("TypeError: Error #1006: no es una función")));
    return f.fn.call(a);
}
inline bool hasKey(const Any& obj, const Any& key) {
    if (obj.k != Any::Obj) return false;
    if (Dictionary* d = dynamic_cast<Dictionary*>(obj.o)) return d->has(key);
    if (Array* a = dynamic_cast<Array*>(obj.o)) { if (key.k == Any::Num) return key.n >= 0 && key.n < a->v.size(); }
    return obj.o->__has(toString(key).str());
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
bool hasKey(T* o, const Any& key) { return hasKey(Any((Object*)o), key); }
inline bool deleteKey(const Any& obj, const Any& key) {
    if (obj.k != Any::Obj) return false;
    if (Dictionary* d = dynamic_cast<Dictionary*>(obj.o)) return d->remove(key);
    if (obj.o->__props) return obj.o->__props->erase(toString(key).str()) > 0;
    return false;
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
bool deleteKey(T* o, const Any& key) { return deleteKey(Any((Object*)o), key); }

inline std::vector<Any> eachKey(const Any& obj) {
    std::vector<Any> r;
    if (obj.k != Any::Obj) return r;
    if (Dictionary* d = dynamic_cast<Dictionary*>(obj.o)) return d->order;
    // AS3: for-in / for-each sobre un Array solo recorre las posiciones que existen; los huecos se guardan aqui como
    // undefined y se saltan (un undefined asignado a mano tambien se salta: caso raro y sin efecto en el juego)
    if (Array* a = dynamic_cast<Array*>(obj.o)) { for (size_t i = 0; i < a->v.size(); i++) if (!a->v[i].isUndefined()) r.push_back(Any((double)i)); return r; }
    for (auto& k : propKeys(obj.o)) r.push_back(Any(String(k)));
    return r;
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
std::vector<Any> eachKey(T* o) { return eachKey(Any((Object*)o)); }
inline std::vector<Any> eachValue(const Any& obj) {
    std::vector<Any> r;
    if (obj.k != Any::Obj) return r;
    if (Array* a = dynamic_cast<Array*>(obj.o)) { for (auto& x : a->v) if (!x.isUndefined()) r.push_back(x); return r; }
    if (xmlListValues(obj.o, r)) return r;
    for (auto& k : eachKey(obj)) r.push_back(at(obj, k));
    return r;
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
std::vector<Any> eachValue(T* o) { return eachValue(Any((Object*)o)); }
template <class T> std::vector<Any> eachValue(Vector<T>* v) { std::vector<Any> r; for (auto& x : v->v) r.push_back(Any(x)); return r; }
template <class T> std::vector<Any> eachKey(Vector<T>* v) { std::vector<Any> r; for (size_t i = 0; i < v->v.size(); i++) r.push_back(Any((double)i)); return r; }

// ----- construcción de literales
inline Array* arrayOf(std::initializer_list<Any> l) { Array* a = new Array(); a->v.assign(l.begin(), l.end()); return a; }
inline Object* objectOf(std::initializer_list<std::pair<const char*, Any>> l) {
    Object* o = new Object();
    for (auto& kv : l) o->__set(kv.first, kv.second);
    return o;
}
template <class T, class V> T asPrim(const V& v) {
    Any a(v);
    if constexpr (std::is_same<T, as3::String>::value) return a.k == Any::Str ? a.s : String();
    else if constexpr (std::is_same<T, bool>::value) return a.k == Any::Bool ? a.b : false;
    else return a.k == Any::Num ? fromAny<T>(a) : T(0);
}
template <class T> T fromAny(const Any& a) {
    if constexpr (std::is_same<T, double>::value) return toNumber(a);
    else if constexpr (std::is_same<T, int32_t>::value) return toInt(a);
    else if constexpr (std::is_same<T, uint32_t>::value) return toUint(a);
    else if constexpr (std::is_same<T, bool>::value) return truthy(a);
    else if constexpr (std::is_same<T, String>::value) return a.isNullish() ? String() : toString(a);
    else if constexpr (std::is_same<T, Any>::value) return a;
    else if constexpr (std::is_same<T, Function>::value) return a.k == Any::Func ? a.fn : Function();
    else if constexpr (std::is_pointer<T>::value) return cast<std::remove_pointer_t<T>>(a);
    else return T();
}
template <class T> Vector<T>* vectorOf(std::initializer_list<T> l) { Vector<T>* v = new Vector<T>(); v->v.assign(l.begin(), l.end()); return v; }
template <class T> Vector<T>* toVector(const Any& a) {
    Vector<T>* v = new Vector<T>();
    if (a.k == Any::Obj) if (Array* ar = dynamic_cast<Array*>(a.o)) for (auto& x : ar->v) v->v.push_back(fromAny<T>(x));
    return v;
}
template <class T> Vector<T>* toVector(Vector<T>* a) { return a; }
template <class T> Vector<T>* toVector(Array* a) { return toVector<T>(Any(a)); }
inline Array* restArgs(const std::vector<Any>& a, size_t from) { Array* r = new Array(); for (size_t i = from; i < a.size(); i++) r->v.push_back(a[i]); return r; }

// ----- métodos y funciones como valores
template <class C, class R, class... A, size_t... I>
Any callMember(C* o, R (C::*m)(A...), const std::vector<Any>& a, std::index_sequence<I...>) {
    if constexpr (std::is_void<R>::value) { (o->*m)(fromAny<std::decay_t<A>>(I < a.size() ? a[I] : Any())...); return Any(); }
    else return Any((o->*m)(fromAny<std::decay_t<A>>(I < a.size() ? a[I] : Any())...));
}
template <class C, class D, class R, class... A> Function bind(D* o, R (C::*m)(A...)) {
    C* c = static_cast<C*>(o);
    Function f([c, m](const std::vector<Any>& a) { return callMember(c, m, a, std::index_sequence_for<A...>{}); });
    std::string id((const char*)&c, sizeof c);
    id.append((const char*)&m, sizeof m);
    f.setId(std::move(id));
    if constexpr (std::is_base_of<Object, C>::value) f.setTarget(static_cast<Object*>(c));
    else f.setTarget(dynamic_cast<Object*>(c));
    return f;
}
// Llamada a un metodo por nombre (reflexion): convierte los argumentos Any a los tipos del metodo
template <class C, class D, class R, class... A> Any invokeMember(D* o, R (C::*m)(A...), const std::vector<Any>& a) {
    return callMember(static_cast<C*>(o), m, a, std::index_sequence_for<A...>{});
}
template <class R, class... A, size_t... I>
Any callFree(R (*f)(A...), const std::vector<Any>& a, std::index_sequence<I...>) {
    if constexpr (std::is_void<R>::value) { f(fromAny<std::decay_t<A>>(I < a.size() ? a[I] : Any())...); return Any(); }
    else return Any(f(fromAny<std::decay_t<A>>(I < a.size() ? a[I] : Any())...));
}
template <class R, class... A> Function bindStatic(R (*f)(A...)) {
    Function fn([f](const std::vector<Any>& a) { return callFree(f, a, std::index_sequence_for<A...>{}); });
    fn.setId(std::string((const char*)&f, sizeof f));
    return fn;
}

// ----- clases como valores (para `new` dinámico, getDefinitionByName, etc.)
template <class T, class = void> struct HasCtor0 : std::false_type {};
template <class T> struct HasCtor0<T, std::void_t<decltype(std::declval<T*>()->__construct())>> : std::true_type {};
// Un valor de tipo Class (p. ej. un parametro `param1:Class` o `[Embed] var ImgX:Class`) sabe construir su clase.
template <class T> Class* classOf() {
    static Class* c = [] {
        Class* k = new Class();
        k->name = String(typeid(T).name());
        k->test = [](Object* o) { return dynamic_cast<T*>(o) != nullptr; };
        if constexpr (HasCtor0<T>::value && !std::is_abstract<T>::value) k->ctor = [](const std::vector<Any>&) -> Object* { return make<T>(); };
        return k;
    }();
    return c;
}
inline bool isDyn(const Any& a, const Any& cls) {
    Class* c = cls.k == Any::Obj ? dynamic_cast<Class*>(cls.o) : nullptr;
    return c && c->test && a.k == Any::Obj && c->test(a.o);
}
template <class T> bool isDyn(T* o, const Any& cls) { return o && isDyn(Any((Object*)o), cls); }
inline Any newDynamic(const Any& cls, const std::vector<Any>& args) {
    Class* c = cls.k == Any::Obj ? dynamic_cast<Class*>(cls.o) : nullptr;
    if (!c || !c->ctor) throw Thrown(Any(String("TypeError: Error #1007: se ha intentado crear una instancia de algo que no es un constructor")));
    return Any(c->ctor(args));
}

// ============================================================ Math
struct Math {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr double E = 2.71828182845904523536;
    static constexpr double SQRT2 = 1.41421356237309504880;
    template <class T> static double abs(T x) { return std::fabs(toNumber(x)); }
    template <class T> static double floor(T x) { return std::floor(toNumber(x)); }
    template <class T> static double ceil(T x) { return std::ceil(toNumber(x)); }
    template <class T> static double round(T x) { double d = toNumber(x); return std::floor(d + 0.5); }
    template <class T> static double sqrt(T x) { return std::sqrt(toNumber(x)); }
    template <class T> static double sin(T x) { return std::sin(toNumber(x)); }
    template <class T> static double cos(T x) { return std::cos(toNumber(x)); }
    template <class T> static double tan(T x) { return std::tan(toNumber(x)); }
    template <class T> static double atan(T x) { return std::atan(toNumber(x)); }
    template <class T> static double asin(T x) { return std::asin(toNumber(x)); }
    template <class T> static double acos(T x) { return std::acos(toNumber(x)); }
    template <class T> static double log(T x) { return std::log(toNumber(x)); }
    template <class T> static double exp(T x) { return std::exp(toNumber(x)); }
    template <class A, class B> static double atan2(A y, B x) { return std::atan2(toNumber(y), toNumber(x)); }
    template <class A, class B> static double pow(A a, B b) { return std::pow(toNumber(a), toNumber(b)); }
    template <class... A> static double max(A... a) {
        double r = -Infinity;
        ((std::isnan(toNumber(a)) ? (r = NaN) : (std::isnan(r) ? r : (r = std::max(r, toNumber(a))))), ...);
        return r;
    }
    template <class... A> static double min(A... a) {
        double r = Infinity;
        ((std::isnan(toNumber(a)) ? (r = NaN) : (std::isnan(r) ? r : (r = std::min(r, toNumber(a))))), ...);
        return r;
    }
    static double random() { return (double)std::rand() / ((double)RAND_MAX + 1.0); }
};

// ============================================================ funciones globales (as3::fn_*)
template <class T> double fn_Number(const T& v) { return toNumber(v); }
template <class T> int32_t fn_int(const T& v) { return toInt(v); }
template <class T> uint32_t fn_uint(const T& v) { return toUint(v); }
template <class T> String fn_String(const T& v) { return toString(v); }
template <class T> bool fn_Boolean(const T& v) { return truthy(v); }
template <class T> bool fn_isNaN(const T& v) { return std::isnan(toNumber(v)); }
template <class T> bool fn_isFinite(const T& v) { return std::isfinite(toNumber(v)); }
inline double fn_parseFloat(const Any& a) {
    std::string tmp = toString(a).str();
    const char* p = tmp.c_str(); char* e = nullptr;
    double d = std::strtod(p, &e);
    return e == p ? NaN : d;
}
inline double fn_parseInt(const String& s, double radix = 10) {
    const char* p = s.str().c_str(); char* e = nullptr;
    int r = (int)radix ? (int)radix : 10;
    if (r == 10 && s.str().size() > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) r = 16;
    long long v = std::strtoll(p, &e, r);
    return e == p ? NaN : (double)v;
}
inline double fn_parseInt(const Any& a, double radix = 10) { return fn_parseInt(toString(a), radix); }
inline void fn_trace(const Any& a) { std::printf("%s\n", toString(a).str().c_str()); }
template <class... A> void fn_trace(A... a) { ((std::printf("%s ", toString(Any(a)).str().c_str())), ...); std::printf("\n"); }
// Reloj fijo opcional (pruebas en PC): si fixedStepMs() > 0, getTimer avanza exactamente ese paso por fotograma del
// escenario, asi las capturas no dependen de lo rapido que corra el PC.
inline double& fixedStepMs() { static double v = 0; return v; }
inline double& virtualNowMs() { static double v = 0; return v; }
inline double fn_getTimer() {
    if (fixedStepMs() > 0) return std::floor(virtualNowMs());
    static const auto t0 = std::chrono::steady_clock::now();
    return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}
inline void fn_navigateToURL(Object*, const String& = String()) {}
inline void fn_navigateToURL(const Any&, const String& = String()) {}
inline String fn_getQualifiedClassName(const Any& a) { return a.k == Any::Obj ? a.o->__className() : String("Object"); }
inline Any fn_getDefinitionByName(const String&) { notImplemented("getDefinitionByName"); }
inline String fn_unescape(const String& s) { return s; }
inline String fn_escape(const String& s) { return s; }
inline String fn_encodeURI(const String& s) { return s; }
inline void fn_fscommand(const String&, const String& = String()) {}
inline double fn_setTimeout(const Function&, double, ...) { return 0; }
inline void fn_clearTimeout(double) {}
inline Array* fn_Array(const Any& a = Any()) { return a.k == Any::Obj ? dynamic_cast<Array*>(a.o) : nullptr; }
inline Any typeOf(const Any& a) {
    switch (a.k) {
        case Any::Undef: return Any(String("undefined"));
        case Any::Num: return Any(String("number"));
        case Any::Bool: return Any(String("boolean"));
        case Any::Str: return Any(String("string"));
        case Any::Func: return Any(String("function"));
        default: return Any(String("object"));
    }
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>> Any typeOf(T*) { return Any(String("object")); }

// ============================================================ Date, Error, XML (mínimos)
// Date: la hora local se trata como UTC. Las propiedades (fullYear, month, date, hours...) son campos y
// getTime() las normaliza (month > 11, hours > 23...) como hace AS3; `time` se traduce a getTime()/setTime().
struct Date : Object {
    double fullYear = 1970, month = 0, date = 1, hours = 0, minutes = 0, seconds = 0, milliseconds = 0;
    static int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {  // m: 1..12
        y -= m <= 2;
        int64_t era = (y >= 0 ? y : y - 399) / 400;
        int64_t yoe = y - era * 400;
        int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }
    static void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
        z += 719468;
        int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        int64_t doe = z - era * 146097;
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        y = yoe + era * 400;
        int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int64_t mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
        m = mp + (mp < 10 ? 3 : -9);
        y += m <= 2;
    }
    void setTime(double ms) {
        if (std::isnan(ms)) { fullYear = month = date = hours = minutes = seconds = milliseconds = NaN; return; }
        int64_t t = (int64_t)std::floor(ms), days = (int64_t)std::floor((double)t / 86400000.0);
        int64_t rem = t - days * 86400000, y, m, d;
        civilFromDays(days, y, m, d);
        fullYear = (double)y; month = (double)(m - 1); date = (double)d;
        hours = (double)(rem / 3600000); minutes = (double)((rem / 60000) % 60);
        seconds = (double)((rem / 1000) % 60); milliseconds = (double)(rem % 1000);
    }
    double getTime() const {
        if (std::isnan(fullYear) || std::isnan(month) || std::isnan(date)) return NaN;
        int64_t y = (int64_t)fullYear + (int64_t)std::floor(month / 12.0);
        int64_t m = (int64_t)(month - std::floor(month / 12.0) * 12.0);
        double days = (double)daysFromCivil(y, m + 1, 1) + (date - 1);
        return days * 86400000.0 + hours * 3600000.0 + minutes * 60000.0 + seconds * 1000.0 + milliseconds;
    }
    void __construct() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        setTime((double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }
    void __construct(double ms) { setTime(ms); }
    void __construct(const String&) { setTime(NaN); }  // el juego no parsea fechas de texto
    void __construct(const Any& a) { if (a.k == Any::Str) setTime(NaN); else setTime(toNumber(a)); }
    void __construct(const Any& y, const Any& mo, const Any& d = Any(1.0), const Any& h = Any(0.0), const Any& mi = Any(0.0), const Any& s = Any(0.0), const Any& ms = Any(0.0)) {
        __construct(toNumber(y), toNumber(mo), toNumber(d), toNumber(h), toNumber(mi), toNumber(s), toNumber(ms));
    }
    void __construct(double y, double mo, double d = 1, double h = 0, double mi = 0, double s = 0, double ms = 0) {
        fullYear = y; month = mo; date = d; hours = h; minutes = mi; seconds = s; milliseconds = ms;
        setTime(getTime());
    }
    void normalize() { setTime(getTime()); }
    double getFullYear() { normalize(); return fullYear; }
    double getMonth() { normalize(); return month; }
    double getDate() { normalize(); return date; }
    double getHours() { normalize(); return hours; }
    double getMinutes() { normalize(); return minutes; }
    double getSeconds() { normalize(); return seconds; }
    double getMilliseconds() { normalize(); return milliseconds; }
    double getDay() { normalize(); int64_t d = (int64_t)std::floor(getTime() / 86400000.0); return (double)(((d % 7) + 11) % 7); }
    double getUTCFullYear() { return getFullYear(); } double getUTCMonth() { return getMonth(); } double getUTCDate() { return getDate(); }
    double getUTCHours() { return getHours(); } double getUTCMinutes() { return getMinutes(); } double getUTCSeconds() { return getSeconds(); }
    double getUTCDay() { return getDay(); }
    double getTimezoneOffset() { return 0; }
    double setFullYear(double v) { fullYear = v; return getTime(); }
    double setMonth(double v) { month = v; return getTime(); }
    double setDate(double v) { date = v; return getTime(); }
    double setHours(double v) { hours = v; return getTime(); }
    double setMinutes(double v) { minutes = v; return getTime(); }
    double setSeconds(double v) { seconds = v; return getTime(); }
    double setMilliseconds(double v) { milliseconds = v; return getTime(); }
    static double UTC(double y, double mo, double d = 1, double h = 0, double mi = 0, double s = 0, double ms = 0) {
        Date t; t.__construct(y, mo, d, h, mi, s, ms); return t.getTime();
    }
    static double now() { Date t; t.__construct(); return t.getTime(); }
    String toString() { return String(std::to_string((long long)getTime())); }
    Date* clone() { Date* d = new Date(); d->setTime(getTime()); return d; }
};
struct Error : Object {
    String message;
    double errorID = 0;
    void __construct(const String& m = String(""), double id = 0) { message = m; errorID = id; }
    String toString() { return message; }
};
// E4X minimo con un parser real: elementos, atributos, texto, comentarios, CDATA y entidades basicas.
struct XMLList;
struct XML : Object {
    String tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<XML*> kids;
    std::string text;       // texto directo (concatenado) del elemento
    XML* parent = nullptr;
    bool isText = false;
    void __trace() override { Object::__trace(); for (auto* k : kids) gc::markObj((Object*)k); gc::markObj((Object*)parent); }
    void __construct() {}
    void __construct(const Any& src);
    void __construct(const String& src) { parse(src.str()); }
    void parse(const std::string& s);
    String name() const { return tag; }
    double length() const { return 1; }
    String toString();
    String toXMLString();
    XML* attribute_(const std::string& n) { return nullptr; }
    Any attributeValue(const std::string& n) const {
        for (auto& a : attrs) if (a.first == n) return Any(String(a.second));
        return Any(String(""));
    }
    XMLList* children();
    XMLList* elements();
    XMLList* child(const String& n);
    bool hasSimpleContent() const { for (auto* k : kids) if (!k->isText) return false; return true; }
    Any __get(const std::string& n) override;
    Any __call(const std::string& n, const std::vector<Any>& a) override;
    String __className() const override { return String("XML"); }
};
struct XMLList : Object {
    std::vector<XML*> items;
    void __trace() override { Object::__trace(); for (auto* x : items) gc::markObj((Object*)x); }
    void __construct() {}
    double length() const { return (double)items.size(); }
    String toString() { std::string r; for (auto* x : items) r += x->toString().str(); return String(r); }
    String toXMLString() { std::string r; for (auto* x : items) r += x->toXMLString().str(); return String(r); }
    XMLList* children() { XMLList* r = new XMLList(); for (auto* x : items) for (auto* k : x->kids) r->items.push_back(k); return r; }
    XMLList* child(const String& n) { XMLList* r = new XMLList(); for (auto* x : items) for (auto* k : x->kids) if (!k->isText && k->tag == n) r->items.push_back(k); return r; }
    Any __get(const std::string& n) override {
        if (n == "length") return Any((double)items.size());
        XMLList* r = child(String(n));
        return Any(r);
    }
    Any __call(const std::string& n, const std::vector<Any>& a) override {
        if (n == "length") return Any((double)items.size());
        if (n == "toString") return Any(toString());
        if (n == "children") return Any(children());
        if (n == "child" && !a.empty()) return Any(child(::as3::toString(a[0])));
        if (n == "name" && !items.empty()) return Any(items[0]->tag);
        return Object::__call(n, a);
    }
    String __className() const override { return String("XMLList"); }
};
inline XMLList* XML::children() { XMLList* r = new XMLList(); r->items = kids; return r; }
inline XMLList* XML::elements() { XMLList* r = new XMLList(); for (auto* k : kids) if (!k->isText) r->items.push_back(k); return r; }
inline XMLList* XML::child(const String& n) { XMLList* r = new XMLList(); for (auto* k : kids) if (!k->isText && k->tag == n) r->items.push_back(k); return r; }
inline Any XML::__get(const std::string& n) { XMLList* r = child(String(n)); return Any(r); }
inline Any XML::__call(const std::string& n, const std::vector<Any>& a) {
    if (n == "length") return Any(1.0);
    if (n == "toString") return Any(toString());
    if (n == "toXMLString") return Any(toXMLString());
    if (n == "children") return Any(children());
    if (n == "elements") return Any(elements());
    if (n == "child" && !a.empty()) return Any(child(::as3::toString(a[0])));
    if (n == "attribute" && !a.empty()) return attributeValue(::as3::toString(a[0]).str());
    if (n == "name") return Any(tag);
    return Object::__call(n, a);
}
inline String XML::toString() {
    if (isText) return String(text);
    if (hasSimpleContent()) { std::string r; for (auto* k : kids) r += k->text; return String(r); }
    return toXMLString();
}
inline String XML::toXMLString() {
    if (isText) return String(text);
    std::string r = "<" + tag.str();
    for (auto& a : attrs) r += " " + a.first + "=\"" + a.second + "\"";
    if (kids.empty()) return String(r + "/>");
    r += ">";
    for (auto* k : kids) r += k->toXMLString().str();
    return String(r + "</" + tag.str() + ">");
}
inline void XML::parse(const std::string& s) {
    size_t i = 0, n = s.size();
    auto decode = [](const std::string& t) {
        std::string r;
        for (size_t p = 0; p < t.size(); p++) {
            if (t[p] == '&') {
                size_t q = t.find(';', p);
                if (q != std::string::npos && q - p <= 8) {
                    std::string e = t.substr(p + 1, q - p - 1);
                    if (e == "amp") { r += '&'; p = q; continue; } if (e == "lt") { r += '<'; p = q; continue; }
                    if (e == "gt") { r += '>'; p = q; continue; } if (e == "quot") { r += '"'; p = q; continue; }
                    if (e == "apos") { r += '\''; p = q; continue; }
                    if (!e.empty() && e[0] == '#') { long c = e[1] == 'x' ? std::strtol(e.c_str() + 2, nullptr, 16) : std::strtol(e.c_str() + 1, nullptr, 10); if (c < 128) r += (char)c; else r += '?'; p = q; continue; }
                }
            }
            r += t[p];
        }
        return r;
    };
    std::vector<XML*> stack;
    XML* root = nullptr;
    while (i < n) {
        if (s[i] == '<') {
            if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i); i = e == std::string::npos ? n : e + 3; continue; }
            if (s.compare(i, 9, "<![CDATA[") == 0) {
                size_t e = s.find("]]>", i); std::string t = s.substr(i + 9, (e == std::string::npos ? n : e) - i - 9);
                if (!stack.empty()) { XML* x = new XML(); x->isText = true; x->text = t; x->parent = stack.back(); stack.back()->kids.push_back(x); }
                i = e == std::string::npos ? n : e + 3; continue;
            }
            if (s[i + 1] == '?' || s[i + 1] == '!') { size_t e = s.find('>', i); i = e == std::string::npos ? n : e + 1; continue; }
            if (s[i + 1] == '/') { size_t e = s.find('>', i); i = e == std::string::npos ? n : e + 1; if (!stack.empty()) stack.pop_back(); continue; }
            size_t j = i + 1;
            while (j < n && !std::isspace((unsigned char)s[j]) && s[j] != '>' && s[j] != '/') j++;
            XML* x = new XML();
            x->tag = String(s.substr(i + 1, j - i - 1));
            bool selfClose = false;
            while (j < n && s[j] != '>') {
                if (std::isspace((unsigned char)s[j])) { j++; continue; }
                if (s[j] == '/') { selfClose = true; j++; continue; }
                size_t k = j;
                while (k < n && s[k] != '=' && !std::isspace((unsigned char)s[k]) && s[k] != '>' && s[k] != '/') k++;
                std::string an = s.substr(j, k - j);
                while (k < n && std::isspace((unsigned char)s[k])) k++;
                std::string av;
                if (k < n && s[k] == '=') {
                    k++;
                    while (k < n && std::isspace((unsigned char)s[k])) k++;
                    char q = s[k];
                    size_t e = s.find(q, k + 1);
                    av = decode(s.substr(k + 1, (e == std::string::npos ? n : e) - k - 1));
                    k = e == std::string::npos ? n : e + 1;
                }
                if (!an.empty()) x->attrs.emplace_back(an, av);
                j = k;
            }
            i = j < n ? j + 1 : n;
            if (stack.empty()) { if (!root) root = x; } else { x->parent = stack.back(); stack.back()->kids.push_back(x); }
            if (!selfClose) stack.push_back(x);
        } else {
            size_t e = s.find('<', i);
            std::string t = s.substr(i, (e == std::string::npos ? n : e) - i);
            i = e == std::string::npos ? n : e;
            bool blank = true;
            for (char c : t) if (!std::isspace((unsigned char)c)) { blank = false; break; }
            if (!blank && !stack.empty()) {
                XML* x = new XML(); x->isText = true; x->text = decode(t); x->parent = stack.back();
                stack.back()->kids.push_back(x); stack.back()->text += x->text;
            }
        }
    }
    if (root) { tag = root->tag; attrs = root->attrs; kids = root->kids; text = root->text; for (auto* k : kids) k->parent = this; }
}
inline void XML::__construct(const Any& src) { parse(::as3::toString(src).str()); }
inline bool xmlListAt(Object* o, const Any& k, Any& out) {
    XMLList* xl = dynamic_cast<XMLList*>(o);
    if (!xl) return false;
    double i = toNumber(k);
    out = (i >= 0 && i < xl->items.size()) ? Any(xl->items[(size_t)i]) : Any();
    return true;
}
inline bool xmlListValues(Object* o, std::vector<Any>& out) {
    XMLList* xl = dynamic_cast<XMLList*>(o);
    if (!xl) return false;
    for (auto* x : xl->items) out.push_back(Any(x));
    return true;
}
inline Any xmlAttr(const Any& x, const char* n) {
    if (x.k != Any::Obj) return Any(String(""));
    if (XML* e = dynamic_cast<XML*>(x.o)) return e->attributeValue(n);
    if (XMLList* l = dynamic_cast<XMLList*>(x.o)) return l->items.empty() ? Any(String("")) : l->items[0]->attributeValue(n);
    return Any(String(""));
}
template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>> Any xmlAttr(T* o, const char* n) { return xmlAttr(Any((Object*)o), n); }

struct JSON : Object {};
struct ByteArray : Object {
    std::vector<uint8_t> data;
    uint32_t position = 0;
    String endian = String("bigEndian");
    void __construct() {}
    uint32_t get_length() const { return (uint32_t)data.size(); }
    uint32_t set_length(double n) { data.resize((size_t)n); if (position > data.size()) position = (uint32_t)data.size(); return (uint32_t)n; }
    uint32_t get_bytesAvailable() const { return (uint32_t)data.size() - position; }
    bool little() const { return endian.str() == "littleEndian"; }
    void need(size_t n) { if (position + n > data.size()) throw Thrown(Any(String("EOFError: Error #2030: se ha encontrado el final del archivo"))); }
    uint8_t readByte_() { need(1); return data[position++]; }
    int32_t readByte() { return (int8_t)readByte_(); }
    uint32_t readUnsignedByte() { return readByte_(); }
    uint32_t readUnsignedShort() { need(2); uint32_t a = data[position], b = data[position + 1]; position += 2; return little() ? (b << 8 | a) : (a << 8 | b); }
    int32_t readShort() { return (int16_t)readUnsignedShort(); }
    uint32_t readUnsignedInt() { need(4); uint32_t v = 0; for (int i = 0; i < 4; i++) { uint32_t b = data[position + (little() ? 3 - i : i)]; v = (v << 8) | b; } position += 4; return v; }
    int32_t readInt() { return (int32_t)readUnsignedInt(); }
    bool readBoolean() { return readByte_() != 0; }
    double readDouble() { need(8); uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | data[position + (little() ? 7 - i : i)]; position += 8; double d; std::memcpy(&d, &v, 8); return d; }
    double readFloat() { uint32_t v = readUnsignedInt(); float f; std::memcpy(&f, &v, 4); return f; }
    String readUTFBytes(double n) { need((size_t)n); String s(std::string((const char*)data.data() + position, (size_t)n)); position += (uint32_t)n; return s; }
    String readUTF() { uint32_t n = readUnsignedShort(); return readUTFBytes(n); }
    void readBytes(ByteArray* dst, double off = 0, double n = 0) {
        size_t cnt = n ? (size_t)n : data.size() - position; need(cnt);
        if (dst->data.size() < (size_t)off + cnt) dst->data.resize((size_t)off + cnt);
        std::memcpy(dst->data.data() + (size_t)off, data.data() + position, cnt); position += (uint32_t)cnt;
    }
    void writeByte(double v) { data.push_back((uint8_t)toInt(v)); position = (uint32_t)data.size(); }
    void writeBoolean(bool v) { writeByte(v ? 1 : 0); }
    void writeShort(double v) { uint32_t x = (uint32_t)toInt(v); if (little()) { data.push_back(x & 255); data.push_back((x >> 8) & 255); } else { data.push_back((x >> 8) & 255); data.push_back(x & 255); } position = (uint32_t)data.size(); }
    void writeInt(double v) { uint32_t x = (uint32_t)toInt(v); for (int i = 0; i < 4; i++) data.push_back((x >> (little() ? 8 * i : 8 * (3 - i))) & 255); position = (uint32_t)data.size(); }
    void writeUnsignedInt(double v) { writeInt(v); }
    void writeUTFBytes(const String& s) { data.insert(data.end(), s.str().begin(), s.str().end()); position = (uint32_t)data.size(); }
    void writeUTF(const String& s) { writeShort((double)s.str().size()); writeUTFBytes(s); }
    void clear() { data.clear(); position = 0; }
    void __loadEmbed(const char* src) override {
        std::string d;
        if (platform::readFile(platform::embedPath(src), d)) { data.assign(d.begin(), d.end()); position = 0; }
        else platform::log("[assets]", "no se encuentra el recurso incrustado %s (%s)", src, platform::embedPath(src).c_str());
    }
    void compress() {}
    void uncompress() {}
    String toString() { return String(std::string(data.begin(), data.end())); }
};
// ByteArray[i]: lectura y escritura de un byte (la escritura amplia el array si hace falta, como en AS3)
inline Any at(ByteArray* b, const Any& k) {
    double i = toNumber(k);
    return (i >= 0 && i < b->data.size()) ? Any((double)b->data[(size_t)i]) : Any((double)0);
}
inline bool byteArrayAt(Object* o, const Any& k, Any& out) {
    ByteArray* b = dynamic_cast<ByteArray*>(o);
    if (!b) return false;
    out = at(b, k);
    return true;
}
inline bool byteArraySet(Object* o, const Any& k, const Any& v) {
    ByteArray* b = dynamic_cast<ByteArray*>(o);
    if (!b) return false;
    double i = toNumber(k);
    if (i < 0) return true;
    if (i >= b->data.size()) b->data.resize((size_t)i + 1);
    b->data[(size_t)i] = (uint8_t)toInt(v);
    return true;
}
inline void setAt(ByteArray* b, const Any& k, const Any& v) {
    double i = toNumber(k);
    if (i < 0) return;
    if (i >= b->data.size()) b->data.resize((size_t)i + 1);
    b->data[(size_t)i] = (uint8_t)toInt(v);
}

template <class T> void setAt(Vector<T>* a, const Any& k, const Any& v) { setAt(a, toNumber(k), v); }
template <class O, class K> Any incAt(O&& o, K&& k, double d, bool post) {
    Any key(k);
    double old = toNumber(Any(at(o, key)));
    setAt(o, key, Any(old + d));
    return Any(post ? old : old + d);
}

// ============================================================ estáticos de clases integradas
struct Number_ {
    // MIN_VALUE: en Flash es 5e-324 (subnormal). La CPU de la Vita trabaja en modo flush-to-zero y lo trata como 0
    // (FlxCamera.fade empieza en MIN_VALUE y comprueba > 0: el fundido no avanzaba y NEW GAME no hacia nada).
    // Se usa el menor numero normal: para el juego es igual de "casi cero" y no se pierde en ARM.
    static constexpr double MAX_VALUE = 1.7976931348623157e308, MIN_VALUE = 2.2250738585072014e-308;
    static constexpr double NaN = as3::NaN, POSITIVE_INFINITY = as3::Infinity, NEGATIVE_INFINITY = -as3::Infinity;
};
struct int_ { static constexpr int32_t MAX_VALUE = 2147483647, MIN_VALUE = -2147483647 - 1; };
struct uint_ { static constexpr uint32_t MAX_VALUE = 4294967295u, MIN_VALUE = 0; };
struct String_ {
    static String fromCharCode(double c) { return String::fromCharCode(c); }
};
struct Object_ {};

// ============================================================ acceso dinámico resuelto por C++
// Cuando el traductor no conoce el tipo AS3 de una expresión, emite estas macros: si el tipo C++ real
// tiene el miembro, se usa directamente (con comprobación de null como TypeError #1009); si es un `Any`
// (Object / * dinámico), cae al acceso por nombre.
template <class T, class V> T coerce(V&& v) {
    if constexpr (std::is_assignable<T&, V&&>::value) return std::forward<V>(v);
    else return fromAny<T>(Any(v));
}
template <class O> void checkNotNull(const O& o) {
    if constexpr (std::is_pointer<std::decay_t<O>>::value) {
        if (!o) throw Thrown(Any(String("TypeError: Error #1009: no se puede acceder a una propiedad o método de una referencia a un objeto null")));
    }
}
template <class O, class G> decltype(auto) mget(O&& o, G g, const char* n) {
    if constexpr (std::is_invocable<G, O&&>::value) { checkNotNull(o); return g(std::forward<O>(o)); }
    else return dyn(o, n);
}
template <class O, class G, class V> decltype(auto) mset(O&& o, G g, const char* n, V&& v) {
    if constexpr (std::is_invocable<G, O&&, V&&>::value) { checkNotNull(o); return g(std::forward<O>(o), std::forward<V>(v)); }
    else { setDyn(o, n, Any(v)); return Any(v); }
}
template <class O, class G, class... A> decltype(auto) mcall(O&& o, G g, const char* n, A&&... a) {
    if constexpr (std::is_invocable<G, O&&, A&&...>::value) { checkNotNull(o); return g(std::forward<O>(o), std::forward<A>(a)...); }
    else return callDyn(o, n, std::vector<Any>{Any(a)...});
}

template <class O, class G, class S> Any minc(O&& o, G g, S s, const char* n, double d, bool post) {
    if constexpr (std::is_invocable<G, O&&>::value) {
        checkNotNull(o);
        auto& ref = g(o);
        using T = std::decay_t<decltype(ref)>;
        double old = toNumber(ref);
        ref = (T)(old + d);
        return Any(post ? old : toNumber(ref));
    } else {
        Any cur = dyn(o, n);
        double old = toNumber(cur);
        setDyn(o, n, Any(old + d));
        return Any(post ? old : old + d);
    }
}

}  // namespace as3

#define AS3INC(o, name, d, post) ::as3::minc((o), [](auto&& _o) -> decltype((_o->name)) { return _o->name; }, 0, #name, (d), (post))
#define AS3GET(o, name) ::as3::mget((o), [](auto&& _o) -> decltype((_o->name)) { return _o->name; }, #name)
#define AS3SET(o, name, val) ::as3::mset((o), [](auto&& _o, auto&& _v) -> decltype((_o->name)) { return _o->name = ::as3::coerce<std::decay_t<decltype(_o->name)>>(_v); }, #name, (val))
#define AS3CALL(o, name, ...) ::as3::mcall((o), [](auto&& _o, auto&&... _a) -> decltype(_o->name(std::forward<decltype(_a)>(_a)...)) { return _o->name(std::forward<decltype(_a)>(_a)...); }, #name, ##__VA_ARGS__)

#include "flash.h"
