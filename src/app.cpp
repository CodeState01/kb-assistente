#include "app.h"
#include "core/text.h"
#include "net/http.h"
#include <windows.h>
#include <mmsystem.h>
#include <bcrypt.h>
#include "core/files.h"
#include "core/install.h"
#include "core/skills.h"
#include "net/web.h"
#include "audio/sfx.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

double NowSeconds() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

// ------------------------------------------------------------------ Sons curtos (earcons)
namespace {
bool g_agendaForced = false;   // KB_AGENDA_PUBLIC (teste)
void SaveCfg(Config c) {
    if (g_agendaForced) c.agendaPublic = false;
    config::Save(c);
}

void PlayChime(int kind) {
    // sons pelo mixer da interface (não cortam o timer nem outros efeitos)
    sfx::Play(kind == 0 ? sfx::ListenStart : kind == 1 ? sfx::ListenStop : sfx::Error);
}

// Converte o fluxo de texto da IA em frases para a fala, pulando código e tabelas.
class TtsFeeder {
public:
    explicit TtsFeeder(TextToSpeech* t) : tts_(t) {}
    void Feed(const std::string& s) { for (char c : s) Char(c); }
    void Flush() {
        if (!inFence_ && !skip_ && !fenceLine_ && !decided_) sentence_ += line_;
        FlushSentence();
    }

private:
    void Char(char c) {
        if (c == '\n') { EndLine(); return; }
        line_ += c;
        if (inFence_) return;
        if (!decided_) {
            char f = line_[0];
            if (f == '|') { decided_ = true; skip_ = true; SayOnce(); return; }
            if (f == '`') {
                if (line_.size() >= 3) {
                    decided_ = true;
                    if (text::StartsWith(line_, "```")) fenceLine_ = true;
                    else sentence_ += line_;
                }
                return;
            }
            decided_ = true;
            sentence_ += line_;
            Check(c);
            return;
        }
        if (skip_ || fenceLine_) return;
        sentence_ += c;
        Check(c);
    }
    void Check(char c) {
        size_t n = sentence_.size();
        if (c == ' ' && n >= 2) {
            char p = sentence_[n - 2];
            if ((p == '.' || p == '!' || p == '?' || p == ';' || p == ':') && n > 16) { FlushSentence(); return; }
            if (n > 240) FlushSentence();
        }
    }
    void EndLine() {
        std::string t = text::Trim(line_);
        if (inFence_) {
            if (text::StartsWith(t, "```")) inFence_ = false;
        } else if (fenceLine_ || text::StartsWith(t, "```")) {
            inFence_ = true;
            if (text::StartsWith(text::Fold(t), "```arquivo") && !saidFile_) {
                FlushSentence();
                if (tts_) tts_->Speak("Preciso da sua permissão para criar um arquivo. Veja o caminho na tela.");
                saidFile_ = said_ = true;
            } else {
                SayOnce();
            }
        } else if (!skip_) {
            if (!decided_) sentence_ += line_;
            FlushSentence();
        }
        line_.clear();
        decided_ = skip_ = fenceLine_ = false;
    }
    void SayOnce() {
        if (said_) return;
        FlushSentence();
        if (tts_) tts_->Speak("Coloquei os detalhes na tela.");
        said_ = true;
    }
    void FlushSentence() {
        std::string s = text::CleanForSpeech(sentence_);
        sentence_.clear();
        bool letters = false;
        for (char c : s) if (isalnum((unsigned char)c) || (unsigned char)c >= 0xC0) { letters = true; break; }
        if (letters && tts_) tts_->Speak(s);
    }

    TextToSpeech* tts_;
    std::string line_, sentence_;
    bool inFence_ = false, decided_ = false, skip_ = false, fenceLine_ = false, said_ = false, saidFile_ = false;
};
} // namespace

// ------------------------------------------------------------------ App
App::App() {}
App::~App() { Shutdown(); }

void App::Spawn(std::function<void()> fn) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> l(bgMtx_);
    bg_.emplace_back(std::thread([fn = std::move(fn), done] {
        fn();
        done->store(true);
    }), done);
}

