#pragma once
#include "imgui.h"
#include <vector>

// Imagens embutidas no executável (foto de fundo e logo) e o "molde" KB das partículas.
namespace assets {
void Load();                                  // precisa de contexto OpenGL ativo
ImTextureID Background(int* w, int* h);       // 0 se não houver
ImTextureID Logo();
// Pontos (normalizados em [-1,1], proporção preservada) que desenham as letras "KB" da logo.
const std::vector<ImVec2>& KbShape();
}
