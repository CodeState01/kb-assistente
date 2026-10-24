#pragma once
#include "core/config.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

struct ChatMsg {
    std::string role;     // system | user | assistant
    std::string content;
    std::vector<std::string> imagesB64; // JPEG em base64 (modelos com visão)
    std::string toolCallsJson;          // assistente: chamadas de ferramenta feitas (JSON)
    std::string toolName;               // role "tool": qual ferramenta respondeu
};

struct ToolCall {
    std::string name;
    std::string argsJson;               // argumentos em JSON
};

struct ModelInfo {
    std::string name;
    uint64_t size = 0;
    std::string details;  // família / quantização
    bool vision = false;  // enxerga imagens
    bool tools = false;   // sabe chamar ferramentas (pesquisa na web, skills...)
    bool capsKnown = false;
};

struct ServerInfo {
    std::string name;     // "Ollama", "LM Studio"...
    std::string url;
    LlmBackend backend;
    std::vector<ModelInfo> models;
};

namespace llm {

constexpr int kLocalGgufPort = 8089;

// Modelo recomendado (~13 GB) para quem ainda não tem nenhum.
constexpr const char* kRecommendedModel = "gemma3:12b-it-q8_0";   // ~13 GB, texto + visão
constexpr const char* kRecommendedLight = "gemma3:12b";          // ~8 GB, texto + visão (cabe em GPUs de 8 GB)

std::string BaseUrl(const Config& c);
std::vector<ModelInfo> ListModels(const Config& c, std::string* err);
// Ollama: consulta /api/show para saber se o modelo tem visão.
void FillCapabilities(const Config& c, std::vector<ModelInfo>& models);
// Procura servidores locais conhecidos (Ollama, LM Studio, llama.cpp, Jan, KoboldCpp...).
std::vector<ServerInfo> DetectServers();

using TokenFn = std::function<bool(const std::string& piece)>;
bool Chat(const Config& c, const std::string& model, const std::vector<ChatMsg>& msgs, const TokenFn& onToken,
          std::string* err, std::atomic<bool>* cancel, const TokenFn& onThink = nullptr,
          const std::string& toolsJson = "", std::vector<ToolCall>* toolCalls = nullptr);

using PullFn = std::function<void(const std::string& status, uint64_t completed, uint64_t total)>;
bool OllamaPull(const std::string& baseUrl, const std::string& model, const PullFn& onProgress, std::string* err,
                std::atomic<bool>* cancel);

// Filtra blocos <think>...</think> de modelos de raciocínio em fluxo.
class ThinkFilter {
public:
    // Retorna o texto visível; o conteúdo de <think>...</think> vai para 'think' (se informado).
    std::string Feed(const std::string& in, std::string* think = nullptr);
    bool Thinking() const { return inThink_; }
private:
    bool inThink_ = false;
    std::string pending_;
};

} // namespace llm
