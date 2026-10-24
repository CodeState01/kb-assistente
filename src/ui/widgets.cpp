#include "ui/widgets.h"
#include <windows.h>
#include <string>
#include <cstring>
#include "ui/anim.h"
#include "ui/theme.h"
#include "ui/orb.h"
#include "imgui_internal.h"
#include <cmath>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

using theme::px;
static constexpr float PI = 3.14159265f;

namespace w {

void (*g_toggleSound)(bool on) = nullptr;

static ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
static ImVec2 V(ImVec2 c, float s, float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); }

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col, float t) {
    if (t <= 0) t = std::max(1.4f, s * 0.085f);
    switch (icon) {
    case Icon::Mic:
        dl->AddRect(V(c, s, -0.14f, -0.38f), V(c, s, 0.14f, 0.08f), col, s * 0.14f, 0, t);
        dl->PathArcTo(V(c, s, 0, -0.08f), s * 0.27f, 0.05f, PI - 0.05f, 16);
        dl->PathStroke(col, 0, t);
        dl->AddLine(V(c, s, 0, 0.19f), V(c, s, 0, 0.36f), col, t);
        dl->AddLine(V(c, s, -0.15f, 0.36f), V(c, s, 0.15f, 0.36f), col, t);
        break;
    case Icon::Send: {
        ImVec2 p0 = V(c, s, -0.34f, -0.32f), p1 = V(c, s, 0.38f, 0.f), p2 = V(c, s, -0.34f, 0.32f), p3 = V(c, s, -0.18f, 0.f);
        dl->AddTriangleFilled(p0, p1, p3, col);
        dl->AddTriangleFilled(p3, p1, p2, col);
        break;
    }
    case Icon::Stop: dl->AddRectFilled(V(c, s, -0.22f, -0.22f), V(c, s, 0.22f, 0.22f), col, s * 0.08f); break;
    case Icon::Chat:
        dl->AddRect(V(c, s, -0.36f, -0.3f), V(c, s, 0.36f, 0.18f), col, s * 0.12f, 0, t);
        dl->AddLine(V(c, s, -0.18f, 0.18f), V(c, s, -0.24f, 0.36f), col, t);
        dl->AddLine(V(c, s, -0.24f, 0.36f), V(c, s, 0.02f, 0.18f), col, t);
        break;
    case Icon::Grid:
        for (int i = 0; i < 4; ++i) {
            float x = (i % 2) ? 0.06f : -0.34f, y = (i / 2) ? 0.06f : -0.34f;
            dl->AddRect(V(c, s, x, y), V(c, s, x + 0.28f, y + 0.28f), col, s * 0.07f, 0, t);
        }
        break;
    case Icon::Cpu:
        dl->AddRect(V(c, s, -0.24f, -0.24f), V(c, s, 0.24f, 0.24f), col, s * 0.06f, 0, t);
        dl->AddRectFilled(V(c, s, -0.09f, -0.09f), V(c, s, 0.09f, 0.09f), col, s * 0.03f);
        for (int i = -1; i <= 1; ++i) {
            float o = i * 0.12f;
            dl->AddLine(V(c, s, o, -0.24f), V(c, s, o, -0.38f), col, t);
            dl->AddLine(V(c, s, o, 0.24f), V(c, s, o, 0.38f), col, t);
            dl->AddLine(V(c, s, -0.24f, o), V(c, s, -0.38f, o), col, t);
            dl->AddLine(V(c, s, 0.24f, o), V(c, s, 0.38f, o), col, t);
        }
        break;
    case Icon::Gear: {
        for (int i = 0; i < 8; ++i) {
            float a = i * PI / 4;
            ImVec2 d(std::cos(a), std::sin(a));
            dl->AddLine(ImVec2(c.x + d.x * s * 0.22f, c.y + d.y * s * 0.22f), ImVec2(c.x + d.x * s * 0.38f, c.y + d.y * s * 0.38f), col, t * 1.8f);
        }
        dl->AddCircleFilled(c, s * 0.27f, col, 24);
        dl->AddCircleFilled(c, s * 0.11f, ImGui::GetColorU32(theme::Bg), 16);
        break;
    }
    case Icon::Close:
        dl->AddLine(V(c, s, -0.25f, -0.25f), V(c, s, 0.25f, 0.25f), col, t);
        dl->AddLine(V(c, s, -0.25f, 0.25f), V(c, s, 0.25f, -0.25f), col, t);
        break;
    case Icon::Minimize: dl->AddLine(V(c, s, -0.26f, 0.05f), V(c, s, 0.26f, 0.05f), col, t); break;
    case Icon::Pin:
        dl->AddCircle(V(c, s, 0, -0.14f), s * 0.17f, col, 16, t);
        dl->AddLine(V(c, s, 0, 0.03f), V(c, s, 0, 0.38f), col, t);
        break;
    case Icon::PopOut:
        dl->PathLineTo(V(c, s, -0.02f, -0.3f));
        dl->PathLineTo(V(c, s, -0.32f, -0.3f));
        dl->PathLineTo(V(c, s, -0.32f, 0.32f));
        dl->PathLineTo(V(c, s, 0.3f, 0.32f));
        dl->PathLineTo(V(c, s, 0.3f, 0.02f));
        dl->PathStroke(col, 0, t);
        dl->AddLine(V(c, s, -0.04f, 0.04f), V(c, s, 0.34f, -0.34f), col, t);
        dl->AddLine(V(c, s, 0.08f, -0.34f), V(c, s, 0.34f, -0.34f), col, t);
        dl->AddLine(V(c, s, 0.34f, -0.34f), V(c, s, 0.34f, -0.08f), col, t);
        break;
    case Icon::Copy:
        dl->AddRect(V(c, s, -0.3f, -0.18f), V(c, s, 0.12f, 0.34f), col, s * 0.06f, 0, t);
        dl->AddRect(V(c, s, -0.12f, -0.34f), V(c, s, 0.3f, 0.16f), col, s * 0.06f, 0, t);
        break;
    case Icon::Refresh: {
        float a0 = -PI * 0.35f, a1 = PI * 1.35f;
        dl->PathArcTo(c, s * 0.28f, a0, a1, 24);
        dl->PathStroke(col, 0, t);
        ImVec2 e(c.x + std::cos(a0) * s * 0.28f, c.y + std::sin(a0) * s * 0.28f);
        dl->AddTriangleFilled(ImVec2(e.x + s * 0.13f, e.y - s * 0.02f), ImVec2(e.x - s * 0.06f, e.y - s * 0.13f), ImVec2(e.x - s * 0.02f, e.y + s * 0.1f), col);
        break;
    }
    case Icon::Download:
        dl->AddLine(V(c, s, 0, -0.36f), V(c, s, 0, 0.12f), col, t);
        dl->AddLine(V(c, s, -0.18f, -0.06f), V(c, s, 0, 0.13f), col, t);
        dl->AddLine(V(c, s, 0.18f, -0.06f), V(c, s, 0, 0.13f), col, t);
        dl->AddLine(V(c, s, -0.32f, 0.34f), V(c, s, 0.32f, 0.34f), col, t);
        break;
    case Icon::Folder:
        dl->PathLineTo(V(c, s, -0.36f, 0.3f));
        dl->PathLineTo(V(c, s, -0.36f, -0.28f));
        dl->PathLineTo(V(c, s, -0.1f, -0.28f));
        dl->PathLineTo(V(c, s, 0.0f, -0.16f));
        dl->PathLineTo(V(c, s, 0.36f, -0.16f));
        dl->PathLineTo(V(c, s, 0.36f, 0.3f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        break;
    case Icon::Check:
        dl->PathLineTo(V(c, s, -0.3f, 0.02f));
        dl->PathLineTo(V(c, s, -0.08f, 0.24f));
        dl->PathLineTo(V(c, s, 0.32f, -0.2f));
        dl->PathStroke(col, 0, t * 1.2f);
        break;
    case Icon::Info:
        dl->AddCircle(c, s * 0.36f, col, 24, t);
        dl->AddLine(V(c, s, 0, -0.04f), V(c, s, 0, 0.2f), col, t);
        dl->AddCircleFilled(V(c, s, 0, -0.17f), t * 0.8f, col);
        break;
    case Icon::Warning:
        dl->AddTriangle(V(c, s, 0, -0.36f), V(c, s, 0.38f, 0.3f), V(c, s, -0.38f, 0.3f), col, t);
        dl->AddLine(V(c, s, 0, -0.12f), V(c, s, 0, 0.1f), col, t);
        dl->AddCircleFilled(V(c, s, 0, 0.2f), t * 0.75f, col);
        break;
    case Icon::Bulb:
        dl->AddCircle(V(c, s, 0, -0.08f), s * 0.24f, col, 24, t);
        dl->AddLine(V(c, s, -0.1f, 0.25f), V(c, s, 0.1f, 0.25f), col, t);
        dl->AddLine(V(c, s, -0.07f, 0.36f), V(c, s, 0.07f, 0.36f), col, t);
        break;
    case Icon::Play: dl->AddTriangleFilled(V(c, s, -0.2f, -0.3f), V(c, s, 0.3f, 0), V(c, s, -0.2f, 0.3f), col); break;
    case Icon::Trash:
        dl->AddLine(V(c, s, -0.32f, -0.24f), V(c, s, 0.32f, -0.24f), col, t);
        dl->AddLine(V(c, s, -0.08f, -0.34f), V(c, s, 0.08f, -0.34f), col, t);
        dl->AddRect(V(c, s, -0.22f, -0.2f), V(c, s, 0.22f, 0.36f), col, s * 0.05f, 0, t);
        break;
    case Icon::Search:
        dl->AddCircle(V(c, s, -0.06f, -0.06f), s * 0.24f, col, 24, t);
        dl->AddLine(V(c, s, 0.12f, 0.12f), V(c, s, 0.34f, 0.34f), col, t * 1.3f);
        break;
    case Icon::Sparkle: {
        auto star = [&](ImVec2 p, float r) {
            dl->AddQuadFilled(ImVec2(p.x, p.y - r), ImVec2(p.x + r * 0.22f, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r * 0.22f, p.y), col);
            dl->AddQuadFilled(ImVec2(p.x - r, p.y), ImVec2(p.x, p.y - r * 0.22f), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r * 0.22f), col);
        };
        star(V(c, s, -0.06f, 0.04f), s * 0.34f);
        star(V(c, s, 0.26f, -0.26f), s * 0.14f);
        break;
    }
    case Icon::Wave:
        for (int i = 0; i < 5; ++i) {
            float hgt = (i == 2 ? 0.34f : (i == 1 || i == 3) ? 0.22f : 0.12f);
            float x = (i - 2) * 0.16f;
            dl->AddLine(V(c, s, x, -hgt), V(c, s, x, hgt), col, t * 1.2f);
        }
        break;
    case Icon::Phone:
        dl->AddRect(V(c, s, -0.2f, -0.38f), V(c, s, 0.2f, 0.38f), col, s * 0.08f, 0, t);
        dl->AddLine(V(c, s, -0.06f, 0.27f), V(c, s, 0.06f, 0.27f), col, t);
        break;
    case Icon::Image:
        dl->AddRect(V(c, s, -0.36f, -0.28f), V(c, s, 0.36f, 0.28f), col, s * 0.06f, 0, t);
        dl->AddCircleFilled(V(c, s, 0.14f, -0.1f), s * 0.06f, col);
        dl->PathLineTo(V(c, s, -0.3f, 0.22f));
        dl->PathLineTo(V(c, s, -0.1f, 0.0f));
        dl->PathLineTo(V(c, s, 0.06f, 0.14f));
        dl->PathLineTo(V(c, s, 0.16f, 0.06f));
        dl->PathLineTo(V(c, s, 0.3f, 0.22f));
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Monitor:
        dl->AddRect(V(c, s, -0.38f, -0.3f), V(c, s, 0.38f, 0.16f), col, s * 0.05f, 0, t);
        dl->AddLine(V(c, s, 0, 0.16f), V(c, s, 0, 0.32f), col, t);
        dl->AddLine(V(c, s, -0.18f, 0.34f), V(c, s, 0.18f, 0.34f), col, t);
        break;
    case Icon::Expand:
    case Icon::Shrink: {
        float a = icon == Icon::Expand ? 0.34f : 0.08f, b2 = icon == Icon::Expand ? 0.14f : 0.3f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                ImVec2 corner = V(c, s, sx * a, sy * a);
                dl->AddLine(corner, V(c, s, sx * (icon == Icon::Expand ? b2 : b2), sy * a), col, t);
                dl->AddLine(corner, V(c, s, sx * a, sy * (icon == Icon::Expand ? b2 : b2)), col, t);
            }
        break;
    }
    case Icon::Bag:
        dl->PathLineTo(V(c, s, -0.32f, -0.14f));
        dl->PathLineTo(V(c, s, 0.32f, -0.14f));
        dl->PathLineTo(V(c, s, 0.28f, 0.38f));
        dl->PathLineTo(V(c, s, -0.28f, 0.38f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->PathArcTo(V(c, s, 0, -0.14f), s * 0.16f, PI, 2 * PI, 12);
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Cart:
        dl->PathLineTo(V(c, s, -0.4f, -0.3f));
        dl->PathLineTo(V(c, s, -0.26f, -0.3f));
        dl->PathLineTo(V(c, s, -0.16f, 0.14f));
        dl->PathLineTo(V(c, s, 0.3f, 0.14f));
        dl->PathLineTo(V(c, s, 0.38f, -0.16f));
        dl->PathLineTo(V(c, s, -0.22f, -0.16f));
        dl->PathStroke(col, 0, t);
        dl->AddCircleFilled(V(c, s, -0.1f, 0.3f), t * 1.3f, col);
        dl->AddCircleFilled(V(c, s, 0.24f, 0.3f), t * 1.3f, col);
        break;
    case Icon::Tag:
        dl->PathLineTo(V(c, s, -0.36f, -0.36f));
        dl->PathLineTo(V(c, s, 0.02f, -0.36f));
        dl->PathLineTo(V(c, s, 0.38f, 0.0f));
        dl->PathLineTo(V(c, s, 0.0f, 0.38f));
        dl->PathLineTo(V(c, s, -0.36f, 0.02f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircleFilled(V(c, s, -0.16f, -0.16f), t * 1.1f, col);
        break;
    case Icon::Calendar:
        dl->AddRect(V(c, s, -0.34f, -0.26f), V(c, s, 0.34f, 0.36f), col, s * 0.06f, 0, t);
        dl->AddLine(V(c, s, -0.34f, -0.06f), V(c, s, 0.34f, -0.06f), col, t);
        dl->AddLine(V(c, s, -0.16f, -0.38f), V(c, s, -0.16f, -0.18f), col, t);
        dl->AddLine(V(c, s, 0.16f, -0.38f), V(c, s, 0.16f, -0.18f), col, t);
        dl->AddRectFilled(V(c, s, -0.2f, 0.06f), V(c, s, -0.06f, 0.2f), col, s * 0.02f);
        break;
    case Icon::User:
        dl->AddCircle(V(c, s, 0, -0.14f), s * 0.16f, col, 20, t);
        dl->PathArcTo(V(c, s, 0, 0.4f), s * 0.32f, PI * 1.08f, PI * 1.92f, 18);
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Globe:
        dl->AddCircle(c, s * 0.36f, col, 28, t);
        dl->AddLine(V(c, s, -0.36f, 0), V(c, s, 0.36f, 0), col, t);
        dl->AddEllipse(c, ImVec2(s * 0.15f, s * 0.36f), col, 0, 24, t);
        break;
    case Icon::Cursor:
        dl->PathLineTo(V(c, s, -0.24f, -0.36f));
        dl->PathLineTo(V(c, s, 0.26f, 0.08f));
        dl->PathLineTo(V(c, s, 0.04f, 0.1f));
        dl->PathLineTo(V(c, s, 0.16f, 0.36f));
        dl->PathLineTo(V(c, s, 0.06f, 0.4f));
        dl->PathLineTo(V(c, s, -0.06f, 0.14f));
        dl->PathLineTo(V(c, s, -0.24f, 0.28f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        break;
    case Icon::Edit:
        dl->AddLine(V(c, s, -0.28f, 0.28f), V(c, s, 0.22f, -0.22f), col, t * 2.2f);
        dl->AddTriangleFilled(V(c, s, -0.36f, 0.36f), V(c, s, -0.33f, 0.2f), V(c, s, -0.2f, 0.33f), col);
        dl->AddLine(V(c, s, -0.36f, 0.4f), V(c, s, 0.34f, 0.4f), col, t * 0.8f);
        break;
    case Icon::External:
        dl->PathLineTo(V(c, s, 0.0f, -0.3f));
        dl->PathLineTo(V(c, s, -0.32f, -0.3f));
        dl->PathLineTo(V(c, s, -0.32f, 0.32f));
        dl->PathLineTo(V(c, s, 0.3f, 0.32f));
        dl->PathLineTo(V(c, s, 0.3f, 0.0f));
        dl->PathStroke(col, 0, t);
        dl->AddLine(V(c, s, -0.02f, 0.02f), V(c, s, 0.36f, -0.36f), col, t);
        dl->AddLine(V(c, s, 0.12f, -0.36f), V(c, s, 0.36f, -0.36f), col, t);
        dl->AddLine(V(c, s, 0.36f, -0.36f), V(c, s, 0.36f, -0.12f), col, t);
        break;
    case Icon::Bell:
        dl->PathArcTo(V(c, s, 0, -0.06f), s * 0.24f, PI, 2 * PI, 14);
        dl->PathLineTo(V(c, s, 0.28f, 0.24f));
        dl->PathLineTo(V(c, s, -0.28f, 0.24f));
        dl->PathLineTo(V(c, s, -0.24f, -0.06f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircleFilled(V(c, s, 0, 0.34f), t * 1.2f, col);
        break;
    case Icon::Shield:
        dl->PathLineTo(V(c, s, 0, -0.38f));
        dl->PathLineTo(V(c, s, 0.32f, -0.24f));
        dl->PathLineTo(V(c, s, 0.28f, 0.1f));
        dl->PathLineTo(V(c, s, 0, 0.38f));
        dl->PathLineTo(V(c, s, -0.28f, 0.1f));
        dl->PathLineTo(V(c, s, -0.32f, -0.24f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->PathLineTo(V(c, s, -0.12f, 0.0f));
        dl->PathLineTo(V(c, s, -0.02f, 0.1f));
        dl->PathLineTo(V(c, s, 0.14f, -0.1f));
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Bolt:
        dl->AddTriangleFilled(V(c, s, 0.08f, -0.4f), V(c, s, -0.24f, 0.06f), V(c, s, 0.02f, 0.04f), col);
        dl->AddTriangleFilled(V(c, s, -0.02f, -0.04f), V(c, s, 0.24f, -0.06f), V(c, s, -0.08f, 0.4f), col);
        break;
    case Icon::Keyboard:
        dl->AddRect(V(c, s, -0.4f, -0.22f), V(c, s, 0.4f, 0.24f), col, s * 0.06f, 0, t);
        for (int r = 0; r < 2; ++r)
            for (int k = 0; k < 5; ++k)
                dl->AddRectFilled(V(c, s, -0.3f + k * 0.13f, -0.12f + r * 0.12f), V(c, s, -0.23f + k * 0.13f, -0.06f + r * 0.12f), col);
        dl->AddLine(V(c, s, -0.18f, 0.14f), V(c, s, 0.18f, 0.14f), col, t);
        break;
    case Icon::Star: {
        for (int i = 0; i < 10; ++i) {
            float a = -PI * 0.5f + i * PI / 5, r = (i % 2) ? 0.16f : 0.38f;
            dl->PathLineTo(ImVec2(c.x + std::cos(a) * s * r, c.y + std::sin(a) * s * r + s * 0.03f));
        }
        dl->PathFillConvex(col);
        break;
    }
    case Icon::Volume:
        dl->PathLineTo(V(c, s, -0.36f, -0.1f));
        dl->PathLineTo(V(c, s, -0.2f, -0.1f));
        dl->PathLineTo(V(c, s, 0.0f, -0.3f));
        dl->PathLineTo(V(c, s, 0.0f, 0.3f));
        dl->PathLineTo(V(c, s, -0.2f, 0.1f));
        dl->PathLineTo(V(c, s, -0.36f, 0.1f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->PathArcTo(V(c, s, 0.02f, 0), s * 0.16f, -PI * 0.3f, PI * 0.3f, 8);
        dl->PathStroke(col, 0, t);
        dl->PathArcTo(V(c, s, 0.02f, 0), s * 0.3f, -PI * 0.3f, PI * 0.3f, 10);
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Clock:
        dl->AddCircle(V(c, s, 0, 0.04f), s * 0.33f, col, 24, t);
        dl->AddLine(V(c, s, 0, 0.04f), V(c, s, 0, -0.16f), col, t);
        dl->AddLine(V(c, s, 0, 0.04f), V(c, s, 0.14f, 0.12f), col, t);
        dl->AddLine(V(c, s, -0.1f, -0.4f), V(c, s, 0.1f, -0.4f), col, t * 1.2f);
        break;
    case Icon::File:
        dl->PathLineTo(V(c, s, -0.26f, -0.36f));
        dl->PathLineTo(V(c, s, 0.1f, -0.36f));
        dl->PathLineTo(V(c, s, 0.28f, -0.18f));
        dl->PathLineTo(V(c, s, 0.28f, 0.36f));
        dl->PathLineTo(V(c, s, -0.26f, 0.36f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddLine(V(c, s, -0.12f, 0.02f), V(c, s, 0.14f, 0.02f), col, t);
        dl->AddLine(V(c, s, -0.12f, 0.18f), V(c, s, 0.14f, 0.18f), col, t);
        break;
    }
}

void Tooltip(const char* text) {
    if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(6)));
        ImGui::SetTooltip("%s", text);
        ImGui::PopStyleVar();
    }
}

// Botão de ícone: quadrado de cantos suaves; ativo = fundo azul translúcido + ícone azul. Sem halos.

// ---- iluminação: superfícies com gradiente, brilho e luz de borda
void GradRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 top, ImVec4 bot, float r, ImDrawFlags f) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, anim::Col(ImVec4(1, 1, 1, top.w)), r, f);
    int v1 = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, v1, a, ImVec2(a.x, b.y), anim::Col(ImVec4(top.x, top.y, top.z, 1)),
                                                  anim::Col(ImVec4(bot.x, bot.y, bot.z, 1)));
}

void Glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImVec4 col, float strength) {
    if (strength <= 0.01f) return;
    for (int i = 4; i >= 1; --i) {
        float e = px(2.5f * i);
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e + px(2)), ImVec2(b.x + e, b.y + e + px(2)), anim::Col(anim::WithAlpha(col, strength * 0.05f * (5 - i))), r + e);
    }
}

void Surface(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float hover, ImVec4 accent, bool selected) {
    ImVec4 top = anim::Lerp(ImVec4(0.135f, 0.152f, 0.185f, 1.f), ImVec4(0.17f, 0.19f, 0.23f, 1.f), hover);
    ImVec4 bot = anim::Lerp(ImVec4(0.105f, 0.118f, 0.143f, 1.f), ImVec4(0.13f, 0.145f, 0.175f, 1.f), hover);
    if (selected) Glow(dl, a, b, r, accent, 0.9f);
    GradRect(dl, a, b, top, bot, r);
    // luz de cima (borda superior clara) e contorno sutil
    dl->AddLine(ImVec2(a.x + r, a.y + 0.5f), ImVec2(b.x - r, a.y + 0.5f), anim::Col(ImVec4(1, 1, 1, 0.07f + 0.05f * hover)), px(1));
    dl->AddRect(a, b, anim::Col(selected ? anim::WithAlpha(accent, 0.9f) : ImVec4(1, 1, 1, 0.045f + 0.04f * hover)), r, 0, selected ? px(1.5f) : px(1));
}

bool IconButton(const char* id, Icon icon, float size, const char* tooltip, bool active, ImVec4 accent, bool round) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##ib", ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImGuiID gid = ImGui::GetItemID();
    float h = anim::To(gid, hovered ? 1.f : 0.f, 16.f);
    float a = anim::To(gid + 1, active ? 1.f : 0.f, 12.f);
    float press = anim::To(gid + 2, held ? 1.f : 0.f, 25.f);
    if (accent.w == 0) accent = theme::Cyan;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    float rr = size * 0.5f * (1.f - 0.05f * press);
    ImVec4 bg = anim::Lerp(ImVec4(1, 1, 1, 0.0f), ImVec4(1, 1, 1, 0.07f), h);
    bg = anim::Lerp(bg, anim::WithAlpha(accent, 0.16f + 0.06f * h), a);
    if (a > 0.01f) {
        if (round) orbfx::RadialGradient(dl, c, rr * 0.6f, rr * 1.9f, anim::Col(anim::WithAlpha(accent, 0.22f * a)), anim::Col(anim::WithAlpha(accent, 0.f)), 32);
        else Glow(dl, ImVec2(c.x - rr, c.y - rr), ImVec2(c.x + rr, c.y + rr), px(6), accent, 0.55f * a);
    }
    if (round) dl->AddCircleFilled(c, rr, anim::Col(bg), 32);
    else dl->AddRectFilled(ImVec2(c.x - rr, c.y - rr), ImVec2(c.x + rr, c.y + rr), anim::Col(bg), px(6));
    if (a > 0.01f && !round) dl->AddLine(ImVec2(c.x - rr + px(6), c.y - rr + 0.5f), ImVec2(c.x + rr - px(6), c.y - rr + 0.5f), anim::Col(anim::WithAlpha(accent, 0.5f * a)), px(1));
    ImVec4 ic = anim::Lerp(theme::TextDim, theme::Text, h);
    ic = anim::Lerp(ic, accent, a);
    DrawIcon(dl, icon, c, size * 0.5f, anim::Col(ic));
    Tooltip(tooltip);
    ImGui::PopID();
    return pressed;
}

static bool ButtonBase(const char* label, ImVec2 size, Icon icon, bool primary) {
    ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImGui::PushFont(theme::fonts.semibold);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    float iconW = (int)icon >= 0 ? ts.y + px(6) : 0.f;
    if (size.x <= 0) size.x = ts.x + iconW + st.FramePadding.x * 2.4f;
    if (size.y <= 0) size.y = ts.y + st.FramePadding.y * 2;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    float h = anim::To(id, hov ? 1.f : 0.f, 16.f);
    float pr = anim::To(id + 1, held ? 1.f : 0.f, 25.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = p, b(p.x + size.x, p.y + size.y);
    float r = px(6);
    ImVec4 tc;
    if (primary) {
        ImVec4 base = anim::Lerp(theme::Cyan, ImVec4(0.32f, 0.65f, 1.f, 1.f), h);
        base = anim::Lerp(base, theme::Blue, pr);
        if (!disabled) Glow(dl, a, b, r, theme::Cyan, 0.35f + 0.65f * h);
        GradRect(dl, a, b, anim::Lerp(base, ImVec4(1, 1, 1, 1), 0.14f), anim::Lerp(base, ImVec4(0, 0, 0, 1), 0.12f), r);
        // linha de luz no topo (acabamento de "botão físico")
        dl->AddLine(ImVec2(a.x + r, a.y + 1), ImVec2(b.x - r, a.y + 1), anim::Col(ImVec4(1, 1, 1, 0.22f)), px(1));
        tc = ImVec4(1, 1, 1, 1);
    } else {
        GradRect(dl, a, b, anim::Lerp(ImVec4(0.145f, 0.162f, 0.195f, 1.f), ImVec4(0.18f, 0.2f, 0.24f, 1.f), h),
                 anim::Lerp(ImVec4(0.11f, 0.124f, 0.15f, 1.f), ImVec4(0.14f, 0.157f, 0.19f, 1.f), h), r);
        dl->AddLine(ImVec2(a.x + r, a.y + 0.5f), ImVec2(b.x - r, a.y + 0.5f), anim::Col(ImVec4(1, 1, 1, 0.07f + 0.05f * h)), px(1));
        dl->AddRect(a, b, anim::Col(anim::Lerp(ImVec4(1, 1, 1, 0.07f), anim::WithAlpha(theme::Cyan, 0.6f), h)), r, 0, px(1));
        tc = anim::Lerp(theme::TextDim, theme::Text, h);
    }
    if (disabled) tc.w *= 0.45f;
    float cx = (a.x + b.x) * 0.5f - (ts.x + iconW) * 0.5f;
    float cy = (a.y + b.y) * 0.5f;
    if ((int)icon >= 0) DrawIcon(dl, icon, ImVec2(cx + ts.y * 0.5f, cy), ts.y * 0.9f, anim::Col(tc));
    dl->AddText(ImVec2(cx + iconW, cy - ts.y * 0.5f), anim::Col(tc), label, end);
    ImGui::PopFont();
    return pressed;
}

bool PrimaryButton(const char* label, ImVec2 size, Icon icon) { return ButtonBase(label, size, icon, true); }
bool GhostButton(const char* label, ImVec2 size, Icon icon) { return ButtonBase(label, size, icon, false); }

bool Toggle(const char* label, bool* v, const char* hint) {
    ImGui::PushID(label);
    float hgt = ImGui::GetFrameHeight() * 0.62f;
    float wdt = hgt * 1.8f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    float rowH = std::max(ImGui::GetFrameHeight(), ts.y);
    bool pressed = ImGui::InvisibleButton("##tg", ImVec2(std::max(avail, wdt + ts.x + px(12)), rowH));
    if (pressed) { *v = !*v; if (g_toggleSound) g_toggleSound(*v); }
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered();
    float t = anim::Spring(id, *v ? 1.f : 0.f, 300.f, 26.f);
    float h = anim::To(id + 7, hov ? 1.f : 0.f, 14.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (h > 0.01f) dl->AddRectFilled(ImVec2(p.x - px(6), p.y), ImVec2(p.x + avail + px(2), p.y + rowH), anim::Col(ImVec4(1, 1, 1, 0.03f * h)), px(6));
    ImVec2 a(p.x + avail - wdt, p.y + (rowH - hgt) * 0.5f);
    ImVec2 b(a.x + wdt, a.y + hgt);
    ImVec4 off(0.20f, 0.22f, 0.26f, 1.f);
    dl->AddRectFilled(a, b, anim::Col(anim::Lerp(off, theme::Cyan, anim::Clamp01(t))), hgt * 0.5f);
    float r = hgt * 0.5f - px(2.5f);
    float kx = a.x + hgt * 0.5f + (wdt - hgt) * t;
    dl->AddCircleFilled(ImVec2(kx, a.y + hgt * 0.5f), r, anim::Col(ImVec4(1, 1, 1, 1)), 24);
    dl->AddText(ImVec2(p.x, p.y + (rowH - ts.y) * 0.5f), anim::Col(anim::Lerp(theme::TextDim, theme::Text, std::max(h, t * 0.6f))), label,
                ImGui::FindRenderedTextEnd(label));
    if (hint) Tooltip(hint);
    ImGui::PopID();
    return pressed;
}

bool Chip(const char* label, float appear) {
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 size(ts.x + px(24), ts.y + px(12));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    ImGuiID id = ImGui::GetItemID();
    float h = anim::To(id, ImGui::IsItemHovered() ? 1.f : 0.f, 16.f);
    float e = anim::EaseOutCubic(appear);
    float dy = (1 - e) * px(10);
    float alpha = anim::Clamp01(appear * 1.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p.x, p.y + dy), b(p.x + size.x, p.y + size.y + dy);
    dl->AddRectFilled(a, b, anim::Col(anim::Lerp(ImVec4(0.118f, 0.133f, 0.161f, 1.f), ImVec4(0.153f, 0.173f, 0.208f, 1.f), h), alpha), px(6));
    dl->AddRectFilled(a, ImVec2(a.x + px(3), b.y), anim::Col(anim::WithAlpha(theme::Cyan, 0.35f + 0.65f * h), alpha), px(6), ImDrawFlags_RoundCornersLeft);
    dl->AddText(ImVec2(a.x + px(13), a.y + px(6)), anim::Col(anim::Lerp(theme::TextDim, theme::Text, h), alpha), label);
    return pressed;
}

void Spinner(const char* id, float radius, float thickness, ImVec4 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(radius * 2, radius * 2));
    (void)id;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + radius, p.y + radius);
    float t = anim::Time();
    float start = t * 5.f;
    float len = PI * (0.6f + 0.5f * std::sin(t * 2.3f));
    dl->AddCircle(c, radius - thickness, anim::Col(anim::WithAlpha(color, 0.15f)), 32, thickness);
    dl->PathArcTo(c, radius - thickness, start, start + len, 24);
    dl->PathStroke(anim::Col(color), 0, thickness);
}

void Progress(float f, ImVec2 size, bool indet) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = px(6);
    ImGui::Dummy(size);
    ImGuiID id = ImGui::GetItemID() ? ImGui::GetItemID() : ImGui::GetID("##prog");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + size.x, p.y + size.y);
    float r = px(2);
    dl->AddRectFilled(p, b, anim::Col(ImVec4(1, 1, 1, 0.07f)), r);
    float t = anim::Time();
    if (indet) {
        float x = std::fmod(t * 0.7f, 1.4f) - 0.4f;
        float x0 = std::max(0.f, x), x1 = std::min(1.f, x + 0.4f);
        if (x1 > x0) dl->AddRectFilled(ImVec2(p.x + size.x * x0, p.y), ImVec2(p.x + size.x * x1, b.y), anim::Col(theme::Cyan), r);
        return;
    }
    float shown = anim::To(id, anim::Clamp01(f), 6.f, 0.f);
    dl->AddRectFilled(p, ImVec2(p.x + std::max(size.y, size.x * shown), b.y), anim::Col(theme::Cyan), r);
}

// Título de seção: marcador azul + texto condensado em maiúsculas + linha fina.
void SectionTitle(const char* label, Icon icon) {
    (void)icon;
    ImGui::Dummy(ImVec2(0, px(8)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(theme::fonts.label);
    float hgt = ImGui::GetTextLineHeight();
    std::wstring w;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, label, -1, nullptr, 0);
        w.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, label, -1, w.data(), n);
        CharUpperW(w.data());
        int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string up(m, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, up.data(), m, nullptr, nullptr);
        up.resize(strlen(up.c_str()));
        dl->AddRectFilled(ImVec2(p.x, p.y + hgt * 0.18f), ImVec2(p.x + px(3), p.y + hgt * 0.86f), anim::Col(theme::Cyan), px(1));
        // espaçamento entre letras
        float x = p.x + px(11);
        const char* s = up.c_str();
        while (*s) {
            const char* nx = s;
            unsigned int cp;
            nx += ImTextCharFromUtf8(&cp, s, nullptr);
            dl->AddText(ImVec2(x, p.y), anim::Col(theme::TextDim), s, nx);
            x += ImGui::CalcTextSize(s, nx).x + px(1.2f);
            s = nx;
        }
        float rx = p.x + ImGui::GetContentRegionAvail().x;
        if (rx > x + px(10)) dl->AddLine(ImVec2(x + px(10), p.y + hgt * 0.55f), ImVec2(rx, p.y + hgt * 0.55f), anim::Col(ImVec4(1, 1, 1, 0.07f)), px(1));
    }
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, hgt + px(4)));
}

