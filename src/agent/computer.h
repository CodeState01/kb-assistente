#pragma once
#include "media/image.h"
#include <atomic>
#include <string>
#include <vector>

// Controle do computador pela IA: lê os botões/campos da janela ativa (UI Automation),
// move o mouse com animação suave, clica, digita, aperta atalhos, rola e abre apps/sites.
namespace computer {

struct Element {
    int id = 0;
    std::string name, type, value;
    int x = 0, y = 0, w = 0, h = 0;     // tela (pixels físicos)
    bool focusable = false;
};

struct Snapshot {
    std::string window, app;
    int wx = 0, wy = 0, ww = 0, wh = 0;
    std::vector<Element> elements;
    PicturePtr screenshot;             // só quando pedido (modelos com visão)
    float shotScale = 1.f;             // pixels da tela por pixel da captura
    int shotX = 0, shotY = 0;          // origem da captura na tela
};

// Inspeciona a janela em primeiro plano (ignorando o próprio KB).
Snapshot Inspect(bool withScreenshot, int maxElements = 70);
std::string Describe(const Snapshot& s);              // texto numerado para a IA

bool ClickElement(const Snapshot& s, int id, bool right, bool dbl, std::string* err);
bool ClickAt(int x, int y, bool right, bool dbl);
void MoveSmooth(int x, int y, int ms = 380);
bool TypeText(const std::string& utf8);
bool PressKeys(const std::string& combo, std::string* err);  // "ctrl+l", "enter", "alt+tab"
void Scroll(int notches);                                    // >0 para cima
bool OpenApp(const std::string& name, std::string* err);
bool FocusTarget();                                          // traz a janela-alvo para frente

// Esc apertado pela pessoa (para tudo)
std::atomic<bool>& StopFlag();

} // namespace computer
