#include "core/platform.h"
#include "core/text.h"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace platform {

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"KBAssistente";

std::string ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return text::Narrow(std::wstring(buf, n));
}

bool SetAutostart(bool enable, bool minimized) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS st;
    if (enable) {
        std::wstring cmd = L"\"" + text::Widen(ExePath()) + L"\"";
        if (minimized) cmd += L" --minimizado";
        st = RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kRunValue);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

bool IsAutostartEnabled() {
    HKEY key;
    // migra a entrada da versão antiga ("Aurora")
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        bool had = RegQueryValueExW(key, L"AuroraAssistente", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        if (had) RegDeleteValueW(key, L"AuroraAssistente");
        RegCloseKey(key);
        if (had) SetAutostart(true, true);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    bool ok = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

void OpenUrl(const std::string& url) {
    if (!(text::StartsWith(url, "http://") || text::StartsWith(url, "https://"))) return;
    ShellExecuteW(nullptr, L"open", text::Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenFolder(const std::string& path) {
    ShellExecuteW(nullptr, L"open", text::Widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string PickFile(const wchar_t* filter, const std::string& title) {
    wchar_t file[MAX_PATH * 4] = L"";
    std::wstring wtitle = text::Widen(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return text::Narrow(file);
    return {};
}

bool FileExists(const std::string& path) {
    if (path.empty()) return false;
    DWORD a = GetFileAttributesW(text::Widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t FileSize(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(text::Widen(path).c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

uint64_t TotalRamBytes() {
    MEMORYSTATUSEX m{sizeof(m)};
    GlobalMemoryStatusEx(&m);
    return m.ullTotalPhys;
}

std::string GetEnv(const char* name) {
    wchar_t buf[2048];
    DWORD n = GetEnvironmentVariableW(text::Widen(name).c_str(), buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) return {};
    return text::Narrow(std::wstring(buf, n));
}

bool LaunchDetached(const std::string& exe, const std::string& args) {
    std::wstring cmd = L"\"" + text::Widen(exe) + L"\" " + text::Widen(args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB,
                        nullptr, nullptr, &si, &pi)) {
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
            return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::string FindOllama() {
    wchar_t buf[MAX_PATH * 2];
    if (SearchPathW(nullptr, L"ollama.exe", nullptr, (DWORD)std::size(buf), buf, nullptr)) return text::Narrow(buf);
    std::string p = GetEnv("LOCALAPPDATA") + "\\Programs\\Ollama\\ollama.exe";
    if (FileExists(p)) return p;
    p = GetEnv("ProgramFiles") + "\\Ollama\\ollama.exe";
    if (FileExists(p)) return p;
    return {};
}

// ------------------------------------------------------------------ ChildProcess
ChildProcess::~ChildProcess() { Stop(); }

bool ChildProcess::Start(const std::string& exe, const std::string& args) {
    Stop();
    // Job object: o filho morre junto com o KB.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));

    std::wstring cmd = L"\"" + text::Widen(exe) + L"\" " + text::Widen(args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring wdir = text::Widen(exe);
    size_t slash = wdir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) wdir = wdir.substr(0, slash); else wdir.clear();
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        wdir.empty() ? nullptr : wdir.c_str(), &si, &pi)) {
        CloseHandle(job);
        return false;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    job_ = job;
    return true;
}

void ChildProcess::Stop() {
    if (process_) {
        TerminateProcess((HANDLE)process_, 0);
        CloseHandle((HANDLE)process_);
        process_ = nullptr;
    }
    if (job_) {
        CloseHandle((HANDLE)job_);
        job_ = nullptr;
    }
}

bool ChildProcess::Running() const {
    if (!process_) return false;
    DWORD code = 0;
    return GetExitCodeProcess((HANDLE)process_, &code) && code == STILL_ACTIVE;
}

void StyleNativeWindow(void* hwndp, bool rounded) {
    HWND hwnd = (HWND)hwndp;
    if (!hwnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int pref = rounded ? 2 /*DWMWCP_ROUND*/ : 1 /*DWMWCP_DONOTROUND*/;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    COLORREF border = RGB(40, 46, 70);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

void SetTopMost(void* hwnd, bool topmost) {
    if (!hwnd) return;
    SetWindowPos((HWND)hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace platform
