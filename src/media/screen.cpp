#include "media/screen.h"
#include "core/text.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <algorithm>
#include <chrono>
#include <vector>

namespace {
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Captura via Desktop Duplication (vê também jogos/janelas aceleradas por GPU).
struct Dupl {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGIOutputDuplication* dup = nullptr;
    ID3D11Texture2D* staging = nullptr;
    int w = 0, h = 0;
    std::vector<uint8_t> last;   // último quadro BGRA (reaproveitado quando a tela não muda)

    void Reset() {
        SafeRelease(staging);
        SafeRelease(dup);
        SafeRelease(ctx);
        SafeRelease(dev);
        w = h = 0;
    }

    HMONITOR target = nullptr; // nullptr = monitor principal

    bool Init() {
        Reset();
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return false;
        // Em notebooks híbridos o monitor pertence a um adaptador específico: procura o dono do monitor principal.
        IDXGIAdapter1* adapter = nullptr;
        IDXGIOutput* output = nullptr;
        for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            IDXGIOutput* o = nullptr;
            for (UINT i = 0; adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_OUTPUT_DESC d;
                o->GetDesc(&d);
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(d.Monitor, &mi);
                if (d.AttachedToDesktop && (target ? d.Monitor == target : (mi.dwFlags & MONITORINFOF_PRIMARY) != 0)) { output = o; break; }
                o->Release();
            }
            if (!output) SafeRelease(adapter);
        }
        factory->Release();
        if (!output) return false;
        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        adapter->Release();
        IDXGIOutput1* o1 = nullptr;
        if (SUCCEEDED(hr)) hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&o1);
        output->Release();
        if (SUCCEEDED(hr)) hr = o1->DuplicateOutput(dev, &dup);
        SafeRelease(o1);
        if (FAILED(hr)) { Reset(); return false; }
        DXGI_OUTDUPL_DESC dd;
        dup->GetDesc(&dd);
        w = dd.ModeDesc.Width;
        h = dd.ModeDesc.Height;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &staging))) { Reset(); return false; }
        return true;
    }

    // Retorna true se há um quadro em 'last'.
    bool Grab() {
        if (!dup && !Init()) return false;
        DXGI_OUTDUPL_FRAME_INFO info;
        IDXGIResource* res = nullptr;
        HRESULT hr = dup->AcquireNextFrame(250, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return !last.empty();
        if (FAILED(hr)) { Reset(); return false; }
        ID3D11Texture2D* tex = nullptr;
        bool ok = false;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) {
            ctx->CopyResource(staging, tex);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                last.resize((size_t)w * h * 4);
                for (int y = 0; y < h; ++y) memcpy(&last[(size_t)y * w * 4], (uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w * 4);
                ctx->Unmap(staging, 0);
                ok = true;
            }
            tex->Release();
        }
        res->Release();
        dup->ReleaseFrame();
        return ok || !last.empty();
    }
};

// Captura um retângulo da área de trabalho (coordenadas virtuais) via GDI.
bool GrabGdi(const RECT& r, std::vector<uint8_t>& out, int& w, int& h) {
    w = r.right - r.left;
    h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
        if (ok) {
            out.assign((uint8_t*)bits, (uint8_t*)bits + (size_t)w * h * 4);
            for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

RECT MonitorRect(HMONITOR m) {
    MONITORINFO mi{sizeof(mi)};
    if (m && GetMonitorInfoW(m, &mi)) return mi.rcMonitor;
    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

// Captura uma janela, mesmo coberta por outras (PrintWindow com conteúdo renderizado pela GPU).
bool GrabWindow(HWND hwnd, std::vector<uint8_t>& out, int& w, int& h) {
    if (!IsWindow(hwnd) || IsIconic(hwnd)) return false;
    RECT wr, fr;
    if (!GetWindowRect(hwnd, &wr)) return false;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr)))) fr = wr;
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    if (ww <= 0 || wh <= 0) return false;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = ww;
    bi.bmiHeader.biHeight = -wh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = PrintWindow(hwnd, mem, 2 /*PW_RENDERFULLCONTENT*/) != FALSE;
        if (ok) {
            // recorta a borda invisível de redimensionamento
            int ox = std::clamp((int)(fr.left - wr.left), 0, ww - 1), oy = std::clamp((int)(fr.top - wr.top), 0, wh - 1);
            w = std::clamp((int)(fr.right - fr.left), 1, ww - ox);
            h = std::clamp((int)(fr.bottom - fr.top), 1, wh - oy);
            out.resize((size_t)w * h * 4);
            for (int y = 0; y < h; ++y) memcpy(&out[(size_t)y * w * 4], (uint8_t*)bits + ((size_t)(y + oy) * ww + ox) * 4, (size_t)w * 4);
            for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

std::string ExeName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    std::wstring s;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) s.assign(buf, n);
    CloseHandle(p);
    s = s.substr(s.find_last_of(L'\\') + 1);
    if (s.size() > 4 && _wcsicmp(s.c_str() + s.size() - 4, L".exe") == 0) s.resize(s.size() - 4);
    return text::Narrow(s);
}

BOOL CALLBACK MonitorProc(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    ((std::vector<HMONITOR>*)lp)->push_back(m);
    return TRUE;
}

