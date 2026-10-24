#pragma once
#include "ui/blocks.h"
#include "imgui.h"
#include <string>
#include <vector>

namespace ui {

// Renderiza os blocos de uma mensagem (parágrafos + embeds) na largura disponível.
void RenderBlocks(const std::vector<Block>& blocks, int msgId, bool streaming);
// Renderiza um embed isolado (cartão, código, tabela...). 'key' identifica unicamente o embed.
void RenderEmbed(const Block& b, int key, bool allowPopOut);
// Destaca um embed numa janela nativa flutuante (pode ser acoplada de volta via docking).
void PopOut(const Block& b, int key, bool pinned = false);
void RenderFloatingEmbeds();
int FloatingCount();
const char* EmbedTitle(const Block& b);
// Begin() de painel: transparente quando acoplado; com fundo animado quando vira janela própria.
bool BeginPanel(const char* name, bool* open, ImGuiWindowFlags flags, ImVec4 accent);
// Foto de fundo da janela principal (opacidade 0..1).
void SetBackdropPhoto(bool on, float opacity);
void DrawBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 accent, bool particles);

} // namespace ui