void App::Init(bool startedMinimized) {
    (void)startedMinimized;
    config::Load(cfg_);
    cfg_.startWithWindows = platform::IsAutostartEnabled();
    // atualiza o caminho do .exe (só a cópia instalada; testes e cópias soltas não mexem na inicialização)
    if (cfg_.startWithWindows && GetEnvironmentVariableW(L"KB_SNAPSHOT", nullptr, 0) == 0 &&
        _wcsicmp(text::Widen(platform::ExePath()).c_str(), install::InstalledExe().c_str()) == 0)
        platform::SetAutostart(true, cfg_.startMinimized);

    sfx::Init();
    sfx::SetEnabled(cfg_.sfx);
    sfx::SetVolume(cfg_.sfxVolume);
    sfx::SetHover(cfg_.sfxHover);

    tts_ = std::make_unique<TextToSpeech>();
    tts_->SetVoice(cfg_.ttsVoice);
    tts_->SetRate(cfg_.ttsRate);
    tts_->SetVolume(cfg_.ttsVolume);

    std::string err;
    if (!mic_.Start(cfg_.micDevice, &err)) Notify(err, 2);

    perf_.Start();
    voiceThread_ = std::thread([this] { VoiceLoop(); });
    llmThread_ = std::thread([this] { LlmLoop(); });

    if (cfg_.mobileToken.empty()) {
        // token aleatório para o app do celular
        static const char* hex = "0123456789abcdef";
        uint8_t rnd[12];
        bool ok = BCryptGenRandom(nullptr, rnd, sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
        if (!ok) for (auto& b : rnd) b = (uint8_t)(rand() ^ (GetTickCount() >> 3));
        for (uint8_t b : rnd) { cfg_.mobileToken += hex[b >> 4]; cfg_.mobileToken += hex[b & 15]; }
        ScheduleSave();
    }

    skills::EnsureDefaults();
    AutoPickWhisper();
    if (cfg_.backend == LlmBackend::LocalGguf) StartLocalServer();
    if (cfg_.backend == LlmBackend::Ollama) EnsureOllama(false);
    RefreshModels();
    ScanLocalFiles();
    // Teste: KB_AGENDA_PUBLIC liga a página /agendar só nesta sessão (não grava na config)
    if (GetEnvironmentVariableW(L"KB_AGENDA_PUBLIC", nullptr, 0) > 0 && !cfg_.agendaPublic) cfg_.agendaPublic = g_agendaForced = true;
    if (cfg_.mobileEnabled || cfg_.agendaPublic) SetMobile(true);

    bool diagnostic = GetEnvironmentVariableW(L"KB_SNAPSHOT", nullptr, 0) > 0;
    if (cfg_.greetOnStart && cfg_.speakReplies && !diagnostic) {
        std::string g = text::GreetingForNow() + "! Estou pronto.";
        tts_->Speak(g);
        std::lock_guard<std::mutex> l(captionMtx_);
        caption_ = g;
    }
    if (diagnostic) {
        silent_ = true; // diagnóstico silencioso, sem alterar a config salva
        wchar_t q[1024], a[1024];
        if (GetEnvironmentVariableW(L"KB_MOBILE", nullptr, 0) > 0) {
            SetMobile(true);
            for (int i = 0; i < 600 && Cfg().mobileMode == 0 && tunnel_.GetState() != Tunnel::State::Ready &&
                            tunnel_.GetState() != Tunnel::State::Error; ++i)
                Sleep(100);
            if (FILE* f = _wfopen((text::Widen(platform::GetEnv("TEMP")) + L"\\kb_url.txt").c_str(), L"wb")) {
                std::string u = MobileUrl();
                for (auto& ip : LanIps()) u += "\n" + ip;
                fwrite(u.data(), 1, u.size(), f);
                fclose(f);
            }
        }
        if (GetEnvironmentVariableW(L"KB_SCREEN", nullptr, 0) > 0) {
            SetScreenShare(true);
            for (int i = 0; i < 40 && !screen_.Latest(); ++i) Sleep(100);
        }
        std::vector<PicturePtr> pics;
        DWORD na = GetEnvironmentVariableW(L"KB_ATTACH", a, 1024);
        if (na && na < 1024) pics.push_back(img::FromFile(text::Narrow(std::wstring(a, na))));
        // KB_SCRIPT="fala 1|fala 2|..." — simula uma conversa (uma fala a cada 1,5 s)
        DWORD ns = GetEnvironmentVariableW(L"KB_SCRIPT", q, 1024);
        if (ns && ns < 1024) {
            auto lines = text::Split(text::Narrow(std::wstring(q, ns)), '|');
            Spawn([this, lines] {
                for (auto& l : lines) {
                    if (quit_) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                    SendText(l);
                }
            });
        }
        DWORD n = GetEnvironmentVariableW(L"KB_ASK", q, 1024);
        if (n && n < 1024) SendText(text::Narrow(std::wstring(q, n)), false, pics);
    }
    stateSince_ = NowSeconds();
}

void App::Shutdown() {
    if (quit_.exchange(true)) return;
    cancelLlm_ = true;
    {
        std::lock_guard<std::mutex> l(dataMtx_);
        for (auto& j : jobs_) j->cancel = true;
    }
    qCv_.notify_all();
    if (voiceThread_.joinable()) voiceThread_.join();
    if (llmThread_.joinable()) llmThread_.join();
    {
        std::lock_guard<std::mutex> l(bgMtx_);
        for (auto& b : bg_) if (b.first.joinable()) b.first.join();
        bg_.clear();
    }
    tunnel_.Stop();
    mobile_.Stop();
    screen_.Stop();
    perf_.Stop();
    mic_.Stop();
    tts_.reset();
    StopLocalServer();
    sfx::Shutdown();
    std::lock_guard<std::mutex> l(cfgMtx_);
    SaveCfg(cfg_);
}

void App::Update() {
    double now = NowSeconds();
    // decaimento do pulso de tokens
    static double last = now;
    float dt = (float)(now - last);
    last = now;
    tokenPulse_ = tokenPulse_.load() * std::exp(-dt * 6.f);

    {
        std::lock_guard<std::mutex> l(cfgMtx_);
        if (saveAt_ > 0 && now >= saveAt_) {
            saveAt_ = 0;
            SaveCfg(cfg_);
        }
    }
    {
        std::lock_guard<std::mutex> l(bgMtx_);
        for (auto it = bg_.begin(); it != bg_.end();) {
            if (it->second->load()) { it->first.join(); it = bg_.erase(it); }
            else ++it;
        }
    }
    // conversa guiada por voz: escuta a resposta assim que terminar de falar
    if (autoListen_ && !TtsSpeaking() && state_ == AssistState::Idle) {
        autoListen_ = false;
        StartListening();
    }
    if (now >= nextModelPoll_ && !modelsLoading_) {
        nextModelPoll_ = now + (backendOnline_ ? 60.0 : 8.0);
        RefreshModels();
    }
}

void App::SetState(AssistState s) {
    if (state_.exchange(s) != s) stateSince_ = NowSeconds();
}

// ------------------------------------------------------------------ Interação
void App::ToggleListening() {
    switch (state_.load()) {
    case AssistState::Idle: StartListening(); break;
    case AssistState::Listening: StopListening(); break;
    case AssistState::Transcribing: break;
    case AssistState::Thinking:
    case AssistState::Speaking: Interrupt(); StartListening(); break;
    }
}

void App::StartListening() {
    if (!stt_.Loaded()) {
        Notify(sttLoading_ ? "O modelo de voz ainda está carregando..." : "Modelo de voz não encontrado. Baixe um em Modelos > Voz.", 2);
        PlayChime(2);
        return;
    }
    if (!mic_.Running()) {
        RestartMic();
        if (!mic_.Running()) { Notify("Microfone indisponível.", 2); return; }
    }
    if (tts_) tts_->StopAll();
    manualStop_ = false;
    {
        std::lock_guard<std::mutex> l(captionMtx_);
        caption_.clear();
    }
    PlayChime(0);
    SetState(AssistState::Listening);
}

void App::StopListening() {
    if (state_ == AssistState::Listening) manualStop_ = true;
}

void App::Interrupt() {
    cancelLlm_ = true;
    if (tts_) tts_->StopAll();
    AssistState s = state_.load();
    if (s == AssistState::Listening) SetState(AssistState::Idle);
}

void App::SendText(const std::string& t, bool fromVoice, std::vector<PicturePtr> pics) {
    std::string q = text::Trim(t);
    pics.erase(std::remove(pics.begin(), pics.end(), nullptr), pics.end());
    if (q.empty() && pics.empty()) return;
    sfx::Play(sfx::Send, 0.8f);
    if (q.empty()) q = "O que você vê nesta imagem?";
    const bool userPics = !pics.empty();
    // Com a tela compartilhada, cada pergunta leva o quadro mais recente.
    if (screen_.Active()) {
        if (auto frame = screen_.Latest()) pics.push_back(frame);
    }
    AssistState s = state_.load();
    if (s == AssistState::Thinking || s == AssistState::Speaking) Interrupt();
    if (!userPics && HandleLocalCommand(q, fromVoice)) return; // treinos / timer
    AddMessage(true, q, false, fromVoice);
    if (!pics.empty()) {
        std::lock_guard<std::mutex> l(msgMtx_);
        messages_.back().pictures = std::move(pics);
    }
    {
        std::lock_guard<std::mutex> l(qMtx_);
        queries_.push_back(q);
    }
    qCv_.notify_one();
}

void App::ClearConversation() {
    Interrupt();
    std::lock_guard<std::mutex> l(msgMtx_);
    messages_.clear();
    ++msgVersion_;
}

void App::LoadDemoConversation() {
    AddMessage(true, "Como faço um café coado perfeito?", false, true);
    int id = AddMessage(false,
        "Claro! Um bom café coado depende de proporção, temperatura e paciência.\n\n"
        "```passos\nAqueça a água entre 92 e 96 graus\nUse 10 g de café para cada 150 ml de água\nEscalde o filtro de papel\n"
        "Faça a pré-infusão e espere 30 segundos\nDespeje o resto em movimentos circulares\n```\n\n"
        "```dica\nMoa o café na hora, com moagem média, para mais aroma.\n```\n\n"
        "| Método | Tempo | Corpo |\n|---|---|---|\n| Coado | 3 min | Leve |\n| Prensa francesa | 4 min | Encorpado |\n\n"
        "Quer que eu calcule a quantidade para uma garrafa térmica inteira?", false);
    (void)id;
    AddMessage(true, "Mostra um exemplo de código em Python que lê um arquivo", false);
    AddMessage(false,
        "Aqui está um exemplo simples:\n\n```python\n# lê um arquivo de texto linha por linha\nwith open(\"notas.txt\", encoding=\"utf-8\") as f:\n"
        "    for linha in f:\n        print(linha.strip())\n```\n\n```aviso\nUse sempre encoding utf-8 para não ter problemas com acentos.\n```", false);
}

float App::SpeechLevel(double now) const {
    (void)now;
    if (tts_ && tts_->Speaking()) {
        double since = NowSeconds() - tts_->LastEventTime();
        if (since > 0.35) {
            // Voz sem eventos de visema: animação procedural
            double t = NowSeconds();
            return (float)(0.45 + 0.25 * std::sin(t * 11.0) * std::sin(t * 3.7) + 0.15 * std::sin(t * 23.0));
        }
        return tts_->Mouth();
    }
    return tokenPulse_.load() * 0.7f;
}

std::string App::LiveCaption() {
    std::lock_guard<std::mutex> l(captionMtx_);
    return caption_;
}

// ------------------------------------------------------------------ Voz
void App::VoiceLoop() {
    Vad listenVad, wakeVad;
    std::vector<float> buf;
    bool listening = false, heard = false;
    double listenStart = 0;
    bool wasBusy = false;

    while (!quit_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        mic_.Drain(buf);
        Config c = Cfg();
        AssistState s = state_.load();
        double now = NowSeconds();

        if (s == AssistState::Listening) {
            if (!listening) {
                listening = true;
                heard = false;
                listenStart = now;
                listenVad.Reset();
                listenVad.sensitivity = c.micSensitivity;
                listenVad.endSilenceSec = 0.9f;
            }
            // ignora o próprio "bip" de início
            if (now - listenStart < 0.25) continue;
            auto ev = listenVad.Process(buf.data(), buf.size());
            if (listenVad.InSpeech()) heard = true;
            bool finished = ev == Vad::Event::SpeechEnd || manualStop_.load();
            if (finished) {
                listening = false;
                manualStop_ = false;
                std::vector<float> utt = listenVad.Utterance();
                if (!heard || utt.size() < kSampleRate / 3) {
                    PlayChime(1);
                    SetState(AssistState::Idle);
                    continue;
                }
                PlayChime(1);
                SetState(AssistState::Transcribing);
                std::string t = stt_.Transcribe(utt, c.sttLanguage, c.sttThreads, "KB, assistente em português.");
                if (t.empty()) {
                    Notify("Não entendi. Pode repetir?", 0);
                    SetState(AssistState::Idle);
                    continue;
                }
                {
                    std::lock_guard<std::mutex> l(captionMtx_);
                    caption_ = t;
                }
                SetState(AssistState::Thinking);
                SendText(t, true);
            } else if (!heard && now - listenStart > 7.0) {
                listening = false;
                PlayChime(1);
                SetState(AssistState::Idle);
            }
            continue;
        }
        listening = false;

        // Palavra de ativação: só quando ocioso e sem fala da KB (evita ouvir a si mesma).
        bool busy = s != AssistState::Idle || (tts_ && tts_->Speaking());
        if (busy || !c.wakeWordEnabled || !stt_.Loaded()) {
            if (busy) wasBusy = true;
            continue;
        }
        if (wasBusy) { wakeVad.Reset(); wasBusy = false; continue; }
        wakeVad.sensitivity = c.micSensitivity;
        wakeVad.endSilenceSec = 0.6f;
        wakeVad.maxSpeechSec = 10.f;
        auto ev = wakeVad.Process(buf.data(), buf.size());
        if (ev != Vad::Event::SpeechEnd) continue;
        const auto& u = wakeVad.Utterance();
        if (u.size() < kSampleRate * 4 / 10) continue;
        std::string t = stt_.Transcribe(u, c.sttLanguage, c.sttThreads, "KB.");
        if (t.empty()) continue;
        std::wstring wt = text::Widen(t);
        std::string folded = text::Fold(t);
        std::string wake = text::Fold(text::Trim(c.wakeWord.empty() ? "kb" : c.wakeWord));
        // "KB" falado vira várias grafias no Whisper; aceita as variações só no começo da frase.
        std::vector<std::string> forms = {wake};
        if (wake == "kb")
            for (const char* a : {"k.b.", "k. b.", "k.b", "k b", "kabe", "ka be", "ka-be", "kbe", "cabe", "ca be", "kab", "kebe", "kay bee", "kb,"})
                forms.push_back(a);
        size_t pos = std::string::npos, len = 0;
        for (auto& f : forms) {
            size_t p = folded.find(f);
            bool atWordStart = p != std::string::npos && (p == 0 || !isalnum((unsigned char)folded[p - 1]));
            bool atWordEnd = p != std::string::npos && (p + f.size() >= folded.size() || !isalnum((unsigned char)folded[p + f.size()]));
            if (atWordStart && atWordEnd && p < 14 && (pos == std::string::npos || p < pos || (p == pos && f.size() > len))) { pos = p; len = f.size(); }
        }
        if (pos == std::string::npos) continue;
        // Fold mapeia 1 caractere largo -> 1 byte, então o índice vale para a string larga.
        std::string rest = text::Narrow(wt.substr(std::min(wt.size(), pos + len)));
        while (!rest.empty() && (rest[0] == ',' || rest[0] == '.' || rest[0] == '!' || rest[0] == '?' || rest[0] == ' '))
            rest.erase(0, 1);
        rest = text::Trim(rest);
        if (rest.size() >= 6) {
            PlayChime(0);
            {
                std::lock_guard<std::mutex> l(captionMtx_);
                caption_ = rest;
            }
            SetState(AssistState::Thinking);
            SendText(rest, true);
        } else {
            StartListening();
        }
    }
}

// ------------------------------------------------------------------ IA
void App::LlmLoop() {
    while (!quit_) {
        std::string q;
        {
            std::unique_lock<std::mutex> l(qMtx_);
            qCv_.wait(l, [&] { return quit_ || !queries_.empty(); });
            if (quit_) break;
            q = std::move(queries_.front());
            queries_.pop_front();
        }
        ProcessQuery(q);
    }
}

int App::AddMessage(bool user, const std::string& t, bool streaming, bool fromVoice) {
    std::lock_guard<std::mutex> l(msgMtx_);
    Message m;
    m.id = nextMsgId_++;
    m.user = user;
    m.text = t;
    m.streaming = streaming;
    m.fromVoice = fromVoice;
    m.time = NowSeconds();
    messages_.push_back(std::move(m));
    ++msgVersion_;
    return messages_.back().id;
}

void App::AppendToMessage(int id, const std::string& piece) {
    std::lock_guard<std::mutex> l(msgMtx_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
        if (it->id == id) {
            it->thinkingLive = false;
            if (it->text.empty()) {
                size_t p = 0;
                while (p < piece.size() && (piece[p] == ' ' || piece[p] == '\n')) ++p;
                it->text += piece.substr(p);
            } else {
                it->text += piece;
            }
            break;
        }
    ++msgVersion_;
}

void App::AppendThinking(int id, const std::string& piece) {
    std::lock_guard<std::mutex> l(msgMtx_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
        if (it->id == id) {
            if (it->thinking.empty()) {
                size_t p = piece.find_first_not_of(" \n\r\t");
                if (p == std::string::npos) return;
                it->thinking += piece.substr(p);
                it->thinkStart = NowSeconds();
            } else {
                it->thinking += piece;
            }
            it->thinkingLive = true;
            it->thinkSecs = NowSeconds() - it->thinkStart;
            break;
        }
    ++msgVersion_;
}

void App::FinishMessage(int id, bool error, const std::string& replace) {
    std::lock_guard<std::mutex> l(msgMtx_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
        if (it->id == id) {
            if (!it->user && it->streaming && !error) sfx::Play(sfx::Receive, 0.7f);
            it->streaming = false;
            it->thinkingLive = false;
            it->error = error;
            if (!replace.empty()) it->text = replace;
            break;
        }
    ++msgVersion_;
}

void App::ProcessQuery(const std::string& q) {
    cancelLlm_ = false;
    currentQuery_ = q;
    computerGranted_ = false;
    computer::StopFlag() = false;
    lastSnap_ = computer::Snapshot();
    Config c = Cfg();
    SetState(AssistState::Thinking);

    std::string err;
    if (c.backend == LlmBackend::LocalGguf && !EnsureLocalServer(&err)) {
        int id = AddMessage(false, "", false);
        FinishMessage(id, true, err);
        if (tts_ && c.speakReplies) tts_->Speak("Não consegui iniciar o modelo local.");
        PlayChime(2);
        SetState(AssistState::Idle);
        return;
    }
    if (c.backend == LlmBackend::Ollama && !backendOnline_) EnsureOllama(true);

    std::string model = ActiveModel();
    if (model.empty() && c.backend == LlmBackend::Ollama) {
        auto list = llm::ListModels(c, &err);
        llm::FillCapabilities(c, list);
        {
            std::lock_guard<std::mutex> l(dataMtx_);
            models_ = list;
            modelsError_ = err;
            backendOnline_ = err.empty();
        }
        model = ActiveModel();
    }
    if (model.empty() && c.backend == LlmBackend::Ollama) {
        int id = AddMessage(false, "", false);
        std::string msg = backendOnline_
            ? std::string("Não encontrei nenhum modelo instalado no Ollama.\n\n```passos\nAbra a aba Modelos\nClique em Baixar no modelo recomendado (") +
                  llm::kRecommendedLight + ", ~8 GB, com visão)\nOu escolha um arquivo .gguf que você já tenha\n```"
            : "Não consegui falar com o Ollama em " + c.ollamaUrl +
                  ".\n\n```aviso\nInstale o Ollama (ollama.com) ou abra seu LM Studio / llama.cpp e clique em Procurar servidores na aba Modelos.\n```";
        FinishMessage(id, true, msg);
        if (tts_ && c.speakReplies) tts_->Speak(backendOnline_ ? "Ainda não há nenhum modelo de IA instalado. Coloquei as instruções na tela." : "Não encontrei nenhum servidor de IA rodando. Coloquei as instruções na tela.");
        SetState(AssistState::Idle);
        return;
    }

    // Imagens da última mensagem do usuário (fotos / tela compartilhada)
    std::vector<std::string> images;
    {
        std::lock_guard<std::mutex> l(msgMtx_);
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
            if (it->user) {
                for (auto& p : it->pictures) if (p && !p->jpegB64.empty()) images.push_back(p->jpegB64);
                break;
            }
    }
    std::string visionNote;
    if (!images.empty() && !ModelHasVision(model)) {
        std::string vm = c.autoVisionModel ? BestVisionModel() : "";
        if (!vm.empty()) {
            visionNote = vm;
            model = vm;
        } else {
            Notify("O modelo " + model + " não enxerga imagens. Instale um com visão (ex.: gemma3:12b) na aba Modelos.", 2);
            images.clear();
        }
    }

    // Monta o contexto (o prompt do sistema nunca aparece na interface)
    std::vector<ChatMsg> msgs;
    std::string sys = c.systemPrompt.empty() ? config::DefaultSystemPrompt() : c.systemPrompt;
    sys += "\n\nCRIAR ARQUIVOS: você pode criar arquivos no PC do usuário quando ele pedir. Escreva um bloco assim, "
           "com o caminho COMPLETO na mesma linha da abertura:\n```arquivo C:\\caminho\\completo\\nome.txt\nconteúdo exato do arquivo\n```\n"
           "Um bloco por arquivo. Área de Trabalho do usuário: " + files::DesktopDir() + " . Documentos: " + files::DocumentsDir() +
           " . O usuário vê o caminho e precisa APROVAR antes de o arquivo ser criado; diga que está aguardando a aprovação, "
           "nunca que já criou. Não crie arquivos sem o usuário pedir. Quando ele pedir um arquivo, escreva o bloco "
           "na mesma resposta, com um conteúdo sensato (ele pode negar e pedir ajustes); não pergunte antes.";
    if (!images.empty())
        sys += "\n\nVISÃO: você está recebendo imagem(ns) junto da mensagem do usuário" +
               (screen_.Active() ? " (incluindo uma captura AO VIVO de \"" + screen_.SourceName() + "\" no computador dele)" : std::string()) +
               ". Descreva e use o que vê para responder.";
    sys += "\n\nData e hora atual: " + text::NowPortuguese() + ".";
    const bool useTools = c.backend == LlmBackend::Ollama && ModelHasTools(model);
    if (useTools) sys += ToolsPrompt();
    msgs.push_back({"system", sys, {}});
    {
        std::lock_guard<std::mutex> l(msgMtx_);
        std::vector<ChatMsg> hist;
        for (auto& m : messages_) {
            if (m.error || m.streaming || m.text.empty()) continue;
            std::string content = m.text;
            if (m.user && !m.pictures.empty() && &m != &messages_.back()) content += "\n[imagem enviada anteriormente]";
            hist.push_back({m.user ? "user" : "assistant", content, {}});
        }
        if (!hist.empty() && hist.back().role == "user") hist.back().imagesB64 = images;
        size_t maxN = (size_t)std::max(2, c.historyTurns * 2);
        size_t start = hist.size() > maxN ? hist.size() - maxN : 0;
        // começa sempre por uma mensagem do usuário
        while (start < hist.size() && hist[start].role != "user") ++start;
        for (size_t i = start; i < hist.size(); ++i) msgs.push_back(hist[i]);
    }

    int aid = AddMessage(false, "", true);
    {
        std::lock_guard<std::mutex> l(msgMtx_);
        messages_.back().modelUsed = model;
    }
    if (!visionNote.empty()) Notify("Usando " + visionNote + " para enxergar a imagem.", 0);
    // Modelos sem ferramentas: pesquisa antes quando o pedido claramente precisa da internet
    if (!useTools && c.webTools && NeedsWebSearch(q)) {
        nlohmann::json qa = {{"query", q}};
        std::string found = RunTool(aid, {"web_search", qa.dump()});
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it)
            if (it->role == "user") {
                it->content += "\n\n[RESULTADOS DA PESQUISA NA WEB, feita agora]\n" + found +
                               "\n[FIM DOS RESULTADOS] Responda usando APENAS estes resultados (eles são mais recentes que o seu conhecimento) e cite o site.";
                break;
            }
    }
    llm::ThinkFilter tf;
    TtsFeeder feeder(c.speakReplies ? tts_.get() : nullptr);
    bool any = false;
    auto onThink = [&](const std::string& th) {
        if (cancelLlm_ || quit_) return false;
        AppendThinking(aid, th);
        tokenPulse_ = 0.6f;
        return true;
    };
    auto onTok = [&](const std::string& piece) {
        if (cancelLlm_ || quit_) return false;
        std::string th;
        std::string vis = tf.Feed(piece, &th);
        if (!th.empty()) AppendThinking(aid, th);
        thinkingHidden_ = tf.Thinking();
        if (vis.empty()) return true;
        if (!any) {
            bool onlySpace = vis.find_first_not_of(" \n\r\t") == std::string::npos;
            if (onlySpace) return true;
            any = true;
            SetState(AssistState::Speaking);
        }
        AppendToMessage(aid, vis);
        tokenPulse_ = 1.f;
        feeder.Feed(vis);
        return true;
    };
    // Laço de ferramentas: o modelo pode pesquisar, ler páginas, usar skills... e depois responder.
    const std::string toolsJson = useTools ? ToolsJson() : std::string();
    lastModelVision_ = useTools && ModelHasVision(model);
    bool ok = false;
    int maxSteps = 6;
    for (int step = 0; step < maxSteps; ++step) {
        std::vector<ToolCall> calls;
        ok = llm::Chat(c, model, msgs, onTok, &err, &cancelLlm_, onThink, toolsJson, useTools ? &calls : nullptr);
        if (!ok || cancelLlm_ || quit_ || calls.empty()) break;
        nlohmann::json tcs = nlohmann::json::array();
        for (auto& call : calls) {
            nlohmann::json args = nlohmann::json::parse(call.argsJson, nullptr, false);
            tcs.push_back({{"function", {{"name", call.name}, {"arguments", args.is_discarded() ? nlohmann::json::object() : args}}}});
        }
        ChatMsg asst{"assistant", "", {}};
        asst.toolCallsJson = tcs.dump();
        msgs.push_back(asst);
        SetState(AssistState::Thinking);
        for (auto& call : calls) {
            std::string shot;
            ChatMsg tm{"tool", RunTool(aid, call, &shot), {}};
            tm.toolName = call.name;
            if (!shot.empty()) tm.imagesB64.push_back(shot);
            msgs.push_back(tm);
            // tarefas na tela precisam de mais passos (abrir, olhar, clicar, digitar...)
            if (call.name == "look_at_screen" || call.name == "click" || call.name == "type_text" || call.name == "press_keys" ||
                call.name == "open_website" || call.name == "open_app" || call.name == "scroll" || call.name == "click_xy")
                maxSteps = 25;
        }
        // as capturas antigas pesam: mantém só a imagem mais recente no histórico
        int seenShot = 0;
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it)
            if (it->role == "tool" && !it->imagesB64.empty() && seenShot++ > 0) it->imagesB64.clear();
        if (cancelLlm_ || computer::StopFlag()) break;
    }
    thinkingHidden_ = false;
    if (computerActive_) {
        computerActive_ = false;
        SetComputerStatus("");
    }
    computerGranted_ = false;

    if (cancelLlm_) {
        FinishMessage(aid, false);
    } else if (!ok) {
        FinishMessage(aid, true, err);
        Notify("Erro na IA: " + err.substr(0, 160), 2);
        if (tts_ && c.speakReplies) tts_->Speak("Desculpe, tive um problema para falar com o modelo de IA.");
        PlayChime(2);
    } else {
        feeder.Flush();
        if (!any) FinishMessage(aid, false, "(sem resposta do modelo)");
        else FinishMessage(aid, false);
    }

    // espera a fala terminar
    while (!quit_ && !cancelLlm_ && tts_ && tts_->Speaking()) std::this_thread::sleep_for(std::chrono::milliseconds(30));
    AssistState s = state_.load();
    if (s == AssistState::Thinking || s == AssistState::Speaking) SetState(AssistState::Idle);
}

// ------------------------------------------------------------------ Visão / Ollama / Celular
bool App::ModelHasVision(const std::string& name) {
    std::lock_guard<std::mutex> l(dataMtx_);
    for (auto& m : models_)
        if (m.name == name) return m.vision || !m.capsKnown;
    return Cfg().backend != LlmBackend::Ollama;
}

std::string App::BestVisionModel() {
    uint64_t ram = platform::TotalRamBytes();
    uint64_t budget = ram ? ram * 7 / 10 : 14ull << 30;
    std::lock_guard<std::mutex> l(dataMtx_);
    const ModelInfo* best = nullptr;
    for (auto& m : models_) {
        if (!m.vision || !m.capsKnown || m.size > budget) continue;
        if (!best || m.size > best->size) best = &m;
    }
    return best ? best->name : std::string();
}

void App::SetScreenShare(bool on) {
    if (on && !screen_.Active()) {
        StartScreenShare(CaptureSource{});
    } else if (!on && screen_.Active()) {
        screen_.Stop();
        Notify("Compartilhamento de tela encerrado.", 0);
    }
}

void App::StartScreenShare(const CaptureSource& src) {
    if (screen_.Active()) screen_.Stop();
    screen_.Start(std::clamp(Cfg().screenIntervalMs, 250, 10000), src);
    std::string what = src.kind == CaptureSource::Window ? "Janela \"" + src.name + "\"" : src.name.empty() ? std::string("Tela") : src.name;
    Notify(what + " compartilhada com o KB. Pergunte: \"o que tem na minha tela?\"", 1);
}

bool App::EnsureOllama(bool wait) {
    Config c = Cfg();
    if (c.backend != LlmBackend::Ollama) return false;
    std::string base = llm::BaseUrl(c);
    if (base.find("127.0.0.1") == std::string::npos && base.find("localhost") == std::string::npos) return false;
    if (http::Get(base + "/api/version", 800).ok()) return true;
    double now = NowSeconds();
    if (now - lastOllamaLaunch_ > 20.0) {
        std::string exe = platform::FindOllama();
        if (exe.empty()) return false;
        lastOllamaLaunch_ = now;
        if (!platform::LaunchDetached(exe, "serve")) return false;
        Notify("Iniciando o Ollama…", 0);
    }
    if (!wait) return false;
    for (int i = 0; i < 40 && !quit_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (http::Get(base + "/api/version", 800).ok()) { backendOnline_ = true; nextModelPoll_ = 0; return true; }
    }
    return false;
}

std::string App::MobileUrl() {
    Config c = Cfg();
    if (c.mobileMode == 0) {
        std::string u = tunnel_.Url();
        return u.empty() ? std::string() : u + "/?t=" + c.mobileToken;
    }
    auto ips = LanIps();
    return "http://" + (ips.empty() ? std::string("127.0.0.1") : ips.front()) + ":" + std::to_string(c.mobilePort) + "/?t=" + c.mobileToken;
}

std::string App::MobileState(int since) {
    using nlohmann::json;
    const char* st = "idle";
    switch (state_.load()) {
    case AssistState::Listening: st = "listening"; break;
    case AssistState::Transcribing: st = "transcribing"; break;
    case AssistState::Thinking: st = "thinking"; break;
    case AssistState::Speaking: st = "speaking"; break;
    default: st = TtsSpeaking() ? "speaking" : "idle";
    }
    json j = {{"state", st}, {"model", ActiveModel()}};
    std::lock_guard<std::mutex> l(msgMtx_);
    int v = msgVersion_.load();
    j["v"] = v;
    if (v != since) {
        json arr = json::array();
        size_t start = messages_.size() > 60 ? messages_.size() - 60 : 0;
        for (size_t i = start; i < messages_.size(); ++i) {
            auto& m = messages_[i];
            arr.push_back({{"id", m.id}, {"user", m.user}, {"text", m.text}, {"streaming", m.streaming}, {"error", m.error},
                           {"img", (int)m.pictures.size()}, {"thinking", m.thinking}});
        }
        j["messages"] = arr;
    }
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

void App::SetMobile(bool on) {
    Config c = Cfg();
    on = on || c.agendaPublic;   // a página de agendamento usa o mesmo servidor
    if (!on) { tunnel_.Stop(); mobile_.Stop(); return; }
    if (c.mobileMode == 0 || c.agendaPublic) tunnel_.Start(c.mobilePort);
    else tunnel_.Stop();
    if (mobile_.Running()) return;
    MobileHooks h;
    h.stateJson = [this](int since) { return MobileState(since); };
    h.send = [this](const std::string& text, const std::string& b64) {
        std::vector<PicturePtr> pics;
        if (!b64.empty()) {
            auto bytes = img::Base64Decode(b64);
            if (auto p = img::FromMemory(bytes.data(), bytes.size(), "Foto do celular")) pics.push_back(p);
        }
        SendText(text, false, pics);
    };
    h.listen = [this] { if (state_ == AssistState::Idle) StartListening(); };
    h.stop = [this] { Interrupt(); };
    h.publicRoute = [this](const std::string& m, const std::string& p, const std::string& t, const std::string& b, int* code, std::string* ct,
                           std::string* out) { return PublicRoute(m, p, t, b, code, ct, out); };
    std::string err;
    if (!mobile_.Start(c.mobilePort, c.mobileToken, h, &err)) Notify("Celular: " + err, 2);
}

// ------------------------------------------------------------------ Notificações
void App::Notify(const std::string& t, int kind) {
    std::lock_guard<std::mutex> l(toastMtx_);
    toasts_.push_back({nextToast_++, t, kind, NowSeconds()});
    sfx::Play(kind == 1 ? sfx::Success : kind == 2 ? sfx::Error : sfx::Notify, 0.8f);
    if (toasts_.size() > 5) toasts_.erase(toasts_.begin());
}

std::vector<Toast> App::Toasts() {
    std::lock_guard<std::mutex> l(toastMtx_);
    double now = NowSeconds();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return now - t.time > 6.0; }),
                  toasts_.end());
    return toasts_;
}

