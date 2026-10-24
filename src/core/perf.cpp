#include "core/perf.h"
#include "core/text.h"
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <chrono>
#include <vector>
#include <algorithm>

namespace {
// ---- NVML (carregado dinamicamente; existe em qualquer PC com driver NVIDIA)
struct NvUtil { unsigned int gpu, memory; };
struct NvMem { unsigned long long total, free, used; };
typedef void* NvDev;
struct Nvml {
    HMODULE dll = nullptr;
    int (*init)() = nullptr;
    int (*handle)(unsigned int, NvDev*) = nullptr;
    int (*name)(NvDev, char*, unsigned int) = nullptr;
    int (*util)(NvDev, NvUtil*) = nullptr;
    int (*mem)(NvDev, NvMem*) = nullptr;
    int (*temp)(NvDev, int, unsigned int*) = nullptr;
    int (*power)(NvDev, unsigned int*) = nullptr;
    NvDev dev = nullptr;
    bool ok = false;

    void Load() {
        dll = LoadLibraryW(L"nvml.dll");
        if (!dll) dll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        if (!dll) return;
        init = (int (*)())GetProcAddress(dll, "nvmlInit_v2");
        handle = (int (*)(unsigned int, NvDev*))GetProcAddress(dll, "nvmlDeviceGetHandleByIndex_v2");
        name = (int (*)(NvDev, char*, unsigned int))GetProcAddress(dll, "nvmlDeviceGetName");
        util = (int (*)(NvDev, NvUtil*))GetProcAddress(dll, "nvmlDeviceGetUtilizationRates");
        mem = (int (*)(NvDev, NvMem*))GetProcAddress(dll, "nvmlDeviceGetMemoryInfo");
        temp = (int (*)(NvDev, int, unsigned int*))GetProcAddress(dll, "nvmlDeviceGetTemperature");
        power = (int (*)(NvDev, unsigned int*))GetProcAddress(dll, "nvmlDeviceGetPowerUsage");
        if (!init || !handle || !util || !mem || init() != 0 || handle(0, &dev) != 0) return;
        ok = true;
    }
};

// ---- PDH (qualquer GPU no Windows 10+): soma do uso dos motores 3D/Compute
struct PdhGpu {
    PDH_HQUERY q = nullptr;
    PDH_HCOUNTER util = nullptr, mem = nullptr;
    bool ok = false;
    void Load() {
        if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return;
        bool a = PdhAddEnglishCounterW(q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &util) == ERROR_SUCCESS;
        PdhAddEnglishCounterW(q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &mem);
        ok = a;
        PdhCollectQueryData(q);
    }
    static double SumArray(PDH_HCOUNTER c, bool maxPerEngineType, bool onlyEngines) {
        DWORD size = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &size, &count, nullptr) != PDH_MORE_DATA) return 0;
        std::vector<unsigned char> buf(size);
        auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) return 0;
        double s3d = 0, scompute = 0, total = 0;
        for (DWORD i = 0; i < count; ++i) {
            double v = items[i].FmtValue.doubleValue;
            if (!onlyEngines) { total += v; continue; }
            std::wstring n = items[i].szName;
            if (n.find(L"engtype_3D") != std::wstring::npos) s3d += v;
            else if (n.find(L"engtype_Compute") != std::wstring::npos) scompute += v;
        }
        (void)maxPerEngineType;
        return onlyEngines ? std::max(s3d, scompute) : total;
    }
};
} // namespace

PerfMonitor::~PerfMonitor() { Stop(); }

void PerfMonitor::Start() {
    if (th_.joinable()) return;
    quit_ = false;
    th_ = std::thread([this] { Run(); });
}

void PerfMonitor::Stop() {
    quit_ = true;
    if (th_.joinable()) th_.join();
}

PerfSnapshot PerfMonitor::Get() {
    std::lock_guard<std::mutex> l(mtx_);
    return snap_;
}

void PerfMonitor::Run() {
    Nvml nv;
    nv.Load();
    PdhGpu pdh;
    if (!nv.ok) pdh.Load();
    std::string gpuName;
    if (nv.ok && nv.name) {
        char buf[96] = {};
        if (nv.name(nv.dev, buf, sizeof(buf)) == 0) gpuName = buf;
    }
    if (gpuName.empty()) {
        DISPLAY_DEVICEW dd{sizeof(dd)};
        if (EnumDisplayDevicesW(nullptr, 0, &dd, 0)) gpuName = text::Narrow(dd.DeviceString);
    }
    FILETIME pi{}, pk{}, pu{};
    GetSystemTimes(&pi, &pk, &pu);
    auto u64 = [](FILETIME f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };

    while (!quit_) {
        for (int i = 0; i < 10 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        PerfSnapshot s;
        {
            std::lock_guard<std::mutex> l(mtx_);
            s = snap_;
        }
        FILETIME ii, kk, uu;
        GetSystemTimes(&ii, &kk, &uu);
        unsigned long long idle = u64(ii) - u64(pi), kern = u64(kk) - u64(pk), user = u64(uu) - u64(pu);
        pi = ii; pk = kk; pu = uu;
        unsigned long long tot = kern + user;
        s.cpu = tot ? (float)(100.0 * (tot - idle) / tot) : 0.f;
        MEMORYSTATUSEX m{sizeof(m)};
        GlobalMemoryStatusEx(&m);
        s.ram = (float)m.dwMemoryLoad;
        s.ramTotalGB = m.ullTotalPhys / 1073741824.0;
        s.ramUsedGB = (m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0;

        s.gpuName = gpuName;
        if (nv.ok) {
            NvUtil u{};
            NvMem mm{};
            s.hasGpu = true;
            if (nv.util(nv.dev, &u) == 0) s.gpu = (float)u.gpu;
            if (nv.mem(nv.dev, &mm) == 0) { s.vramUsedGB = mm.used / 1073741824.0; s.vramTotalGB = mm.total / 1073741824.0; }
            unsigned int t = 0, p = 0;
            if (nv.temp && nv.temp(nv.dev, 0, &t) == 0) s.gpuTempC = (int)t;
            if (nv.power && nv.power(nv.dev, &p) == 0) s.gpuPowerW = p / 1000.f;
        } else if (pdh.ok && PdhCollectQueryData(pdh.q) == ERROR_SUCCESS) {
            s.hasGpu = true;
            s.gpu = (float)std::min(100.0, PdhGpu::SumArray(pdh.util, true, true));
            if (pdh.mem) s.vramUsedGB = PdhGpu::SumArray(pdh.mem, false, false) / 1073741824.0;
        }
        s.cpuHist[s.histPos] = s.cpu;
        s.gpuHist[s.histPos] = s.gpu;
        s.histPos = (s.histPos + 1) % PerfSnapshot::kHist;
        std::lock_guard<std::mutex> l(mtx_);
        snap_ = s;
    }
    if (pdh.q) PdhCloseQuery(pdh.q);
}
