#include "net/mobile_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <map>
#include <algorithm>

using nlohmann::json;

namespace {
std::string LoadResource(const wchar_t* name) {
    HRSRC r = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!r) return {};
    HGLOBAL g = LoadResource(nullptr, r);
    DWORD sz = SizeofResource(nullptr, r);
    const char* p = g ? (const char*)LockResource(g) : nullptr;
    return p ? std::string(p, sz) : std::string();
}

bool SendAll(SOCKET s, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        int n = send(s, d.data() + off, (int)std::min<size_t>(d.size() - off, 64 * 1024), 0);
        if (n <= 0) return false;
        off += n;
    }
    return true;
}

void Reply(SOCKET s, int code, const char* type, const std::string& body) {
    const char* msg = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 413 ? "Too Large" : "Bad Request";
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + msg + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n";
    SendAll(s, h + body);
}

std::string QueryParam(const std::string& target, const std::string& key) {
    size_t q = target.find('?');
    if (q == std::string::npos) return {};
    std::string qs = target.substr(q + 1);
    size_t pos = 0;
    while (pos <= qs.size()) {
        size_t amp = qs.find('&', pos);
        std::string kv = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return kv.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

bool ConstantTimeEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
} // namespace

MobileServer::~MobileServer() { Stop(); }

bool MobileServer::Start(int port, const std::string& token, MobileHooks hooks, std::string* err) {
    Stop();
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { if (err) *err = "Falha ao criar socket"; return false; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 16) != 0) {
        closesocket(s);
        if (err) *err = "A porta " + std::to_string(port) + " já está em uso";
        return false;
    }
    listen_ = (uintptr_t)s;
    port_ = port;
    token_ = token;
    hooks_ = std::move(hooks);
    running_ = true;
    th_ = std::thread([this] { Run(); });
    return true;
}

void MobileServer::Stop() {
    if (!running_.exchange(false)) return;
    closesocket((SOCKET)listen_);
    if (th_.joinable()) th_.join();
    listen_ = (uintptr_t)~0ull;
}

void MobileServer::Run() {
    while (running_) {
        SOCKET c = accept((SOCKET)listen_, nullptr, nullptr);
        if (c == INVALID_SOCKET) { if (!running_) break; Sleep(50); continue; }
        DWORD to = 15000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        Handle((uintptr_t)c);
        shutdown(c, SD_BOTH);
        closesocket(c);
    }
}

void MobileServer::Handle(uintptr_t sockp) {
    SOCKET s = (SOCKET)sockp;
    std::string req;
    char buf[16384];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return;
        req.append(buf, n);
        headerEnd = req.find("\r\n\r\n");
        if (req.size() > 64 * 1024 && headerEnd == std::string::npos) return;
    }
    std::string head = req.substr(0, headerEnd);
    std::string body = req.substr(headerEnd + 4);
    size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return;
    std::string method = head.substr(0, sp1);
    std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = target.substr(0, target.find('?'));
    size_t contentLength = 0;
    {
        std::string lower = head;
        for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
        size_t cl = lower.find("\r\ncontent-length:");
        if (cl != std::string::npos) contentLength = strtoull(lower.c_str() + cl + 17, nullptr, 10);
    }
    const size_t maxBody = 16 * 1024 * 1024;
    if (contentLength > maxBody) { Reply(s, 413, "text/plain", "grande demais"); return; }
    while (body.size() < contentLength) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return;
        body.append(buf, n);
    }

    // Arquivos públicos (sem dados): ícone e manifesto
    if (path == "/icon.svg") {
        Reply(s, 200, "image/svg+xml",
              "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'><defs><radialGradient id='g' cx='40%' cy='35%' r='70%'>"
              "<stop offset='0' stop-color='#bff4ff'/><stop offset='.45' stop-color='#4cc9f0'/><stop offset='1' stop-color='#7b4dff'/></radialGradient></defs>"
              "<rect width='512' height='512' rx='112' fill='#0b0f1a'/><circle cx='256' cy='256' r='170' fill='url(#g)'/>"
              "<text x='256' y='300' font-family='Segoe UI,Arial' font-weight='700' font-size='130' text-anchor='middle' fill='#0b0f1a'>KB</text></svg>");
        return;
    }
    std::string t = QueryParam(target, "t");
    if (path == "/manifest.webmanifest") {
        json m = {{"name", "KB"}, {"short_name", "KB"}, {"start_url", "/?t=" + t}, {"display", "standalone"},
                  {"background_color", "#0b0f1a"}, {"theme_color", "#0b0f1a"},
                  {"icons", json::array({{{"src", "/icon.svg"}, {"sizes", "any"}, {"type", "image/svg+xml"}}})}};
        Reply(s, 200, "application/manifest+json", m.dump());
        return;
    }
    if (!ConstantTimeEq(t, token_)) {
        Reply(s, 403, "text/html; charset=utf-8",
              "<meta name=viewport content='width=device-width'><body style='background:#0b0f1a;color:#cfe;font-family:sans-serif;padding:24px'>"
              "<h2>KB</h2><p>Abra o KB no PC &rarr; <b>Celular</b> e escaneie o QR code para conectar.</p></body>");
        return;
    }
    if (path == "/" && method == "GET") {
        static const std::string page = LoadResource(L"MOBILE_HTML");
        Reply(s, 200, "text/html; charset=utf-8", page.empty() ? "<h1>KB</h1>" : page);
        return;
    }
    if (path == "/api/state" && method == "GET") {
        int since = atoi(QueryParam(target, "since").c_str());
        Reply(s, 200, "application/json", hooks_.stateJson ? hooks_.stateJson(since) : "{}");
        return;
    }
    if (path == "/api/send" && method == "POST") {
        json j = json::parse(body, nullptr, false);
        if (j.is_discarded()) { Reply(s, 400, "text/plain", "json"); return; }
        std::string text = j.value("text", "");
        std::string image = j.value("image", "");
        if (text.size() > 8000) text.resize(8000);
        if (hooks_.send && (!text.empty() || !image.empty())) hooks_.send(text, image);
        Reply(s, 200, "application/json", "{\"ok\":true}");
        return;
    }
    if (path == "/api/listen" && method == "POST") { if (hooks_.listen) hooks_.listen(); Reply(s, 200, "application/json", "{\"ok\":true}"); return; }
    if (path == "/api/stop" && method == "POST") { if (hooks_.stop) hooks_.stop(); Reply(s, 200, "application/json", "{\"ok\":true}"); return; }
    Reply(s, 404, "text/plain", "não encontrado");
}

std::vector<std::string> MobileServer::LanAddresses() {
    std::vector<std::string> good, other;
    ULONG size = 32 * 1024;
    std::vector<unsigned char> buf(size);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
                             nullptr, aa, &size) != NO_ERROR)
        return good;
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            char ip[64];
            auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            std::string s = ip;
            if (s.rfind("169.254.", 0) == 0) continue;
            bool priv = s.rfind("192.168.", 0) == 0 || s.rfind("10.", 0) == 0 ||
                        (s.rfind("172.", 0) == 0 && atoi(s.c_str() + 4) >= 16 && atoi(s.c_str() + 4) <= 31);
            // adaptadores com gateway (Wi-Fi/Ethernet de verdade) primeiro; VPNs/virtuais depois
            if (priv && a->FirstGatewayAddress) good.push_back(s);
            else other.push_back(s);
        }
    }
    good.insert(good.end(), other.begin(), other.end());
    return good;
}
