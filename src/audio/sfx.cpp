#include "audio/sfx.h"
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include "miniaudio.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <objbase.h>
#include <vector>

namespace sfx {
namespace {

constexpr int kSR = 48000;
constexpr float PI2 = 6.2831853f;

using Buf = std::vector<float>;
Buf g_bufs[Count];

struct Voice { const Buf* b = nullptr; size_t pos = 0; float gain = 0; };
Voice g_voices[24];
std::mutex g_mtx;
ma_context* g_ctx = nullptr;
ma_device* g_dev = nullptr;
bool g_devOk = false, g_ctxOk = false;
std::atomic<float> g_volume{0.5f};
std::atomic<bool> g_enabled{true}, g_hover{true};

void Ensure(Buf& b, float sec) { size_t n = (size_t)(sec * kSR); if (b.size() < n) b.resize(n, 0.f); }

// Nota com ataque curto e decaimento exponencial; 'bright' adiciona harmônicos (som de sino/marimba).
void Note(Buf& b, float at, float f, float dur, float gain, float bright = 0.3f, float attack = 0.004f) {
    Ensure(b, at + dur);
    size_t s0 = (size_t)(at * kSR), n = (size_t)(dur * kSR);
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        float t = (float)i / kSR;
        float env = std::min(1.f, t / attack) * std::exp(-t * 5.f / dur);
        float v = std::sin(PI2 * f * t) + bright * 0.5f * std::sin(PI2 * f * 2.f * t) * std::exp(-t * 14.f / dur) +
                  bright * 0.25f * std::sin(PI2 * f * 3.01f * t) * std::exp(-t * 24.f / dur);
        b[s0 + i] += gain * env * v;
    }
}

// Varredura de frequência (blip/whoosh tonal)
void Sweep(Buf& b, float at, float f0, float f1, float dur, float gain) {
    Ensure(b, at + dur);
    size_t s0 = (size_t)(at * kSR), n = (size_t)(dur * kSR);
    double ph = 0;
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        float t = (float)i / n;
        float f = f0 * std::pow(f1 / f0, t);
        ph += PI2 * f / kSR;
        float env = std::sin(3.14159f * std::min(1.f, t * 1.4f)) * (1.f - t * 0.6f);
        b[s0 + i] += gain * env * (float)std::sin(ph);
    }
}

// Ruído filtrado (passa-faixa que se move): "whoosh" / "swoosh"
void Whoosh(Buf& b, float at, float c0, float c1, float dur, float gain) {
    Ensure(b, at + dur);
    size_t s0 = (size_t)(at * kSR), n = (size_t)(dur * kSR);
    float y1 = 0, y2 = 0;
    uint32_t r = 22222;
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        float t = (float)i / n;
        float fc = c0 + (c1 - c0) * t;
        float q = 0.92f, w = PI2 * fc / kSR;
        float a1 = 2 * q * std::cos(w), a2 = -q * q;
        r = r * 1664525u + 1013904223u;
        float x = ((r >> 9) / 8388608.f - 1.f);
        float y = (1 - q) * x + a1 * y1 + a2 * y2;
        y2 = y1; y1 = y;
        float env = std::sin(3.14159f * t);
        b[s0 + i] += gain * env * env * y * 3.f;
    }
}

// Tique curto (clique de interface)
void Tick(Buf& b, float at, float f, float dur, float gain) {
    Ensure(b, at + dur);
    size_t s0 = (size_t)(at * kSR), n = (size_t)(dur * kSR);
    uint32_t r = 777;
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        float t = (float)i / kSR;
        r = r * 1664525u + 1013904223u;
        float noise = ((r >> 9) / 8388608.f - 1.f) * std::exp(-t * 900.f);
        b[s0 + i] += gain * (std::sin(PI2 * f * t) * std::exp(-t * 6.f / dur) * std::min(1.f, t / 0.0008f) + noise * 0.35f);
    }
}