// ------------------------------------------------------------------ Configuração
Config App::Cfg() {
    std::lock_guard<std::mutex> l(cfgMtx_);
    Config c = cfg_;
    if (silent_) c.speakReplies = false;
    return c;
}

void App::ScheduleSave() { saveAt_ = NowSeconds() + 0.8; }

void App::ApplyConfig(const Config& c) {
    bool refresh = false, mic = false, stt = false, autostart = false, local = false, mobile = false;
    {
        std::lock_guard<std::mutex> l(cfgMtx_);
        Config& o = cfg_;
        if (o.hotkeyMods != c.hotkeyMods || o.hotkeyVk != c.hotkeyVk) hotkeyDirty_ = true;
        if (tts_) {
            if (o.ttsVoice != c.ttsVoice) tts_->SetVoice(c.ttsVoice);
            if (o.ttsRate != c.ttsRate) tts_->SetRate(c.ttsRate);
            if (o.ttsVolume != c.ttsVolume) tts_->SetVolume(c.ttsVolume);
        }
        refresh = o.backend != c.backend || o.ollamaUrl != c.ollamaUrl || o.openaiUrl != c.openaiUrl || o.openaiKey != c.openaiKey;
        local = c.backend == LlmBackend::LocalGguf &&
                (o.backend != c.backend || o.ggufPath != c.ggufPath || o.ggufGpuLayers != c.ggufGpuLayers ||
                 o.contextSize != c.contextSize || o.llamaServerPath != c.llamaServerPath);
        mic = o.micDevice != c.micDevice;
        mobile = o.mobileEnabled != c.mobileEnabled || (c.mobileEnabled && o.mobileMode != c.mobileMode) || o.agendaPublic != c.agendaPublic;
        stt = o.whisperModel != c.whisperModel;
        autostart = o.startWithWindows != c.startWithWindows || (c.startWithWindows && o.startMinimized != c.startMinimized);
        if (o.backend == LlmBackend::LocalGguf && c.backend != LlmBackend::LocalGguf) local = false;
        bool keepSpeak = cfg_.speakReplies;
        cfg_ = c;
        if (silent_) cfg_.speakReplies = keepSpeak;
        ScheduleSave();
    }
    sfx::SetEnabled(c.sfx);
    sfx::SetVolume(c.sfxVolume);
    sfx::SetHover(c.sfxHover);
    if (autostart && !platform::SetAutostart(c.startWithWindows, c.startMinimized))
        Notify("Não foi possível alterar a inicialização com o Windows.", 2);
    if (local) StartLocalServer();
    if (c.backend != LlmBackend::LocalGguf && LocalServerRunning()) StopLocalServer();
    if (refresh) { backendOnline_ = false; RefreshModels(); }
    if (mic) RestartMic();
    if (mobile) SetMobile(c.mobileEnabled);
    if (stt && !c.whisperModel.empty()) LoadStt(c.whisperModel);
}

