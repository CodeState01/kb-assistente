#include "ui/orb.h"
#include "ui/anim.h"
#include "ui/theme.h"
#include "ui/assets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

static constexpr float PI = 3.14159265f;
static constexpr float TAU = 6.28318531f;

namespace orbfx {

void RadialGradient(ImDrawList* dl, ImVec2 c, float r0, float r1, ImU32 inner, ImU32 outer, int segs) {
    if (((inner | outer) & IM_COL32_A_MASK) == 0) return;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    if (r0 <= 0.5f) {
        dl->PrimReserve(segs * 3, segs + 1);
        unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(c, uv, inner);
        for (int i = 0; i < segs; ++i) {
            float a = TAU * i / segs;
            dl->PrimWriteVtx(ImVec2(c.x + std::cos(a) * r1, c.y + std::sin(a) * r1), uv, outer);
        }
        for (int i = 0; i < segs; ++i) {
            dl->PrimWriteIdx((ImDrawIdx)base);
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % segs));
        }
        return;
    }
    dl->PrimReserve(segs * 6, segs * 2);
    unsigned int base = dl->_VtxCurrentIdx;
    for (int i = 0; i < segs; ++i) {
        float a = TAU * i / segs;
        float cs = std::cos(a), sn = std::sin(a);
        dl->PrimWriteVtx(ImVec2(c.x + cs * r0, c.y + sn * r0), uv, inner);
        dl->PrimWriteVtx(ImVec2(c.x + cs * r1, c.y + sn * r1), uv, outer);
    }
    for (int i = 0; i < segs; ++i) {
        int j = (i + 1) % segs;
        unsigned int i0 = base + i * 2, i1 = i0 + 1, j0 = base + j * 2, j1 = j0 + 1;
        dl->PrimWriteIdx((ImDrawIdx)i0); dl->PrimWriteIdx((ImDrawIdx)i1); dl->PrimWriteIdx((ImDrawIdx)j1);
        dl->PrimWriteIdx((ImDrawIdx)i0); dl->PrimWriteIdx((ImDrawIdx)j1); dl->PrimWriteIdx((ImDrawIdx)j0);
    }
}

void FillStar(ImDrawList* dl, ImVec2 c, const ImVec2* pts, int n, ImU32 center, ImU32 edge) {
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    const float aa = 1.2f;
    ImU32 edgeT = edge & ~IM_COL32_A_MASK;
    dl->PrimReserve(n * 3 + n * 6, 1 + n * 2);
    unsigned int base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, center);
    for (int i = 0; i < n; ++i) dl->PrimWriteVtx(pts[i], uv, edge);
    for (int i = 0; i < n; ++i) {
        float dx = pts[i].x - c.x, dy = pts[i].y - c.y;
        float l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-4f) l = 1e-4f;
        dl->PrimWriteVtx(ImVec2(pts[i].x + dx / l * aa, pts[i].y + dy / l * aa), uv, edgeT);
    }
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        dl->PrimWriteIdx((ImDrawIdx)base);
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + j));
    }
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        unsigned int e0 = base + 1 + i, e1 = base + 1 + j, f0 = base + 1 + n + i, f1 = base + 1 + n + j;
        dl->PrimWriteIdx((ImDrawIdx)e0); dl->PrimWriteIdx((ImDrawIdx)e1); dl->PrimWriteIdx((ImDrawIdx)f1);
        dl->PrimWriteIdx((ImDrawIdx)e0); dl->PrimWriteIdx((ImDrawIdx)f1); dl->PrimWriteIdx((ImDrawIdx)f0);
    }
}

} // namespace orbfx

namespace {
struct Palette { ImVec4 a, b, c; };
Palette PaletteFor(OrbMode m) {
    using namespace theme;
    switch (m) {
    case OrbMode::Idle: return {Cyan, Violet, Blue};
    case OrbMode::Listening: return {ImVec4(0.25f, 0.98f, 0.75f, 1), Cyan, ImVec4(0.1f, 0.8f, 0.7f, 1)};
    case OrbMode::Transcribing: return {ImVec4(0.45f, 0.85f, 1.0f, 1), Blue, Cyan};
    case OrbMode::Thinking: return {Violet, Pink, ImVec4(0.5f, 0.3f, 1.0f, 1)};
    case OrbMode::Speaking: return {ImVec4(0.40f, 0.78f, 1.0f, 1), ImVec4(0.62f, 0.45f, 1.0f, 1), Cyan};
    case OrbMode::Showing: return {Amber, Pink, Amber};
    case OrbMode::Error: return {Red, Pink, Red};
    default: return {Cyan, Violet, Blue};
    }
}
ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return anim::Lerp(a, b, t); }
} // namespace