void Hint(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ImGui::PushFont(theme::fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextFaint);
    ImGui::TextWrapped("%s", buf);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void StatusDot(ImVec4 color, bool pulse) {
    float s = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(s * 0.7f, s));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + s * 0.35f, p.y + s * 0.5f);
    float r = s * 0.18f;
    if (pulse) {
        float t = std::fmod(anim::Time() * 0.9f, 1.f);
        dl->AddCircleFilled(c, r * (1 + 1.4f * t), anim::Col(anim::WithAlpha(color, 0.35f * (1 - t))), 20);
    }
    dl->AddCircleFilled(c, r, anim::Col(color), 16);
}

// Cartão: fundo sólido, borda fina e (opcional) barra de cor à esquerda.
void BeginCard(const char* id, ImVec4 tint, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(13)));
    ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    ImVec2 b(a.x + sz.x, a.y + sz.y);
    float r = px(8);
    dl->AddRectFilled(a, b, anim::Col(ImVec4(0.098f, 0.110f, 0.133f, 0.97f)), r);
    dl->AddRect(a, b, anim::Col(ImVec4(1, 1, 1, 0.06f)), r, 0, px(1));
    if (tint.w > 0) dl->AddRectFilled(a, ImVec2(a.x + px(3), b.y), anim::Col(tint), r, ImDrawFlags_RoundCornersLeft);
}

void EndCard() { ImGui::EndChild(); }

void Badge(const char* label, ImVec4 color) {
    ImGui::PushFont(theme::fonts.caption);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(ts.x + px(12), ts.y + px(4));
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), anim::Col(anim::WithAlpha(color, 0.15f)), px(4));
    dl->AddText(ImVec2(p.x + px(6), p.y + px(2)), anim::Col(color), label);
    ImGui::PopFont();
}

} // namespace w
