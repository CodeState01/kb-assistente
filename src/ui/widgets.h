#pragma once
#include "imgui.h"

enum class Icon {
    Mic, Send, Stop, Chat, Grid, Cpu, Gear, Close, Minimize, Pin, PopOut, Copy, Refresh, Download, Folder,
    Check, Info, Warning, Bulb, Play, Trash, Search, Sparkle, Wave, Phone, Image, Monitor, Expand, Shrink, File, Clock,
    Bag, Cart, Tag, Calendar, User, Globe, Cursor, Edit, External, Bell, Shield, Bolt, Keyboard, Star, Volume,
};

namespace w {

// som ao ligar/desligar um interruptor (o app instala; o instalador não tem som)
extern void (*g_toggleSound)(bool on);

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 col, float thickness = 0.f);

// Iluminação
void GradRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 top, ImVec4 bot, float r, ImDrawFlags f = 0);
void Glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImVec4 col, float strength);
// Cartão/superfície com luz de cima, gradiente e brilho quando selecionado
void Surface(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float hover, ImVec4 accent = ImVec4(0.18f, 0.565f, 1.f, 1.f), bool selected = false);

// Botão redondo/quadrado com ícone, brilho animado ao passar o mouse.
bool IconButton(const char* id, Icon icon, float size, const char* tooltip = nullptr, bool active = false,
                ImVec4 accent = ImVec4(0, 0, 0, 0), bool round = false);
// Botão com gradiente.
bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0), Icon icon = (Icon)-1);
// Botão discreto com borda.
bool GhostButton(const char* label, ImVec2 size = ImVec2(0, 0), Icon icon = (Icon)-1);
// Interruptor animado (estilo iOS).
bool Toggle(const char* label, bool* v, const char* hint = nullptr);
// "Pílula" clicável (sugestões).
bool Chip(const char* label, float appear = 1.f);
// Spinner de arcos.
void Spinner(const char* id, float radius, float thickness, ImVec4 color);
// Barra de progresso com brilho deslizante.
void Progress(float fraction, ImVec2 size, bool indeterminate = false);
// Cabeçalho de seção com linha.
void SectionTitle(const char* label, Icon icon = (Icon)-1);
// Texto de ajuda discreto.
void Hint(const char* fmt, ...);
// Ponto de status pulsante.
void StatusDot(ImVec4 color, bool pulse);
// Cartão: inicia um "child" com fundo arredondado. Use EndCard() no final.
void BeginCard(const char* id, ImVec4 tint = ImVec4(0, 0, 0, 0), float width = 0.f);
void EndCard();
// Badge pequena.
void Badge(const char* label, ImVec4 color);
void Tooltip(const char* text);

} // namespace w
