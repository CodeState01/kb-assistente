#pragma once
#include "core/config.h"
#include "core/platform.h"
#include "llm/llm_client.h"
#include "llm/model_scan.h"
#include "audio/audio_capture.h"
#include "audio/stt_whisper.h"
#include "audio/tts_sapi.h"
#include "ui/blocks.h"
#include "media/image.h"
#include "media/screen.h"
#include "net/mobile_server.h"
#include "core/perf.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class AssistState { Idle, Listening, Transcribing, Thinking, Speaking };

struct Message {
    int id = 0;
    bool user = false;
    bool fromVoice = false;
    bool streaming = false;
    bool error = false;
    std::string text;
    std::vector<PicturePtr> pictures;  // imagens anexadas (fotos, prints da tela)
    std::string modelUsed;             // modelo que respondeu (ex.: troca automática para visão)
    std::string thinking;              // raciocínio do modelo (quando ele mostra)
    bool thinkingLive = false;         // ainda pensando
    double thinkStart = 0, thinkSecs = 0;
    double time = 0;
    // Cache da interface
    float appear = 0.f;
    size_t parsedLen = (size_t)-1;
    std::vector<Block> blocks;
};

struct Toast {
    int id;
    std::string text;
    int kind;  // 0 info, 1 sucesso, 2 erro
    double time;
};

// Tarefa em segundo plano com progresso (downloads, buscas).
struct Job {
    std::string title;
    std::atomic<uint64_t> done{0}, total{0};
    std::atomic<bool> finished{false}, failed{false}, cancel{false};
    std::mutex m;
    std::string status;
    void SetStatus(const std::string& s) { std::lock_guard<std::mutex> l(m); status = s; }
    std::string Status() { std::lock_guard<std::mutex> l(m); return status; }
    float Progress() const { uint64_t t = total.load(); return t ? (float)((double)done.load() / (double)t) : 0.f; }
};

class App {
public:
    App();
    ~App();
    void Init(bool startedMinimized);
    void Shutdown();
    void Update(); // chamado a cada frame (thread da UI)

    // ---- Interação
    void ToggleListening();          // atalho global / botão do microfone / clique no orb
    void StartListening();
    void StopListening();            // encerra e processa o que foi ouvido
    void Interrupt();                // para fala e geração
    void SendText(const std::string& text, bool fromVoice = false, std::vector<PicturePtr> pics = {});
    void ClearConversation();
    void LoadDemoConversation();   // usado no modo de diagnóstico (KB_DEMO=1)

    AssistState State() const { return state_.load(); }
    bool TtsSpeaking() const { return tts_ && tts_->Speaking(); }
    float MicLevel() const { return mic_.Level(); }
    void MicHistory(float* out, int n) const { mic_.LevelHistory(out, n); }
    float SpeechLevel(double now) const;
    float TokenPulse() const { return tokenPulse_.load(); }
    double StateSince() const { return stateSince_.load(); }
    std::string LiveCaption();       // último texto ouvido / status
    bool ThinkingHidden() const { return thinkingHidden_.load(); }

    // ---- Mensagens (acesso com trava)
    std::mutex& MessagesMutex() { return msgMtx_; }
    std::vector<Message>& MessagesUnsafe() { return messages_; }
    int MessagesVersion() const { return msgVersion_.load(); }

    // ---- Notificações
    void Notify(const std::string& text, int kind = 0);
    std::vector<Toast> Toasts();

    // ---- Configuração
    Config Cfg();
    void ApplyConfig(const Config& c);
    bool TakeHotkeyDirty() { return hotkeyDirty_.exchange(false); }

    // ---- Modelos de IA
    void RefreshModels();
    std::vector<ModelInfo> Models();
    bool ModelsLoading() const { return modelsLoading_.load(); }
    std::string ModelsError();
    std::string ActiveModel();       // o configurado ou o primeiro disponível
    bool BackendOnline() const { return backendOnline_.load(); }

    void DetectServers();
    std::vector<ServerInfo> Servers();
    bool DetectingServers() const { return detecting_.load(); }

    void ScanLocalFiles();
    std::vector<LocalFile> GgufFiles();
    std::vector<LocalFile> WhisperFiles();
    bool ScanningFiles() const { return scanning_.load(); }

    std::shared_ptr<Job> PullOllamaModel(const std::string& name);
    std::shared_ptr<Job> DownloadWhisper(const char* id);
    std::vector<std::shared_ptr<Job>> Jobs();

