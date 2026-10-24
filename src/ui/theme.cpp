#include "ui/theme.h"
#include <windows.h>
#include <string>

namespace theme {

Fonts fonts;
static float g_scale = 1.f;
float S() { return g_scale; }

// Identidade KB: preto grafite de academia, azul da logo e vermelho do Muay Thai.
const ImVec4 Bg        = ImVec4(0.043f, 0.047f, 0.059f, 1.00f);   // #0B0C0F
const ImVec4 Bg2       = ImVec4(0.063f, 0.071f, 0.086f, 1.00f);   // #101216
const ImVec4 Panel     = ImVec4(0.082f, 0.094f, 0.114f, 1.00f);   // #15181D
const ImVec4 PanelHi   = ImVec4(0.110f, 0.125f, 0.153f, 1.00f);   // #1C2027
const ImVec4 Border    = ImVec4(0.600f, 0.650f, 0.750f, 0.14f);
const ImVec4 Text      = ImVec4(0.953f, 0.957f, 0.965f, 1.00f);
const ImVec4 TextDim   = ImVec4(0.631f, 0.655f, 0.702f, 1.00f);
const ImVec4 TextFaint = ImVec4(0.392f, 0.420f, 0.471f, 1.00f);
const ImVec4 Cyan      = ImVec4(0.180f, 0.565f, 1.000f, 1.00f);   // azul KB (destaque principal)
const ImVec4 Blue      = ImVec4(0.122f, 0.435f, 0.878f, 1.00f);   // azul escuro
const ImVec4 Violet    = ImVec4(0.420f, 0.520f, 0.700f, 1.00f);   // azul-aço (sem roxo)
const ImVec4 Pink      = ImVec4(1.000f, 0.302f, 0.380f, 1.00f);   // vermelho claro
const ImVec4 Green     = ImVec4(0.133f, 0.773f, 0.369f, 1.00f);
const ImVec4 Amber     = ImVec4(0.961f, 0.647f, 0.141f, 1.00f);
const ImVec4 Red       = ImVec4(0.902f, 0.224f, 0.275f, 1.00f);   // vermelho Muay Thai

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
    // títulos em fonte condensada esportiva (Bahnschrift, do Windows 10/11)
    std::string disp = WinFont("bahnschrift.ttf");
    if (disp.empty()) disp = sb;
    fonts.title = add(disp, 24.f);
    fonts.huge = add(disp, 40.f);
    fonts.label = add(disp, 14.f);
    {
        static const ImWchar digits[] = {0x0020, 0x0020, 0x002B, 0x002B, 0x002D, 0x003A, 0};
        ImFontConfig dc;
        dc.OversampleH = 1;
        dc.OversampleV = 1;
        if (!disp.empty()) fonts.clock = io.Fonts->AddFontFromFileTTF(disp.c_str(), 160.f * scale, &dc, digits);
        if (!fonts.clock) fonts.clock = fonts.huge;
    }
    fonts.mono = add(mono, 15.f);
    io.FontDefault = fonts.body;

    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 9);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 8;
    s.GrabMinSize = 8;
    s.WindowRounding = 0;   // janelas nativas já têm cantos arredondados (Win11)
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 3;
    s.GrabRounding = 3;
    s.TabRounding = 4;
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
    const ImVec4 acc = Cyan;                             // azul KB
    auto A = [&](float a) { return ImVec4(acc.x, acc.y, acc.z, a); };
    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = TextFaint;
    c[ImGuiCol_WindowBg] = Panel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.098f, 0.110f, 0.133f, 0.99f);
    c[ImGuiCol_Border] = Border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.118f, 0.133f, 0.161f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.141f, 0.157f, 0.188f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.153f, 0.173f, 0.208f, 1.f);
    c[ImGuiCol_TitleBg] = Bg2;
    c[ImGuiCol_TitleBgActive] = Panel;
    c[ImGuiCol_TitleBgCollapsed] = Bg2;
    c[ImGuiCol_MenuBarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.18f);
    c[ImGuiCol_ScrollbarGrabActive] = A(0.7f);
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.118f, 0.133f, 0.161f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.153f, 0.173f, 0.208f, 1.f);
    c[ImGuiCol_ButtonActive] = A(0.35f);
    c[ImGuiCol_Header] = A(0.14f);
    c[ImGuiCol_HeaderHovered] = A(0.22f);
    c[ImGuiCol_HeaderActive] = A(0.32f);
    c[ImGuiCol_Separator] = Border;
    c[ImGuiCol_SeparatorHovered] = A(0.6f);
    c[ImGuiCol_SeparatorActive] = acc;
    c[ImGuiCol_ResizeGrip] = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_ResizeGripHovered] = A(0.5f);
    c[ImGuiCol_ResizeGripActive] = acc;
    c[ImGuiCol_Tab] = Bg2;
    c[ImGuiCol_TabHovered] = PanelHi;
    c[ImGuiCol_TabSelected] = Panel;
    c[ImGuiCol_TabSelectedOverline] = acc;
    c[ImGuiCol_TabDimmed] = Bg2;
    c[ImGuiCol_TabDimmedSelected] = Panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(1, 1, 1, 0.15f);
    c[ImGuiCol_DockingPreview] = A(0.35f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableHeaderBg] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_TableBorderStrong] = Border;
    c[ImGuiCol_TableBorderLight] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg] = A(0.35f);
    c[ImGuiCol_NavCursor] = A(0.6f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

} // namespace theme
