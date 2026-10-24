#pragma once
#include "imgui.h"

namespace theme {

struct Fonts {
    ImFont* body = nullptr;
    ImFont* caption = nullptr;
    ImFont* semibold = nullptr;
    ImFont* title = nullptr;
    ImFont* huge = nullptr;
    ImFont* mono = nullptr;
    ImFont* label = nullptr;   // rótulos de seção (condensada, maiúsculas)
};
extern Fonts fonts;

void Load(float scale);
float S();
inline float px(float v) { return v * S(); }

// Paleta "aurora boreal" escura
extern const ImVec4 Bg;
extern const ImVec4 Bg2;
extern const ImVec4 Panel;
extern const ImVec4 PanelHi;
extern const ImVec4 Border;
extern const ImVec4 Text;
extern const ImVec4 TextDim;
extern const ImVec4 TextFaint;
extern const ImVec4 Cyan;
extern const ImVec4 Blue;
extern const ImVec4 Violet;
extern const ImVec4 Pink;
extern const ImVec4 Green;
extern const ImVec4 Amber;
extern const ImVec4 Red;

} // namespace theme
