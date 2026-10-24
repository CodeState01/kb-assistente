#pragma once
#include "media/image.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// O que transmitir: um monitor inteiro ou uma janela.
struct CaptureSource {
    enum Kind { Monitor = 0, Window = 1 } kind = Monitor;
    void* handle = nullptr;       // HMONITOR ou HWND (nullptr = monitor principal)
    std::string name;             // "Tela 1 (principal)", "Google Chrome"...
    std::string app;              // nome do executável (janelas)
    int w = 0, h = 0;
    bool primary = false;
    bool minimized = false;
    PicturePtr thumb;             // miniatura para o seletor
};

namespace screencap {
// Lista monitores e janelas que podem ser transmitidos. Com thumbs=true captura uma miniatura de cada.
std::vector<CaptureSource> List(bool thumbs);
}

// Transmissão de tela para o modelo: captura a fonte escolhida ~1x por segundo
// (monitor: DXGI Desktop Duplication com GDI como alternativa; janela: PrintWindow) e guarda o último quadro.
class ScreenShare {
public:
    ~ScreenShare();
    void Start(int intervalMs = 1000, const CaptureSource& src = {});
    void Stop();
    bool Active() const { return active_.load(); }
    PicturePtr Latest();
    uint64_t FrameId() const { return frameId_.load(); }
    const char* Method() const { return method_; }
    std::string SourceName();
    bool SourceLost() const { return lost_.load(); }  // a janela foi fechada

private:
    void Run();
    std::thread th_;
    std::atomic<bool> active_{false}, quit_{false}, lost_{false};
    std::atomic<uint64_t> frameId_{0};
    std::mutex mtx_;
    PicturePtr latest_;
    CaptureSource src_;
    int intervalMs_ = 1000;
    const char* method_ = "";
};
