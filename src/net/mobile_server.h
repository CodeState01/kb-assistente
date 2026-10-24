#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Servidor HTTP na rede local para o app do celular (Android / iPhone via navegador, instalável na tela inicial).
// Todas as rotas exigem o token secreto (?t=...) que aparece no QR code do PC.
struct MobileHooks {
    std::function<std::string(int since)> stateJson;                          // estado + mensagens
    std::function<void(const std::string& text, const std::string& jpegB64)> send;
    std::function<void()> listen;                                             // ouvir pelo microfone do PC
    std::function<void()> stop;                                               // interromper
};

class MobileServer {
public:
    ~MobileServer();
    bool Start(int port, const std::string& token, MobileHooks hooks, std::string* err);
    void Stop();
    bool Running() const { return running_.load(); }
    int Port() const { return port_; }
    static std::vector<std::string> LanAddresses();

private:
    void Run();
    void Handle(uintptr_t sock);
    std::thread th_;
    std::atomic<bool> running_{false};
    uintptr_t listen_ = (uintptr_t)~0ull;
    int port_ = 0;
    std::string token_;
    MobileHooks hooks_;
};
