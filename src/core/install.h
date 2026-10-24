#pragma once
#include <string>

// Instalação "por usuário" (sem precisar de administrador): %LOCALAPPDATA%\Programs\KB
namespace install {

constexpr const char* kVersion = "1.5.0";

std::wstring InstallDir();
std::wstring InstalledExe();
bool IsInstalled();
bool CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args, const std::wstring& desc);
std::wstring StartMenuShortcut();
std::wstring DesktopShortcut();
void RegisterUninstall(unsigned long sizeKB);
// Fecha instâncias do KB que estejam rodando a partir de 'exePath'.
void CloseRunning(const std::wstring& exePath);
// Remove atalhos, registro, inicialização automática e (opcional) dados do usuário. Agenda a remoção da pasta.
void Uninstall(bool removeUserData);

struct DeviceInfo {
    std::string os;        // "Windows 11 (build 22631)"
    std::string arch;      // "x64" / "ARM64"
    std::string kind;      // "PC de mesa", "Notebook", "Tablet / 2 em 1"
    bool supported = true;
};
DeviceInfo DetectDevice();

} // namespace install