// ------------------------------------------------------------------ Modelos
void App::RefreshModels() {
    if (modelsLoading_.exchange(true)) return;
    Spawn([this] {
        Config c = Cfg();
        std::string err;
        auto list = llm::ListModels(c, &err);
        if (!err.empty() && c.backend == LlmBackend::Ollama && EnsureOllama(true)) {
            err.clear();
            list = llm::ListModels(c, &err);
        }
        // remove modelos de embedding (não conversam)
        list.erase(std::remove_if(list.begin(), list.end(), [](const ModelInfo& m) {
            return text::Fold(m.name).find("embed") != std::string::npos;
        }), list.end());
        // capacidades (visão): reaproveita o que já sabemos e consulta só os modelos novos
        std::vector<ModelInfo> unknown;
        {
            std::lock_guard<std::mutex> l(dataMtx_);
            for (auto& m : list)
                for (auto& o : models_)
                    if (o.name == m.name && o.capsKnown) { m.vision = o.vision; m.tools = o.tools; m.capsKnown = true; }
        }
        for (auto& m : list) if (!m.capsKnown) unknown.push_back(m);
        llm::FillCapabilities(c, unknown);
        for (auto& m : list)
            for (auto& u : unknown)
                if (u.name == m.name) { m.vision = u.vision; m.tools = u.tools; m.capsKnown = u.capsKnown; }
        {
            std::lock_guard<std::mutex> l(dataMtx_);
            models_ = list;
            modelsError_ = err;
        }
        backendOnline_ = err.empty();
        if (c.backend == LlmBackend::LocalGguf) localReady_ = err.empty();
        modelsLoading_ = false;
    });
}

