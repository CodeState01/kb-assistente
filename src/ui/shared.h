#pragma once
#include "imgui.h"
#include "media/image.h"
#include <string>

class App;

// Peças da interface usadas por mais de uma tela (implementadas em views.cpp).
namespace ui {
ImTextureID PictureTexture(const PicturePtr& p);
void FloatingWindowSetup(ImVec2 size, int slot);          // posição/tamanho iniciais de uma janela flutuante
bool SegmentedTabs(const char* id, int* v, const char* const* labels, int n);
void DrawQrCode(const std::string& data, float size);
void FieldLabel(const char* text);                          // rótulo pequeno acima de um campo

// Telas novas
void ShopWindow(App& app, bool* open);
void ShopShowProduct(const std::string& id);
void ShopTestView(bool admin, int cat);                   // KB_FOCUS=shopadmin / shopkb              // abre a loja já no produto (IA / voz)
void AgendaWindow(App& app, bool* open);
void AgendaSetTab(int tab);
void PermissionPopups(App& app);                          // pedidos de permissão (sempre visíveis, mesmo minimizado)
void ComputerOverlay(App& app);                           // aviso "o KB está usando o mouse"
}