    // ---- GGUF local via llama-server
    void StartLocalServer();
    void StopLocalServer();
    bool LocalServerRunning() const { std::lock_guard<std::mutex> l(procMtx_); return llamaProc_.Running(); }
    bool LocalServerReady() const { return localReady_.load(); }

    // ---- Voz
    bool SttLoaded() const { return stt_.Loaded(); }
    bool SttLoading() const { return sttLoading_.load(); }
    std::string SttModelPath() { return stt_.ModelPath(); }
    void LoadStt(const std::string& path);
    std::vector<VoiceInfo> Voices() { return tts_ ? tts_->Voices() : std::vector<VoiceInfo>{}; }
    std::string CurrentVoice() { return tts_ ? tts_->CurrentVoice() : ""; }
    void TestVoice();
    bool MicRunning() const { return mic_.Running(); }
    std::vector<std::string> MicDevices();
    void RestartMic();

    // ---- Visão
    ScreenShare& Screen() { return screen_; }
    void SetScreenShare(bool on);
    bool ModelHasVision(const std::string& name);
    std::string BestVisionModel();      // maior modelo instalado com visão (vazio = nenhum)

    // ---- Desempenho
    PerfSnapshot Perf() { return perf_.Get(); }

    // ---- Celular
    bool MobileRunning() const { return mobile_.Running(); }
    std::string MobileUrl();            // http://ip:porta/?t=token
    std::vector<std::string> LanIps() { return MobileServer::LanAddresses(); }
    void SetMobile(bool on);

    // ---- Ollama
    bool OllamaInstalled() { return !platform::FindOllama().empty(); }
    bool EnsureOllama(bool wait);       // inicia o servidor do Ollama se estiver instalado e parado

    // ---- Janela
    std::atomic<bool> requestShowMain{false};
    std::atomic<bool> requestQuit{false};

private:
    void SetState(AssistState s);
    void VoiceLoop();
    void LlmLoop();
    void ProcessQuery(const std::string& q);
    int AddMessage(bool user, const std::string& text, bool streaming, bool fromVoice = false);
    void AppendToMessage(int id, const std::string& piece);
    void FinishMessage(int id, bool error, const std::string& replaceText = "");
    void ScheduleSave();
    bool EnsureLocalServer(std::string* err);
    void AutoPickWhisper();
    void Spawn(std::function<void()> fn);

    Config cfg_;
    std::mutex cfgMtx_;
    double saveAt_ = 0;
    std::atomic<bool> hotkeyDirty_{true};

    std::atomic<AssistState> state_{AssistState::Idle};
    std::atomic<double> stateSince_{0};
    std::atomic<bool> quit_{false};
    std::atomic<bool> manualStop_{false};
    std::atomic<bool> cancelLlm_{false};
    std::atomic<float> tokenPulse_{0.f};
    std::atomic<bool> thinkingHidden_{false};

    AudioCapture mic_;
    SpeechToText stt_;
    std::atomic<bool> sttLoading_{false};
    std::unique_ptr<TextToSpeech> tts_;

    std::thread voiceThread_, llmThread_;
    std::mutex qMtx_;
    std::condition_variable qCv_;
    std::deque<std::string> queries_;

    std::mutex msgMtx_;
    std::vector<Message> messages_;
    std::atomic<int> msgVersion_{0};
    int nextMsgId_ = 1;

    std::mutex captionMtx_;
    std::string caption_;

    std::mutex toastMtx_;
    std::vector<Toast> toasts_;
    int nextToast_ = 1;

    std::mutex dataMtx_;
    std::vector<ModelInfo> models_;
    std::string modelsError_;
    std::vector<ServerInfo> servers_;
    std::vector<LocalFile> ggufFiles_, whisperFiles_;
    std::vector<std::shared_ptr<Job>> jobs_;
    std::atomic<bool> modelsLoading_{false}, detecting_{false}, scanning_{false}, backendOnline_{false};
    double lastOllamaLaunch_ = -100;
    ScreenShare screen_;
    PerfMonitor perf_;
    void AppendThinking(int id, const std::string& piece);
    MobileServer mobile_;
    std::string MobileState(int since);

    platform::ChildProcess llamaProc_;
    mutable std::mutex procMtx_;
    std::atomic<bool> localReady_{false};

    std::mutex bgMtx_;
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> bg_;
    double nextModelPoll_ = 0;
    bool silent_ = false;
};

double NowSeconds();
