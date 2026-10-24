#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// Desempenho do PC: CPU, RAM e GPU (NVIDIA via NVML; outras placas via contadores do Windows).
struct PerfSnapshot {
    float cpu = 0;                 // %
    float ram = 0;                 // %
    double ramUsedGB = 0, ramTotalGB = 0;
    bool hasGpu = false;
    std::string gpuName;
    float gpu = 0;                 // % de uso
    double vramUsedGB = 0, vramTotalGB = 0;
    int gpuTempC = -1;
    float gpuPowerW = -1;
    static constexpr int kHist = 60;
    float cpuHist[kHist] = {}, gpuHist[kHist] = {};
    int histPos = 0;
};

class PerfMonitor {
public:
    ~PerfMonitor();
    void Start();
    void Stop();
    PerfSnapshot Get();

private:
    void Run();
    std::thread th_;
    std::atomic<bool> quit_{false};
    std::mutex mtx_;
    PerfSnapshot snap_;
};