std::vector<ModelInfo> App::Models() {
    std::lock_guard<std::mutex> l(dataMtx_);
    return models_;
}

std::string App::ModelsError() {
    std::lock_guard<std::mutex> l(dataMtx_);
    return modelsError_;
}

std::string App::ActiveModel() {
    Config c = Cfg();
    if (c.backend == LlmBackend::LocalGguf) {
        std::string p = c.ggufPath;
        return p.substr(p.find_last_of("\\/") + 1);
    }
    std::lock_guard<std::mutex> l(dataMtx_);
    if (!c.model.empty()) {
        if (models_.empty()) return c.model;
        for (auto& m : models_) if (m.name == c.model) return c.model;
    }
    if (models_.empty()) return {};
    // Escolhe automaticamente: o maior modelo que cabe confortavelmente na RAM (alvo ~13 GB).
    uint64_t ram = platform::TotalRamBytes();
    uint64_t budget = ram ? ram * 7 / 10 : 14ull << 30;
    const ModelInfo* best = nullptr;
    for (auto& m : models_) {
        if (m.size == 0 || m.size > budget) continue;
        if (!best || m.size > best->size) best = &m;
    }
    if (!best) best = &models_.front();
    return best->name;
}

void App::DetectServers() {
    if (detecting_.exchange(true)) return;
    Spawn([this] {
        auto s = llm::DetectServers();
        {
            std::lock_guard<std::mutex> l(dataMtx_);
            servers_ = s;
        }
        detecting_ = false;
        Notify(s.empty() ? "Nenhum servidor de IA local encontrado." : "Encontrei " + std::to_string(s.size()) + " servidor(es) de IA.", s.empty() ? 2 : 1);
    });
}

