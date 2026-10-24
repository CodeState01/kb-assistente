#pragma once
#include "imgui.h"

// Pequeno sistema de animação: valores suavizados por ID, molas e curvas de easing.
namespace anim {

float Dt();
float Time();
void SetReduceMotion(bool v);
bool ReduceMotion();

// Aproxima exponencialmente de 'target' (independente do FPS).
float To(ImGuiID id, float target, float speed = 12.f, float initial = -1.f);
float To(const char* key, float target, float speed = 12.f, float initial = -1.f);
// Mola amortecida (dá aquele "overshoot" suave).
float Spring(ImGuiID id, float target, float stiffness = 180.f, float damping = 18.f);

float Approach(float cur, float target, float speed);
float Clamp01(float x);
float EaseOutCubic(float t);
float EaseInOutCubic(float t);
float EaseOutBack(float t);
float EaseOutElastic(float t);
float Smoothstep(float a, float b, float x);
float Noise(float x);                 // ruído suave 1D [-1, 1]
float Noise2(float x, float y);

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t);
ImU32 Col(const ImVec4& c, float alphaMul = 1.f); // aplica o alpha global de estilo
ImVec4 WithAlpha(ImVec4 c, float a);
ImVec4 Hsv(float h, float s, float v, float a = 1.f);

} // namespace anim
