// KB — assistente de voz local em C++ (Dear ImGui + GLFW + whisper.cpp + SAPI)
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <set>
#include <vector>
#include <cstdio>
#include <string>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app.h"
#include "core/config.h"
#include "core/install.h"
#include "core/platform.h"
#include "ui/theme.h"
#include "ui/views.h"

namespace {
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr int kHotkeyId = 1;

App* g_app = nullptr;
GLFWwindow* g_win = nullptr;
HWND g_hwnd = nullptr;
WNDPROC g_prevProc = nullptr;
UINT g_msgShow = 0, g_msgTaskbar = 0;
NOTIFYICONDATAW g_nid{};
bool g_trayAdded = false;
bool g_quit = false;
float g_scale = 1.f;
bool g_fullscreen = false;

void AddTray() {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"GLFW_ICON", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"KB — assistente de voz");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

void RemoveTray() {
    if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
}

void ShowMain() {
    glfwShowWindow(g_win);
    if (glfwGetWindowAttrib(g_win, GLFW_ICONIFIED)) glfwRestoreWindow(g_win);
    glfwFocusWindow(g_win);
    SetForegroundWindow(g_hwnd);
}

void HideMain() { glfwHideWindow(g_win); }

void TrayMenu() {
    HMENU m = CreatePopupMenu();
    Config c = g_app->Cfg();
    AppendMenuW(m, MF_STRING, 1, L"Abrir KB");
    AppendMenuW(m, MF_STRING, 2, L"Falar agora");
    AppendMenuW(m, MF_STRING | (c.showMiniOrb ? MF_CHECKED : 0), 3, L"Orb flutuante");
    AppendMenuW(m, MF_STRING | (c.startWithWindows ? MF_CHECKED : 0), 4, L"Iniciar com o Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 9, L"Sair");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1: ShowMain(); break;
    case 2: g_app->StartListening(); break;
    case 3: c.showMiniOrb = !c.showMiniOrb; g_app->ApplyConfig(c); break;
    case 4: c.startWithWindows = !c.startWithWindows; g_app->ApplyConfig(c); break;
    case 9: g_quit = true; break;
    }
}

LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        LRESULT r = CallWindowProcW(g_prevProc, h, msg, wp, lp);
        if (r != HTCLIENT) return r;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        ui::TitleBarInfo tb = ui::GetTitleBar();
        // A barra de título (fora dos botões) arrasta a janela como uma barra nativa (inclui Aero Snap).
        if (!g_fullscreen && pt.y >= 6 && pt.y < (LONG)tb.height && pt.x >= 6 && pt.x < (LONG)tb.buttonsStartX) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) { ui::Requests().toggleFullscreen = true; return 0; }
        break;
    case WM_HOTKEY:
        if (wp == kHotkeyId && g_app) g_app->ToggleListening();
        return 0;
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP: ShowMain(); break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: TrayMenu(); break;
        }
        return 0;
    default:
        if (msg == g_msgShow && g_msgShow) { ShowMain(); return 0; }
        if (msg == g_msgTaskbar && g_msgTaskbar) { AddTray(); return 0; }
        break;
    }
    return CallWindowProcW(g_prevProc, h, msg, wp, lp);
}

void RegisterHotkey(const Config& c) {
    UnregisterHotKey(g_hwnd, kHotkeyId);
    if (!RegisterHotKey(g_hwnd, kHotkeyId, (UINT)c.hotkeyMods | MOD_NOREPEAT, (UINT)c.hotkeyVk))
        g_app->Notify(std::string("O atalho ") + ui::HotkeyName(c.hotkeyMods, c.hotkeyVk) + " já está em uso por outro programa.", 2);
}

// Redimensionamento pelas bordas (a janela não tem moldura nativa).

RECT g_prevRect{};

