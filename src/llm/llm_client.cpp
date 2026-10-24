#include "llm/llm_client.h"
#include "net/http.h"
#include "core/text.h"
#include <nlohmann/json.hpp>
#include <thread>

using nlohmann::json;

namespace llm {

static std::string StripSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string BaseUrl(const Config& c) {
    switch (c.backend) {
    case LlmBackend::Ollama: return StripSlash(c.ollamaUrl);
    case LlmBackend::OpenAICompat: return StripSlash(c.openaiUrl);
    case LlmBackend::LocalGguf: return "http://127.0.0.1:" + std::to_string(kLocalGgufPort);
    }
    return {};
}

static std::vector<std::string> AuthHeaders(const Config& c) {
    std::vector<std::string> h;
    if (c.backend == LlmBackend::OpenAICompat && !c.openaiKey.empty()) h.push_back("Authorization: Bearer " + c.openaiKey);
    return h;
}

static std::vector<ModelInfo> ParseOllamaTags(const std::string& body) {
    std::vector<ModelInfo> out;
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("models")) return out;
    for (auto& m : j["models"]) {
        ModelInfo mi;
        mi.name = m.value("name", "");
        mi.size = m.value("size", (uint64_t)0);
        if (m.contains("details")) {
            auto& d = m["details"];
            mi.details = d.value("parameter_size", "") + " " + d.value("quantization_level", "");
        }
        if (!mi.name.empty()) out.push_back(mi);
    }
    return out;
}

static std::vector<ModelInfo> ParseOpenAIModels(const std::string& body) {
    std::vector<ModelInfo> out;
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("data")) return out;
    for (auto& m : j["data"]) {
        ModelInfo mi;
        mi.name = m.value("id", "");
        if (!mi.name.empty()) out.push_back(mi);
    }
    return out;
}

std::vector<ModelInfo> ListModels(const Config& c, std::string* err) {
    std::string base = BaseUrl(c);
    http::Options o;
    o.connectTimeoutMs = 2000;
    o.receiveTimeoutMs = 5000;
    o.headers = AuthHeaders(c);
    if (c.backend == LlmBackend::Ollama) {
        auto r = http::Request("GET", base + "/api/tags", "", o);
        if (!r.ok()) { if (err) *err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error; return {}; }
        return ParseOllamaTags(r.body);
    }
    auto r = http::Request("GET", base + "/v1/models", "", o);
    if (!r.ok()) { if (err) *err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error; return {}; }
    return ParseOpenAIModels(r.body);
}

void FillCapabilities(const Config& c, std::vector<ModelInfo>& models) {
    if (c.backend != LlmBackend::Ollama) {
        for (auto& m : models) { m.vision = true; m.capsKnown = false; } // desconhecido: tenta enviar
        return;
    }
    for (auto& m : models) {
        http::Options o;
        o.connectTimeoutMs = 2000;
        o.receiveTimeoutMs = 8000;
        auto r = http::Request("POST", BaseUrl(c) + "/api/show", json({{"model", m.name}}).dump(), o);
        if (!r.ok()) continue;
        json j = json::parse(r.body, nullptr, false);
        if (j.is_discarded()) continue;
        m.capsKnown = true;
        if (j.contains("capabilities") && j["capabilities"].is_array())
            for (auto& cap : j["capabilities"]) if (cap.is_string() && cap.get<std::string>() == "vision") m.vision = true;
    }
}

std::vector<ServerInfo> DetectServers() {
    struct Probe { const char* name; const char* url; LlmBackend b; };
    static const Probe probes[] = {
        {"Ollama", "http://127.0.0.1:11434", LlmBackend::Ollama},
        {"LM Studio", "http://127.0.0.1:1234", LlmBackend::OpenAICompat},
        {"llama.cpp server", "http://127.0.0.1:8080", LlmBackend::OpenAICompat},
        {"Jan", "http://127.0.0.1:1337", LlmBackend::OpenAICompat},
        {"KoboldCpp", "http://127.0.0.1:5001", LlmBackend::OpenAICompat},
        {"text-generation-webui", "http://127.0.0.1:5000", LlmBackend::OpenAICompat},
        {"vLLM / LocalAI", "http://127.0.0.1:8000", LlmBackend::OpenAICompat},
    };
    std::vector<ServerInfo> found(std::size(probes));
    std::vector<std::thread> th;
    for (size_t i = 0; i < std::size(probes); ++i) {
        th.emplace_back([&, i] {
            const Probe& p = probes[i];
            http::Options o;
            o.connectTimeoutMs = 600;
            o.receiveTimeoutMs = 2500;
            auto r = http::Request("GET", std::string(p.url) + (p.b == LlmBackend::Ollama ? "/api/tags" : "/v1/models"), "", o);
            if (!r.ok()) return;
            ServerInfo s{p.name, p.url, p.b, {}};
            s.models = p.b == LlmBackend::Ollama ? ParseOllamaTags(r.body) : ParseOpenAIModels(r.body);
            found[i] = std::move(s);
        });
    }
    for (auto& t : th) t.join();
    std::vector<ServerInfo> out;
    for (auto& s : found) if (!s.name.empty()) out.push_back(std::move(s));
    return out;
}

