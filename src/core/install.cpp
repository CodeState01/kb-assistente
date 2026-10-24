#include "core/install.h"
#include "core/text.h"
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <vector>

namespace install {

static const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KB";

static std::wstring Known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring InstallDir() { return Known(FOLDERID_LocalAppData) + L"\\Programs\\KB"; }
std::wstring InstalledExe() { return InstallDir() + L"\\KB.exe"; }
bool IsInstalled() { return GetFileAttributesW(InstalledExe().c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring StartMenuShortcut() { return Known(FOLDERID_Programs) + L"\\KB.lnk"; }
std::wstring DesktopShortcut() { return Known(FOLDERID_Desktop) + L"\\KB.lnk"; }

bool CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args, const std::wstring& desc) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
    sl->SetPath(target.c_str());
    sl->SetArguments(args.c_str());
    sl->SetDescription(desc.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    std::wstring dir = target.substr(0, target.find_last_of(L'\\'));
    sl->SetWorkingDirectory(dir.c_str());
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

void RegisterUninstall(unsigned long sizeKB) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    auto setS = [&](const wchar_t* n, const std::wstring& v) {
        RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    };
    auto setD = [&](const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, sizeof(v)); };
    std::wstring exe = InstalledExe();
    setS(L"DisplayName", L"KB — assistente de voz");
    setS(L"DisplayVersion", text::Widen(kVersion));
    setS(L"Publisher", L"KB (código aberto)");
    setS(L"DisplayIcon", exe);
    setS(L"InstallLocation", InstallDir());
    setS(L"UninstallString", L"\"" + exe + L"\" --desinstalar");
    setS(L"URLInfoAbout", L"https://github.com/CodeState01/kb-assistente");
    setD(L"NoModify", 1);
    setD(L"NoRepair", 1);
    setD(L"EstimatedSize", sizeKB);
    RegCloseKey(k);
}

void CloseRunning(const std::wstring& exePath) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{sizeof(pe)};
    DWORD self = GetCurrentProcessId();
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == self || _wcsicmp(pe.szExeFile, L"KB.exe") != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD n = (DWORD)std::size(path);
        if (QueryFullProcessImageNameW(h, 0, path, &n) && _wcsicmp(path, exePath.c_str()) == 0) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 3000);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
}

void Uninstall(bool removeUserData) {
    DeleteFileW(StartMenuShortcut().c_str());
    DeleteFileW(DesktopShortcut().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    HKEY run;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        RegDeleteValueW(run, L"KBAssistente");
        RegCloseKey(run);
    }
    std::wstring cmd = L"cmd.exe /c timeout /t 2 /nobreak >nul & rmdir /s /q \"" + InstallDir() + L"\"";
    if (removeUserData) cmd += L" & rmdir /s /q \"" + Known(FOLDERID_RoamingAppData) + L"\\KB\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

DeviceInfo DetectDevice() {
    DeviceInfo d;
    // Versão real do Windows (sem o "mentir" do manifesto)
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{sizeof(v)};
    if (auto fn = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) fn(&v);
    std::string name = v.dwMajorVersion >= 10 ? (v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10") : "Windows antigo";
    d.os = name + " (build " + std::to_string(v.dwBuildNumber) + ")";
    d.supported = v.dwMajorVersion >= 10;

    USHORT proc = 0, native = 0;
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto w2 = (IsWow64Process2Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (w2 && w2(GetCurrentProcess(), &proc, &native)) d.arch = native == IMAGE_FILE_MACHINE_ARM64 ? "ARM64" : native == IMAGE_FILE_MACHINE_AMD64 ? "x64" : "x86";
    else {
        SYSTEM_INFO si;
        GetNativeSystemInfo(&si);
        d.arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "ARM64" : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "x86";
    }
    if (d.arch == "x86") d.supported = false;

    // Tipo de aparelho: perfil de energia + bateria + tela sensível ao toque
    SYSTEM_POWER_STATUS ps{};
    GetSystemPowerStatus(&ps);
    bool battery = ps.BatteryFlag != 128 && ps.BatteryFlag != 255;
    bool touch = (GetSystemMetrics(SM_DIGITIZER) & NID_READY) != 0;
    bool slate = GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0 && touch;
    if (slate) d.kind = "Tablet / 2 em 1";
    else if (battery) d.kind = "Notebook";
    else d.kind = "PC de mesa";
    return d;
}

} // namespace install
