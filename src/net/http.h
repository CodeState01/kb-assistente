#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace http {

struct Options {
    int connectTimeoutMs = 3000;
    int receiveTimeoutMs = 300000;     // modelos grandes podem demorar para carregar
    std::vector<std::string> headers;  // "Nome: valor"
    std::atomic<bool>* cancel = nullptr;
    std::function<void(int status)> onStatus; // chamado após receber os cabeçalhos
};

// Mensagem amigável para códigos de erro do WinHTTP (ex.: 12029 = não conectou).
std::string FriendlyError(unsigned long code);

struct Result {
    int status = 0;                    // 0 = falha de conexão
    std::string body;                  // preenchido quando não há onData
    std::string error;
    uint64_t contentLength = 0;
    bool ok() const { return status >= 200 && status < 300; }
};

// onData: chamado a cada bloco recebido; retornar false aborta.
using DataFn = std::function<bool(const char* data, size_t len, uint64_t total)>;

Result Request(const std::string& method, const std::string& url, const std::string& body,
               const Options& opt = {}, const DataFn& onData = nullptr);

inline Result Get(const std::string& url, int timeoutMs = 3000) {
    Options o;
    o.connectTimeoutMs = timeoutMs;
    o.receiveTimeoutMs = timeoutMs;
    return Request("GET", url, "", o);
}

// Baixa arquivo para disco com progresso (bytes, total). Grava em .part e renomeia ao final.
Result Download(const std::string& url, const std::string& dest,
                const std::function<void(uint64_t, uint64_t)>& progress, std::atomic<bool>* cancel);

// Divide um fluxo em linhas (NDJSON / SSE).
class LineSplitter {
public:
    template <class F> void Feed(const char* d, size_t n, F&& onLine) {
        buf_.append(d, n);
        size_t pos;
        while ((pos = buf_.find('\n')) != std::string::npos) {
            std::string line = buf_.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf_.erase(0, pos + 1);
            onLine(line);
        }
    }
    std::string Rest() const { return buf_; }
private:
    std::string buf_;
};

} // namespace http
