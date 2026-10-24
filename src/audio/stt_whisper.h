#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct whisper_context;

// Reconhecimento de fala local com whisper.cpp.
class SpeechToText {
public:
    ~SpeechToText();
    bool Load(const std::string& modelPath, std::string* err);
    void Unload();
    bool Loaded() const { return ctx_ != nullptr; }
    std::string ModelPath() const { return path_; }

    // Bloqueante. Retorna texto UTF-8 (vazio se nada útil foi reconhecido).
    std::string Transcribe(const std::vector<float>& pcm16k, const std::string& lang, int threads,
                           const std::string& hint);

private:
    whisper_context* ctx_ = nullptr;
    std::string path_;
    std::mutex mtx_;
};

namespace stt {
struct WhisperModelOption { const char* id; const char* label; const char* size; unsigned long long bytes; };
// Modelos multilíngues recomendados para português.
const std::vector<WhisperModelOption>& Options();
std::string DownloadUrl(const char* id);
bool IsHallucination(const std::string& t);
// Tamanho esperado de um ggml-*.bin oficial (0 = desconhecido).
unsigned long long ExpectedSize(const std::string& path);
}
