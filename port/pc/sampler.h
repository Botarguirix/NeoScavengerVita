// sampler.h — perfilador por muestreo para el arnes de PC (Windows): un hilo detiene el hilo principal cada ~1 ms
// y anota su contador de programa. Las direcciones se traducen luego con tools/as3cpp/perfil.py (nm).
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <unordered_map>
#include <cstring>
#include <string>

namespace sampler {
inline HANDLE target = nullptr;
inline std::atomic<bool> on{false}, quit{false};
inline std::unordered_map<uint64_t, uint32_t> hist;
inline DWORD WINAPI loop(LPVOID) {
    timeBeginPeriod(1);
    while (!quit) {
        Sleep(1);
        if (!on) continue;
        if (SuspendThread(target) == (DWORD)-1) continue;
        CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(target, &c)) hist[c.Rip]++;
        ResumeThread(target);
    }
    return 0;
}
inline void start() {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(nullptr, 0, loop, nullptr, 0, nullptr);
}
inline void dump(const char* path) {
    on = false; Sleep(5);
    FILE* f = std::fopen(path, "w");
    if (!f) return;
    std::fprintf(f, "base %llx\n", (unsigned long long)(uintptr_t)GetModuleHandleA(nullptr));
    HMODULE self = GetModuleHandleA(nullptr);
    std::unordered_map<std::string, uint32_t> mods;
    for (auto& kv : hist) {
        HMODULE m = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)kv.first, &m) && m != self) {
            char name[MAX_PATH] = "?"; GetModuleFileNameA(m, name, MAX_PATH);
            const char* b = std::strrchr(name, '\\'); mods[b ? b + 1 : name] += kv.second;
        } else std::fprintf(f, "%llx %u\n", (unsigned long long)kv.first, kv.second);
    }
    for (auto& kv : mods) std::fprintf(f, "mod:%s %u\n", kv.first.c_str(), kv.second);
    std::fclose(f);
}
}  // namespace sampler
