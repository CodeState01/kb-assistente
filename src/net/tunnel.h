#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// Túnel seguro (Cloudflare Quick Tunnel) para o app do celular funcionar pela internet,
// sem depender de Wi-Fi, roteador ou Firewall. Gera um endereço https://xxxx.trycloudflare.com.
class Tunnel {
public:
    enum class State { Off, Downloading, Starting, Ready, Error };
    ~Tunnel();
    void Start(int localPort);
    void Stop();
    State GetState() const { return state_.load(); }
    std::string Url();
    std::string Error();
    float DownloadProgress() const { return progress_.load(); }

private:
    void Run(int port);
    std::thread th_;
    std::atomic<State> state_{State::Off};
    std::atomic<bool> quit_{false};
    std::atomic<float> progress_{0.f};
    std::mutex mtx_;
    std::string url_, error_;
    void* process_ = nullptr;
    void* job_ = nullptr;
};