BOOL CALLBACK WindowProc(HWND hwnd, LPARAM lp) {
    auto* out = (std::vector<HWND>*)lp;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;  // as janelas do próprio KB
    wchar_t cls[64];
    GetClassNameW(hwnd, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return TRUE;
    RECT r;
    GetWindowRect(hwnd, &r);
    if (!IsIconic(hwnd) && (r.right - r.left < 120 || r.bottom - r.top < 80)) return TRUE;
    out->push_back(hwnd);
    return TRUE;
}
} // namespace

namespace screencap {
std::vector<CaptureSource> List(bool thumbs) {
    std::vector<CaptureSource> out;
    std::vector<uint8_t> buf;
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(nullptr, nullptr, MonitorProc, (LPARAM)&mons);
    // principal primeiro
    std::stable_sort(mons.begin(), mons.end(), [](HMONITOR a, HMONITOR b) {
        MONITORINFO ma{sizeof(ma)}, mb{sizeof(mb)};
        GetMonitorInfoW(a, &ma);
        GetMonitorInfoW(b, &mb);
        return (ma.dwFlags & MONITORINFOF_PRIMARY) > (mb.dwFlags & MONITORINFOF_PRIMARY);
    });
    for (size_t i = 0; i < mons.size(); ++i) {
        CaptureSource s;
        s.kind = CaptureSource::Monitor;
        s.handle = mons[i];
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mons[i], &mi);
        s.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        s.w = mi.rcMonitor.right - mi.rcMonitor.left;
        s.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        s.name = "Tela " + std::to_string(i + 1) + (s.primary ? " (principal)" : "");
        int w, h;
        if (thumbs && GrabGdi(mi.rcMonitor, buf, w, h)) s.thumb = img::FromPixels(buf.data(), w, h, w * 4, true, s.name, 400);
        out.push_back(std::move(s));
    }
    std::vector<HWND> wins;
    EnumWindows(WindowProc, (LPARAM)&wins);
    for (HWND hw : wins) {
        CaptureSource s;
        s.kind = CaptureSource::Window;
        s.handle = hw;
        int n = GetWindowTextLengthW(hw);
        std::wstring t(n + 1, L'\0');
        GetWindowTextW(hw, t.data(), n + 1);
        t.resize(wcslen(t.c_str()));
        s.name = text::Narrow(t);
        s.app = ExeName(hw);
        s.minimized = IsIconic(hw) != FALSE;
        int w, h;
        if (thumbs && !s.minimized && GrabWindow(hw, buf, w, h)) {
            s.w = w;
            s.h = h;
            s.thumb = img::FromPixels(buf.data(), w, h, w * 4, true, s.name, 400);
        }
        out.push_back(std::move(s));
    }
    return out;
}
} // namespace screencap

ScreenShare::~ScreenShare() { Stop(); }

void ScreenShare::Start(int intervalMs, const CaptureSource& src) {
    if (active_) return;
    intervalMs_ = intervalMs;
    {
        std::lock_guard<std::mutex> l(mtx_);
        src_ = src;
        src_.thumb.reset();
        if (src_.name.empty()) src_.name = "Tela principal";
    }
    if (src.kind == CaptureSource::Window && src.handle && IsIconic((HWND)src.handle)) ShowWindow((HWND)src.handle, SW_RESTORE);
    quit_ = false;
    lost_ = false;
    active_ = true;
    th_ = std::thread([this] { Run(); });
}

void ScreenShare::Stop() {
    quit_ = true;
    if (th_.joinable()) th_.join();
    active_ = false;
    std::lock_guard<std::mutex> l(mtx_);
    latest_.reset();
}

PicturePtr ScreenShare::Latest() {
    std::lock_guard<std::mutex> l(mtx_);
    return latest_;
}

std::string ScreenShare::SourceName() {
    std::lock_guard<std::mutex> l(mtx_);
    return src_.name;
}

void ScreenShare::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CaptureSource src;
    {
        std::lock_guard<std::mutex> l(mtx_);
        src = src_;
    }
    bool window = src.kind == CaptureSource::Window;
    Dupl d;
    d.target = window ? nullptr : (HMONITOR)src.handle;
    bool useDupl = !window && d.Init();
    method_ = window ? "Janela" : useDupl ? "DXGI" : "GDI";
    std::vector<uint8_t> buf;
    while (!quit_) {
        auto t0 = std::chrono::steady_clock::now();
        PicturePtr pic;
        int w, h;
        if (window) {
            if (!IsWindow((HWND)src.handle)) lost_ = true;
            else if (GrabWindow((HWND)src.handle, buf, w, h)) pic = img::FromPixels(buf.data(), w, h, w * 4, true, src.name, 1280);
        } else if (useDupl && d.Grab()) {
            pic = img::FromPixels(d.last.data(), d.w, d.h, d.w * 4, true, "Tela", 1280);
        } else {
            if (GrabGdi(MonitorRect((HMONITOR)src.handle), buf, w, h)) pic = img::FromPixels(buf.data(), w, h, w * 4, true, "Tela", 1280);
            if (useDupl && !d.dup) useDupl = d.Init(); // tenta voltar para DXGI (ex.: após troca de resolução)
        }
        if (pic) {
            std::lock_guard<std::mutex> l(mtx_);
            latest_ = pic;
            ++frameId_;
        }
        while (!quit_ && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(intervalMs_))
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    d.Reset();
    CoUninitialize();
}
