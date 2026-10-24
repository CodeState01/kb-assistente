// KB-Setup — instalador do KB (Windows). Mesmo visual do app: Dear ImGui + orb animado.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "core/install.h"
#include "core/platform.h"
#include "core/text.h"
#include "net/http.h"
#include "ui/anim.h"
#include "ui/orb.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using theme::px;

namespace {
struct Step {
    std::string title;
    std::atomic<int> state{0};      // 0 esperando, 1 rodando, 2 ok, 3 falhou, 4 pulado
    std::atomic<uint64_t> done{0}, total{0};
    std::mutex m;
    std::string detail;
    void Set(int s, const std::string& d = "") { std::lock_guard<std::mutex> l(m); state = s; if (!d.empty()) detail = d; }
    std::string Detail() { std::lock_guard<std::mutex> l(m); return detail; }
};

GLFWwindow* g_win = nullptr;
HWND g_hwnd = nullptr;
WNDPROC g_prev = nullptr;
float g_scale = 1.f;
int g_page = 0;                   // 0 boas-vindas, 1 instalando, 2 pronto
std::atomic<bool> g_working{false}, g_failed{false};
std::vector<std::unique_ptr<Step>> g_steps;
bool optDesktop = true, optAutostart = true, optWhisper = true, optOllama = true, optFirewall = true;
bool ollamaInstalled = false, whisperPresent = false;
install::DeviceInfo g_dev;
Orb g_orb;

std::wstring KnownW(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}
std::wstring ModelsDirW() { return KnownW(FOLDERID_RoamingAppData) + L"\\KB\\models"; }

bool AnyWhisperModel() {
    std::wstring dir = ModelsDirW();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\ggml-*.bin").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) {
        LRESULT r = CallWindowProcW(g_prev, h, msg, wp, lp);
        if (r != HTCLIENT) return r;
        POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(h, &pt);
        RECT rc;
        GetClientRect(h, &rc);
        if (pt.y < (LONG)(44 * g_scale) && pt.x < rc.right - (LONG)(60 * g_scale)) return HTCAPTION;
        return HTCLIENT;
    }
    return CallWindowProcW(g_prev, h, msg, wp, lp);
}

bool ExtractKb(const std::wstring& dest, std::string* err) {
    HRSRC r = FindResourceW(nullptr, L"KB_EXE", MAKEINTRESOURCEW(10));
    if (!r) { *err = "pacote corrompido (KB.exe não encontrado)"; return false; }
    HGLOBAL g = LoadResource(nullptr, r);
    DWORD sz = SizeofResource(nullptr, r);
    const void* data = g ? LockResource(g) : nullptr;
    if (!data) { *err = "pacote corrompido"; return false; }
    std::wstring tmp = dest + L".new";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *err = "não foi possível gravar na pasta de instalação"; return false; }
    DWORD wr = 0;
    BOOL ok = WriteFile(f, data, sz, &wr, nullptr);
    CloseHandle(f);
    if (!ok || wr != sz) { *err = "falha ao gravar o KB.exe"; return false; }
    for (int i = 0; i < 20; ++i) {
        if (MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        Sleep(250); // o KB antigo pode ainda estar fechando
    }
    *err = "o KB está em uso; feche-o e tente de novo";
    return false;
}