void Build() {
    Tick(g_bufs[Hover], 0, 2600.f, 0.018f, 0.10f);
    Tick(g_bufs[Click], 0, 1250.f, 0.045f, 0.55f);
    Note(g_bufs[Click], 0.004f, 2500.f, 0.03f, 0.08f, 0.f);
    Note(g_bufs[ToggleOn], 0, 740.f, 0.07f, 0.35f, 0.2f);
    Note(g_bufs[ToggleOn], 0.055f, 1108.f, 0.10f, 0.35f, 0.2f);
    Note(g_bufs[ToggleOff], 0, 988.f, 0.07f, 0.3f, 0.2f);
    Note(g_bufs[ToggleOff], 0.055f, 659.f, 0.10f, 0.3f, 0.2f);
    Whoosh(g_bufs[Open], 0, 500.f, 2600.f, 0.20f, 0.35f);
    Sweep(g_bufs[Open], 0.03f, 420.f, 880.f, 0.16f, 0.16f);
    Whoosh(g_bufs[Close], 0, 2400.f, 450.f, 0.18f, 0.3f);
    Sweep(g_bufs[Close], 0.02f, 820.f, 400.f, 0.14f, 0.14f);
    Tick(g_bufs[Tab], 0, 1700.f, 0.035f, 0.35f);
    Whoosh(g_bufs[Send], 0, 700.f, 3800.f, 0.16f, 0.4f);
    Sweep(g_bufs[Send], 0, 600.f, 1500.f, 0.12f, 0.15f);
    Note(g_bufs[Receive], 0, 1318.5f, 0.35f, 0.28f, 0.5f);
    Note(g_bufs[Receive], 0.09f, 1975.5f, 0.45f, 0.22f, 0.5f);
    Note(g_bufs[Notify], 0, 1046.5f, 0.3f, 0.3f, 0.6f);
    Note(g_bufs[Notify], 0.11f, 1568.f, 0.4f, 0.26f, 0.6f);
    Note(g_bufs[Success], 0, 1046.5f, 0.18f, 0.25f, 0.4f);
    Note(g_bufs[Success], 0.08f, 1318.5f, 0.18f, 0.25f, 0.4f);
    Note(g_bufs[Success], 0.16f, 1568.f, 0.4f, 0.28f, 0.4f);
    Note(g_bufs[Error], 0, 330.f, 0.18f, 0.35f, 0.8f);
    Note(g_bufs[Error], 0.13f, 262.f, 0.30f, 0.35f, 0.8f);
    Note(g_bufs[Permission], 0, 1568.f, 0.25f, 0.28f, 0.6f);
    Note(g_bufs[Permission], 0.14f, 1568.f, 0.25f, 0.28f, 0.6f);
    Note(g_bufs[Permission], 0.28f, 1174.7f, 0.6f, 0.3f, 0.6f);
    Sweep(g_bufs[ListenStart], 0, 560.f, 1180.f, 0.13f, 0.35f);
    Note(g_bufs[ListenStart], 0.1f, 1180.f, 0.12f, 0.15f, 0.2f);
    Sweep(g_bufs[ListenStop], 0, 1100.f, 520.f, 0.14f, 0.32f);
    Note(g_bufs[Coin], 0, 1975.5f, 0.12f, 0.25f, 0.9f);
    Note(g_bufs[Coin], 0.07f, 2637.f, 0.5f, 0.25f, 0.9f);
    Sweep(g_bufs[Pop], 0, 380.f, 1200.f, 0.06f, 0.4f);
    // suaviza o fim de cada som e limita o pico
    for (auto& b : g_bufs) {
        float peak = 0.f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        float norm = peak > 0.9f ? 0.9f / peak : 1.f;
        size_t fade = std::min<size_t>(b.size(), kSR / 200);
        for (size_t i = 0; i < b.size(); ++i) {
            b[i] *= norm;
            if (i + fade >= b.size()) b[i] *= (float)(b.size() - i) / fade;
        }
    }
}

