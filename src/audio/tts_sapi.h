#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct VoiceInfo {
    std::string name;
    std::string language;  // "pt-BR", "pt-PT", "en-US"...
    bool portuguese = false;
};

// Texto -> fala com SAPI (inclui vozes OneCore do Windows 10/11, ex.: "Microsoft Maria").
class TextToSpeech {
public:
    TextToSpeech();
    ~TextToSpeech();

    void Speak(const std::string& utf8);
    void StopAll();
    bool Speaking() const { return speaking_.load(); }

    // Abertura de "boca" 0..1 vinda dos visemas, para animar o orb.
    float Mouth() const { return mouth_.load(); }
    double LastEventTime() const { return lastEvent_.load(); }

    void SetVoice(const std::string& name);
    void SetRate(int rate);
    void SetVolume(int vol);
    std::vector<VoiceInfo> Voices();
    std::string CurrentVoice();

private:
    void Run();
    std::thread th_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::wstring> queue_;
    std::atomic<bool> quit_{false}, stop_{false}, speaking_{false};
    std::atomic<float> mouth_{0.f};
    std::atomic<double> lastEvent_{0.0};
    std::string wantVoice_;
    int wantRate_ = 1, wantVolume_ = 100;
    bool dirty_ = true;
    std::vector<VoiceInfo> voices_;
    std::string current_;
};
