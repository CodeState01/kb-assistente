#include "net/http.h"
#include "core/text.h"
#include <windows.h>
#include <winhttp.h>
#include <cstdio>

namespace http {

std::string FriendlyError(unsigned long code) {
    char buf[64];
    snprintf(buf, sizeof(buf), " (erro %lu)", code);
    switch (code) {
    case ERROR_WINHTTP_CANNOT_CONNECT: return std::string("não conseguiu conectar — o servidor não está rodando") + buf;
    case ERROR_WINHTTP_TIMEOUT: return std::string("tempo esgotado") + buf;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return std::string("sem internet ou endereço inválido") + buf;
    case ERROR_WINHTTP_CONNECTION_ERROR: return std::string("a conexão caiu") + buf;
    case ERROR_WINHTTP_SECURE_FAILURE: return std::string("falha de segurança (certificado)") + buf;
    default: return std::string("falha de rede") + buf;
    }
}

namespace {
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
};

std::string LastError(const char* where) {
    DWORD code = GetLastError();
    return std::string(where) + ": " + FriendlyError(code);
}
} // namespace

Result Request(const std::string& method, const std::string& url, const std::string& body, const Options& opt,
               const DataFn& onData) {
    Result r;
    std::wstring wurl = text::Widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)std::size(path);
    wchar_t extra[4096] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = (DWORD)std::size(extra);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "URL inválida: " + url;
        return r;
    }
    std::wstring whost(host);
    bool local = whost == L"localhost" || whost == L"127.0.0.1" || whost == L"::1";
    Handle session(WinHttpOpen(L"KB/1.0", local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        // Windows antigo sem AUTOMATIC_PROXY
        session.h = WinHttpOpen(L"KB/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) { r.error = LastError("WinHttpOpen"); return r; }
    WinHttpSetTimeouts(session, opt.connectTimeoutMs, opt.connectTimeoutMs, opt.receiveTimeoutMs, opt.receiveTimeoutMs);

    Handle conn(WinHttpConnect(session, host, uc.nPort, 0));
    if (!conn) { r.error = LastError("WinHttpConnect"); return r; }

    std::wstring fullPath = std::wstring(path) + extra;
    if (fullPath.empty()) fullPath = L"/";
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req(WinHttpOpenRequest(conn, text::Widen(method).c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) { r.error = LastError("WinHttpOpenRequest"); return r; }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    std::wstring hdrs;
    if (!body.empty()) hdrs += L"Content-Type: application/json\r\n";
    for (auto& h : opt.headers) hdrs += text::Widen(h) + L"\r\n";

    BOOL sent = WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
                                   hdrs.empty() ? 0 : (DWORD)-1L, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                   (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        r.error = LastError("Conexão");
        return r;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;
    wchar_t lenBuf[64] = {};
    DWORD lenSz = sizeof(lenBuf);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSz,
                            WINHTTP_NO_HEADER_INDEX))
        r.contentLength = _wcstoui64(lenBuf, nullptr, 10);
    if (opt.onStatus) opt.onStatus(r.status);

    std::vector<char> buf(64 * 1024);
    for (;;) {
        if (opt.cancel && opt.cancel->load()) { r.error = "cancelado"; break; }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { r.error = LastError("Leitura"); break; }
        if (avail == 0) break;
        if (avail > buf.size()) buf.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &read)) { r.error = LastError("Leitura"); break; }
        if (read == 0) break;
        if (onData && r.ok()) {
            if (!onData(buf.data(), read, r.contentLength)) { r.error = "cancelado"; break; }
        } else {
            r.body.append(buf.data(), read);
        }
    }
    return r;
}

Result Download(const std::string& url, const std::string& dest, const std::function<void(uint64_t, uint64_t)>& progress,
                std::atomic<bool>* cancel) {
    // Baixa em "<dest>.part", retomando de onde parou (HTTP Range) se a conexão cair.
    std::wstring part = text::Widen(dest + ".part");
    Result last;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (cancel && cancel->load()) { last.error = "cancelado"; break; }
        uint64_t have = 0;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(part.c_str(), GetFileExInfoStandard, &fa))
            have = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        Options o;
        o.connectTimeoutMs = 15000;
        o.receiveTimeoutMs = 60000;
        o.cancel = cancel;
        if (have > 0) o.headers.push_back("Range: bytes=" + std::to_string(have) + "-");
        FILE* f = nullptr;
        uint64_t got = have, total = 0;
        o.onStatus = [&](int status) {
            if (status == 206) f = _wfopen(part.c_str(), L"ab");
            else if (status == 200) { f = _wfopen(part.c_str(), L"wb"); got = 0; }
        };
        Result r = Request("GET", url, "", o, [&](const char* d, size_t n, uint64_t len) {
            if (!f) return false;
            if (total == 0) total = (got == 0 ? 0 : got) + len;
            if (fwrite(d, 1, n, f) != n) return false;
            got += n;
            if (progress) progress(got, total);
            return true;
        });
        if (f) fclose(f);
        last = r;
        if (r.status == 416) { // já completo
            last.error.clear();
            last.status = 200;
            break;
        }
        bool complete = r.ok() && r.error.empty() && (total == 0 || got >= total);
        if (complete) break;
        if (r.ok() && r.error.empty()) last.error = "download incompleto";
        if (r.error == "cancelado") break;
        if (r.status >= 400 && r.status != 416) break; // erro definitivo do servidor
        Sleep(1500 * (attempt + 1));
    }
    if (last.ok() && last.error.empty()) {
        if (!MoveFileExW(part.c_str(), text::Widen(dest).c_str(), MOVEFILE_REPLACE_EXISTING)) last.error = "Falha ao renomear";
    } else if (last.error.empty()) {
        last.error = "HTTP " + std::to_string(last.status);
    }
    // em caso de falha o .part fica guardado para retomar depois
    return last;
}

} // namespace http
