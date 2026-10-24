#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include "miniaudio.h"

#include "audio/audio_capture.h"
#include <algorithm>
#include <cmath>
#include <cstring>

struct AudioCapture::Impl {
    ma_context ctx{};
    bool ctxOk = false;
    ma_device device{};
    bool devOk = false;
};

static void DataCallback(ma_device* dev, void*, const void* input, ma_uint32 frames) {
    auto* self = (AudioCapture*)dev->pUserData;
    if (input) self->OnSamples((const float*)input, frames);
}

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}
AudioCapture::~AudioCapture() { Stop(); }

std::vector<std::string> AudioCapture::ListDevices() {
    std::vector<std::string> out;
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return out;
    ma_device_info *pb, *cap;
    ma_uint32 npb, ncap;
    if (ma_context_get_devices(&ctx, &pb, &npb, &cap, &ncap) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < ncap; ++i) out.push_back(cap[i].name);
    ma_context_uninit(&ctx);
    return out;
}

bool AudioCapture::Start(const std::string& deviceName, std::string* err) {
    Stop();
    if (ma_context_init(nullptr, 0, nullptr, &impl_->ctx) != MA_SUCCESS) {
        if (err) *err = "Falha ao iniciar o áudio";
        return false;
    }
    impl_->ctxOk = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = kSampleRate;
    cfg.periodSizeInMilliseconds = 20;
    cfg.dataCallback = DataCallback;
    cfg.pUserData = this;

    ma_device_id chosen{};
    if (!deviceName.empty()) {
        ma_device_info *pb, *cap;
        ma_uint32 npb, ncap;
        if (ma_context_get_devices(&impl_->ctx, &pb, &npb, &cap, &ncap) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < ncap; ++i)
                if (deviceName == cap[i].name) { chosen = cap[i].id; cfg.capture.pDeviceID = &chosen; }
        }
    }
    if (ma_device_init(&impl_->ctx, &cfg, &impl_->device) != MA_SUCCESS) {
        if (err) *err = "Nenhum microfone disponível";
        Stop();
        return false;
    }
    impl_->devOk = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        if (err) *err = "Não foi possível abrir o microfone";
        Stop();
        return false;
    }
    running_ = true;
    return true;
}

void AudioCapture::Stop() {
    running_ = false;
    if (impl_->devOk) { ma_device_uninit(&impl_->device); impl_->devOk = false; }
    if (impl_->ctxOk) { ma_context_uninit(&impl_->ctx); impl_->ctxOk = false; }
}

void AudioCapture::OnSamples(const float* in, unsigned frames) {
    double sum = 0;
    for (unsigned i = 0; i < frames; ++i) sum += (double)in[i] * in[i];
    float rms = frames ? (float)std::sqrt(sum / frames) : 0.f;
    float db = 20.f * std::log10(std::max(rms, 1e-6f));
    float norm = std::clamp((db + 58.f) / 46.f, 0.f, 1.f);
    float prev = level_.load();
    float k = norm > prev ? 0.55f : 0.12f; // ataque rápido, queda suave
    float lv = prev + (norm - prev) * k;
    level_.store(lv);

    std::lock_guard<std::mutex> lock(mtx_);
    hist_[histPos_] = lv;
    histPos_ = (histPos_ + 1) % 64;
    queue_.insert(queue_.end(), in, in + frames);
    const size_t maxQ = kSampleRate * 30;
    if (queue_.size() > maxQ) queue_.erase(queue_.begin(), queue_.begin() + (queue_.size() - maxQ));
}

void AudioCapture::Drain(std::vector<float>& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    out.swap(queue_);
    queue_.clear();
}

void AudioCapture::LevelHistory(float* out, int n) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (int i = 0; i < n; ++i) out[i] = hist_[(histPos_ - n + i + 64 * 4) % 64];
}

// ------------------------------------------------------------------ VAD
void Vad::Reset() {
    frameBuf_.clear();
    preroll_.clear();
    utt_.clear();
    inSpeech_ = false;
    loudFrames_ = silenceFrames_ = 0;
}

Vad::Event Vad::Process(const float* s, size_t n) {
    Event ev = Event::None;
    frameBuf_.insert(frameBuf_.end(), s, s + n);
    size_t off = 0;
    while (frameBuf_.size() - off >= (size_t)kFrame) {
        const float* f = frameBuf_.data() + off;
        off += kFrame;
        double sum = 0;
        for (int i = 0; i < kFrame; ++i) sum += (double)f[i] * f[i];
        float e = (float)std::sqrt(sum / kFrame);

        float thr = std::max(0.0045f, floor_ * 3.0f) / std::max(0.2f, sensitivity);
        bool loud = e > thr;

        if (!inSpeech_) {
            // piso de ruído acompanha o ambiente quando não há fala
            floor_ = loud ? floor_ : floor_ * 0.97f + e * 0.03f;
            preroll_.insert(preroll_.end(), f, f + kFrame);
            const size_t maxPre = kSampleRate * 3 / 10; // 300 ms
            if (preroll_.size() > maxPre) preroll_.erase(preroll_.begin(), preroll_.begin() + (preroll_.size() - maxPre));
            loudFrames_ = loud ? loudFrames_ + 1 : std::max(0, loudFrames_ - 1);
            if (loudFrames_ >= 4) {
                inSpeech_ = true;
                silenceFrames_ = 0;
                utt_ = preroll_;
                preroll_.clear();
                ev = Event::SpeechStart;
            }
        } else {
            utt_.insert(utt_.end(), f, f + kFrame);
            bool quiet = e < thr * 0.75f;
            silenceFrames_ = quiet ? silenceFrames_ + 1 : 0;
            if (SilenceSeconds() >= endSilenceSec || SpeechSeconds() >= maxSpeechSec) {
                inSpeech_ = false;
                loudFrames_ = 0;
                ev = Event::SpeechEnd;
                break;
            }
        }
    }
    frameBuf_.erase(frameBuf_.begin(), frameBuf_.begin() + off);
    return ev;
}
