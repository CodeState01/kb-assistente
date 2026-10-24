#include "ui/theme.h"
#include <windows.h>
#include <string>

namespace theme {

Fonts fonts;
static float g_scale = 1.f;
float S() { return g_scale; }

const ImVec4 Bg        = ImVec4(0.035f, 0.045f, 0.085f, 1.00f);
const ImVec4 Bg2       = ImVec4(0.060f, 0.050f, 0.130f, 1.00f);
const ImVec4 Panel     = ImVec4(0.075f, 0.090f, 0.150f, 0.97f);
const ImVec4 PanelHi   = ImVec4(0.110f, 0.130f, 0.210f, 1.00f);
const ImVec4 Border    = ImVec4(0.300f, 0.350f, 0.550f, 0.25f);
const ImVec4 Text      = ImVec4(0.930f, 0.950f, 1.000f, 1.00f);
const ImVec4 TextDim   = ImVec4(0.640f, 0.680f, 0.800f, 1.00f);
const ImVec4 TextFaint = ImVec4(0.420f, 0.460f, 0.600f, 1.00f);
const ImVec4 Cyan      = ImVec4(0.300f, 0.820f, 0.980f, 1.00f);
const ImVec4 Blue      = ImVec4(0.300f, 0.480f, 1.000f, 1.00f);
const ImVec4 Violet    = ImVec4(0.560f, 0.400f, 1.000f, 1.00f);
const ImVec4 Pink      = ImVec4(0.970f, 0.330f, 0.680f, 1.00f);
const ImVec4 Green     = ImVec4(0.200f, 0.900f, 0.640f, 1.00f);
const ImVec4 Amber     = ImVec4(1.000f, 0.720f, 0.300f, 1.00f);
const ImVec4 Red       = ImVec4(1.000f, 0.380f, 0.420f, 1.00f);

static std::string WinFont(const char* file) {
    char dir[MAX_PATH];
    GetWindowsDirectoryA(dir, MAX_PATH);
    std::string p = std::string(dir) + "\\Fonts\\" + file;
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES ? p : std::string();
}

void Load(float scale) {
    g_scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Latim + acentos do português + pontuação tipográfica + setas/símbolos
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        static const ImWchar extra[] = {0x0100, 0x017F, 0x2000, 0x206F, 0x20A0, 0x20CF, 0x2190, 0x21FF, 0x2200, 0x22FF,
                                        0x2500, 0x25FF, 0x2600, 0x27BF, 0};
        b.AddRanges(extra);
        b.BuildRanges(&ranges);
    }
    static const ImWchar symRanges[] = {0x2190, 0x21FF, 0x2200, 0x22FF, 0x2500, 0x25FF, 0x2600, 0x27BF, 0};

    std::string ui = WinFont("segoeui.ttf");
    std::string sb = WinFont("seguisb.ttf");
    std::string sym = WinFont("seguisym.ttf");
    std::string mono = WinFont("CascadiaMono.ttf");
    if (mono.empty()) mono = WinFont("consola.ttf");
    if (sb.empty()) sb = ui;

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    auto add = [&](const std::string& path, float size) -> ImFont* {
        ImFont* f = nullptr;
        if (!path.empty()) f = io.Fonts->AddFontFromFileTTF(path.c_str(), size * scale, &cfg, ranges.Data);
        if (!f) {
            ImFontConfig d;
            d.SizePixels = size * scale;
            f = io.Fonts->AddFontDefault(&d);
            return f;
        }
        if (!sym.empty()) {
            ImFontConfig m = cfg;
            m.MergeMode = true;
            m.GlyphMinAdvanceX = 0;
            io.Fonts->AddFontFromFileTTF(sym.c_str(), size * scale, &m, symRanges);
        }
        return f;
    };

    fonts.body = add(ui, 17.f);
    fonts.caption = add(ui, 14.f);
    fonts.semibold = add(sb, 17.f);
    fonts.title = add(sb, 24.f);
    fonts.huge = add(sb, 34.f);
    fonts.mono = add(mono, 15.f);
    io.FontDefault = fonts.body;

    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 9);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 10;
    s.GrabMinSize = 12;
    s.WindowRounding = 0;   // janelas nativas já têm cantos arredondados (Win11)
    s.ChildRounding = 14;
    s.FrameRounding = 10;
    s.PopupRounding = 12;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 8;
    s.TabRounding = 9;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.DockingSeparatorSize = 2;
    s.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    s.SeparatorTextBorderSize = 1;
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = TextFaint;
    c[ImGuiCol_WindowBg] = ImVec4(0.050f, 0.060f, 0.110f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.080f, 0.140f, 0.98f);
    c[ImGuiCol_Border] = Border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.055f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.085f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.82f, 0.98f, 0.16f);
    c[ImGuiCol_TitleBg] = ImVec4(0.050f, 0.060f, 0.110f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.070f, 0.085f, 0.150f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.050f, 0.060f, 0.110f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.18f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.82f, 0.98f, 0.5f);
    c[ImGuiCol_CheckMark] = Cyan;
    c[ImGuiCol_SliderGrab] = Cyan;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.6f, 0.92f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.11f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.82f, 0.98f, 0.25f);
    c[ImGuiCol_Header] = ImVec4(0.30f, 0.82f, 0.98f, 0.12f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.82f, 0.98f, 0.18f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.82f, 0.98f, 0.26f);
    c[ImGuiCol_Separator] = Border;
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.82f, 0.98f, 0.5f);
    c[ImGuiCol_SeparatorActive] = Cyan;
    c[ImGuiCol_ResizeGrip] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.82f, 0.98f, 0.4f);
    c[ImGuiCol_ResizeGripActive] = Cyan;
    c[ImGuiCol_Tab] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.82f, 0.98f, 0.20f);
    c[ImGuiCol_TabSelected] = ImVec4(0.30f, 0.82f, 0.98f, 0.13f);
    c[ImGuiCol_TabSelectedOverline] = Cyan;
    c[ImGuiCol_TabDimmed] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.5f, 0.5f, 0.7f, 0.5f);
    c[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.82f, 0.98f, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableHeaderBg] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_TableBorderStrong] = Border;
    c[ImGuiCol_TableBorderLight] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.82f, 0.98f, 0.3f);
    c[ImGuiCol_NavCursor] = ImVec4(0.30f, 0.82f, 0.98f, 0.6f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

} // namespace theme
