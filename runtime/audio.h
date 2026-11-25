// audio.h — mezclador por software para flash.media.Sound (NEO Scavenger: efectos y musica en MP3 incrustados).
// Cada voz decodifica su MP3 en streaming con minimp3 dentro del hilo de audio (otro nucleo): el hilo del juego solo
// sincroniza volumen/panoramica y recoge las voces terminadas (SOUND_COMPLETE), menos de 1 ms por fotograma.
// La implementacion de minimp3 (MINIMP3_IMPLEMENTATION) va en el main de cada ejecutable.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "third_party/minimp3.h"

namespace audio {

using Bytes = std::shared_ptr<const std::string>;

struct Voice {
    Bytes mp3;
    mp3dec_t dec;
    size_t off = 0;                        // posicion en el MP3
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int pcmFrames = 0, chans = 2, hz = 44100;
    double srcPos = 0;                     // posicion (en muestras de origen) dentro de pcm
    double skipMs = 0;                     // posicion inicial pedida por play()
    int loops = 0;                         // repeticiones que quedan (play(..., loops))
    std::atomic<float> vol{1}, pan{0};
    std::atomic<bool> stop{false}, done{false};
    std::atomic<int64_t> playedFrames{0};  // muestras de salida reproducidas (posicion del canal)
    bool started = false;

    void restart() { mp3dec_init(&dec); off = 0; pcmFrames = 0; srcPos = 0; }
    bool decodeNext() {
        while (off < mp3->size()) {
            mp3dec_frame_info_t info;
            int n = mp3dec_decode_frame(&dec, (const uint8_t*)mp3->data() + off, (int)(mp3->size() - off), pcm, &info);
            if (info.frame_bytes <= 0) { off = mp3->size(); break; }
            off += (size_t)info.frame_bytes;
            if (n > 0) { pcmFrames = n; chans = info.channels; hz = info.hz; srcPos = 0; return true; }
        }
        return false;
    }
};

struct Mixer {
    std::mutex m;
    std::vector<std::shared_ptr<Voice>> voices;
    std::atomic<float> master{1};
    int outHz = 44100;

    void add(const std::shared_ptr<Voice>& v) { std::lock_guard<std::mutex> g(m); voices.push_back(v); }

    // Mezcla 'frames' muestras estereo de 16 bits en out (lo llama el hilo de audio)
    void mix(int16_t* out, int frames) {
        static thread_local std::vector<float> acc;
        acc.assign((size_t)frames * 2, 0.0f);
        std::vector<std::shared_ptr<Voice>> list;
        { std::lock_guard<std::mutex> g(m); list = voices; }
        for (auto& vp : list) {
            Voice& v = *vp;
            if (v.done) continue;
            if (v.stop) { v.done = true; continue; }
            if (!v.started) {
                v.started = true; v.restart();
                // posicion inicial: saltar tramas hasta llegar a skipMs
                double ms = 0;
                while (v.skipMs > 0 && ms + 1e-9 < v.skipMs && v.decodeNext()) ms += 1000.0 * v.pcmFrames / v.hz;
                if (v.skipMs <= 0 && !v.decodeNext()) { v.done = true; continue; }
            }
            float vol = v.vol.load() * master.load(), pan = v.pan.load();
            float lv = vol * (pan > 0 ? 1 - pan : 1), rv = vol * (pan < 0 ? 1 + pan : 1);
            double step = (double)v.hz / outHz;
            int produced = 0;
            for (int i = 0; i < frames; i++) {
                if (v.srcPos >= v.pcmFrames) {
                    double carry = v.srcPos - v.pcmFrames;
                    if (!v.decodeNext()) {
                        if (v.loops > 0) { v.loops--; v.restart(); if (!v.decodeNext()) { v.done = true; break; } }
                        else { v.done = true; break; }
                    }
                    v.srcPos = carry;
                }
                int s = (int)v.srcPos;
                float l, r;
                if (v.chans == 2) { l = v.pcm[s * 2]; r = v.pcm[s * 2 + 1]; } else { l = r = v.pcm[s]; }
                acc[(size_t)i * 2] += l * lv;
                acc[(size_t)i * 2 + 1] += r * rv;
                v.srcPos += step;
                produced++;
            }
            v.playedFrames += produced;
        }
        for (size_t i = 0; i < acc.size(); i++) {
            float x = acc[i];
            out[i] = (int16_t)(x > 32767.f ? 32767 : x < -32768.f ? -32768 : x);
        }
        // quitar las voces terminadas
        std::lock_guard<std::mutex> g(m);
        size_t k = 0;
        for (auto& v : voices) if (!v->done) voices[k++] = v;
        voices.resize(k);
    }
};
inline Mixer& mixer() { static Mixer mx; return mx; }

// Bytes de un MP3 (del paquete de datos). Los efectos cortos (< 256 KB) se guardan en cache; la musica se lee al
// reproducirla y se libera al terminar.
inline Bytes load(const std::string& path, bool (*readFile)(const std::string&, std::string&)) {
    static std::unordered_map<std::string, Bytes> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    auto s = std::make_shared<std::string>();
    if (!readFile(path, *s)) return nullptr;
    Bytes b = s;
    if (b->size() < 256 * 1024) cache[path] = b;
    return b;
}

// Duracion en ms (recorre las cabeceras de trama sin decodificar)
inline double lengthMs(const Bytes& b) {
    if (!b) return 0;
    mp3dec_t d; mp3dec_init(&d);
    size_t off = 0; double ms = 0;
    while (off < b->size()) {
        mp3dec_frame_info_t info;
        int n = mp3dec_decode_frame(&d, (const uint8_t*)b->data() + off, (int)(b->size() - off), nullptr, &info);
        if (info.frame_bytes <= 0) break;
        off += (size_t)info.frame_bytes;
        if (n > 0 && info.hz > 0) ms += 1000.0 * n / info.hz;
    }
    return ms;
}

}  // namespace audio
