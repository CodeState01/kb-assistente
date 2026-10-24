#include "media/screen.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
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
                if (d.AttachedToDesktop && (mi.dwFlags & MONITORINFOF_PRIMARY)) { output = o; break; }
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

bool GrabGdi(std::vector<uint8_t>& out, int& w, int& h) {
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
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
        ok = BitBlt(mem, 0, 0, w, h, screen, 0, 0, SRCCOPY | CAPTUREBLT) != FALSE;
        if (ok) out.assign((uint8_t*)bits, (uint8_t*)bits + (size_t)w * h * 4);
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}
} // namespace

ScreenShare::~ScreenShare() { Stop(); }

void ScreenShare::Start(int intervalMs) {
    if (active_) return;
    intervalMs_ = intervalMs;
    quit_ = false;
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

void ScreenShare::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Dupl d;
    bool useDupl = d.Init();
    method_ = useDupl ? "DXGI" : "GDI";
    std::vector<uint8_t> gdi;
    while (!quit_) {
        auto t0 = std::chrono::steady_clock::now();
        PicturePtr pic;
        if (useDupl && d.Grab()) {
            pic = img::FromPixels(d.last.data(), d.w, d.h, d.w * 4, true, "Tela", 1280);
        } else {
            int w, h;
            if (GrabGdi(gdi, w, h)) pic = img::FromPixels(gdi.data(), w, h, w * 4, true, "Tela", 1280);
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