bool Chat(const Config& c, const std::string& model, const std::vector<ChatMsg>& msgs, const TokenFn& onToken,
          std::string* err, std::atomic<bool>* cancel) {
    json jm = json::array();
    for (auto& m : msgs) {
        if (m.imagesB64.empty()) { jm.push_back({{"role", m.role}, {"content", m.content}}); continue; }
        if (c.backend == LlmBackend::Ollama) {
            jm.push_back({{"role", m.role}, {"content", m.content}, {"images", m.imagesB64}});
        } else {
            json parts = json::array();
            parts.push_back({{"type", "text"}, {"text", m.content}});
            for (auto& b : m.imagesB64)
                parts.push_back({{"type", "image_url"}, {"image_url", {{"url", "data:image/jpeg;base64," + b}}}});
            jm.push_back({{"role", m.role}, {"content", parts}});
        }
    }

    http::Options o;
    o.connectTimeoutMs = 3000;
    o.receiveTimeoutMs = 600000;
    o.cancel = cancel;
    o.headers = AuthHeaders(c);
    http::LineSplitter lines;
    bool stop = false;
    std::string base = BaseUrl(c);
    http::Result r;

    if (c.backend == LlmBackend::Ollama) {
        json body = {{"model", model},
                     {"messages", jm},
                     {"stream", true},
                     {"keep_alive", "30m"},
                     {"options", {{"temperature", c.temperature}, {"num_ctx", c.contextSize}}}};
        r = http::Request("POST", base + "/api/chat", body.dump(), o, [&](const char* d, size_t n, uint64_t) {
            lines.Feed(d, n, [&](const std::string& line) {
                if (stop || line.empty()) return;
                json j = json::parse(line, nullptr, false);
                if (j.is_discarded()) return;
                if (j.contains("error")) { if (err) *err = j["error"].get<std::string>(); stop = true; return; }
                if (j.contains("message") && j["message"].contains("content")) {
                    std::string piece = j["message"]["content"].get<std::string>();
                    if (!piece.empty() && !onToken(piece)) stop = true;
                }
            });
            return !stop;
        });
    } else {
        json body = {{"model", model.empty() ? "local" : model},
                     {"messages", jm},
                     {"stream", true},
                     {"temperature", c.temperature}};
        r = http::Request("POST", base + "/v1/chat/completions", body.dump(), o, [&](const char* d, size_t n, uint64_t) {
            lines.Feed(d, n, [&](const std::string& line) {
                if (stop || !text::StartsWith(line, "data:")) return;
                std::string payload = text::Trim(line.substr(5));
                if (payload == "[DONE]") return;
                json j = json::parse(payload, nullptr, false);
                if (j.is_discarded()) return;
                if (j.contains("error")) { if (err) *err = j["error"].dump(); stop = true; return; }
                if (j.contains("choices") && !j["choices"].empty()) {
                    auto& ch = j["choices"][0];
                    if (ch.contains("delta") && ch["delta"].contains("content") && ch["delta"]["content"].is_string()) {
                        std::string piece = ch["delta"]["content"].get<std::string>();
                        if (!piece.empty() && !onToken(piece)) stop = true;
                    }
                }
            });
            return !stop;
        });
    }

    if (!r.ok()) {
        if (err && err->empty()) {
            std::string detail = r.body;
            json j = json::parse(detail, nullptr, false);
            if (!j.is_discarded() && j.contains("error")) detail = j["error"].is_string() ? j["error"].get<std::string>() : j["error"].dump();
            *err = r.status == 0 ? "Não consegui conectar ao servidor de IA em " + base + ". " + r.error
                                 : "Servidor respondeu HTTP " + std::to_string(r.status) + ": " + detail;
        }
        return false;
    }
    if (err && err->empty() && !r.error.empty() && r.error != "cancelado") *err = r.error;
    return err ? err->empty() : true;
}

bool OllamaPull(const std::string& baseUrl, const std::string& model, const PullFn& onProgress, std::string* err,
                std::atomic<bool>* cancel) {
    http::Options o;
    o.receiveTimeoutMs = 120000;
    o.cancel = cancel;
    http::LineSplitter lines;
    bool success = false;
    json body = {{"model", model}, {"stream", true}};
    auto r = http::Request("POST", StripSlash(baseUrl) + "/api/pull", body.dump(), o, [&](const char* d, size_t n, uint64_t) {
        lines.Feed(d, n, [&](const std::string& line) {
            json j = json::parse(line, nullptr, false);
            if (j.is_discarded()) return;
            if (j.contains("error")) { if (err) *err = j["error"].get<std::string>(); return; }
            std::string st = j.value("status", "");
            if (st == "success") success = true;
            onProgress(st, j.value("completed", (uint64_t)0), j.value("total", (uint64_t)0));
        });
        return true;
    });
    if (!r.ok() && err && err->empty()) *err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
    return success;
}

std::string ThinkFilter::Feed(const std::string& in) {
    std::string out;
    pending_ += in;
    for (;;) {
        const char* tag = inThink_ ? "</think>" : "<think>";
        size_t tagLen = inThink_ ? 8 : 7;
        size_t pos = pending_.find(tag);
        if (pos != std::string::npos) {
            if (!inThink_) out += pending_.substr(0, pos);
            pending_.erase(0, pos + tagLen);
            inThink_ = !inThink_;
            continue;
        }
        // Mantém possível início de tag parcial no fim do buffer.
        size_t keep = 0;
        for (size_t k = 1; k < tagLen && k <= pending_.size(); ++k)
            if (pending_.compare(pending_.size() - k, k, tag, k) == 0) keep = k;
        if (!inThink_) out += pending_.substr(0, pending_.size() - keep);
        pending_.erase(0, pending_.size() - keep);
        break;
    }
    return out;
}

} // namespace llm
