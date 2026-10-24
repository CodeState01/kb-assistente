#include "ui/timer.h"
#include "app.h"
#include "core/text.h"
#include "ui/anim.h"
#include "ui/embeds.h"
#include "ui/orb.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "imgui_internal.h"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using theme::px;
static constexpr float PI = 3.14159265f;

namespace timer {
namespace {

// ------------------------------------------------------------------ Sons sintetizados
constexpr int kSR = 22050;

struct Wave {
    std::vector<float> s;
    void Ensure(float sec) { size_t n = (size_t)(sec * kSR); if (s.size() < n) s.resize(n, 0.f); }
};

// Sino: parciais inarmônicos com decaimento exponencial (cada parcial some mais rápido)
void Bell(Wave& w, float at, float f0, float decay, float gain) {
    static const float ratios[] = {0.5f, 1.f, 1.183f, 1.506f, 2.f, 2.514f, 2.662f, 3.011f};
    static const float amps[] = {0.30f, 1.f, 0.38f, 0.26f, 0.20f, 0.10f, 0.07f, 0.04f}; // agudos suaves: menos estridente
    w.Ensure(at + decay * 1.2f);
    size_t start = (size_t)(at * kSR);
    for (int p = 0; p < 8; ++p) {
        float f = f0 * ratios[p], d = decay / (1.f + p * 0.45f);
        for (size_t i = 0; start + i < w.s.size(); ++i) {
            float tt = (float)i / kSR;
            float env = std::exp(-tt / d) * std::min(1.f, tt / 0.002f);
            if (env < 1e-4f && tt > 0.05f) break;
            w.s[start + i] += gain * amps[p] * env * std::sin(2 * PI * f * tt);
        }
    }
}

void Gong(Wave& w, float at, float gain) {
    static const float ratios[] = {1.f, 1.47f, 2.09f, 2.56f, 2.95f, 3.62f, 4.18f, 5.3f};
    w.Ensure(at + 4.5f);
    size_t start = (size_t)(at * kSR);
    const float f0 = 98.f;
    for (int p = 0; p < 8; ++p) {
        float f = f0 * ratios[p], d = 3.2f / (1.f + p * 0.25f);
        float bloom = 0.04f + p * 0.05f; // os agudos "florescem" um pouco depois
        for (size_t i = 0; start + i < w.s.size(); ++i) {
            float tt = (float)i / kSR;
            float env = (1.f - std::exp(-tt / bloom)) * std::exp(-tt / d);
            float shimmer = 1.f + 0.15f * std::sin(2 * PI * (2.5f + p) * tt);
            w.s[start + i] += gain * (p == 0 ? 1.f : 0.5f / (1 + p * 0.3f)) * env * shimmer * std::sin(2 * PI * f * tt + 0.3f * std::sin(2 * PI * 0.7f * tt));
        }
    }
}

void Tone(Wave& w, float at, float dur, float f, float gain, bool square = false, float vibrato = 0.f) {
    w.Ensure(at + dur + 0.02f);
    size_t start = (size_t)(at * kSR);
    double ph = 0;
    uint32_t rnd = 12345;
    for (size_t i = 0; i < (size_t)(dur * kSR) && start + i < w.s.size(); ++i) {
        float tt = (float)i / kSR;
        float env = std::min(1.f, tt / 0.01f) * std::min(1.f, (dur - tt) / 0.03f);
        float ff = f * (1.f + vibrato * std::sin(2 * PI * 28.f * tt));
        ph += 2 * PI * ff / kSR;
        float v = square ? (std::sin(ph) > 0 ? 0.6f : -0.6f) : (float)std::sin(ph);
        if (vibrato > 0) { rnd = rnd * 1664525u + 1013904223u; v += ((rnd >> 9) / 8388608.f - 1.f) * 0.15f; }
        w.s[start + i] += gain * env * v;
    }
}

// "Claque" de madeira: ruído curto filtrado em ressonância
void Clack(Wave& w, float at, float gain) {
    w.Ensure(at + 0.15f);
    size_t start = (size_t)(at * kSR);
    float y1 = 0, y2 = 0;
    const float f = 1700.f, r = 0.985f;
    float a1 = 2 * r * std::cos(2 * PI * f / kSR), a2 = -r * r;
    uint32_t rnd = 987654;
    for (size_t i = 0; i < (size_t)(0.12f * kSR) && start + i < w.s.size(); ++i) {
        float tt = (float)i / kSR;
        rnd = rnd * 1664525u + 1013904223u;
        float x = tt < 0.004f ? ((rnd >> 9) / 8388608.f - 1.f) : 0.f;
        float y = x + a1 * y1 + a2 * y2;
        y2 = y1; y1 = y;
        w.s[start + i] += gain * y * 0.25f;
    }
}

std::vector<char> ToWav(const Wave& w) {
    float peak = 0.001f;
    for (float v : w.s) peak = std::max(peak, std::fabs(v));
    float norm = 0.7f / peak;
    size_t n = w.s.size();
    std::vector<char> wav(44 + n * 2);
    auto w32 = [&](int off, uint32_t v) { memcpy(&wav[off], &v, 4); };
    auto w16 = [&](int off, uint16_t v) { memcpy(&wav[off], &v, 2); };
    memcpy(&wav[0], "RIFF", 4); w32(4, (uint32_t)(36 + n * 2)); memcpy(&wav[8], "WAVE", 4);
    memcpy(&wav[12], "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1); w32(24, kSR); w32(28, kSR * 2); w16(32, 2); w16(34, 16);
    memcpy(&wav[36], "data", 4); w32(40, (uint32_t)(n * 2));
    for (size_t i = 0; i < n; ++i) {
        int16_t v = (int16_t)std::clamp(w.s[i] * norm * 32767.f, -32767.f, 32767.f);
        memcpy(&wav[44 + i * 2], &v, 2);
    }
    return wav;
}

const char* kSoundNames[] = {"Sino de boxe (3 toques)", "Sino único longo", "Gongo tailandês", "Apito de árbitro",
                             "Buzina", "Bipes digitais", "Batida de madeira (claque)", "Sem som"};
constexpr int kSoundCount = 8;

const std::vector<char>& SoundWav(int id) {
    static std::vector<char> cache[kSoundCount];
    id = std::clamp(id, 0, kSoundCount - 1);
    if (!cache[id].empty() || id == kSoundCount - 1) return cache[id];
    Wave w;
    switch (id) {
    case 0: for (int i = 0; i < 3; ++i) Bell(w, i * 0.34f, 880.f, 0.85f, 1.f); break;
    case 1: Bell(w, 0, 620.f, 2.6f, 1.f); break;
    case 2: Gong(w, 0, 1.f); break;
    case 3: Tone(w, 0, 0.35f, 2900.f, 0.8f, false, 0.03f); Tone(w, 0.48f, 0.7f, 2900.f, 0.8f, false, 0.03f); break;
    case 4: Tone(w, 0, 1.1f, 196.f, 0.6f, true); Tone(w, 0, 1.1f, 247.f, 0.5f, true); break;
    case 5: Tone(w, 0, 0.14f, 1000.f, 0.7f); Tone(w, 0.3f, 0.14f, 1000.f, 0.7f); Tone(w, 0.6f, 0.45f, 1500.f, 0.7f); break;
    case 6: Clack(w, 0, 1.f); Clack(w, 0.22f, 1.f); break;
    }
    cache[id] = ToWav(w);
    return cache[id];
}

int g_volume = 40; // 0-100 (Config::timerVolume)

// Toca um WAV em memória aplicando o volume (curva perceptiva).
void PlayScaled(const std::vector<char>& b) {
    if (b.size() <= 44 || g_volume <= 0) return;
    static std::vector<char> bufs[2];
    static int k = 0;
    k ^= 1;
    std::vector<char>& o = bufs[k];
    o = b;
    float v = std::pow(std::clamp(g_volume, 0, 100) / 100.f, 1.6f);
    for (size_t i = 44; i + 1 < o.size(); i += 2) {
        int16_t smp;
        memcpy(&smp, &o[i], 2);
        smp = (int16_t)(smp * v);
        memcpy(&o[i], &smp, 2);
    }
    PlaySoundW((LPCWSTR)o.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void Play(int id) { PlayScaled(SoundWav(id)); }

void Beep(bool high) {
    static std::vector<char> lo, hi;
    if (lo.empty()) { Wave a; Tone(a, 0, 0.12f, 880.f, 0.6f); lo = ToWav(a); Wave b; Tone(b, 0, 0.3f, 1320.f, 0.7f); hi = ToWav(b); }
    PlayScaled(high ? hi : lo);
}

// ------------------------------------------------------------------ Motor do timer
enum Phase { Stopped, Prep, Round, Rest, Done };
struct State {
    Phase phase = Stopped;
    int round = 0;
    double phaseStart = 0, phaseLen = 0;
    double pausedAt = -1;
    bool warned = false;
    int lastCount = -1;
    double doneAt = 0;
} g;

struct Preset { const char* name; int rounds, roundSec, restSec; };
const Preset kPresets[] = {
    {"Muay Thai amador · 3×2min", 3, 120, 60},
    {"Muay Thai profissional · 5×3min", 5, 180, 120},
    {"Treino de saco · 6×3min", 6, 180, 60},
    {"Sparring · 4×2min", 4, 120, 45},
    {"Pular corda · 5×1min", 5, 60, 20},
    {"Tabata · 8×20s", 8, 20, 10},
};

char g_badge[32] = "";

double Now() { return NowSeconds(); }
double Elapsed() { return (g.pausedAt >= 0 ? g.pausedAt : Now()) - g.phaseStart; }
double Remaining() { return std::max(0.0, g.phaseLen - Elapsed()); }

void StartPhase(App& app, Phase p, double len) {
    Config c = app.Cfg();
    g.phase = p;
    g.phaseStart = Now();
    g.phaseLen = len;
    g.warned = false;
    g.lastCount = -1;
    char msg[64];
    switch (p) {
    case Prep: if (c.timerVoice) app.Say("Prepare-se"); break;
    case Round:
        Play(c.timerStartSound);
        if (c.timerVoice) {
            if (g.round == c.timerRounds && c.timerRounds > 1) app.Say("Último round");
            else { snprintf(msg, sizeof(msg), "Round %d", g.round); app.Say(msg); }
        }
        break;
    case Rest: Play(c.timerEndSound); if (c.timerVoice) app.Say("Descanso"); break;
    case Done: Play(c.timerEndSound); if (c.timerVoice) app.Say("Fim do treino. Bom trabalho!"); g.doneAt = Now(); break;
    default: break;
    }
}

void Start(App& app) {
    Config c = app.Cfg();
    g.round = 1;
    g.pausedAt = -1;
    if (c.timerPrepSec > 0) StartPhase(app, Prep, c.timerPrepSec);
    else StartPhase(app, Round, c.timerRoundSec);
}

void Advance(App& app) {
    Config c = app.Cfg();
    switch (g.phase) {
    case Prep: StartPhase(app, Round, c.timerRoundSec); break;
    case Round:
        if (g.round >= c.timerRounds) StartPhase(app, Done, 0);
        else if (c.timerRestSec > 0) StartPhase(app, Rest, c.timerRestSec);
        else { ++g.round; StartPhase(app, Round, c.timerRoundSec); }
        break;
    case Rest: ++g.round; StartPhase(app, Round, c.timerRoundSec); break;
    default: break;
    }
}

std::string Clock(double s) {
    int t = (int)std::ceil(s - 1e-6);
    char b[16];
    snprintf(b, sizeof(b), "%d:%02d", t / 60, t % 60);
    return b;
}
} // namespace

void StartNow(App& app) { Start(app); }
bool Running() { return g.phase != Stopped && g.phase != Done && g.pausedAt < 0; }
const char* Badge() { return g_badge; }

void Tick(App& app) {
    g_badge[0] = 0;
    g_volume = app.Cfg().timerVolume;
    if (g.phase == Stopped) return;
    if (g.phase == Done) {
        if (Now() - g.doneAt > 8) g.phase = Stopped;
        else snprintf(g_badge, sizeof(g_badge), "Fim!");
        return;
    }
    Config c = app.Cfg();
    double rem = Remaining();
    const char* tag = g.phase == Prep ? "Prep" : g.phase == Rest ? "Desc" : nullptr;
    if (tag) snprintf(g_badge, sizeof(g_badge), "%s %s", tag, Clock(rem).c_str());
    else snprintf(g_badge, sizeof(g_badge), "R%d %s", g.round, Clock(rem).c_str());
    if (g.pausedAt >= 0) return;
    // aviso antes do fim do round (claque / "10 segundos")
    if (g.phase == Round && !g.warned && c.timerWarnSec > 0 && rem <= c.timerWarnSec && g.phaseLen > c.timerWarnSec + 1) {
        g.warned = true;
        Play(c.timerWarnSound);
        if (c.timerVoice) { char m[32]; snprintf(m, sizeof(m), "%d segundos", c.timerWarnSec); app.Say(m); }
    }
    // contagem 3-2-1 no fim da preparação / descanso
    if ((g.phase == Prep || g.phase == Rest) && rem <= 3.0 && rem > 0) {
        int sec = (int)std::ceil(rem);
        if (sec != g.lastCount) { g.lastCount = sec; Beep(false); }
    }
    if (rem <= 0) Advance(app);
}

namespace {
ImVec4 PhaseColor(const Config& c, double rem) {
    switch (g.phase) {
    case Prep: return theme::Amber;
    case Rest: return theme::Cyan;
    case Done: return theme::Green;
    case Round: return theme::Red;
    default: return ImVec4(0.55f, 0.58f, 0.64f, 1.f);
    }
}

const char* PhaseName() {
    switch (g.phase) {
    case Prep: return "PREPARE-SE";
    case Round: return "LUTA";
    case Rest: return "DESCANSO";
    case Done: return "FIM DO TREINO";
    default: return "PRONTO";
    }
}

std::string NextLabel(const Config& c) {
    switch (g.phase) {
    case Prep: return "A seguir: round 1 · " + Clock(c.timerRoundSec);
    case Round:
        if (g.round >= c.timerRounds) return "Último round";
        return c.timerRestSec > 0 ? "A seguir: descanso · " + Clock(c.timerRestSec) : "A seguir: round " + std::to_string(g.round + 1);
    case Rest: return "A seguir: round " + std::to_string(g.round + 1) + " · " + Clock(c.timerRoundSec);
    case Done: return "Bom trabalho!";
    default: {
        int total = c.timerRounds * c.timerRoundSec + std::max(0, c.timerRounds - 1) * c.timerRestSec + c.timerPrepSec;
        return std::to_string(c.timerRounds) + " rounds de " + Clock(c.timerRoundSec) + " · treino de " + Clock(total);
    }
    }
}

// Botão redondo desenhado à mão. kind: 0 play, 1 pause, 2 reiniciar, 3 pular
bool RoundButton(const char* id, int kind, float d, ImVec4 fill, bool solid, const char* tip) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool click = ImGui::InvisibleButton(id, ImVec2(d, d));
    float hov = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
    float press = ImGui::IsItemActive() ? 0.94f : 1.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    float r = d * 0.5f * press;
    if (solid) {
        dl->AddCircleFilled(c, r, anim::Col(anim::Lerp(fill, ImVec4(1, 1, 1, 1), hov * 0.12f)), 48);
    } else {
        dl->AddCircleFilled(c, r, anim::Col(anim::Lerp(ImVec4(0.13f, 0.145f, 0.175f, 1.f), ImVec4(0.18f, 0.2f, 0.24f, 1.f), hov)), 40);
        dl->AddCircle(c, r - px(0.5f), anim::Col(ImVec4(1, 1, 1, 0.08f)), 40, px(1));
    }
    ImU32 ic = anim::Col(solid ? ImVec4(1, 1, 1, 1) : anim::Lerp(theme::TextDim, theme::Text, hov));
    float s = d * 0.2f;
    switch (kind) {
    case 0: dl->AddTriangleFilled(ImVec2(c.x - s * 0.7f, c.y - s), ImVec2(c.x - s * 0.7f, c.y + s), ImVec2(c.x + s * 1.05f, c.y), ic); break;
    case 1:
        dl->AddRectFilled(ImVec2(c.x - s * 0.8f, c.y - s), ImVec2(c.x - s * 0.22f, c.y + s), ic, px(1.5f));
        dl->AddRectFilled(ImVec2(c.x + s * 0.22f, c.y - s), ImVec2(c.x + s * 0.8f, c.y + s), ic, px(1.5f));
        break;
    case 2: w::DrawIcon(dl, Icon::Refresh, c, d * 0.42f, ic); break;
    case 3:
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.9f, c.y - s * 0.85f), ImVec2(c.x - s * 0.9f, c.y + s * 0.85f), ImVec2(c.x + s * 0.45f, c.y), ic);
        dl->AddRectFilled(ImVec2(c.x + s * 0.55f, c.y - s * 0.85f), ImVec2(c.x + s * 0.9f, c.y + s * 0.85f), ic, px(1));
        break;
    }
    if (tip) w::Tooltip(tip);
    return click;
}

// Caixinha com − valor + (segura para repetir). Retorna true se mudou.
bool Stepper(const char* id, const char* label, int* v, int mn, int mx, int step, bool time, float wdt, const char* suffix = "") {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = px(78);
    dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), anim::Col(ImVec4(0.118f, 0.133f, 0.161f, 1.f)), px(8));
    ImGui::PushFont(theme::fonts.label);
    dl->AddText(ImVec2(p.x + px(12), p.y + px(9)), anim::Col(theme::TextDim), label);
    ImGui::PopFont();
    char val[32];
    if (time) {
        if (*v == 0) snprintf(val, sizeof(val), "--");
        else snprintf(val, sizeof(val), "%d:%02d", *v / 60, *v % 60);
    } else snprintf(val, sizeof(val), "%d%s", *v, suffix);
    ImFont* f = theme::fonts.title;
    ImVec2 ts = f->CalcTextSizeA(f->FontSize * 1.15f, FLT_MAX, 0, val);
    float cy = p.y + px(30) + (h - px(30)) * 0.5f;
    dl->AddText(f, f->FontSize * 1.15f, ImVec2(p.x + (wdt - ts.x) * 0.5f, cy - ts.y * 0.5f), anim::Col(theme::Text), val);
    bool changed = false;
    float b = px(30);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    for (int side = 0; side < 2; ++side) {
        ImVec2 bp(side == 0 ? p.x + px(10) : p.x + wdt - px(10) - b, cy - b * 0.5f);
        ImGui::SetCursorScreenPos(bp);
        ImGui::PushID(side);
        bool can = side == 0 ? *v > mn : *v < mx;
        if (ImGui::InvisibleButton("##b", ImVec2(b, b)) && can) {
            int nv = side == 0 ? *v - step : *v + step;
            // arredonda para o passo (ex.: 1:10 → 1:15 → 1:30)
            nv = side == 0 ? (int)std::ceil(nv / (float)step) * step : (nv / step) * step;
            *v = std::clamp(nv, mn, mx);
            changed = true;
        }
        float hv = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() && can ? 1.f : 0.f, 14.f);
        ImGui::PopID();
        ImVec2 c(bp.x + b * 0.5f, bp.y + b * 0.5f);
        dl->AddCircleFilled(c, b * 0.5f, anim::Col(anim::Lerp(ImVec4(1, 1, 1, 0.05f), anim::WithAlpha(theme::Cyan, 0.9f), hv)), 28);
        ImU32 ic = anim::Col(can ? theme::Text : theme::TextFaint);
        float s = b * 0.22f, t = px(1.8f);
        dl->AddRectFilled(ImVec2(c.x - s, c.y - t * 0.5f), ImVec2(c.x + s, c.y + t * 0.5f), ic);
        if (side == 1) dl->AddRectFilled(ImVec2(c.x - t * 0.5f, c.y - s), ImVec2(c.x + t * 0.5f, c.y + s), ic);
    }
    ImGui::PopItemFlag();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(wdt, 0));
    ImGui::PopID();
    return changed;
}