std::vector<ServerInfo> App::Servers() {
    std::lock_guard<std::mutex> l(dataMtx_);
    return servers_;
}

void App::ScanLocalFiles() {
    if (scanning_.exchange(true)) return;
    Spawn([this] {
        auto g = scan::FindGgufModels();
        auto w = scan::FindWhisperModels();
        std::string ls = scan::FindLlamaServer();
        {
            std::lock_guard<std::mutex> l(dataMtx_);
            ggufFiles_ = g;
            whisperFiles_ = w;
        }
        if (!ls.empty()) {
            std::lock_guard<std::mutex> l(cfgMtx_);
            if (cfg_.llamaServerPath.empty() || !platform::FileExists(cfg_.llamaServerPath)) {
                cfg_.llamaServerPath = ls;
                ScheduleSave();
            }
        }
        scanning_ = false;
    });
}

std::vector<LocalFile> App::GgufFiles() { std::lock_guard<std::mutex> l(dataMtx_); return ggufFiles_; }
std::vector<LocalFile> App::WhisperFiles() { std::lock_guard<std::mutex> l(dataMtx_); return whisperFiles_; }

std::vector<std::shared_ptr<Job>> App::Jobs() {
    std::lock_guard<std::mutex> l(dataMtx_);
    double now = NowSeconds();
    (void)now;
    return jobs_;
}

