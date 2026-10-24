#pragma once
#include <string>

// Tipos de backend de IA suportados.
enum class LlmBackend : int {
    Ollama = 0,        // http://localhost:11434 (api nativa)
    OpenAICompat = 1,  // LM Studio, llama.cpp server, Jan, KoboldCpp, vLLM...
    LocalGguf = 2,     // KB inicia o llama-server com um .gguf escolhido
};

struct Config {
    // ---- IA
    LlmBackend backend = LlmBackend::Ollama;
    std::string ollamaUrl = "http://127.0.0.1:11434";
    std::string openaiUrl = "http://127.0.0.1:1234";
    std::string openaiKey;                 // opcional
    std::string model;                     // vazio = usa o primeiro instalado
    std::string ggufPath;                  // modo LocalGguf
    std::string llamaServerPath;           // caminho do llama-server.exe
    int ggufGpuLayers = 99;
    int contextSize = 8192;
    float temperature = 0.7f;
    int historyTurns = 12;
    std::string systemPrompt;              // vazio = prompt padrão em pt-BR

    // ---- Voz
    std::string whisperModel;              // caminho do ggml-*.bin
    std::string sttLanguage = "pt";
    int sttThreads = 0;                    // 0 = automático
    std::string micDevice;                 // vazio = padrão
    float micSensitivity = 1.0f;           // multiplica o limiar do VAD
    bool wakeWordEnabled = false;
    std::string wakeWord = "kb";
    bool speakReplies = true;
    std::string ttsVoice;                  // descrição da voz SAPI (vazio = 1ª pt-BR)
    int ttsRate = 1;                       // -10..10
    int ttsVolume = 100;

    // ---- Sistema
    bool startWithWindows = false;
    bool startMinimized = true;
    bool minimizeToTray = true;
    bool showMiniOrb = true;
    bool greetOnStart = true;
    int hotkeyMods = 0x0002 | 0x0004;      // MOD_CONTROL | MOD_SHIFT
    int hotkeyVk = 0x20;                   // VK_SPACE
    float uiScale = 1.0f;
    bool reduceMotion = false;
    float miniOrbX = -1, miniOrbY = -1;

    // ---- Celular / visão
    bool mobileEnabled = false;            // servidor na rede local para o app do celular
    int mobilePort = 7777;
    std::string mobileToken;               // gerado na primeira vez
    int screenIntervalMs = 1000;           // intervalo da transmissão de tela
    bool autoVisionModel = true;           // troca para um modelo com visão quando há imagem

    bool firstRun = true;
};

namespace config {
std::string AppDataDir();            // %APPDATA%\KB (UTF-8)
std::string ModelsDir();             // %APPDATA%\KB\models
std::string ConfigPath();
bool Load(Config& out);
bool Save(const Config& c);
std::string DefaultSystemPrompt();
}
