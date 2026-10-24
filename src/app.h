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
#include "net/tunnel.h"
#include "core/perf.h"
#include "agent/computer.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class AssistState { Idle, Listening, Transcribing, Thinking, Speaking };

// Passo de ferramenta mostrado na conversa (ex.: "Pesquisando na web: ...")
struct ToolEvent {
    std::string name, label;
    int status = 0; // 0 rodando, 1 ok, 2 erro
    std::vector<std::pair<std::string, std::string>> sources; // título, url
};

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
    std::vector<ToolEvent> toolEvents; // pesquisas na web, skills usadas...
    bool thinkingLive = false;         // ainda pensando
    double thinkStart = 0, thinkSecs = 0;
    double time = 0;
    // Cache da interface
    float appear = 0.f;
    size_t parsedLen = (size_t)-1;
    std::vector<Block> blocks;
};

// Pedido de permissão mostrado num cartão sempre visível (mesmo com o KB minimizado).
struct PermissionReq {
    int id = 0;
    int kind = 0;            // 0 controlar o computador, 1 aula experimental, 2 outro
    std::string title, detail, okLabel = "Permitir", noLabel = "Negar", ref;
    int answer = -1;         // -1 aguardando, 0 negado, 1 permitido
    double time = 0;
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
    void StartScreenShare(const CaptureSource& src);
    bool ModelHasVision(const std::string& name);
    std::string BestVisionModel();      // maior modelo instalado com visão (vazio = nenhum)

    // ---- Desempenho
    PerfSnapshot Perf() { return perf_.Get(); }

    // ---- Celular
    bool MobileRunning() const { return mobile_.Running(); }
    std::string MobileUrl();            // http://ip:porta/?t=token
    std::vector<std::string> LanIps() { return MobileServer::LanAddresses(); }
    void SetMobile(bool on);
    Tunnel& GetTunnel() { return tunnel_; }

    // ---- Ollama
    bool OllamaInstalled() { return !platform::FindOllama().empty(); }
    bool EnsureOllama(bool wait);       // inicia o servidor do Ollama se estiver instalado e parado

    // ---- Fala direta (timer etc.)
    void Say(const std::string& text) { if (tts_ && !silent_) tts_->Speak(text); }
    std::atomic<int> requestTimer{0};   // 1 = abrir o painel, 2 = abrir e iniciar

    // ---- Permissões (controle do PC, aulas experimentais)
    int AskPermission(PermissionReq r);                 // não bloqueia; devolve o id
    int WaitPermission(int id, double timeoutSec);      // bloqueia a thread de trabalho até a resposta (-1 = expirou)
    std::vector<PermissionReq> PendingPermissions();
    void AnswerPermission(int id, int answer);

    // ---- Controle do computador pela IA
    bool ComputerActive() const { return computerActive_.load(); }
    std::string ComputerStatus() { std::lock_guard<std::mutex> l(compMtx_); return computerStatus_; }
    void StopComputer();
    std::atomic<bool> requestMinimizeMain{false};

    // ---- Agenda
    std::string AgendaLink();                           // endereço público da página de agendamento (vazio se desligado)
    std::string RequestTrial(const std::string& name, const std::string& phone, const std::string& slotKey,
                             const std::string& source, std::string* err);

    // ---- Pedidos para a interface ("shop", "shop:<id>", "agenda", "panel:<aba>")
    void PostUi(const std::string& what) { std::lock_guard<std::mutex> l(uiReqMtx_); uiReqs_.push_back(what); }
    std::vector<std::string> TakeUi() { std::lock_guard<std::mutex> l(uiReqMtx_); auto v = std::move(uiReqs_); uiReqs_.clear(); return v; }

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
    Tunnel tunnel_;
    std::string MobileState(int since);

    platform::ChildProcess llamaProc_;
    mutable std::mutex procMtx_;
    std::atomic<bool> localReady_{false};

    std::mutex bgMtx_;
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> bg_;
    double nextModelPoll_ = 0;
    bool silent_ = false;

    // ferramentas da IA (app_tools.cpp)
    std::string ToolsJson();
    std::string ToolsPrompt();
    std::string RunTool(int msgId, const ToolCall& call, std::string* imageB64 = nullptr);
    void AddToolEvent(int msgId, const ToolEvent& ev);
    void UpdateLastToolEvent(int msgId, int status, const std::vector<std::pair<std::string, std::string>>& sources);
    bool ModelHasTools(const std::string& name);
    bool NeedsWebSearch(const std::string& q);

    // comandos locais (criar treino, timer)
    bool HandleLocalCommand(const std::string& q, bool fromVoice);
    void Reply(const std::string& msg, bool fromVoice, bool expectAnswer);
    std::mutex dialogMtx_;
    int dlgStep_ = 0, dlgRounds_ = 3, dlgRoundSec_ = 180;
    std::string dlgName_;
    std::atomic<bool> autoListen_{false};
    std::mutex uiReqMtx_;
    std::vector<std::string> uiReqs_;

    // permissões
    std::mutex permMtx_;
    std::condition_variable permCv_;
    std::vector<PermissionReq> perms_;
    int nextPerm_ = 1;
    // controle do computador
    std::atomic<bool> computerActive_{false}, computerGranted_{false};
    std::mutex compMtx_;
    std::string computerStatus_, currentQuery_;
    computer::Snapshot lastSnap_;
    bool lastModelVision_ = false;
    void SetComputerStatus(const std::string& s) { std::lock_guard<std::mutex> l(compMtx_); computerStatus_ = s; }
    bool EnsureComputerPermission(int msgId);
    std::string RunComputerTool(int msgId, const ToolCall& call, std::string* imageB64);
    // página pública de agendamento
    bool PublicRoute(const std::string& method, const std::string& path, const std::string& target, const std::string& body,
                     int* code, std::string* ctype, std::string* out);
    struct PublicSession {
        std::vector<ChatMsg> msgs;                         // histórico para o modelo
        std::vector<std::pair<bool, std::string>> shown;   // (é do visitante?, texto)
        bool busy = false;
        double last = 0;
        std::string bookingId;
    };
    std::mutex pubMtx_;
    std::map<std::string, PublicSession> pub_;
    void PublicChatReply(const std::string& sid);
};

double NowSeconds();