std::shared_ptr<Job> App::PullOllamaModel(const std::string& name) {
    auto job = std::make_shared<Job>();
    job->title = "Baixando " + name;
    job->SetStatus("Conectando ao Ollama...");
    {
        std::lock_guard<std::mutex> l(dataMtx_);
        jobs_.push_back(job);
    }
    Spawn([this, job, name] {
        std::string err;
        if (!EnsureOllama(true)) {
            bool installed = !platform::FindOllama().empty();
            err = installed ? "o Ollama não respondeu. Abra o Ollama e tente de novo."
                            : "o Ollama não está instalado. Baixe em ollama.com e tente de novo.";
            job->failed = true;
            job->SetStatus(err);
            Notify("Falha ao baixar " + name + ": " + err, 2);
            if (!installed) platform::OpenUrl("https://ollama.com/download/windows");
            job->finished = true;
            return;
        }
        bool ok = llm::OllamaPull(Cfg().ollamaUrl, name, [&](const std::string& st, uint64_t done, uint64_t total) {
            if (total) { job->total = total; job->done = done; }
            std::string s = st;
            if (text::StartsWith(s, "pulling")) s = "Baixando camadas";
            else if (s == "verifying sha256 digest") s = "Verificando";
            else if (s == "writing manifest") s = "Finalizando";
            else if (s == "success") s = "Concluído";
            job->SetStatus(s);
        }, &err, &job->cancel);
        job->failed = !ok;
        if (ok) {
            {
                std::lock_guard<std::mutex> l(cfgMtx_);
                cfg_.backend = LlmBackend::Ollama;
                cfg_.model = name;
                ScheduleSave();
            }
            Notify("Modelo " + name + " pronto!", 1);
            nextModelPoll_ = 0;
        } else {
            job->SetStatus(err.empty() ? "Falhou" : err);
            Notify("Falha ao baixar " + name + (err.empty() ? "" : ": " + err), 2);
        }
        job->finished = true;
    });
    return job;
}

