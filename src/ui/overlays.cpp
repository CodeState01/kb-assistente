// Janelas que ficam por cima de tudo: pedidos de permissão e o aviso de "KB usando o mouse".
#include "ui/shared.h"
#include "app.h"
#include "audio/sfx.h"
#include "core/platform.h"
#include "ui/anim.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/orb.h"
#include "imgui_internal.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <map>

using theme::px;

namespace ui {
namespace {
RECT WorkArea() {
    RECT r{0, 0, 1920, 1080};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
    return r;
}

void TopWindowClass() {
    ImGuiWindowClass wc;
    wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_TopMost | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&wc);
}

void KeepOnTop() {
    ImGuiViewport* vp = ImGui::GetWindowViewport();
    static std::map<void*, bool> done;
    if (vp && vp != ImGui::GetMainViewport() && vp->PlatformHandleRaw && !done[vp->PlatformHandleRaw]) {
        platform::SetTopMost(vp->PlatformHandleRaw, true);
        done[vp->PlatformHandleRaw] = true;
    }
}
} // namespace

void PermissionPopups(App& app) {
    auto reqs = app.PendingPermissions();
    static double appearT = 0;
    static int lastCount = 0;
    if ((int)reqs.size() > lastCount) appearT = ImGui::GetTime();
    lastCount = (int)reqs.size();
    if (reqs.empty()) return;
    if (reqs.size() > 3) reqs.resize(3);
    RECT wa = WorkArea();
    float slide = 1.f - anim::EaseOutCubic(std::clamp((float)(ImGui::GetTime() - appearT) / 0.35f, 0.f, 1.f));
    TopWindowClass();
    ImGui::SetNextWindowPos(ImVec2((float)wa.right - px(18) + slide * px(120), (float)wa.bottom - px(18)), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowSize(ImVec2(px(440), 0));
    ImGui::SetNextWindowBgAlpha(1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.f - slide);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.082f, 0.1f, 1.f));
    ImGui::Begin("##kbperm", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
    KeepOnTop();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // faixa de luz no topo da janela
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 4.f);
    for (size_t i = 0; i < reqs.size(); ++i) {
        const PermissionReq& r = reqs[i];
        ImGui::PushID(r.id);
        ImVec4 acc = r.kind == 1 ? ImVec4(0.902f, 0.224f, 0.275f, 1.f) : theme::Cyan;
        if (i) { ImGui::Dummy(ImVec2(0, px(4))); ImGui::Separator(); ImGui::Dummy(ImVec2(0, px(4))); }
        ImVec2 p = ImGui::GetCursorScreenPos();
        float ic = px(40);
        orbfx::RadialGradient(dl, ImVec2(p.x + ic * 0.5f, p.y + ic * 0.5f), 0, ic * (0.9f + 0.15f * pulse), anim::Col(anim::WithAlpha(acc, 0.35f)), anim::Col(anim::WithAlpha(acc, 0.f)), 32);
        dl->AddCircleFilled(ImVec2(p.x + ic * 0.5f, p.y + ic * 0.5f), ic * 0.5f, anim::Col(anim::WithAlpha(acc, 0.18f)), 32);
        w::DrawIcon(dl, r.kind == 1 ? Icon::Calendar : r.kind == 0 ? Icon::Cursor : Icon::Shield, ImVec2(p.x + ic * 0.5f, p.y + ic * 0.5f), ic * 0.55f, anim::Col(acc));
        ImGui::SetCursorScreenPos(ImVec2(p.x + ic + px(12), p.y));
        ImGui::BeginGroup();
        ImGui::PushFont(theme::fonts.label);
        ImGui::PushStyleColor(ImGuiCol_Text, acc);
        ImGui::TextUnformatted(r.kind == 1 ? "KB · NOVO PEDIDO" : "KB · PERMISSÃO");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PushFont(theme::fonts.semibold);
        ImGui::TextUnformatted(r.title.c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(440) - ic - px(48));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
        ImGui::TextUnformatted(r.detail.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, px(4)));
        if (w::PrimaryButton(r.okLabel.c_str(), ImVec2(px(180), px(38)), Icon::Check)) app.AnswerPermission(r.id, 1);
        ImGui::SameLine(0, px(8));
        if (w::GhostButton(r.noLabel.c_str(), ImVec2(px(140), px(38)), Icon::Close)) app.AnswerPermission(r.id, 0);
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, px(2)));
    dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + px(3)), anim::Col(anim::WithAlpha(reqs[0].kind == 1 ? ImVec4(0.902f, 0.224f, 0.275f, 1.f) : theme::Cyan, 0.6f + 0.4f * pulse)));
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void ComputerOverlay(App& app) {
    static bool escWas = false;
    if (!app.ComputerActive()) { escWas = false; return; }
    // Esc em qualquer lugar para tudo
    bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (esc && !escWas) { app.StopComputer(); sfx::Play(sfx::Close); }
    escWas = esc;
    RECT wa = WorkArea();
    TopWindowClass();
    ImGui::SetNextWindowPos(ImVec2((wa.left + wa.right) * 0.5f, (float)wa.bottom - px(14)), ImGuiCond_Always, ImVec2(0.5f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(10)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.082f, 0.1f, 0.97f));
    ImGui::Begin("##kbcomputer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
    KeepOnTop();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.f);
    float h = ImGui::GetFrameHeight();
    ImVec2 c(p.x + px(8), p.y + h * 0.5f);
    orbfx::RadialGradient(dl, c, 0, px(14), anim::Col(anim::WithAlpha(theme::Red, 0.5f * pulse)), anim::Col(anim::WithAlpha(theme::Red, 0.f)), 24);
    dl->AddCircleFilled(c, px(5), anim::Col(theme::Red), 16);
    ImGui::SetCursorScreenPos(ImVec2(p.x + px(22), p.y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(theme::fonts.semibold);
    ImGui::TextUnformatted("KB está usando o mouse e o teclado");
    ImGui::PopFont();
    std::string st = app.ComputerStatus();
    if (!st.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
        ImGui::TextUnformatted(("· " + st).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0, px(16));
    if (w::GhostButton("Parar (Esc)", ImVec2(0, 0), Icon::Stop)) app.StopComputer();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace ui
