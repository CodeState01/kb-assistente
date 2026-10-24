#pragma once

// Sons curtos da interface, sintetizados na hora (sem arquivos) e misturados num dispositivo
// de saída próprio — vários podem tocar ao mesmo tempo sem cortar a voz do KB nem o timer.
namespace sfx {

enum Id {
    Hover, Click, ToggleOn, ToggleOff, Open, Close, Tab, Send, Receive, Notify, Success, Error,
    Permission, ListenStart, ListenStop, Coin, Pop, Count
};

void Init();
void Shutdown();
void Play(Id id, float gain = 1.f);
void SetVolume(int percent);   // 0-100
void SetEnabled(bool on);
void SetHover(bool on);        // tique ao passar o mouse
bool Enabled();

// Chamado uma vez por frame pela interface: toca hover/clique em qualquer controle do ImGui.
void UiFrame();

} // namespace sfx
