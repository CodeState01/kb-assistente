#pragma once
#include "media/image.h"
#include <atomic>
#include <mutex>
#include <thread>

// Transmissão de tela para o modelo: captura o monitor principal ~1x por segundo
// (DXGI Desktop Duplication, com GDI como alternativa) e guarda o último quadro.
class ScreenShare {
public:
    ~ScreenShare();
    void Start(int intervalMs = 1000);
    void Stop();
    bool Active() const { return active_.load(); }
    PicturePtr Latest();
    uint64_t FrameId() const { return frameId_.load(); }
    const char* Method() const { return method_; }

private:
    void Run();
    std::thread th_;
    std::atomic<bool> active_{false}, quit_{false};
    std::atomic<uint64_t> frameId_{0};
    std::mutex mtx_;
    PicturePtr latest_;
    int intervalMs_ = 1000;
    const char* method_ = "";
};