void Orb::Draw(ImDrawList* dl, ImVec2 c, float R, const OrbInput& in) {
    using anim::Col;
    using anim::WithAlpha;
    const float dt = anim::Dt();
    const float t = anim::Time();
    const bool reduce = anim::ReduceMotion();
    const float motion = reduce ? 0.35f : 1.f;

    // ---- pesos de cada modo (transições cruzadas)
    float sum = 0;
    for (int i = 0; i < (int)OrbMode::Count; ++i) {
        if (!init_) weight_[i] = (i == (int)in.mode) ? 1.f : 0.f;
        weight_[i] = anim::Approach(weight_[i], i == (int)in.mode ? 1.f : 0.f, 4.f);
        sum += weight_[i];
    }
    init_ = true;
    ImVec4 A(0, 0, 0, 0), B(0, 0, 0, 0), C(0, 0, 0, 0);
    for (int i = 0; i < (int)OrbMode::Count; ++i) {
        float wv = weight_[i] / std::max(sum, 1e-4f);
        Palette p = PaletteFor((OrbMode)i);
        A = ImVec4(A.x + p.a.x * wv, A.y + p.a.y * wv, A.z + p.a.z * wv, 1);
        B = ImVec4(B.x + p.b.x * wv, B.y + p.b.y * wv, B.z + p.b.z * wv, 1);
        C = ImVec4(C.x + p.c.x * wv, C.y + p.c.y * wv, C.z + p.c.z * wv, 1);
    }
    colA_ = A; colB_ = B; colC_ = C;
    const float wI = weight_[(int)OrbMode::Idle], wL = weight_[(int)OrbMode::Listening];
    const float wTr = weight_[(int)OrbMode::Transcribing], wT = weight_[(int)OrbMode::Thinking];
    const float wS = weight_[(int)OrbMode::Speaking], wSh = weight_[(int)OrbMode::Showing];
    const float wE = weight_[(int)OrbMode::Error];
    const float wTh = wT + wTr;

    level_ = anim::Approach(level_, in.micLevel, 18.f);
    speech_ = anim::Approach(speech_, in.speechLevel, 16.f);
    hover_ = anim::Approach(hover_, in.hovered ? 1.f : 0.f, 10.f);
    press_ = anim::Approach(press_, in.pressed ? 1.f : 0.f, 22.f);
    rot_ += dt * motion * (0.35f + 1.2f * wTh + 0.3f * wS);
    const float Rb = R * (1.f - 0.06f * press_ + 0.04f * hover_);
    const float energy = wL * level_ + wS * speech_ + wTh * 0.3f + wSh * 0.4f;

    // brilho bem suave por trás das partículas (dá profundidade, sem "bola")
    orbfx::RadialGradient(dl, c, 0, Rb * (1.6f + 0.4f * energy), Col(WithAlpha(C, 0.10f + 0.10f * energy)), Col(WithAlpha(C, 0.f)), 48);

    // ---- partículas
    const auto& shape = assets::KbShape();
    const int N = in.compact ? 320 : 950;
    if ((int)parts_.size() != N) {
        parts_.assign(N, ImVec2(0, 0));
        vels_.assign(N, ImVec2(0, 0));
        seeds_.resize(N * 3);
        uint32_t st = 0x9E3779B9u;
        auto rnd = [&]() { st ^= st << 13; st ^= st >> 17; st ^= st << 5; return (st & 0xFFFFFF) / 16777216.f; };
        for (int i = 0; i < N * 3; ++i) seeds_[i] = rnd();
    }
    float hist[64] = {};
    int hn = std::min(64, in.micHistoryN);
    for (int i = 0; i < hn; ++i) hist[i] = in.micHistory[i];

    const float golden = PI * (3.f - std::sqrt(5.f));
    const float ry = rot_ * 0.6f, rx = 0.45f + 0.15f * std::sin(t * 0.3f);
    const float cyr = std::cos(ry), syr = std::sin(ry), cxr = std::cos(rx), sxr = std::sin(rx);
    const float breathe = 1.f + 0.035f * std::sin(t * 1.6f) * motion + 0.12f * wL * level_ + 0.04f * hover_;
    const float k = 55.f, damp = std::exp(-dt * 8.5f);

    for (int i = 0; i < N; ++i) {
        float h1 = seeds_[i * 3], h2 = seeds_[i * 3 + 1], h3 = seeds_[i * 3 + 2];
        float u = (i + 0.5f) / N;

        // repouso / erro: esfera de Fibonacci girando em 3D
        float yS = 1.f - 2.f * u, rS = std::sqrt(std::max(0.f, 1.f - yS * yS)), phi = i * golden;
        float x3 = std::cos(phi) * rS, y3 = yS, z3 = std::sin(phi) * rS;
        float xr = x3 * cyr + z3 * syr, zr = -x3 * syr + z3 * cyr;
        float yr = y3 * cxr - zr * sxr, zf = y3 * sxr + zr * cxr;
        float depth = 0.5f + 0.5f * zf; // 0 = atrás, 1 = na frente
        ImVec2 sphere(xr * Rb * 0.92f * breathe, yr * Rb * 0.92f * breathe);
        if (wE > 0.01f) {
            sphere.x += anim::Noise(i * 0.7f + t * 14.f) * Rb * 0.06f * wE;
            sphere.y += anim::Noise(i * 0.9f + t * 14.f + 5.f) * Rb * 0.06f * wE;
        }
        ImVec2 target(sphere.x * (wI + wE), sphere.y * (wI + wE));

        // ouvindo: anéis que reagem ao microfone
        if (wL > 0.001f) {
            float ang = u * TAU * 3.f + h1 * 0.2f + rot_ * 0.2f;
            float hu = std::fmod(u * 3.f, 1.f);
            hu = hu < 0.5f ? hu * 2 : (1 - hu) * 2;
            float mic = hn ? hist[std::clamp((int)(hu * (hn - 1)), 0, 63)] : level_;
            float ring = 0.62f + 0.16f * std::floor(h1 * 3.f);
            float r = Rb * (ring + 0.55f * mic * (0.5f + h2) + 0.03f * std::sin(t * 3.f + i));
            target.x += std::cos(ang) * r * wL;
            target.y += std::sin(ang) * r * wL;
        }
        // pensando / transcrevendo: galáxia em espiral
        if (wTh > 0.001f) {
            float r = Rb * (0.08f + 1.0f * std::sqrt(h1));
            float arm = std::floor(h3 * 4.f) * TAU / 4.f;
            float spin = t * (wTr > wT ? 2.2f : 1.2f) * motion / (0.45f + r / Rb);
            float scatter = (h2 - 0.5f) * (h3 * 4.f - std::floor(h3 * 4.f) < 0.3f ? TAU : 1.1f); // 30% formam o disco
            float ang = arm + std::log(1.f + 3.f * r / Rb) * 2.2f + spin + scatter;
            target.x += std::cos(ang) * r * wTh;
            target.y += std::sin(ang) * r * 0.62f * wTh;
        }
        // respondendo: as letras KB da logo
        if (wS > 0.001f) {
            ImVec2 kb;
            if (!shape.empty()) {
                const ImVec2& s = shape[i % shape.size()];
                float j = Rb * 0.018f * (0.5f + 2.f * speech_) * motion;
                kb = ImVec2(s.x * Rb * 1.05f * (1 + 0.05f * speech_) + anim::Noise(i * 0.37f + t * 3.f) * j,
                            s.y * Rb * 1.05f * (1 + 0.05f * speech_) + anim::Noise(i * 0.53f + t * 3.f + 9.f) * j);
            } else {
                kb = ImVec2(sphere.x * (1 + 0.25f * speech_), sphere.y * (1 + 0.25f * speech_));
            }
            target.x += kb.x * wS;
            target.y += kb.y * wS;
        }
        // mostrando: faíscas saindo em rajadas
        if (wSh > 0.001f) {
            float f = std::fmod(h1 + t * (0.35f + 0.3f * h2) * motion, 1.f);
            float ang = h2 * TAU + h3 * 0.5f;
            float r = Rb * (0.25f + 1.05f * f);
            target.x += std::cos(ang) * r * wSh;
            target.y += std::sin(ang) * r * wSh;
        }

        // mola com pequeno atraso por partícula (as formas se montam em onda)
        ImVec2& p = parts_[i];
        ImVec2& v = vels_[i];
        float kk = k * (0.6f + 0.8f * h1);
        v.x = (v.x + (target.x - p.x) * kk * dt) * damp;
        v.y = (v.y + (target.y - p.y) * kk * dt) * damp;
        p.x += v.x * dt;
        p.y += v.y * dt;

        // cor, tamanho e brilho
        float dep = (wI + wE) * depth + (1.f - (wI + wE)) * (0.55f + 0.45f * h3);
        ImVec4 pc = Mix(Mix(A, B, h2), ImVec4(1, 1, 1, 1), 0.18f * h3 + 0.25f * wS * h3);
        if (wSh > 0.01f) pc = Mix(pc, Mix(theme::Amber, ImVec4(1, 1, 1, 1), 0.3f), wSh * 0.7f);
        float alpha = (0.35f + 0.65f * dep);
        if (wSh > 0.01f) {
            float f = std::fmod(h1 + t * (0.35f + 0.3f * h2) * motion, 1.f);
            alpha *= 1.f - wSh * f * 0.9f;
        }
        float sz = R * (in.compact ? 0.022f : 0.0105f) * (0.55f + 0.75f * h2) * (0.6f + 0.6f * dep);
        ImVec2 sp(c.x + p.x, c.y + p.y);
        dl->AddCircleFilled(sp, sz * 2.8f, Col(WithAlpha(pc, 0.09f * alpha)), 8);
        dl->AddCircleFilled(sp, sz, Col(WithAlpha(pc, alpha)), 8);
    }
}