std::shared_ptr<Job> App::DownloadWhisper(const char* id) {
    auto job = std::make_shared<Job>();
    std::string sid = id;
    job->title = "Modelo de voz " + sid;
    job->SetStatus("Baixando...");
    {
        std::lock_guard<std::mutex> l(dataMtx_);
        jobs_.push_back(job);
    }
    Spawn([this, job, sid] {
        std::string dest = config::ModelsDir() + "\\ggml-" + sid + ".bin";
        auto r = http::Download(stt::DownloadUrl(sid.c_str()), dest, [&](uint64_t d, uint64_t t) {
            job->done = d;
            job->total = t;
        }, &job->cancel);
        if (r.ok() && r.error.empty()) {
            job->SetStatus("Concluído");
            {
                std::lock_guard<std::mutex> l(cfgMtx_);
                cfg_.whisperModel = dest;
                ScheduleSave();
            }
            LoadStt(dest);
        } else {
            job->failed = true;
            job->SetStatus(r.error);
            Notify("Falha no download do modelo de voz: " + r.error, 2);
        }
        job->finished = true;
    });
    return job;
}

// ------------------------------------------------------------------ GGUF local
void App::StartLocalServer() {
    Config c = Cfg();
    std::string exe = c.llamaServerPath;
    if (!platform::FileExists(exe)) exe = scan::FindLlamaServer();
    if (!platform::FileExists(exe)) {
        Notify("llama-server.exe não encontrado. Indique o caminho em Modelos > GGUF.", 2);
        return;
    }
    if (!platform::FileExists(c.ggufPath)) {
        Notify("Escolha um arquivo .gguf em Modelos.", 2);
        return;
    }
    std::string args = "-m \"" + c.ggufPath + "\" --host 127.0.0.1 --port " + std::to_string(llm::kLocalGgufPort) +
                       " -ngl " + std::to_string(c.ggufGpuLayers) + " -c " + std::to_string(c.contextSize);
    localReady_ = false;
    bool ok;
    {
        std::lock_guard<std::mutex> l(procMtx_);
        ok = llamaProc_.Start(exe, args);
    }
    if (!ok) Notify("Falha ao iniciar o llama-server.", 2);
    else Notify("Carregando modelo local... (arquivos grandes levam alguns segundos)", 0);
    nextModelPoll_ = 0;
}

void App::StopLocalServer() {
    std::lock_guard<std::mutex> l(procMtx_);
    llamaProc_.Stop();
    localReady_ = false;
}

bool App::EnsureLocalServer(std::string* err) {
    if (!LocalServerRunning()) StartLocalServer();
    if (!LocalServerRunning()) {
        *err = "O servidor local (llama-server) não está rodando. Verifique o caminho do llama-server.exe e do arquivo .gguf na aba Modelos.";
        return false;
    }
    std::string base = "http://127.0.0.1:" + std::to_string(llm::kLocalGgufPort);
    double start = NowSeconds();
    while (!quit_ && !cancelLlm_ && NowSeconds() - start < 240) {
        auto r = http::Get(base + "/health", 1500);
        if (r.ok()) { localReady_ = true; return true; }
        if (!LocalServerRunning()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    *err = "O modelo local não ficou pronto a tempo. Verifique se há memória suficiente para o arquivo .gguf.";
    return false;
}

// ------------------------------------------------------------------ Voz (modelos)
// Modelo de voz incompleto (download interrompido) vira ".part" para o download continuar de onde parou.
static bool ValidateWhisperFile(const std::string& path) {
    unsigned long long expected = stt::ExpectedSize(path);
    if (!expected || !platform::FileExists(path)) return platform::FileExists(path);
    if (platform::FileSize(path) == expected) return true;
    std::wstring w = text::Widen(path);
    MoveFileExW(w.c_str(), (w + L".part").c_str(), MOVEFILE_REPLACE_EXISTING);
    return false;
}

void App::LoadStt(const std::string& path) {
    if (!ValidateWhisperFile(path)) {
        std::string name = path.substr(path.find_last_of("\\/") + 1);
        Notify("O modelo de voz " + name + " está incompleto (download interrompido). Clique em Baixar em Modelos > Voz para continuar de onde parou.", 2);
        return;
    }
    if (sttLoading_.exchange(true)) return;
    Spawn([this, path] {
        std::string err;
        if (stt_.Load(path, &err)) Notify("Reconhecimento de voz pronto.", 1);
        else Notify(err, 2);
        sttLoading_ = false;
    });
}

void App::AutoPickWhisper() {
    Config c = Cfg();
    // valida todos os modelos baixados (corrige os que ficaram truncados)
    for (auto& o : stt::Options()) ValidateWhisperFile(config::ModelsDir() + "\\ggml-" + o.id + ".bin");
    if (platform::FileExists(c.whisperModel)) { LoadStt(c.whisperModel); return; }
    auto files = scan::FindWhisperModels();
    if (files.empty()) {
        Notify("Baixe um modelo de voz (Whisper) na aba Modelos para falar comigo.", 0);
        return;
    }
    // prefere "small", depois "base", depois o maior
    const LocalFile* pick = &files.front();
    for (auto& f : files) if (f.name.find("small") != std::string::npos && f.name.find(".en") == std::string::npos) { pick = &f; break; }
    {
        std::lock_guard<std::mutex> l(cfgMtx_);
        cfg_.whisperModel = pick->path;
        ScheduleSave();
    }
    LoadStt(pick->path);
}

void App::TestVoice() {
    if (!tts_) return;
    tts_->StopAll();
    tts_->Speak("Olá! Esta é a minha voz. Como posso ajudar você hoje?");
}

std::vector<std::string> App::MicDevices() { return AudioCapture::ListDevices(); }

void App::RestartMic() {
    std::string err;
    if (!mic_.Start(Cfg().micDevice, &err)) Notify(err, 2);
}
