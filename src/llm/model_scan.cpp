#include "llm/model_scan.h"
#include "core/config.h"
#include "core/platform.h"
#include "core/text.h"
#include <windows.h>
#include <algorithm>
#include <functional>

namespace scan {

static void Walk(const std::wstring& dir, int depth, const std::function<void(const std::wstring&, uint64_t)>& onFile) {
    if (depth < 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) Walk(full, depth - 1, onFile);
        } else {
            onFile(full, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static std::vector<std::wstring> CommonDirs() {
    std::wstring home = text::Widen(platform::GetEnv("USERPROFILE"));
    std::vector<std::wstring> d = {
        text::Widen(config::ModelsDir()),
        home + L"\\.lmstudio\\models",
        home + L"\\.cache\\lm-studio\\models",
        home + L"\\AppData\\Roaming\\Jan\\data\\models",
        home + L"\\jan\\models",
        home + L"\\models",
        home + L"\\Downloads",
        home + L"\\Documents\\models",
    };
    // Pasta do executável também.
    std::wstring exe = text::Widen(platform::ExePath());
    d.push_back(exe.substr(0, exe.find_last_of(L"\\/")));
    return d;
}

static bool EndsWithI(const std::wstring& s, const std::wstring& suf) {
    if (s.size() < suf.size()) return false;
    return _wcsicmp(s.c_str() + s.size() - suf.size(), suf.c_str()) == 0;
}

std::vector<LocalFile> FindGgufModels() {
    std::vector<LocalFile> out;
    for (auto& d : CommonDirs()) {
        Walk(d, 4, [&](const std::wstring& f, uint64_t sz) {
            if (!EndsWithI(f, L".gguf") || sz < 200ull * 1024 * 1024) return; // ignora projetores/mmproj pequenos
            std::wstring name = f.substr(f.find_last_of(L"\\/") + 1);
            if (name.find(L"mmproj") != std::wstring::npos) return;
            out.push_back({text::Narrow(f), text::Narrow(name), sz});
        });
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.size > b.size; });
    out.erase(std::unique(out.begin(), out.end(), [](auto& a, auto& b) { return a.path == b.path; }), out.end());
    return out;
}

std::vector<LocalFile> FindWhisperModels() {
    std::vector<LocalFile> out;
    for (auto& d : CommonDirs()) {
        Walk(d, 2, [&](const std::wstring& f, uint64_t sz) {
            std::wstring name = f.substr(f.find_last_of(L"\\/") + 1);
            if (name.rfind(L"ggml-", 0) == 0 && EndsWithI(name, L".bin")) out.push_back({text::Narrow(f), text::Narrow(name), sz});
        });
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.size > b.size; });
    out.erase(std::unique(out.begin(), out.end(), [](auto& a, auto& b) { return a.path == b.path; }), out.end());
    return out;
}

std::string FindLlamaServer() {
    wchar_t buf[MAX_PATH * 2];
    if (SearchPathW(nullptr, L"llama-server.exe", nullptr, (DWORD)std::size(buf), buf, nullptr)) return text::Narrow(buf);
    std::string found;
    std::wstring home = text::Widen(platform::GetEnv("USERPROFILE"));
    std::wstring local = text::Widen(platform::GetEnv("LOCALAPPDATA"));
    for (auto& d : {text::Widen(config::AppDataDir()), home + L"\\llama.cpp", home + L"\\Downloads",
                    local + L"\\Microsoft\\WinGet\\Packages"}) {
        Walk(d, 3, [&](const std::wstring& f, uint64_t) {
            if (found.empty() && EndsWithI(f, L"\\llama-server.exe")) found = text::Narrow(f);
        });
        if (!found.empty()) break;
    }
    return found;
}

} // namespace scan