void RunInstall() {
    g_working = true;
    g_failed = false;
    auto& st = g_steps;
    std::string err;
    // 1. arquivos
    st[0]->Set(1);
    std::wstring dir = install::InstallDir();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    install::CloseRunning(install::InstalledExe());
    if (!ExtractKb(install::InstalledExe(), &err)) { st[0]->Set(3, err); g_failed = true; g_working = false; return; }
    st[0]->Set(2, text::Narrow(dir));

    // 2. atalhos + registro
    st[1]->Set(1);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring exe = install::InstalledExe();
    install::CreateShortcut(install::StartMenuShortcut(), exe, L"", L"KB — assistente de voz");
    if (optDesktop) install::CreateShortcut(install::DesktopShortcut(), exe, L"", L"KB — assistente de voz");
    install::RegisterUninstall((unsigned long)(platform::FileSize(text::Narrow(exe)) / 1024));
    HKEY run;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        if (optAutostart) {
            std::wstring cmd = L"\"" + exe + L"\" --minimizado";
            RegSetValueExW(run, L"KBAssistente", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(run, L"KBAssistente");
        }
        RegCloseKey(run);
    }
    CoUninitialize();
    st[1]->Set(2, optAutostart ? "Menu Iniciar, área de trabalho e inicialização com o Windows" : "Menu Iniciar e área de trabalho");

    // 2b. celular: regra no Firewall (o Windows pede confirmação de administrador)
    if (optFirewall && !platform::FirewallRuleExists()) {
        st[1]->Set(1, "Liberando o celular no Firewall (confirme na janela do Windows)…");
        bool ok = platform::AddFirewallRuleElevated(7777, text::Narrow(install::InstalledExe()));
        st[1]->Set(2, ok ? "Atalhos criados e celular liberado no Firewall" : "Atalhos criados (Firewall não liberado: dá para fazer depois em Configurações > Celular)");
    }

    // 3. modelo de voz
    if (optWhisper && !whisperPresent) {
        st[2]->Set(1, "Baixando ggml-small.bin…");
        SHCreateDirectoryExW(nullptr, ModelsDirW().c_str(), nullptr);
        std::string dest = text::Narrow(ModelsDirW()) + "\\ggml-small.bin";
        auto r = http::Download("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin", dest,
                                [&](uint64_t d, uint64_t t) { st[2]->done = d; st[2]->total = t; }, nullptr);
        if (r.ok() && r.error.empty()) st[2]->Set(2, "Reconhecimento de voz em português pronto");
        else st[2]->Set(3, "Falhou (" + r.error + "). Dá para baixar depois dentro do KB.");
    } else {
        st[2]->Set(4, whisperPresent ? "Já existe um modelo de voz" : "Pulado");
    }

    // 4. Ollama
    if (optOllama && !ollamaInstalled) {
        st[3]->Set(1, "Baixando o instalador do Ollama…");
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::string setup = text::Narrow(tmp) + "OllamaSetup.exe";
        auto r = http::Download("https://ollama.com/download/OllamaSetup.exe", setup,
                                [&](uint64_t d, uint64_t t) { st[3]->done = d; st[3]->total = t; }, nullptr);
        if (r.ok() && r.error.empty()) {
            st[3]->Set(1, "Instalando o Ollama…");
            st[3]->total = 0;
            SHELLEXECUTEINFOW sei{sizeof(sei)};
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            std::wstring file = text::Widen(setup);
            sei.lpFile = file.c_str();
            sei.lpParameters = L"/VERYSILENT /NORESTART /SUPPRESSMSGBOXES";
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 15 * 60 * 1000);
                CloseHandle(sei.hProcess);
                st[3]->Set(2, "Ollama instalado — o KB o inicia sozinho");
            } else {
                st[3]->Set(3, "Não foi possível executar o instalador do Ollama");
            }
            DeleteFileW(file.c_str());
        } else {
            st[3]->Set(3, "Falhou (" + r.error + "). Instale depois em ollama.com.");
        }
    } else {
        st[3]->Set(4, ollamaInstalled ? "Ollama já instalado" : "Pulado (você pode usar LM Studio, llama.cpp, Jan…)");
    }
    g_working = false;
}

void Backdrop(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    float t = anim::Time();
    dl->AddRectFilledMultiColor(a, b, anim::Col(theme::Bg), anim::Col(theme::Bg), anim::Col(theme::Bg2), anim::Col(theme::Bg2));
    float w = b.x - a.x, h = b.y - a.y, m = std::max(w, h);
    orbfx::RadialGradient(dl, ImVec2(a.x + w * (0.2f + 0.05f * std::sin(t * 0.3f)), a.y + h * 0.25f), 0, m * 0.6f,
                          anim::Col(anim::WithAlpha(theme::Violet, 0.14f)), anim::Col(anim::WithAlpha(theme::Violet, 0)), 48);
    orbfx::RadialGradient(dl, ImVec2(a.x + w * 0.85f, a.y + h * (0.8f + 0.05f * std::cos(t * 0.25f))), 0, m * 0.5f,
                          anim::Col(anim::WithAlpha(theme::Cyan, 0.09f)), anim::Col(anim::WithAlpha(theme::Cyan, 0)), 48);
}

void StepRow(Step& s) {
    float lh = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + lh * 0.6f, p.y + lh * 0.6f);
    int state = s.state.load();
    ImVec4 col = state == 2 ? theme::Green : state == 3 ? theme::Red : state == 1 ? theme::Cyan : theme::TextFaint;
    if (state == 1) {
        float a0 = anim::Time() * 6.f;
        dl->PathArcTo(c, lh * 0.5f, a0, a0 + 4.2f, 20);
        dl->PathStroke(anim::Col(col), 0, px(2.2f));
    } else {
        dl->AddCircleFilled(c, lh * 0.55f, anim::Col(anim::WithAlpha(col, 0.18f)), 24);
        w::DrawIcon(dl, state == 3 ? Icon::Close : state == 2 ? Icon::Check : Icon::Minimize, c, lh * 0.75f, anim::Col(col));
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + lh * 1.6f, p.y));
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, state == 0 || state == 4 ? theme::TextDim : theme::Text);
    ImGui::TextUnformatted(s.title.c_str());
    ImGui::PopStyleColor();
    std::string d = s.Detail();
    uint64_t done = s.done, total = s.total;
    if (state == 1 && total) d += "  " + text::FormatBytes(done) + " / " + text::FormatBytes(total);
    if (!d.empty()) w::Hint("%s", d.c_str());
    if (state == 1 && total) w::Progress((float)((double)done / total), ImVec2(ImGui::GetContentRegionAvail().x, px(6)));
    else if (state == 1) w::Progress(0, ImVec2(ImGui::GetContentRegionAvail().x, px(6)), true);
    ImGui::EndGroup();
    ImGui::Spacing();
}