void SetFullscreenMode(bool on) {
    if (on == g_fullscreen) return;
    if (on) {
        GetWindowRect(g_hwnd, &g_prevRect);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        RECT r = mi.rcMonitor;
        platform::StyleNativeWindow(g_hwnd, false);
        SetWindowPos(g_hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        platform::StyleNativeWindow(g_hwnd, true);
        SetWindowPos(g_hwnd, nullptr, g_prevRect.left, g_prevRect.top, g_prevRect.right - g_prevRect.left,
                     g_prevRect.bottom - g_prevRect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    g_fullscreen = on;
    ui::SetFullscreen(on);
}

void HandleEdgeResize() {
    static int edge = 0;
    if (g_fullscreen) return;
    static POINT start{};
    static RECT startRect{};
    if (glfwGetWindowAttrib(g_win, GLFW_MAXIMIZED)) return;
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    POINT cur;
    GetCursorPos(&cur);
    const int b = (int)(7 * g_scale);
    ImGuiIO& io = ImGui::GetIO();
    if (edge == 0) {
        if (WindowFromPoint(cur) != g_hwnd) return;
        bool l = cur.x >= wr.left && cur.x < wr.left + b, r = cur.x <= wr.right && cur.x > wr.right - b;
        bool t = cur.y >= wr.top && cur.y < wr.top + b, bo = cur.y <= wr.bottom && cur.y > wr.bottom - b;
        int e = (l ? 1 : 0) | (r ? 2 : 0) | (t ? 4 : 0) | (bo ? 8 : 0);
        if (!e) return;
        ImGuiMouseCursor mc = (e == 1 || e == 2) ? ImGuiMouseCursor_ResizeEW
                            : (e == 4 || e == 8) ? ImGuiMouseCursor_ResizeNS
                            : (e == 5 || e == 10) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW;
        ImGui::SetMouseCursor(mc);
        if (ImGui::IsMouseClicked(0)) {
            edge = e;
            start = cur;
            startRect = wr;
        }
        return;
    }
    if (!io.MouseDown[0]) { edge = 0; return; }
    int dx = cur.x - start.x, dy = cur.y - start.y;
    RECT n = startRect;
    const int minW = (int)(720 * g_scale), minH = (int)(520 * g_scale);
    if (edge & 1) n.left = std::min(startRect.left + dx, startRect.right - minW);
    if (edge & 2) n.right = std::max(startRect.right + dx, startRect.left + minW);
    if (edge & 4) n.top = std::min(startRect.top + dy, startRect.bottom - minH);
    if (edge & 8) n.bottom = std::max(startRect.bottom + dy, startRect.top + minH);
    SetWindowPos(g_hwnd, nullptr, n.left, n.top, n.right - n.left, n.bottom - n.top, SWP_NOZORDER | SWP_NOACTIVATE);
    ImGuiMouseCursor mc = (edge == 1 || edge == 2) ? ImGuiMouseCursor_ResizeEW
                        : (edge == 4 || edge == 8) ? ImGuiMouseCursor_ResizeNS
                        : (edge == 5 || edge == 10) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW;
    ImGui::SetMouseCursor(mc);
}
// Diagnóstico: salva o framebuffer da janela principal em BMP (KB_SNAPSHOT=arquivo.bmp).
void SaveSnapshot(const std::wstring& path, int w, int h) {
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px.data());
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)px.size();
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
}

std::wstring Env(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return n && n < 1024 ? std::wstring(buf, n) : std::wstring();
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    bool startMin = cmdLine && wcsstr(cmdLine, L"--minimizado") != nullptr;

    if (cmdLine && wcsstr(cmdLine, L"--desinstalar")) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (MessageBoxW(nullptr, L"Deseja desinstalar o KB deste computador?", L"Desinstalar KB", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        bool data = MessageBoxW(nullptr, L"Apagar também suas configurações e os modelos de voz baixados?\n\n(Os modelos de IA do Ollama não são apagados.)",
                                L"Desinstalar KB", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
        install::CloseRunning(install::InstalledExe());
        install::Uninstall(data);
        MessageBoxW(nullptr, L"O KB foi desinstalado.", L"KB", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Instância única: se já estiver aberta, só mostra a janela existente.
    g_msgShow = RegisterWindowMessageW(L"KB_ASSISTENTE_MOSTRAR");
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\KBAssistenteInstancia");
    if (GetLastError() == ERROR_ALREADY_EXISTS && GetEnvironmentVariableW(L"KB_SNAPSHOT", nullptr, 0) == 0) {
        PostMessageW(HWND_BROADCAST, g_msgShow, 0, 0);
        return 0;
    }
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    timeBeginPeriod(1);

    if (!glfwInit()) {
        MessageBoxW(nullptr, L"Falha ao iniciar GLFW/OpenGL.", L"KB", MB_ICONERROR);
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    static App app;
    g_app = &app;
    app.Init(startMin);
    Config cfg = app.Cfg();

    float xs = 1.f, ys = 1.f;
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (mon) glfwGetMonitorContentScale(mon, &xs, &ys);
    g_scale = std::max(1.f, xs) * std::clamp(cfg.uiScale, 0.75f, 2.0f);

    int ww = (int)(1240 * g_scale), wh = (int)(800 * g_scale);
    int mx = 0, my = 0, mw = 1920, mh = 1080;
    if (mon) glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
    ww = std::min(ww, mw - 40);
    wh = std::min(wh, mh - 40);
    g_win = glfwCreateWindow(ww, wh, "KB", nullptr, nullptr);
    if (!g_win) {
        MessageBoxW(nullptr, L"Não foi possível criar a janela OpenGL 3.0.", L"KB", MB_ICONERROR);
        return 1;
    }
    glfwSetWindowPos(g_win, mx + (mw - ww) / 2, my + (mh - wh) / 2);
    glfwSetWindowSizeLimits(g_win, (int)(720 * g_scale), (int)(520 * g_scale), GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);
    g_hwnd = glfwGetWin32Window(g_win);
    platform::StyleNativeWindow(g_hwnd, true);

    glfwSetWindowCloseCallback(g_win, [](GLFWwindow* w) {
        if (g_app->Cfg().minimizeToTray) {
            glfwSetWindowShouldClose(w, GLFW_FALSE);
            HideMain();
        }
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoDefaultParent = true;
    io.ConfigDockingTransparentPayload = true;
    static std::string iniPath = config::AppDataDir() + "\\layout.ini";
    io.IniFilename = iniPath.c_str();

    ui::Init(app, g_scale);
    ImGui_ImplGlfw_InitForOpenGL(g_win, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Nosso WndProc entra por cima do GLFW/ImGui (barra de título, bandeja, atalho global).
    g_prevProc = (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC);
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)MainProc);
    AddTray();

    if (!startMin) ShowMain();

    std::wstring snapPath = Env(L"KB_SNAPSHOT");
    int snapFrame = snapPath.empty() ? -1 : _wtoi(Env(L"KB_SNAPSHOT_FRAME").c_str());
    if (snapFrame == 0) snapFrame = 150;
    if (!Env(L"KB_DEMO").empty()) app.LoadDemoConversation();
    if (!Env(L"KB_MODE").empty()) ui::SetDebugMode(_wtoi(Env(L"KB_MODE").c_str()));
    int frame = 0;
    std::set<void*> styled;
    bool excluded = false;
    glfwSetDropCallback(g_win, [](GLFWwindow*, int n, const char** paths) {
        std::vector<std::string> v(paths, paths + n);
        ui::OnFilesDropped(*g_app, v);
    });
    styled.insert(g_hwnd);
    double lastFrame = glfwGetTime();

    while (!g_quit && !glfwWindowShouldClose(g_win)) {
        bool visible = glfwGetWindowAttrib(g_win, GLFW_VISIBLE) && !glfwGetWindowAttrib(g_win, GLFW_ICONIFIED);
        Config c = app.Cfg();
        bool needFrames = visible || c.showMiniOrb || app.State() != AssistState::Idle || app.TtsSpeaking() ||
                          ImGui::GetPlatformIO().Viewports.Size > 1;
        if (needFrames) glfwPollEvents();
        else glfwWaitEventsTimeout(0.25);

        if (app.TakeHotkeyDirty()) RegisterHotkey(c);
        app.Update();
        if (app.requestShowMain.exchange(false)) ShowMain();
        if (app.requestQuit.exchange(false)) g_quit = true;
        ui::WindowRequests& rq = ui::Requests();
        if (rq.minimize) { rq.minimize = false; glfwIconifyWindow(g_win); }
        if (rq.hideToTray) { rq.hideToTray = false; HideMain(); }
        if (rq.quit) { rq.quit = false; g_quit = true; }
        if (rq.toggleFullscreen) { rq.toggleFullscreen = false; SetFullscreenMode(!g_fullscreen); }

        visible = glfwGetWindowAttrib(g_win, GLFW_VISIBLE) && !glfwGetWindowAttrib(g_win, GLFW_ICONIFIED);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ui::Frame(app, visible);
        if (visible) HandleEdgeResize();
        if (visible && ImGui::IsKeyPressed(ImGuiKey_F11, false)) SetFullscreenMode(!g_fullscreen);
        if (visible && g_fullscreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()) SetFullscreenMode(false);
        ImGui::Render();

        if (visible) {
            int fw, fh;
            glfwGetFramebufferSize(g_win, &fw, &fh);
            glViewport(0, 0, fw, fh);
            glClearColor(theme::Bg.x, theme::Bg.y, theme::Bg.z, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (frame + 1 == snapFrame) SaveSnapshot(snapPath, fw, fh);
        }
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        if (++frame == snapFrame) {
            // também salva cada janela flutuante (viewport secundário)
            ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
            for (int i = 1; i < pio.Viewports.Size; ++i) {
                ImGuiViewport* vp = pio.Viewports[i];
                GLFWwindow* w = (GLFWwindow*)vp->PlatformHandle;
                if (!w || !vp->DrawData) continue;
                glfwMakeContextCurrent(w);
                pio.Renderer_RenderWindow(vp, nullptr);
                int vw, vh;
                glfwGetFramebufferSize(w, &vw, &vh);
                SaveSnapshot(snapPath + L"_vp" + std::to_wstring(i) + L".bmp", vw, vh);
            }
            glfwMakeContextCurrent(backup);
            if (!Env(L"KB_SNAPSHOT_EXIT").empty()) g_quit = true;
        }
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);

        // Visual Win11 (cantos arredondados, modo escuro) nas janelas flutuantes novas.
        for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports)
            if (vp->PlatformHandleRaw && !styled.count(vp->PlatformHandleRaw)) {
                platform::StyleNativeWindow(vp->PlatformHandleRaw, true);
                styled.insert(vp->PlatformHandleRaw);
                if (excluded) SetWindowDisplayAffinity((HWND)vp->PlatformHandleRaw, 0x11);
            }
        // Enquanto a tela é compartilhada, as janelas do próprio KB ficam fora da captura.
        bool wantExclude = app.Screen().Active();
        if (wantExclude != excluded) {
            excluded = wantExclude;
            DWORD aff = excluded ? 0x11 /*WDA_EXCLUDEFROMCAPTURE*/ : 0 /*WDA_NONE*/;
            for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports)
                if (vp->PlatformHandleRaw) SetWindowDisplayAffinity((HWND)vp->PlatformHandleRaw, aff);
        }

        if (visible) {
            glfwSwapBuffers(g_win);
        } else {
            // sem vsync da janela principal: limita a ~60 fps
            double now = glfwGetTime();
            double wait = 1.0 / 60.0 - (now - lastFrame);
            if (wait > 0) Sleep((DWORD)(wait * 1000.0));
        }
        lastFrame = glfwGetTime();
    }

    UnregisterHotKey(g_hwnd, kHotkeyId);
    RemoveTray();
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_prevProc);
    glfwHideWindow(g_win);
    app.Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_win);
    glfwTerminate();
    timeEndPeriod(1);
    CoUninitialize();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
