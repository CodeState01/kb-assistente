#include "core/config.h"
#include "core/text.h"
#include <windows.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace config {

std::string AppDataDir() {
    static std::string cached;
    if (!cached.empty()) return cached;
    PWSTR path = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        dir = std::wstring(path) + L"\\KB";
        // migra os dados da versão antiga ("Aurora")
        std::wstring old = std::wstring(path) + L"\\Aurora";
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
            MoveFileW(old.c_str(), dir.c_str());
        CoTaskMemFree(path);
    } else {
        dir = L".\\KB";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    cached = text::Narrow(dir);
    return cached;
}

std::string ModelsDir() {
    std::string d = AppDataDir() + "\\models";
    CreateDirectoryW(text::Widen(d).c_str(), nullptr);
    return d;
}

std::string ConfigPath() { return AppDataDir() + "\\config.json"; }

std::string DefaultSystemPrompt() {
    return
        "Você é KB, um assistente de voz que vive no computador do usuário (Windows). "
        "Responda SEMPRE em português do Brasil, de forma natural, calorosa e objetiva, "
        "pois suas respostas são lidas em voz alta. Prefira frases curtas. "
        "Não use emojis.\n\n"
        "Quando for útil MOSTRAR algo na tela (código, passos, tabelas, dados, resumos), use blocos especiais "
        "que a interface transforma em cartões visuais:\n"
        "```card\nTítulo do cartão\nConteúdo do cartão em uma ou mais linhas\n```\n"
        "```passos\nPrimeiro passo\nSegundo passo\n```\n"
        "```aviso\nTexto de alerta importante\n```\n"
        "```dica\nUma dica útil\n```\n"
        "```tabela\nColuna A | Coluna B\nvalor 1 | valor 2\n```\n"
        "e para código use ```linguagem ... ``` normalmente. "
        "Fora dos blocos, fale como numa conversa; não leia código em voz alta, apenas diga que mostrou na tela.";
}

#define GET(field) if (j.contains(#field)) { try { j.at(#field).get_to(c.field); } catch (...) {} }

bool Load(Config& c) {
    std::ifstream f(text::Widen(ConfigPath()));
    if (!f) return false;
    json j;
    try { f >> j; } catch (...) { return false; }
    int backend = (int)c.backend;
    if (j.contains("backend")) { try { backend = j["backend"].get<int>(); } catch (...) {} }
    c.backend = (LlmBackend)backend;
    GET(ollamaUrl) GET(openaiUrl) GET(openaiKey) GET(model) GET(ggufPath) GET(llamaServerPath)
    GET(ggufGpuLayers) GET(contextSize) GET(temperature) GET(historyTurns) GET(systemPrompt)
    GET(whisperModel) GET(sttLanguage) GET(sttThreads) GET(micDevice) GET(micSensitivity)
    GET(wakeWordEnabled) GET(wakeWord) GET(speakReplies) GET(ttsVoice) GET(ttsRate) GET(ttsVolume)
    GET(startWithWindows) GET(startMinimized) GET(minimizeToTray) GET(showMiniOrb) GET(greetOnStart)
    GET(hotkeyMods) GET(hotkeyVk) GET(uiScale) GET(reduceMotion) GET(miniOrbX) GET(miniOrbY) GET(firstRun)
    GET(mobileEnabled) GET(mobileMode) GET(mobilePort) GET(mobileToken) GET(screenIntervalMs) GET(autoVisionModel)
    GET(showPerf) GET(bgImage) GET(bgOpacity) GET(showThinking) GET(splash)
    GET(timerRounds) GET(timerRoundSec) GET(timerRestSec) GET(timerPrepSec) GET(timerWarnSec)
    GET(timerStartSound) GET(timerEndSound) GET(timerWarnSound) GET(timerVoice)
    // migração da versão "Aurora"
    size_t p = c.whisperModel.find("\\Aurora\\");
    if (p != std::string::npos) c.whisperModel.replace(p, 8, "\\KB\\");
    if (c.wakeWord == "aurora") c.wakeWord = "kb";
    if (c.temperature > 1.2f) c.temperature = 0.7f; // valores muito altos fazem o modelo "delirar"
    return true;
}

#define PUT(field) j[#field] = c.field;

bool Save(const Config& c) {
    json j;
    j["backend"] = (int)c.backend;
    PUT(ollamaUrl) PUT(openaiUrl) PUT(openaiKey) PUT(model) PUT(ggufPath) PUT(llamaServerPath)
    PUT(ggufGpuLayers) PUT(contextSize) PUT(temperature) PUT(historyTurns) PUT(systemPrompt)
    PUT(whisperModel) PUT(sttLanguage) PUT(sttThreads) PUT(micDevice) PUT(micSensitivity)
    PUT(wakeWordEnabled) PUT(wakeWord) PUT(speakReplies) PUT(ttsVoice) PUT(ttsRate) PUT(ttsVolume)
    PUT(startWithWindows) PUT(startMinimized) PUT(minimizeToTray) PUT(showMiniOrb) PUT(greetOnStart)
    PUT(hotkeyMods) PUT(hotkeyVk) PUT(uiScale) PUT(reduceMotion) PUT(miniOrbX) PUT(miniOrbY) PUT(firstRun)
    PUT(mobileEnabled) PUT(mobileMode) PUT(mobilePort) PUT(mobileToken) PUT(screenIntervalMs) PUT(autoVisionModel)
    PUT(showPerf) PUT(bgImage) PUT(bgOpacity) PUT(showThinking) PUT(splash)
    PUT(timerRounds) PUT(timerRoundSec) PUT(timerRestSec) PUT(timerPrepSec) PUT(timerWarnSec)
    PUT(timerStartSound) PUT(timerEndSound) PUT(timerWarnSound) PUT(timerVoice)
    std::wstring tmp = text::Widen(ConfigPath() + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << j.dump(2);
    }
    return MoveFileExW(tmp.c_str(), text::Widen(ConfigPath()).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

} // namespace config
