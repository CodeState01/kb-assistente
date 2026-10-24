#pragma once
#include <string>
#include <vector>

class App;

namespace ui {

struct WindowRequests {
    bool minimize = false;
    bool hideToTray = false;
    bool toggleMaximize = false;
    bool quit = false;
    bool toggleFullscreen = false;
};

struct TitleBarInfo {
    float height = 0;        // altura da barra de título (px, coordenadas do cliente)
    float buttonsStartX = 0; // a partir daqui ficam os botões (não arrastável)
};

void Init(App& app, float scale);
// Desenha um frame completo. mainVisible = janela principal visível (não oculta / minimizada).
void Frame(App& app, bool mainVisible);
WindowRequests& Requests();
TitleBarInfo GetTitleBar();
bool WantsContinuousRender();
const char* HotkeyName(int mods, int vk);
void SetDebugMode(int mode); // -1 = normal
void SetFullscreen(bool on);
// Arquivos arrastados para a janela (imagens viram anexos).
void OnFilesDropped(App& app, const std::vector<std::string>& paths);

} // namespace ui