void Frame() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = vp->Pos, b(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    Backdrop(ImGui::GetBackgroundDrawList(), a, b);

    // barra de título
    ImGui::PushFont(theme::fonts.caption);
    dl->AddText(ImVec2(a.x + px(18), a.y + px(14)), anim::Col(theme::TextDim), "Instalador do KB");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(b.x - px(46), a.y + px(6)));
    if (w::IconButton("x", Icon::Close, px(34), "Fechar", false, theme::Red, false) && !g_working) glfwSetWindowShouldClose(g_win, 1);

    // orb à esquerda
    float leftW = vp->Size.x * 0.38f;
    ImVec2 oc(a.x + leftW * 0.5f, a.y + vp->Size.y * 0.46f);
    OrbInput in;
    in.mode = g_page == 0 ? OrbMode::Idle : g_page == 1 ? (g_working ? OrbMode::Thinking : OrbMode::Showing) : (g_failed ? OrbMode::Error : OrbMode::Showing);
    in.speechLevel = 0.f;
    g_orb.Draw(dl, oc, std::min(leftW * 0.28f, px(100)), in);
    ImGui::PushFont(theme::fonts.huge);
    ImVec2 ts = ImGui::CalcTextSize("KB");
    dl->AddText(ImVec2(oc.x - ts.x * 0.5f, oc.y + px(130)), anim::Col(theme::Text), "KB");
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts.caption);
    const char* sub = "Assistente de voz com IA local";
    ImVec2 ss = ImGui::CalcTextSize(sub);
    dl->AddText(ImVec2(oc.x - ss.x * 0.5f, oc.y + px(176)), anim::Col(theme::TextDim), sub);
    std::string ver = std::string("versão ") + install::kVersion;
    ImVec2 vs = ImGui::CalcTextSize(ver.c_str());
    dl->AddText(ImVec2(oc.x - vs.x * 0.5f, oc.y + px(196)), anim::Col(theme::TextFaint), ver.c_str());
    ImGui::PopFont();

    // conteúdo à direita
    float x0 = a.x + leftW, cw = vp->Size.x - leftW - px(36);
    ImGui::SetCursorScreenPos(ImVec2(x0, a.y + px(56)));
    ImGui::BeginChild("##content", ImVec2(cw, vp->Size.y - px(76)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    float appear = anim::To(ImGui::GetID(g_page == 0 ? "p0" : g_page == 1 ? "p1" : "p2"), 1.f, 6.f, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, appear);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1 - anim::EaseOutCubic(appear)) * px(20));

    if (g_page == 0) {
        ImGui::PushFont(theme::fonts.title);
        ImGui::TextUnformatted(install::IsInstalled() ? "Atualizar o KB" : "Instalar o KB");
        ImGui::PopFont();
        ImGui::Spacing();
        w::BeginCard("##dev", g_dev.supported ? theme::Cyan : theme::Red);
        ImVec2 ip = ImGui::GetCursorScreenPos();
        float lh = ImGui::GetTextLineHeight();
        w::DrawIcon(ImGui::GetWindowDrawList(), g_dev.kind == "Notebook" ? Icon::Monitor : Icon::Cpu, ImVec2(ip.x + lh, ip.y + lh), lh * 1.6f, anim::Col(theme::Cyan));
        ImGui::SetCursorScreenPos(ImVec2(ip.x + lh * 2.6f, ip.y));
        ImGui::BeginGroup();
        ImGui::Text("Dispositivo detectado: %s", g_dev.kind.c_str());
        w::Hint("%s · %s", g_dev.os.c_str(), g_dev.arch.c_str());
        if (g_dev.supported)
            w::Hint("Será instalada a versão para PC (Windows %s)%s.", g_dev.arch == "ARM64" ? "x64, via emulação do Windows 11" : "x64",
                    g_dev.kind == "Tablet / 2 em 1" ? " com interface de toque" : "");
        else
            w::Hint(g_dev.arch == "ARM64" ? "No ARM, o KB precisa do Windows 11 (o Windows 10 ARM não roda programas x64)." : "Este Windows não é compatível (é preciso Windows 10/11 de 64 bits).");
        ImGui::EndGroup();
        w::EndCard();
        w::Hint("No celular (Android ou iPhone): depois de instalar, abra o KB > Configurações > Celular e escaneie o QR code.");
        ImGui::Spacing();
        w::Toggle("Atalho na área de trabalho", &optDesktop);
        w::Toggle("Iniciar com o Windows (minimizado)", &optAutostart);
        w::Toggle("Permitir usar pelo celular (libera no Firewall)", &optFirewall, "O Windows vai pedir confirmação de administrador uma vez.");
        if (whisperPresent) { bool f = false; ImGui::BeginDisabled(); w::Toggle("Modelo de voz (já instalado)", &f); ImGui::EndDisabled(); }
        else w::Toggle("Baixar reconhecimento de voz em português (466 MB)", &optWhisper);
        if (ollamaInstalled) { bool f = false; ImGui::BeginDisabled(); w::Toggle("Ollama (já instalado)", &f); ImGui::EndDisabled(); }
        else w::Toggle("Instalar o Ollama, o motor de IA local (≈1 GB)", &optOllama, "Se você já usa LM Studio, llama.cpp ou Jan, pode desmarcar.");
        w::Hint("Pasta: %s (não precisa de administrador)", text::Narrow(install::InstallDir()).c_str());
        ImGui::Spacing();
        if (!g_dev.supported) ImGui::BeginDisabled();
        if (w::PrimaryButton(install::IsInstalled() ? "Atualizar agora" : "Instalar agora", ImVec2(px(200), px(44)), Icon::Download)) {
            g_page = 1;
            std::thread(RunInstall).detach();
        }
        if (!g_dev.supported) ImGui::EndDisabled();
    } else if (g_page == 1) {
        ImGui::PushFont(theme::fonts.title);
        ImGui::TextUnformatted(g_working ? "Instalando…" : g_failed ? "Não foi possível instalar" : "Tudo pronto!");
        ImGui::PopFont();
        ImGui::Spacing();
        for (auto& s : g_steps) StepRow(*s);
        if (!g_working) {
            ImGui::Spacing();
            if (!g_failed) {
                if (w::PrimaryButton("Abrir o KB", ImVec2(px(170), px(44)), Icon::Play)) {
                    ShellExecuteW(nullptr, L"open", install::InstalledExe().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    glfwSetWindowShouldClose(g_win, 1);
                }
                ImGui::SameLine();
            }
            if (w::GhostButton("Fechar", ImVec2(px(120), px(44)))) glfwSetWindowShouldClose(g_win, 1);
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::End();
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_dev = install::DetectDevice();
    ollamaInstalled = !platform::FindOllama().empty();
    whisperPresent = AnyWhisperModel();
    optOllama = !ollamaInstalled;
    optWhisper = !whisperPresent;
    for (const char* t : {"Copiar o KB", "Criar atalhos", "Modelo de voz (Whisper)", "Motor de IA (Ollama)"}) {
        g_steps.push_back(std::make_unique<Step>());
        g_steps.back()->title = t;
    }

    // Modo silencioso: KB-Setup.exe /S
    if (cmdLine && (wcsstr(cmdLine, L"/S") || wcsstr(cmdLine, L"--silencioso"))) {
        optWhisper = optOllama = optFirewall = false;
        RunInstall();
        return g_failed ? 1 : 0;
    }

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    float xs = 1, ys = 1;
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (mon) glfwGetMonitorContentScale(mon, &xs, &ys);
    g_scale = std::max(1.f, xs);
    int w = (int)(820 * g_scale), h = (int)(560 * g_scale);
    g_win = glfwCreateWindow(w, h, "Instalar KB", nullptr, nullptr);
    if (!g_win) return 1;
    int mx = 0, my = 0, mw = 1920, mh = 1080;
    if (mon) glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
    glfwSetWindowPos(g_win, mx + (mw - w) / 2, my + (mh - h) / 2);
    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);
    g_hwnd = glfwGetWin32Window(g_win);
    platform::StyleNativeWindow(g_hwnd, true);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    theme::Load(g_scale);
    ImGui_ImplGlfw_InitForOpenGL(g_win, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    g_prev = (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC);
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)Proc);
    glfwShowWindow(g_win);

    while (!glfwWindowShouldClose(g_win)) {
        glfwPollEvents();
        if (glfwWindowShouldClose(g_win) && g_working) glfwSetWindowShouldClose(g_win, 0); // não fecha no meio
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        Frame();
        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(g_win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(theme::Bg.x, theme::Bg.y, theme::Bg.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_win);
    }
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_prev);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_win);
    glfwTerminate();
    return 0;
}
