#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct LocalFile {
    std::string path;
    std::string name;
    uint64_t size = 0;
};

namespace scan {
// Procura arquivos .gguf em pastas comuns (LM Studio, Downloads, pasta do KB...).
std::vector<LocalFile> FindGgufModels();
// Procura modelos do whisper (ggml-*.bin).
std::vector<LocalFile> FindWhisperModels();
// Procura llama-server.exe no PATH e em pastas comuns.
std::string FindLlamaServer();
}
