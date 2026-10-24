#pragma once
#include "imgui.h"
#include <vector>

enum class OrbMode { Idle = 0, Listening, Transcribing, Thinking, Speaking, Showing, Error, Count };

struct OrbInput {
    OrbMode mode = OrbMode::Idle;
    float micLevel = 0.f;        // 0..1
    float speechLevel = 0.f;     // 0..1
    const float* micHistory = nullptr;
    int micHistoryN = 0;
    bool hovered = false;
    bool pressed = false;
    bool compact = false;        // versão mini (menos efeitos)
};

// Orb animado: cada modo tem um conjunto de efeitos; transições são cruzadas por pesos suavizados.
class Orb {
public:
    void Draw(ImDrawList* dl, ImVec2 center, float radius, const OrbInput& in);
    ImVec4 CurrentColor() const { return colA_; }

private:
    struct Ripple { float born; float strength; };
    struct Particle { float angle, radius, speed, size, life, born; };
    float weight_[(int)OrbMode::Count] = {1.f};
    float level_ = 0.f, speech_ = 0.f, hover_ = 0.f, press_ = 0.f, rot_ = 0.f, think_ = 0.f;
    float lastRipple_ = 0.f;
    std::vector<Ripple> ripples_;
    std::vector<Particle> sparks_;
    float bars_[48] = {};
    ImVec4 colA_, colB_, colC_;
    bool init_ = false;
};

namespace orbfx {
// Utilitários de desenho com gradiente (também usados no fundo).
void RadialGradient(ImDrawList* dl, ImVec2 c, float r0, float r1, ImU32 inner, ImU32 outer, int segs = 64);
void FillStar(ImDrawList* dl, ImVec2 c, const ImVec2* pts, int n, ImU32 center, ImU32 edge);
}
