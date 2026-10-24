#include "ui/orb.h"
#include "ui/anim.h"
#include "ui/theme.h"
#include "ui/assets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

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
        weight_[i] = anim::Approach(weight_[i], i == (int)in.mode ? 1.f : 0.f, 5.f);
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
    const float wIdle = weight_[(int)OrbMode::Idle];
    const float wL = weight_[(int)OrbMode::Listening];
    const float wTr = weight_[(int)OrbMode::Transcribing];
    const float wT = weight_[(int)OrbMode::Thinking];
    const float wS = weight_[(int)OrbMode::Speaking];
    const float wSh = weight_[(int)OrbMode::Showing];
    const float wE = weight_[(int)OrbMode::Error];
    const float thinkW = std::min(1.f, wT + 0.8f * wTr);

    level_ = anim::Approach(level_, in.micLevel, 18.f);
    speech_ = anim::Approach(speech_, in.speechLevel, 16.f);
    hover_ = anim::Approach(hover_, in.hovered ? 1.f : 0.f, 10.f);
    press_ = anim::Approach(press_, in.pressed ? 1.f : 0.f, 22.f);
    think_ = anim::Approach(think_, thinkW, 4.f);
    float energy = wL * level_ + wS * speech_ + thinkW * 0.25f + wSh * 0.3f;
    rot_ += dt * motion * (0.25f + 1.8f * wT + 2.6f * wTr + 0.5f * wS + 0.6f * wSh);

    float Rb = R * (1.f - 0.05f * press_ + 0.03f * hover_);
    // respiração no repouso
    Rb *= 1.f + 0.025f * wIdle * std::sin(t * 1.6f) * motion;

    // ---- 1. brilho externo
    orbfx::RadialGradient(dl, c, Rb * 0.75f, Rb * (2.0f + 0.6f * energy), Col(WithAlpha(C, 0.30f + 0.25f * energy)), Col(WithAlpha(C, 0.f)), 72);
    orbfx::RadialGradient(dl, c, Rb * 0.9f, Rb * (1.35f + 0.3f * energy), Col(WithAlpha(A, 0.22f + 0.2f * energy)), Col(WithAlpha(A, 0.f)), 72);

    // ---- 2. ouvindo: ondas que se expandem + equalizador circular
    if (wL > 0.01f && !in.compact) {
        float interval = std::max(0.25f, 0.85f - 0.6f * level_);
        if (t - lastRipple_ > interval && in.mode == OrbMode::Listening) {
            ripples_.push_back({t, 0.4f + 0.6f * level_});
            lastRipple_ = t;
        }
    }
    for (auto it = ripples_.begin(); it != ripples_.end();) {
        float a = (t - it->born) / 1.7f;
        if (a >= 1.f) { it = ripples_.erase(it); continue; }
        float e = anim::EaseOutCubic(a);
        float rr = Rb * (1.02f + 1.0f * e);
        float alpha = (1 - a) * 0.55f * it->strength * std::max(wL, 0.2f);
        dl->AddCircle(c, rr, Col(WithAlpha(Mix(A, B, a), alpha)), 96, R * 0.012f + R * 0.02f * (1 - a));
        ++it;
    }
    if (wL > 0.01f || wS > 0.01f) {
        const int N = in.compact ? 32 : 48;
        float hist[64] = {};
        if (in.micHistory && in.micHistoryN > 0)
            for (int i = 0; i < std::min(64, in.micHistoryN); ++i) hist[i] = in.micHistory[i];
        for (int i = 0; i < N; ++i) {
            float u = (float)i / N;
            // espelha o histórico em volta do círculo
            float hu = u < 0.5f ? u * 2 : (1 - u) * 2;
            int hi = std::clamp((int)(hu * (in.micHistoryN - 1)), 0, 63);
            float micV = in.micHistoryN ? hist[hi] : level_;
            float spV = speech_ * (0.55f + 0.45f * anim::Noise(u * 9.f + t * 4.f));
            float target = wL * micV * (0.6f + 0.4f * anim::Noise(u * 13.f + t * 6.f)) + wS * spV;
            bars_[i % 48] = anim::Approach(bars_[i % 48], target, 20.f);
            float v = bars_[i % 48];
            float ang = u * TAU - PI * 0.5f + rot_ * 0.15f;
            float r0 = Rb * 1.10f, r1 = Rb * (1.12f + 0.06f + 0.42f * v);
            ImVec2 d(std::cos(ang), std::sin(ang));
            float alpha = std::max(wL, wS) * (0.35f + 0.65f * v);
            dl->AddLine(ImVec2(c.x + d.x * r0, c.y + d.y * r0), ImVec2(c.x + d.x * r1, c.y + d.y * r1),
                        Col(WithAlpha(Mix(A, B, hu), alpha)), std::max(1.5f, R * 0.028f));
        }
    }

    // ---- 3. pensando / transcrevendo: órbitas com partículas e rastro
    if (think_ > 0.01f) {
        int orbits = in.compact ? 2 : 3;
        for (int k = 0; k < orbits; ++k) {
            float tilt = rot_ * 0.35f + k * PI / 3.f;
            float ex = Rb * (1.38f + 0.06f * k), ey = Rb * (0.42f + 0.05f * k);
            float ct = std::cos(tilt), st = std::sin(tilt);
            auto P = [&](float u) {
                float x = std::cos(u) * ex, y = std::sin(u) * ey;
                return ImVec2(c.x + x * ct - y * st, c.y + x * st + y * ct);
            };
            // trilha da órbita (só a parte "de trás" fica mais fraca)
            for (int s = 0; s < 64; ++s) {
                float u0 = TAU * s / 64, u1 = TAU * (s + 1) / 64;
                float depth = 0.5f + 0.5f * std::sin(u0);
                dl->AddLine(P(u0), P(u1), Col(WithAlpha(Mix(A, B, (float)k / 3), (0.05f + 0.12f * depth) * think_)), std::max(1.f, R * 0.01f));
            }
            float speed = (1.4f + 0.35f * k) * (1.f + 1.2f * wTr) * motion;
            float head = t * speed + k * 2.1f;
            for (int j = 0; j < 14; ++j) {
                float u = head - j * 0.07f;
                float f = 1.f - j / 14.f;
                dl->AddCircleFilled(P(u), R * (0.045f * f + 0.01f), Col(WithAlpha(j == 0 ? ImVec4(1, 1, 1, 1) : Mix(A, B, 1 - f), think_ * f * 0.9f)), 12);
            }
        }
        // arco giratório com degradê
        float a0 = rot_ * 1.6f;
        const int segs = 40;
        for (int s = 0; s < segs; ++s) {
            float u0 = a0 + (PI * 1.25f) * s / segs, u1 = a0 + (PI * 1.25f) * (s + 1) / segs;
            float f = (float)s / segs;
            dl->PathArcTo(c, Rb * 1.2f, u0, u1, 3);
            dl->PathStroke(Col(WithAlpha(Mix(B, A, f), f * 0.85f * think_)), 0, std::max(1.5f, R * 0.03f));
        }
    }

    // ---- 4. falando: anéis pulsando com a voz
    if (wS > 0.01f) {
        dl->AddCircle(c, Rb * (1.04f + 0.22f * speech_), Col(WithAlpha(A, 0.55f * wS * (0.3f + speech_))), 96, R * 0.025f);
        dl->AddCircle(c, Rb * (1.12f + 0.45f * speech_), Col(WithAlpha(B, 0.28f * wS * speech_)), 96, R * 0.018f);
    }

    // ---- 5. mostrando: anel tracejado + faíscas
    if (wSh > 0.01f) {
        const int dashes = 28;
        for (int i = 0; i < dashes; ++i) {
            float a = rot_ * 0.8f + TAU * i / dashes;
            dl->PathArcTo(c, Rb * 1.28f, a, a + TAU / dashes * 0.5f, 4);
            dl->PathStroke(Col(WithAlpha(Mix(A, B, (float)i / dashes), 0.7f * wSh)), 0, std::max(1.5f, R * 0.025f));
        }
        if (!in.compact && sparks_.size() < 40 && std::fmod(t, 0.06f) < dt) {
            float ang = anim::Noise(t * 7.f) * PI + t * 3.f;
            sparks_.push_back({ang, 1.05f, 0.5f + 0.5f * std::fabs(anim::Noise(t * 3.f)), 0.03f + 0.03f * std::fabs(anim::Noise(t * 11.f)), 1.2f, t});
        }
    }
    for (auto it = sparks_.begin(); it != sparks_.end();) {
        float a = (t - it->born) / it->life;
        if (a >= 1.f) { it = sparks_.erase(it); continue; }
        float rr = Rb * (it->radius + a * 1.1f * it->speed);
        ImVec2 p(c.x + std::cos(it->angle) * rr, c.y + std::sin(it->angle) * rr);
        float s = R * it->size * (1 - a);
        ImU32 col = Col(WithAlpha(Mix(A, ImVec4(1, 1, 1, 1), 0.4f), (1 - a) * std::max(wSh, 0.2f)));
        dl->AddQuadFilled(ImVec2(p.x, p.y - s * 2), ImVec2(p.x + s * 0.5f, p.y), ImVec2(p.x, p.y + s * 2), ImVec2(p.x - s * 0.5f, p.y), col);
        dl->AddQuadFilled(ImVec2(p.x - s * 2, p.y), ImVec2(p.x, p.y - s * 0.5f), ImVec2(p.x + s * 2, p.y), ImVec2(p.x, p.y + s * 0.5f), col);
        ++it;
    }

    // ---- 6. núcleo: três "blobs" deformados por ruído
    const int NP = in.compact ? 64 : 120;
    ImVec2 pts[128];
    float amp = 0.022f + 0.015f * std::sin(t * 1.3f) * wIdle + 0.14f * wL * level_ + 0.17f * wS * speech_ + 0.05f * thinkW +
                0.04f * wSh + 0.03f * hover_ + 0.06f * wE * std::fabs(std::sin(t * 9.f));
    amp *= motion;
    // ao responder, o núcleo se desfaz em partículas que formam "KB"
    const bool hasShape = !assets::KbShape().empty();
    const float coreA = hasShape ? 1.f - 0.97f * wS : 1.f;
    for (int L = 0; L < 3; ++L) {
        float rl = Rb * (L == 0 ? 1.f : 0.80f - 0.08f * L);
        float ph = L * 2.1f;
        ImVec2 off(std::cos(t * 0.7f + ph) * R * 0.14f * (L ? 1.f : 0.f) * (0.4f + 0.6f * motion), std::sin(t * 0.9f + ph * 1.3f) * R * 0.14f * (L ? 1.f : 0.f) * (0.4f + 0.6f * motion));
        ImVec2 cc(c.x + off.x, c.y + off.y);
        for (int i = 0; i < NP; ++i) {
            float th = TAU * i / NP;
            float d = std::sin(2 * th + t * 0.9f + ph) * 0.5f + std::sin(3 * th - t * 1.3f + ph) * 0.35f +
                      std::sin(5 * th + t * 2.1f + ph * 0.7f) * 0.25f + anim::Noise(th * 2.f + t * 1.7f + L * 10.f) * 0.3f;
            float r = rl * (1.f + amp * d);
            pts[i] = ImVec2(cc.x + std::cos(th) * r, cc.y + std::sin(th) * r);
        }
        ImU32 cin, cout;
        if (L == 0) {
            // núcleo mais escuro e borda brilhante (efeito "fresnel")
            cin = Col(Mix(Mix(C, B, 0.5f), theme::Bg, 0.45f));
            cout = Col(Mix(A, ImVec4(1, 1, 1, 1), 0.25f));
            cin = Col(Mix(Mix(C, B, 0.5f), theme::Bg, 0.45f), coreA);
            cout = Col(Mix(A, ImVec4(1, 1, 1, 1), 0.25f), coreA);
            orbfx::FillStar(dl, c, pts, NP, cin, cout);
        } else if (L == 1) {
            cin = Col(WithAlpha(Mix(A, ImVec4(1, 1, 1, 1), 0.15f), 0.85f * coreA));
            cout = Col(WithAlpha(B, 0.0f));
            orbfx::FillStar(dl, cc, pts, NP, cin, cout);
        } else {
            cin = Col(WithAlpha(Mix(B, theme::Pink, 0.35f), 0.75f * coreA));
            cout = Col(WithAlpha(C, 0.0f));
            orbfx::FillStar(dl, cc, pts, NP, cin, cout);
        }
    }

    // ondas de voz dentro do orb
    float waveAmp = wS * (0.15f + speech_) * (hasShape ? 0.f : 1.f) + wL * level_ * 0.9f;
    if (waveAmp > 0.01f) {
        const int WN = 48;
        ImVec2 wp[WN];
        for (int k = 0; k < 3; ++k) {
            float freq = 1.5f + k * 0.7f, spd = (3.5f + k * 1.3f) * motion, phase = k * 1.7f;
            float ampK = Rb * 0.32f * waveAmp * (1.f - k * 0.22f);
            for (int i = 0; i < WN; ++i) {
                float x = -0.82f + 1.64f * i / (WN - 1);
                float env = std::sqrt(std::max(0.f, 1.f - (x / 0.84f) * (x / 0.84f)));
                env *= env;
                float y = std::sin(x * freq * PI + t * spd + phase) * ampK * env;
                wp[i] = ImVec2(c.x + x * Rb, c.y + y);
            }
            ImVec4 wc = k == 0 ? ImVec4(1, 1, 1, 0.85f) : WithAlpha(Mix(A, B, k * 0.5f), 0.8f);
            dl->AddPolyline(wp, WN, Col(wc, std::min(1.f, waveAmp * 2.f)), 0, std::max(1.2f, R * (0.028f - k * 0.006f)));
        }
    }

    // ---- 7. partículas "KB" (resposta)
    if (hasShape && (wS > 0.005f || !parts_.empty())) {
        const auto& shape = assets::KbShape();
        int N = std::min<int>((int)shape.size(), in.compact ? 360 : 1200);
        if ((int)parts_.size() != N) { parts_.assign(N, ImVec2(0, 0)); vels_.assign(N, ImVec2(0, 0)); }
        float form = anim::EaseInOutCubic(wS);
        float pulse = 1.f + 0.05f * speech_;
        float k = 60.f, damp = std::exp(-dt * 9.f);
        for (int i = 0; i < N; ++i) {
            float h1 = std::fabs(anim::Noise(i * 1.731f + 0.3f)), h2 = std::fabs(anim::Noise(i * 3.117f + 7.1f)), h3 = std::fabs(anim::Noise(i * 0.913f + 2.2f));
            float ang = h1 * TAU * 3.f + t * (0.25f + 0.5f * h2) * motion;
            float rad = std::sqrt(std::min(1.f, h3 * 1.6f)) * Rb * 0.92f;
            ImVec2 home(std::cos(ang) * rad, std::sin(ang) * rad);
            ImVec2 tg(shape[i].x * Rb * 1.05f * pulse, shape[i].y * Rb * 1.05f * pulse);
            float j = Rb * 0.02f * (0.4f + 1.6f * speech_) * motion;
            tg.x += anim::Noise(i * 0.37f + t * 3.f) * j;
            tg.y += anim::Noise(i * 0.53f + t * 3.f + 9.f) * j;
            // pequeno atraso por partícula: o "KB" se monta em onda
            float fi = anim::Clamp01(form * 1.25f - h1 * 0.25f);
            ImVec2 goal(home.x + (tg.x - home.x) * fi, home.y + (tg.y - home.y) * fi);
            ImVec2& p = parts_[i];
            ImVec2& v = vels_[i];
            v.x = (v.x + (goal.x - p.x) * k * dt) * damp;
            v.y = (v.y + (goal.y - p.y) * k * dt) * damp;
            p.x += v.x * dt;
            p.y += v.y * dt;
            float alpha = anim::Smoothstep(0.f, 0.25f, wS);
            if (alpha < 0.01f) continue;
            ImVec2 sp(c.x + p.x, c.y + p.y);
            float sz = R * (0.009f + 0.008f * h2) * (0.7f + 0.3f * fi);
            ImVec4 pc = Mix(Mix(theme::Cyan, A, h2), ImVec4(1, 1, 1, 1), 0.25f * h3);
            dl->AddCircleFilled(sp, sz * 2.6f, Col(WithAlpha(pc, 0.10f * alpha)), 10);
            dl->AddCircleFilled(sp, sz, Col(WithAlpha(pc, (0.55f + 0.45f * h3) * alpha)), 8);
        }
        if (wS < 0.005f) parts_.clear();
    }

    // reflexo de vidro + aro
    orbfx::RadialGradient(dl, ImVec2(c.x - Rb * 0.32f, c.y - Rb * 0.38f), 0, Rb * 0.55f, Col(ImVec4(1, 1, 1, (0.30f + 0.1f * hover_) * coreA)), Col(ImVec4(1, 1, 1, 0)), 40);
    dl->AddCircle(c, Rb * 0.995f, Col(ImVec4(1, 1, 1, (0.10f + 0.1f * hover_) * coreA)), 96, std::max(1.f, R * 0.012f));
}
