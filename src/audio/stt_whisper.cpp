#include "audio/stt_whisper.h"
#include "core/text.h"
#include "whisper.h"
#include <algorithm>
#include <thread>

static void QuietLog(enum ggml_log_level, const char*, void*) {}

SpeechToText::~SpeechToText() { Unload(); }

bool SpeechToText::Load(const std::string& modelPath, std::string* err) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (ctx_) { whisper_free(ctx_); ctx_ = nullptr; }
    whisper_log_set(QuietLog, nullptr);
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = true;
    ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), cp);
    if (!ctx_) {
        if (err) *err = "Não foi possível carregar o modelo de voz: " + modelPath;
        return false;
    }
    path_ = modelPath;
    return true;
}

void SpeechToText::Unload() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (ctx_) { whisper_free(ctx_); ctx_ = nullptr; }
}

std::string SpeechToText::Transcribe(const std::vector<float>& pcm, const std::string& lang, int threads,
                                     const std::string& hint) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!ctx_ || pcm.size() < 16000 / 4) return {};

    // whisper precisa de pelo menos ~1 s; completa com silêncio.
    std::vector<float> buf = pcm;
    if (buf.size() < 16000 + 1600) buf.resize(16000 + 1600, 0.f);

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    int hw = (int)std::thread::hardware_concurrency();
    p.n_threads = threads > 0 ? threads : std::clamp(hw - 2, 2, 8);
    p.language = lang.empty() ? "pt" : lang.c_str();
    p.translate = false;
    p.no_context = true;
    p.no_timestamps = true;
    p.single_segment = false;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_special = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.suppress_nst = true;
    p.temperature = 0.0f;
    p.initial_prompt = hint.empty() ? nullptr : hint.c_str();

    if (whisper_full(ctx_, p, buf.data(), (int)buf.size()) != 0) return {};
    std::string out;
    int n = whisper_full_n_segments(ctx_);
    for (int i = 0; i < n; ++i) out += whisper_full_get_segment_text(ctx_, i);
    out = text::Trim(out);
    if (stt::IsHallucination(out)) return {};
    return out;
}

namespace stt {

const std::vector<WhisperModelOption>& Options() {
    static const std::vector<WhisperModelOption> v = {
        {"base", "Base (rápido)", "142 MB", 147951465ull},
        {"small", "Small (recomendado)", "466 MB", 487601967ull},
        {"medium", "Medium (mais preciso)", "1.5 GB", 1533763059ull},
        {"large-v3-turbo", "Large v3 Turbo (o melhor)", "1.6 GB", 1624555275ull},
    };
    return v;
}

std::string DownloadUrl(const char* id) {
    return std::string("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-") + id + ".bin";
}

unsigned long long ExpectedSize(const std::string& path) {
    std::string name = path.substr(path.find_last_of("\\/") + 1);
    for (auto& o : Options())
        if (name == std::string("ggml-") + o.id + ".bin") return o.bytes;
    return 0;
}

bool IsHallucination(const std::string& t) {
    // Frases que o whisper costuma "inventar" em silêncio, principalmente em português.
    static const char* bad[] = {"amara.org", "legendas pela comunidade", "legenda adriana", "obrigado por assistir",
                                "inscreva-se", "subtitles by", "[música]", "(música)", "[silêncio]", "♪"};
    std::string f = text::Fold(t);
    for (auto* b : bad)
        if (f.find(text::Fold(b)) != std::string::npos) return true;
    // só pontuação
    bool letters = false;
    for (char c : f) if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { letters = true; break; }
    return !letters;
}

} // namespace stt
