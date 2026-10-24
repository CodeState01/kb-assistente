#include "ui/anim.h"
#include <cmath>
#include <unordered_map>

namespace anim {

static bool g_reduce = false;
static std::unordered_map<ImGuiID, float> g_vals;
static std::unordered_map<ImGuiID, ImVec2> g_springs;

float Dt() {
    float dt = ImGui::GetIO().DeltaTime;
    return dt > 0.1f ? 0.1f : dt;
}
float Time() { return (float)ImGui::GetTime(); }
void SetReduceMotion(bool v) { g_reduce = v; }
bool ReduceMotion() { return g_reduce; }

float Approach(float cur, float target, float speed) {
    if (g_reduce) speed *= 2.5f;
    float k = 1.f - std::exp(-speed * Dt());
    return cur + (target - cur) * k;
}

float To(ImGuiID id, float target, float speed, float initial) {
    auto it = g_vals.find(id);
    if (it == g_vals.end()) it = g_vals.emplace(id, initial < 0 ? target : initial).first;
    it->second = Approach(it->second, target, speed);
    if (std::fabs(it->second - target) < 0.0005f) it->second = target;
    return it->second;
}

float To(const char* key, float target, float speed, float initial) { return To(ImGui::GetID(key), target, speed, initial); }

float Spring(ImGuiID id, float target, float k, float d) {
    auto it = g_springs.find(id);
    if (it == g_springs.end()) it = g_springs.emplace(id, ImVec2(target, 0)).first;
    ImVec2& s = it->second;
    if (g_reduce) { s.x = Approach(s.x, target, 20.f); s.y = 0; return s.x; }
    float dt = Dt();
    // integra em passos pequenos para estabilidade
    int steps = 1 + (int)(dt / 0.008f);
    float h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        float a = -k * (s.x - target) - d * s.y;
        s.y += a * h;
        s.x += s.y * h;
    }
    return s.x;
}

float Clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float EaseOutCubic(float t) { t = Clamp01(t); float u = 1 - t; return 1 - u * u * u; }
float EaseInOutCubic(float t) { t = Clamp01(t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }
float EaseOutBack(float t) {
    t = Clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3.f) + c1 * std::pow(t - 1, 2.f);
}
float EaseOutElastic(float t) {
    t = Clamp01(t);
    if (t == 0 || t == 1) return t;
    const float c4 = (2 * 3.14159265f) / 3;
    return std::pow(2.f, -10 * t) * std::sin((t * 10 - 0.75f) * c4) + 1;
}
float Smoothstep(float a, float b, float x) {
    float t = Clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}

static float Hash(int n) {
    unsigned int x = (unsigned int)n;
    x = (x << 13) ^ x;
    x = x * (x * x * 15731u + 789221u) + 1376312589u;
    return 1.f - (float)(x & 0x7fffffffu) / 1073741824.f;
}
float Noise(float x) {
    int i = (int)std::floor(x);
    float f = x - i;
    float u = f * f * (3 - 2 * f);
    return Hash(i) * (1 - u) + Hash(i + 1) * u;
}
float Noise2(float x, float y) { return Noise(x + Noise(y * 1.37f) * 3.1f); }

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
ImU32 Col(const ImVec4& c, float alphaMul) {
    ImVec4 v = c;
    v.w *= alphaMul;
    return ImGui::GetColorU32(v);
}
ImVec4 WithAlpha(ImVec4 c, float a) { c.w = a; return c; }
ImVec4 Hsv(float h, float s, float v, float a) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImVec4(r, g, b, a);
}

} // namespace anim
