#pragma once
#include <string>
#include <cstdint>

namespace platform {
std::string ExePath();
bool SetAutostart(bool enable, bool minimized);
bool IsAutostartEnabled();
void OpenUrl(const std::string& url);
void OpenFolder(const std::string& path);
std::string PickFile(const wchar_t* filter, const std::string& title);
bool FileExists(const std::string& path);
uint64_t FileSize(const std::string& path);
uint64_t TotalRamBytes();
std::string GetEnv(const char* name);
// Inicia um processo independente (continua rodando se o KB fechar).
bool LaunchDetached(const std::string& exe, const std::string& args);
std::string FindOllama();
// Regra de entrada no Firewall do Windows para o app do celular.
bool FirewallRuleExists();
// Abre o pedido de administrador (UAC) do Windows para criar a regra; retorna true se foi criada.
bool AddFirewallRuleElevated(int port, const std::string& exePath = "");

// Processo filho (llama-server). Encerrado ao sair do app.
class ChildProcess {
public:
    ~ChildProcess();
    bool Start(const std::string& exe, const std::string& args);
    void Stop();
    bool Running() const;
private:
    void* process_ = nullptr;
    void* job_ = nullptr;
};

// Visual do Windows 11: cantos arredondados + modo escuro na janela nativa.
void StyleNativeWindow(void* hwnd, bool rounded);
void SetTopMost(void* hwnd, bool topmost);
}