void SubTitle(const char* t) {
    ImGui::Dummy(ImVec2(0, px(6)));
    w::SectionTitle(t);
}
} // namespace

void Window(App& app, bool* open) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(360), px(420)), ImVec2(FLT_MAX, FLT_MAX));
    if (!ui::BeginPanel("Timer###timer2", open, ImGuiWindowFlags_NoCollapse, theme::Red)) { ImGui::End(); return; }
    Config c = app.Cfg();
    bool ch = false;
    const bool live = g.phase != Stopped && g.phase != Done;  // treinando: só o relógio, grande
    const bool running = Running();

    float avail = ImGui::GetContentRegionAvail().x;
    double rem = g.phase == Stopped ? c.timerRoundSec : g.phase == Done ? 0.0 : Remaining();
    ImVec4 col = PhaseColor(c, rem);
    bool warn = g.phase == Round && c.timerWarnSec > 0 && rem <= c.timerWarnSec && rem > 0;
    float flash = warn && running ? 0.5f + 0.5f * std::sin(anim::Time() * 9.f) : 0.f;
    col = anim::Lerp(col, warn ? theme::Amber : col, flash);
    ImVec4 colS(anim::To(ImGui::GetID("##cr"), col.x, 8.f), anim::To(ImGui::GetID("##cg"), col.y, 8.f),
                anim::To(ImGui::GetID("##cb"), col.z, 8.f), 1.f);

    // ---------------------------------------------------------- placar
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float ctrlH = px(96);
    float cardH = live ? std::max(px(300), ImGui::GetContentRegionAvail().y - ctrlH - px(8)) : px(290);
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + avail, a.y + cardH);
    ImGui::Dummy(ImVec2(avail, cardH));
    float rad = px(10);
    dl->AddRectFilled(a, b, anim::Col(ImVec4(0.067f, 0.072f, 0.086f, 1.f)), rad);
    // brilho da fase vindo de cima
    dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + px(40)), ImVec2(b.x, a.y + cardH * 0.75f),
                                anim::Col(anim::WithAlpha(colS, 0.16f + 0.10f * flash)), anim::Col(anim::WithAlpha(colS, 0.16f + 0.10f * flash)),
                                anim::Col(anim::WithAlpha(colS, 0.f)), anim::Col(anim::WithAlpha(colS, 0.f)));
    dl->AddRect(a, b, anim::Col(anim::WithAlpha(colS, 0.35f)), rad, 0, px(1));
    // faixa da fase
    float bandH = px(40);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + bandH), anim::Col(anim::WithAlpha(colS, 0.92f)), rad, ImDrawFlags_RoundCornersTop);
    ImGui::PushFont(theme::fonts.title);
    {
        const char* pn = PhaseName();
        ImVec2 ts = ImGui::CalcTextSize(pn);
        dl->AddText(ImVec2(a.x + px(16), a.y + (bandH - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), pn);
        char rl[32];
        if (g.phase == Stopped) snprintf(rl, sizeof(rl), "%d ROUNDS", c.timerRounds);
        else snprintf(rl, sizeof(rl), "ROUND %d/%d", std::clamp(g.round, 1, c.timerRounds), c.timerRounds);
        ImVec2 rs = ImGui::CalcTextSize(rl);
        dl->AddText(ImVec2(b.x - rs.x - px(16), a.y + (bandH - rs.y) * 0.5f), IM_COL32(255, 255, 255, 230), rl);
    }
    ImGui::PopFont();
    if (g.pausedAt >= 0 && live) {
        ImGui::PushFont(theme::fonts.label);
        const char* pz = "PAUSADO";
        ImVec2 ts = ImGui::CalcTextSize(pz);
        float al = 0.6f + 0.4f * std::sin(anim::Time() * 4.f);
        dl->AddText(ImVec2(a.x + (avail - ts.x) * 0.5f, a.y + bandH + px(12)), anim::Col(anim::WithAlpha(theme::Text, al)), pz);
        ImGui::PopFont();
    }
    // relógio
    std::string big = Clock(rem);
    ImFont* cf = theme::fonts.clock;
    float maxH = cardH - bandH - px(96);
    float fs = std::min(cf->FontSize, maxH);
    ImVec2 bs = cf->CalcTextSizeA(fs, FLT_MAX, 0, big.c_str());
    if (bs.x > avail - px(40)) { fs *= (avail - px(40)) / bs.x; bs = cf->CalcTextSizeA(fs, FLT_MAX, 0, big.c_str()); }
    float midY = a.y + bandH + (cardH - bandH - px(52)) * 0.5f;
    ImVec2 tp(a.x + (avail - bs.x) * 0.5f, midY - bs.y * 0.5f);
    ImVec4 clockCol = warn ? anim::Lerp(theme::Text, theme::Amber, flash) : theme::Text;
    if (g.pausedAt >= 0 && live) clockCol = anim::WithAlpha(theme::Text, 0.55f);
    dl->AddText(cf, fs, tp, anim::Col(clockCol), big.c_str());
    dl->AddText(cf, fs, ImVec2(tp.x + px(1), tp.y), anim::Col(clockCol), big.c_str()); // um pouco mais grosso
    // próximo
    ImGui::PushFont(live ? theme::fonts.semibold : theme::fonts.caption);
    {
        std::string nx = NextLabel(c);
        ImVec2 ts = ImGui::CalcTextSize(nx.c_str());
        dl->AddText(ImVec2(a.x + (avail - ts.x) * 0.5f, tp.y + bs.y + px(2)), anim::Col(theme::TextDim), nx.c_str());
    }
    ImGui::PopFont();
    // marcadores dos rounds
    {
        int n = std::max(1, c.timerRounds);
        float pad = px(16), gap = px(5);
        float mw = std::min(px(46), (avail - pad * 2 - gap * (n - 1)) / n);
        float total = mw * n + gap * (n - 1);
        float x = a.x + (avail - total) * 0.5f, y = b.y - px(40);
        for (int i = 1; i <= n; ++i) {
            bool done = g.phase == Done || (live && (i < g.round || (i == g.round && g.phase == Rest)));
            bool cur = live && i == g.round && g.phase == Round;
            ImVec4 mc = done ? ImVec4(1, 1, 1, 0.75f) : cur ? colS : ImVec4(1, 1, 1, 0.10f);
            ImVec2 m0(x, y), m1(x + mw, y + px(6));
            dl->AddRectFilled(m0, m1, anim::Col(mc), px(3));
            if (cur && g.phaseLen > 0) {
                // round atual enche conforme o tempo passa
                float f = 1.f - (float)(rem / g.phaseLen);
                dl->AddRectFilled(m0, m1, anim::Col(ImVec4(1, 1, 1, 0.12f)), px(3));
                dl->AddRectFilled(m0, ImVec2(m0.x + mw * f, m1.y), anim::Col(colS), px(3));
            }
            x += mw + gap;
        }
    }
    // barra da fase atual
    {
        float frac = g.phase == Stopped || g.phaseLen <= 0 ? (g.phase == Done ? 1.f : 0.f) : 1.f - (float)(rem / g.phaseLen);
        float sh = anim::To(ImGui::GetID("##tbar"), frac, 12.f);
        ImVec2 p0(a.x + px(16), b.y - px(20)), p1(b.x - px(16), b.y - px(16));
        dl->AddRectFilled(p0, p1, anim::Col(ImVec4(1, 1, 1, 0.08f)), px(2));
        if (sh > 0.002f) dl->AddRectFilled(p0, ImVec2(p0.x + (p1.x - p0.x) * sh, p1.y), anim::Col(colS), px(2));
    }

    // ---------------------------------------------------------- controles
    ImGui::Dummy(ImVec2(0, px(4)));
    {
        float big2 = px(72), small = px(52), gap = px(28);
        float total = big2 + small * 2 + gap * 2;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float x = p.x + (avail - total) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(x, p.y + (big2 - small) * 0.5f));
        if (RoundButton("##reset", 2, small, theme::Cyan, false, "Reiniciar")) g = State();
        ImGui::SetCursorScreenPos(ImVec2(x + small + gap, p.y));
        bool startNew = g.phase == Stopped || g.phase == Done;
        if (RoundButton("##play", running ? 1 : 0, big2, startNew ? theme::Red : colS, true, startNew ? "Começar" : running ? "Pausar" : "Continuar")) {
            if (startNew) Start(app);
            else if (running) g.pausedAt = Now();
            else { g.phaseStart += Now() - g.pausedAt; g.pausedAt = -1; }
        }
        ImGui::SetCursorScreenPos(ImVec2(x + small + gap + big2 + gap, p.y + (big2 - small) * 0.5f));
        if (RoundButton("##skip", 3, small, theme::Cyan, false, "Pular para a próxima fase") && live) {
            if (g.pausedAt >= 0) g.pausedAt = -1;
            Advance(app);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + big2 + px(8)));
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (live) { ImGui::End(); return; }

    // ---------------------------------------------------------- treino
    SubTitle("Treino");
    {
        float gap = px(10);
        float cw = (avail - gap) * 0.5f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ch |= Stepper("r", "ROUNDS", &c.timerRounds, 1, 30, 1, false, cw);
        ImGui::SetCursorScreenPos(ImVec2(p.x + cw + gap, p.y));
        ch |= Stepper("d", "DURAÇÃO DO ROUND", &c.timerRoundSec, 15, 1800, 15, true, cw);
        p.y += px(78) + gap;
        ImGui::SetCursorScreenPos(p);
        ch |= Stepper("rest", "DESCANSO", &c.timerRestSec, 0, 600, 15, true, cw);
        ImGui::SetCursorScreenPos(ImVec2(p.x + cw + gap, p.y));
        ch |= Stepper("prep", "PREPARAÇÃO", &c.timerPrepSec, 0, 60, 5, true, cw);
        p.y += px(78) + gap;
        ImGui::SetCursorScreenPos(p);
        ch |= Stepper("warn", "AVISO ANTES DO FIM", &c.timerWarnSec, 0, 30, 5, true, cw);
        ImGui::SetCursorScreenPos(ImVec2(p.x + cw + gap, p.y));
        if (Stepper("vol", "VOLUME DOS SINAIS", &c.timerVolume, 0, 100, 10, false, cw, "%")) { ch = true; g_volume = c.timerVolume; Play(c.timerStartSound); }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(78)));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // ---------------------------------------------------------- treinos prontos (cartões em 2 colunas)
    auto workoutCard = [&](const char* id, const std::string& name, int r, int rs, int rest, float cw, bool* del) -> bool {
        ImGui::PushID(id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = px(56);
        bool active = c.timerRounds == r && c.timerRoundSec == rs && c.timerRestSec == rest;
        bool click = ImGui::InvisibleButton("##c", ImVec2(cw - (del ? px(40) : 0), h));
        float hv = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + h), anim::Col(anim::Lerp(ImVec4(0.118f, 0.133f, 0.161f, 1.f), theme::PanelHi, hv)), px(8));
        if (active) dl->AddRect(p, ImVec2(p.x + cw, p.y + h), anim::Col(theme::Red), px(8), 0, px(1.5f));
        float tw = cw - px(24) - (del ? px(40) : 0);
        std::string nm = name;
        ImGui::PushFont(theme::fonts.semibold);
        while (nm.size() > 3 && ImGui::CalcTextSize(nm.c_str()).x > tw) {
            nm.pop_back();
            while (!nm.empty() && ((unsigned char)nm.back() & 0xC0) == 0x80) nm.pop_back();
        }
        if (nm != name) nm += "…";
        dl->AddText(ImVec2(p.x + px(12), p.y + px(8)), anim::Col(theme::Text), nm.c_str());
        ImGui::PopFont();
        char det[64];
        snprintf(det, sizeof(det), "%d × %s  ·  descanso %s", r, Clock(rs).c_str(), rest ? Clock(rest).c_str() : "--");
        ImGui::PushFont(theme::fonts.caption);
        dl->AddText(ImVec2(p.x + px(12), p.y + px(31)), anim::Col(active ? theme::Red : theme::TextDim), det);
        ImGui::PopFont();
        if (del) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - px(38), p.y + (h - px(30)) * 0.5f));
            *del = w::IconButton("del", Icon::Trash, px(30), "Apagar");
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        ImGui::Dummy(ImVec2(cw, 0));
        ImGui::PopID();
        if (click) { c.timerRounds = r; c.timerRoundSec = rs; c.timerRestSec = rest; ch = true; }
        return click;
    };
    SubTitle("Treinos prontos");
    {
        float gap = px(10), cw = (avail - gap) * 0.5f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        int n = (int)std::size(kPresets);
        for (int i = 0; i < n; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + (i % 2) * (cw + gap), p0.y + (i / 2) * (px(56) + gap)));
            std::string nm = kPresets[i].name;
            nm = nm.substr(0, nm.find(" · "));
            workoutCard(kPresets[i].name, nm, kPresets[i].rounds, kPresets[i].roundSec, kPresets[i].restSec, cw, nullptr);
        }
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + ((n + 1) / 2) * (px(56) + gap) - gap));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // ---------------------------------------------------------- meus treinos
    SubTitle("Meus treinos");
    {
        int removeIdx = -1;
        float gap = px(10), cw = (avail - gap) * 0.5f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        int k = 0;
        for (size_t i = 0; i < c.workouts.size(); ++i) {
            auto parts = text::Split(c.workouts[i], '|');
            if (parts.size() < 4) continue;
            ImGui::SetCursorScreenPos(ImVec2(p0.x + (k % 2) * (cw + gap), p0.y + (k / 2) * (px(56) + gap)));
            bool del = false;
            workoutCard(std::to_string(1000 + i).c_str(), parts[0], atoi(parts[1].c_str()), atoi(parts[2].c_str()), atoi(parts[3].c_str()), cw, &del);
            if (del) removeIdx = (int)i;
            ++k;
        }
        if (k) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + ((k + 1) / 2) * (px(56) + gap)));
            ImGui::Dummy(ImVec2(0, 0));
        } else {
            w::Hint("Nenhum treino salvo ainda. Diga \"KB, criar treino\" e eu pergunto o nome, os rounds e o tempo.");
        }
        if (removeIdx >= 0) { c.workouts.erase(c.workouts.begin() + removeIdx); ch = true; }
        if (w::GhostButton("Criar treino por voz", ImVec2(0, 0), Icon::Mic)) app.SendText("criar treino");
    }

    // ---------------------------------------------------------- sinais
    SubTitle("Sinais");
    auto soundCombo = [&](const char* label, const char* id, int* v) {
        ImGui::PushID(id);
        float lw = px(150);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine(lw);
        float tb = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - tb - px(8));
        if (ImGui::BeginCombo("##c", kSoundNames[std::clamp(*v, 0, kSoundCount - 1)])) {
            for (int s = 0; s < kSoundCount; ++s)
                if (ImGui::Selectable(kSoundNames[s], s == *v)) { *v = s; ch = true; Play(s); }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, px(8));
        if (w::IconButton("test", Icon::Play, tb, "Ouvir")) Play(*v);
        ImGui::PopID();
    };
    soundCombo("Início do round", "s1", &c.timerStartSound);
    soundCombo("Fim do round", "s2", &c.timerEndSound);
    soundCombo("Últimos segundos", "s3", &c.timerWarnSound);
    ImGui::Dummy(ImVec2(0, px(2)));
    ch |= w::Toggle("Anunciar por voz", &c.timerVoice, "Fala \"round 2\", \"descanso\" e \"10 segundos\".");

    if (ch) app.ApplyConfig(c);
    ImGui::End();
}

} // namespace timer
