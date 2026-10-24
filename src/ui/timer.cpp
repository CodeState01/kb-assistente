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

void Window(App& app, bool* open) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(320), px(360)), ImVec2(FLT_MAX, FLT_MAX));
    if (!ui::BeginPanel("Timer###timer", open, 0, theme::Red)) { ImGui::End(); return; }
    Config c = app.Cfg();
    bool ch = false;

    // ---- relógio
    float avail = ImGui::GetContentRegionAvail().x;
    float R = std::min(avail * 0.36f, px(150));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 cc(p0.x + avail * 0.5f, p0.y + R + px(16));
    ImGui::Dummy(ImVec2(avail, R * 2 + px(36)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    double rem = g.phase == Stopped ? c.timerRoundSec : Remaining();
    float frac = g.phase == Stopped || g.phaseLen <= 0 ? 1.f : (float)(rem / g.phaseLen);
    ImVec4 col = g.phase == Prep ? theme::Amber : g.phase == Rest ? theme::Blue : g.phase == Done ? theme::Violet : theme::Green;
    if (g.phase == Round && c.timerWarnSec > 0 && rem <= c.timerWarnSec) col = theme::Red;
    float shown = anim::To(ImGui::GetID("##tfrac"), frac, 10.f);
    float pulse = (g.phase == Round && rem <= c.timerWarnSec) ? 0.5f + 0.5f * std::sin(anim::Time() * 10.f) : 0.f;
    orbfx::RadialGradient(dl, cc, R * 0.7f, R * (1.35f + 0.08f * pulse), anim::Col(anim::WithAlpha(col, 0.18f + 0.15f * pulse)), anim::Col(anim::WithAlpha(col, 0)), 64);
    dl->AddCircle(cc, R, anim::Col(ImVec4(1, 1, 1, 0.08f)), 96, px(10));
    if (shown > 0.001f) {
        dl->PathArcTo(cc, R, -PI * 0.5f, -PI * 0.5f + 2 * PI * shown, 96);
        dl->PathStroke(anim::Col(col), 0, px(10));
        float ea = -PI * 0.5f + 2 * PI * shown;
        ImVec2 tip(cc.x + std::cos(ea) * R, cc.y + std::sin(ea) * R);
        dl->AddCircleFilled(tip, px(9), anim::Col(ImVec4(1, 1, 1, 0.95f)), 20);
        orbfx::RadialGradient(dl, tip, 0, px(26), anim::Col(anim::WithAlpha(col, 0.6f)), anim::Col(anim::WithAlpha(col, 0)), 24);
    }
    // partículas girando em volta do relógio
    for (int i = 0; i < 60; ++i) {
        float a = i * 2 * PI / 60 + anim::Time() * (Running() ? 0.6f : 0.15f);
        float rr = R * (1.12f + 0.05f * std::sin(i * 1.7f + anim::Time() * 2.f));
        dl->AddCircleFilled(ImVec2(cc.x + std::cos(a) * rr, cc.y + std::sin(a) * rr), px(1.6f), anim::Col(anim::WithAlpha(col, 0.35f)), 6);
    }
    const char* phaseName = g.phase == Prep ? "PREPARAR" : g.phase == Round ? "ROUND" : g.phase == Rest ? "DESCANSO" : g.phase == Done ? "FIM DO TREINO" : "PRONTO";
    std::string big = g.phase == Done ? "0:00" : Clock(rem);
    ImFont* hf = theme::fonts.huge;
    float bigSize = std::min(R * 0.62f, hf->FontSize * 2.6f);
    ImVec2 bs = hf->CalcTextSizeA(bigSize, FLT_MAX, 0, big.c_str());
    dl->AddText(hf, bigSize, ImVec2(cc.x - bs.x * 0.5f, cc.y - bs.y * 0.5f), anim::Col(theme::Text), big.c_str());
    ImGui::PushFont(theme::fonts.semibold);
    ImVec2 ps = ImGui::CalcTextSize(phaseName);
    dl->AddText(ImVec2(cc.x - ps.x * 0.5f, cc.y - bs.y * 0.5f - ps.y - px(2)), anim::Col(col), phaseName);
    ImGui::PopFont();
    char rl[48];
    snprintf(rl, sizeof(rl), "Round %d de %d", std::max(1, g.round), c.timerRounds);
    ImGui::PushFont(theme::fonts.caption);
    ImVec2 rs = ImGui::CalcTextSize(rl);
    dl->AddText(ImVec2(cc.x - rs.x * 0.5f, cc.y + bs.y * 0.5f + px(2)), anim::Col(theme::TextDim), rl);
    ImGui::PopFont();

    // ---- controles
    float bw = px(120);
    float total = bw * 3 + px(20);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (avail - total) * 0.5f));
    bool running = Running();
    if (w::PrimaryButton(g.phase == Stopped || g.phase == Done ? "Iniciar" : running ? "Pausar" : "Continuar", ImVec2(bw, px(42)),
                         running ? Icon::Stop : Icon::Play)) {
        if (g.phase == Stopped || g.phase == Done) Start(app);
        else if (running) g.pausedAt = Now();
        else { g.phaseStart += Now() - g.pausedAt; g.pausedAt = -1; }
    }
    ImGui::SameLine(0, px(10));
    if (w::GhostButton("Reiniciar", ImVec2(bw, px(42)), Icon::Refresh)) { g = State(); }
    ImGui::SameLine(0, px(10));
    if (w::GhostButton("Pular", ImVec2(bw, px(42)), Icon::Send) && g.phase != Stopped && g.phase != Done) {
        if (g.pausedAt >= 0) g.pausedAt = -1;
        Advance(app);
    }

    // ---- predefinições
    w::SectionTitle("Treinos prontos", Icon::Sparkle);
    float lineW = 0;
    for (size_t i = 0; i < std::size(kPresets); ++i) {
        float wdt = ImGui::CalcTextSize(kPresets[i].name).x + px(38);
        if (i && lineW + wdt < avail) ImGui::SameLine(0, px(8));
        else lineW = 0;
        lineW += wdt + px(8);
        bool active = c.timerRounds == kPresets[i].rounds && c.timerRoundSec == kPresets[i].roundSec && c.timerRestSec == kPresets[i].restSec;
        ImGui::PushID((int)i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, theme::Cyan);
        if (w::Chip(kPresets[i].name)) {
            c.timerRounds = kPresets[i].rounds;
            c.timerRoundSec = kPresets[i].roundSec;
            c.timerRestSec = kPresets[i].restSec;
            ch = true;
        }
        if (active) ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // ---- treinos criados pela pessoa (por voz: "KB, criar treino")
    w::SectionTitle("Meus treinos", Icon::Sparkle);
    if (c.workouts.empty()) w::Hint("Diga \"KB, criar treino\" ou clique abaixo: eu pergunto o nome, quantos rounds e quanto tempo.");
    int removeIdx = -1;
    for (size_t i = 0; i < c.workouts.size(); ++i) {
        auto parts = text::Split(c.workouts[i], '|');
        if (parts.size() < 4) continue;
        int r = atoi(parts[1].c_str()), rs = atoi(parts[2].c_str()), rest = atoi(parts[3].c_str());
        ImGui::PushID(1000 + (int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rowW = ImGui::GetContentRegionAvail().x, rowH = ImGui::GetFrameHeight() + px(14);
        bool active = c.timerRounds == r && c.timerRoundSec == rs && c.timerRestSec == rest;
        bool clicked = ImGui::InvisibleButton("##w", ImVec2(rowW - px(80), rowH));
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        dl2->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH), anim::Col(hov ? theme::PanelHi : ImVec4(0.118f, 0.133f, 0.161f, 1.f)), px(6));
        dl2->AddRectFilled(p, ImVec2(p.x + px(3), p.y + rowH), anim::Col(active ? theme::Red : theme::Cyan), px(6), ImDrawFlags_RoundCornersLeft);
        ImGui::PushFont(theme::fonts.semibold);
        dl2->AddText(ImVec2(p.x + px(14), p.y + px(4)), anim::Col(theme::Text), parts[0].c_str());
        ImGui::PopFont();
        char det[96];
        snprintf(det, sizeof(det), "%d rounds × %s  ·  descanso %s", r, Clock(rs).c_str(), rest ? Clock(rest).c_str() : "nenhum");
        ImGui::PushFont(theme::fonts.caption);
        dl2->AddText(ImVec2(p.x + px(14), p.y + rowH * 0.5f + px(1)), anim::Col(theme::TextDim), det);
        ImGui::PopFont();
        if (clicked) { c.timerRounds = r; c.timerRoundSec = rs; c.timerRestSec = rest; ch = true; }
        ImGui::SameLine(0, 0);
        ImGui::SetCursorScreenPos(ImVec2(p.x + rowW - px(76), p.y + (rowH - px(32)) * 0.5f));
        if (w::IconButton("play", Icon::Play, px(32), "Começar este treino")) {
            c.timerRounds = r; c.timerRoundSec = rs; c.timerRestSec = rest;
            app.ApplyConfig(c);
            Start(app);
        }
        ImGui::SameLine(0, px(6));
        if (w::IconButton("del", Icon::Trash, px(32), "Apagar")) removeIdx = (int)i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + px(6)));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (removeIdx >= 0) { c.workouts.erase(c.workouts.begin() + removeIdx); ch = true; }
    if (w::GhostButton("Novo treino", ImVec2(0, 0), Icon::Mic)) app.SendText("criar treino");
    ImGui::SameLine();
    w::Hint("Também por voz: \"KB, começar treino <nome>\".");

    // ---- ajustes
    w::SectionTitle("Ajustes", Icon::Gear);
    char fmt[64];
    ImGui::SetNextItemWidth(-1);
    snprintf(fmt, sizeof(fmt), "Rounds: %d", c.timerRounds);
    ch |= ImGui::SliderInt("##rounds", &c.timerRounds, 1, 15, fmt);
    auto timeSlider = [&](const char* id, const char* label, int* v, int mn, int mx, int step) {
        int steps = *v / step;
        snprintf(fmt, sizeof(fmt), "%s: %d:%02d", label, *v / 60, *v % 60);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt(id, &steps, mn / step, mx / step, fmt)) { *v = steps * step; ch = true; }
    };
    timeSlider("##round", "Duração do round", &c.timerRoundSec, 15, 600, 15);
    timeSlider("##rest", "Descanso", &c.timerRestSec, 0, 300, 5);
    timeSlider("##prep", "Preparação", &c.timerPrepSec, 0, 60, 5);
    ImGui::SetNextItemWidth(-1);
    snprintf(fmt, sizeof(fmt), c.timerWarnSec ? "Aviso antes do fim: %d s" : "Aviso antes do fim: desligado", c.timerWarnSec);
    ch |= ImGui::SliderInt("##warn", &c.timerWarnSec, 0, 30, fmt);

    w::SectionTitle("Sinais", Icon::Wave);
    auto soundCombo = [&](const char* label, const char* id, int* v) {
        ImGui::PushID(id);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        float tb = px(36);
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
    soundCombo("Aviso (últimos segundos)", "s3", &c.timerWarnSound);
    ImGui::SetNextItemWidth(-1);
    snprintf(fmt, sizeof(fmt), c.timerVolume ? "Volume dos sinais: %d%%%%" : "Sinais sem som", c.timerVolume);
    if (ImGui::SliderInt("##vol", &c.timerVolume, 0, 100, fmt)) ch = true;
    if (ImGui::IsItemDeactivatedAfterEdit()) { g_volume = c.timerVolume; Play(c.timerStartSound); }
    ch |= w::Toggle("Anunciar por voz (round, descanso, últimos segundos)", &c.timerVoice);
    w::Hint("Dica: peça por voz \"KB, abre o timer\" ou use o ícone de cronômetro na barra lateral.");

    if (ch) app.ApplyConfig(c);
    ImGui::End();
}

} // namespace timer
