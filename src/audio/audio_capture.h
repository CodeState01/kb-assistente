#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

constexpr int kSampleRate = 16000;

// Captura do microfone (16 kHz mono float) com fila para o processamento de voz.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    bool Start(const std::string& deviceName, std::string* err);
    void Stop();
    bool Running() const { return running_; }

    // Copia (e esvazia) as amostras novas.
    void Drain(std::vector<float>& out);

    // Nível atual 0..1 (suavizado) para animações.
    float Level() const { return level_.load(); }
    // Histórico curto de níveis para o equalizador circular.
    void LevelHistory(float* out, int n) const;

    static std::vector<std::string> ListDevices();

    void OnSamples(const float* in, unsigned frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::atomic<float> level_{0.f};
    mutable std::mutex mtx_;
    std::vector<float> queue_;
    float hist_[64] = {};
    int histPos_ = 0;
};

// Detector de atividade de voz por energia, com piso de ruído adaptativo.
class Vad {
public:
    enum class Event { None, SpeechStart, SpeechEnd };
    void Reset();
    // Processa amostras; retorna evento. Durante a fala, acumula em Utterance().
    Event Process(const float* s, size_t n);
    const std::vector<float>& Utterance() const { return utt_; }
    bool InSpeech() const { return inSpeech_; }
    double SilenceSeconds() const { return silenceFrames_ * kFrame / (double)kSampleRate; }
    double SpeechSeconds() const { return utt_.size() / (double)kSampleRate; }

    float sensitivity = 1.0f;     // >1 = mais sensível
    float endSilenceSec = 0.85f;
    float maxSpeechSec = 25.f;

private:
    static constexpr int kFrame = 320; // 20 ms
    std::vector<float> frameBuf_, preroll_, utt_;
    float floor_ = 0.004f;
    bool inSpeech_ = false;
    int loudFrames_ = 0, silenceFrames_ = 0;
};