void Callback(ma_device*, void* out, const void*, ma_uint32 frames) {
    float* o = (float*)out;
    std::fill(o, o + frames * 2, 0.f);
    float vol = g_volume.load();
    std::lock_guard<std::mutex> l(g_mtx);
    for (auto& v : g_voices) {
        if (!v.b) continue;
        for (ma_uint32 i = 0; i < frames && v.pos < v.b->size(); ++i, ++v.pos) {
            float s = (*v.b)[v.pos] * v.gain * vol;
            o[i * 2] += s;
            o[i * 2 + 1] += s;
        }
        if (v.pos >= v.b->size()) v.b = nullptr;
    }
    for (ma_uint32 i = 0; i < frames * 2; ++i) o[i] = std::tanh(o[i]); // nunca estoura
}
} // namespace

void Init() {
    if (g_devOk) return;
    Build();
    // inicia numa thread própria (COM em modo MTA, como o WASAPI prefere)
    std::thread([] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        g_ctx = new ma_context();
        g_dev = new ma_device();
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = kSR;
        cfg.periodSizeInMilliseconds = 15;
        cfg.dataCallback = Callback;
        g_ctxOk = ma_context_init(nullptr, 0, nullptr, g_ctx) == MA_SUCCESS;
        if (g_ctxOk && ma_device_init(g_ctx, &cfg, g_dev) == MA_SUCCESS) {
            if (ma_device_start(g_dev) == MA_SUCCESS) g_devOk = true;
            else ma_device_uninit(g_dev);
        }
    }).join();
}

void Shutdown() {
    if (g_devOk) { ma_device_uninit(g_dev); g_devOk = false; }
    if (g_ctxOk) { ma_context_uninit(g_ctx); g_ctxOk = false; }
}

void Play(Id id, float gain) {
    if (!g_devOk || !g_enabled || id < 0 || id >= Count || g_bufs[id].empty()) return;
    if (id == Hover && !g_hover) return;
    // evita "metralhadora" do mesmo som
    static double last[Count] = {};
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - last[id] < (id == Hover ? 0.045 : 0.03)) return;
    last[id] = now;
    std::lock_guard<std::mutex> l(g_mtx);
    Voice* slot = nullptr;
    for (auto& v : g_voices) if (!v.b) { slot = &v; break; }
    if (!slot) slot = &g_voices[0];
    slot->b = &g_bufs[id];
    slot->pos = 0;
    slot->gain = gain;
}

void SetVolume(int p) { g_volume = std::clamp(p, 0, 100) / 100.f * 0.8f; }
void SetEnabled(bool on) { g_enabled = on; }
void SetHover(bool on) { g_hover = on; }
bool Enabled() { return g_enabled; }

void UiFrame() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    static ImGuiID lastHover = 0, lastActive = 0;
    ImGuiID hov = g.HoveredId;
    // tique só quando o mouse chega num controle novo (não ao rolar a lista nem com o mouse parado)
    if (hov && hov != lastHover && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0) && !ImGui::IsMouseDown(0))
        Play(Hover);
    lastHover = hov;
    ImGuiID act = g.ActiveId;
    if (act && act != lastActive && ImGui::IsMouseClicked(0) && g.ActiveIdWindow && !(g.ActiveIdWindow->Flags & ImGuiWindowFlags_NoInputs)) {
        // barras de rolagem e arrastar janelas não fazem "clique"
        bool chrome = g.MovingWindow != nullptr || act == ImGui::GetWindowScrollbarID(g.ActiveIdWindow, ImGuiAxis_Y) ||
                      act == ImGui::GetWindowScrollbarID(g.ActiveIdWindow, ImGuiAxis_X);
        if (!chrome) Play(Click, 0.8f);
    }
    lastActive = act;
}

} // namespace sfx
