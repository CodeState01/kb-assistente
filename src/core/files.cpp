#include "core/files.h"
#include "core/text.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>

namespace files {

static std::string Known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = text::Narrow(p);
    if (p) CoTaskMemFree(p);
    return out;
}

std::string DesktopDir() { return Known(FOLDERID_Desktop); }
std::string DocumentsDir() { return Known(FOLDERID_Documents); }

static bool StartsWithI(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && text::Fold(s.substr(0, p.size())) == text::Fold(p);
}

Resolved Resolve(const std::string& requested) {
    Resolved r;
    std::string p = text::Trim(requested);
    if (p.size() >= 2 && (p.front() == '"' || p.front() == '\'' || p.front() == '`') && p.back() == p.front())
        p = p.substr(1, p.size() - 2);
    std::replace(p.begin(), p.end(), '/', '\\');
    if (p.empty()) { r.error = "Caminho vazio"; return r; }
    if (p.rfind("\\\\", 0) == 0) { r.error = "Caminhos de rede ou de dispositivo não são permitidos"; return r; }

    // Atalhos comuns
    if (p[0] == '~') p = text::Narrow(_wgetenv(L"USERPROFILE") ? _wgetenv(L"USERPROFILE") : L"") + p.substr(1);
    for (const char* d : {"Desktop\\", "Área de Trabalho\\", "Area de Trabalho\\"})
        if (StartsWithI(p, d)) p = DesktopDir() + "\\" + p.substr(strlen(d));
    for (const char* d : {"Documentos\\", "Documents\\"})
        if (StartsWithI(p, d)) p = DocumentsDir() + "\\" + p.substr(strlen(d));

    wchar_t expanded[2048];
    std::wstring wp = text::Widen(p);
    if (ExpandEnvironmentStringsW(wp.c_str(), expanded, 2048)) wp = expanded;

    // Relativo -> Documentos\KB
    bool absolute = wp.size() > 2 && wp[1] == L':' && wp[2] == L'\\';
    if (!absolute) wp = text::Widen(DocumentsDir()) + L"\\KB\\" + wp;

    wchar_t full[2048];
    DWORD n = GetFullPathNameW(wp.c_str(), 2048, full, nullptr);
    if (n == 0 || n >= 2048) { r.error = "Caminho inválido"; return r; }
    std::wstring f(full, n);
    if (f.back() == L'\\') { r.error = "O caminho aponta para uma pasta, não para um arquivo"; return r; }
    std::wstring name = f.substr(f.find_last_of(L'\\') + 1);
    if (name.find_first_of(L"<>:\"|?*") != std::wstring::npos) { r.error = "Nome de arquivo inválido"; return r; }

    r.path = text::Narrow(f);
    DWORD attr = GetFileAttributesW(f.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) { r.error = "Já existe uma pasta com esse nome"; return r; }
    r.exists = attr != INVALID_FILE_ATTRIBUTES;
    std::wstring parent = f.substr(0, f.find_last_of(L'\\'));
    DWORD pa = GetFileAttributesW(parent.c_str());
    r.parentMissing = pa == INVALID_FILE_ATTRIBUTES;

    std::string ext = text::Fold(r.path.substr(std::min(r.path.size(), r.path.find_last_of('.'))));
    for (const char* e : {".exe", ".bat", ".cmd", ".ps1", ".vbs", ".js", ".jse", ".wsf", ".msi", ".scr", ".com", ".dll", ".lnk", ".reg", ".hta", ".psm1"})
        if (ext == e) r.risky = true;
    std::string lower = text::Fold(r.path);
    if (lower.find("\\windows\\") != std::string::npos || lower.find("\\program files") != std::string::npos ||
        lower.find("\\startup\\") != std::string::npos || lower.find("\\inicializar\\") != std::string::npos)
        r.risky = true;
    r.ok = true;
    return r;
}

bool WriteExact(const std::string& path, const std::string& content, std::string* err) {
    std::wstring w = text::Widen(path);
    std::wstring parent = w.substr(0, w.find_last_of(L'\\'));
    if (GetFileAttributesW(parent.c_str()) == INVALID_FILE_ATTRIBUTES) {
        int rc = SHCreateDirectoryExW(nullptr, parent.c_str(), nullptr);
        if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS) { if (err) *err = "Não foi possível criar a pasta"; return false; }
    }
    HANDLE h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = GetLastError() == ERROR_ACCESS_DENIED ? "Acesso negado (talvez precise de administrador)" : "Não foi possível criar o arquivo";
        return false;
    }
    std::string data = content;
    if (!data.empty() && data.back() != '\n') data += "\r\n";
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    if (!ok || wr != data.size()) { if (err) *err = "Falha ao gravar o conteúdo"; return false; }
    return true;
}

void RevealInExplorer(const std::string& path) {
    std::wstring args = L"/select,\"" + text::Widen(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

} // namespace files
