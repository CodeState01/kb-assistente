#include "ui/views.h"
#include "app.h"
#include "core/text.h"
#include "ui/anim.h"
#include "ui/embeds.h"
#include "ui/orb.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/assets.h"
#include "core/files.h"
#include "media/image.h"
#include "qrcodegen.hpp"
#include "imgui_internal.h"
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using theme::px;

namespace ui {

namespace {
Orb g_orb, g_miniOrb, g_logoOrb;
Config g_edit;
WindowRequests g_req;
TitleBarInfo g_title;
OrbMode g_mode = OrbMode::Idle;
char g_input[4096] = "";
bool g_inputFocused = false;
bool g_showEmbeds = true, g_showModels = true, g_showSettings = true;
int g_sel = 0;
const char* g_focusNext = nullptr;
bool g_stickBottom = true;
int g_lastVersion = -1;
float g_showUntil = 0.f;
int g_lastMsgId = -1, g_lastEmbedCount = 0;
float g_emptySince = 0.f;
char g_pullName[128] = "";
char g_wakeBuf[64] = "";
char g_urlBuf[256] = "";
char g_keyBuf[256] = "";
char g_promptBuf[8192] = "";
bool g_promptLoaded = false;
bool g_fullscreen = false;
std::vector<PicturePtr> g_attach;                       // anexos esperando o envio
std::vector<std::pair<std::weak_ptr<Picture>, GLuint>> g_texs;
GLuint g_screenTex = 0;
uint64_t g_screenFrame = 0;
int g_screenW = 0, g_screenH = 0;

GLuint UploadTexture(GLuint tex, const uint8_t* rgba, int w, int h) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return tex;
}

// Textura da miniatura (criada sob demanda; apagada quando a imagem deixa de existir).
ImTextureID TextureFor(const PicturePtr& p) {
    if (!p || p->rgba.empty()) return (ImTextureID)0;
    if (!p->tex) {
        p->tex = UploadTexture(0, p->rgba.data(), p->w, p->h);
        g_texs.push_back({p, p->tex});
    }
    return (ImTextureID)(intptr_t)p->tex;
}

void CollectTextures() {
    for (auto it = g_texs.begin(); it != g_texs.end();) {
        if (it->first.expired()) { glDeleteTextures(1, &it->second); it = g_texs.erase(it); }
        else ++it;
    }
}

// Desenha uma imagem com cantos arredondados.
void RoundedImage(ImTextureID tex, ImVec2 size, float rounding, float alpha = 1.f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImGui::GetWindowDrawList()->AddImageRounded(tex, p, ImVec2(p.x + size.x, p.y + size.y), ImVec2(0, 0), ImVec2(1, 1),
                                                ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), rounding);
}

void AddAttachment(App& app, PicturePtr p) {
    if (!p) { app.Notify("Não consegui abrir essa imagem.", 2); return; }
    if (g_attach.size() >= 4) { app.Notify("Máximo de 4 imagens por mensagem.", 0); return; }
    g_attach.push_back(p);
}
int g_debugMode = -1;

const char* kChat = "Conversa###chat";
const char* kEmbeds = "Embeds###embeds";
const char* kModels = "Modelos###models";
const char* kSettings = "Configurações###settings";

struct HotkeyPreset { const char* name; int mods; int vk; };
const HotkeyPreset kHotkeys[] = {
    {"Ctrl + Shift + Espaço", MOD_CONTROL | MOD_SHIFT, VK_SPACE},
    {"Ctrl + Alt + Espaço", MOD_CONTROL | MOD_ALT, VK_SPACE},
    {"Win + Shift + A", MOD_WIN | MOD_SHIFT, 'A'},
    {"Ctrl + Shift + A", MOD_CONTROL | MOD_SHIFT, 'A'},
    {"F9", 0, VK_F9},
    {"Pause", 0, VK_PAUSE},
};

void Apply(App& app) { app.ApplyConfig(g_edit); }

int CountFences(const std::string& s) {
    int n = 0;
    size_t pos = 0;
    while ((pos = s.find("```", pos)) != std::string::npos) { ++n; pos += 3; }
    return (n + 1) / 2;
}

OrbMode ComputeMode(App& app) {
    float t = anim::Time();
    int embeds = 0, lastId = -1;
    bool lastError = false;
    double lastTime = 0;
    {
        std::lock_guard<std::mutex> l(app.MessagesMutex());
        auto& ms = app.MessagesUnsafe();
        for (auto it = ms.rbegin(); it != ms.rend(); ++it)
            if (!it->user) {
                lastId = it->id;
                embeds = CountFences(it->text);
                lastError = it->error;
                lastTime = it->time;
                break;
            }
    }
    if (lastId == g_lastMsgId && embeds > g_lastEmbedCount) g_showUntil = t + 2.8f;
    g_lastMsgId = lastId;
    g_lastEmbedCount = embeds;
    bool recentError = lastError && NowSeconds() - lastTime < 2.5;
    switch (app.State()) {
    case AssistState::Listening: return OrbMode::Listening;
    case AssistState::Transcribing: return OrbMode::Transcribing;
    case AssistState::Thinking: return OrbMode::Thinking;
    case AssistState::Speaking: return t < g_showUntil ? OrbMode::Showing : OrbMode::Speaking;
    case AssistState::Idle:
    default:
        if (recentError) return OrbMode::Error;
        if (app.TtsSpeaking()) return t < g_showUntil ? OrbMode::Showing : OrbMode::Speaking;
        return t < g_showUntil ? OrbMode::Showing : OrbMode::Idle;
    }
}

const char* ModeLabel(OrbMode m) {
    switch (m) {
    case OrbMode::Listening: return "Ouvindo…";
    case OrbMode::Transcribing: return "Entendendo…";
    case OrbMode::Thinking: return "Pensando…";
    case OrbMode::Speaking: return "Falando";
    case OrbMode::Showing: return "Mostrando na tela";
    case OrbMode::Error: return "Algo deu errado";
    default: return "Olá! Como posso ajudar?";
    }
}

OrbInput MakeOrbInput(App& app, OrbMode mode, float* hist, int n, bool hov, bool held, bool compact) {
    app.MicHistory(hist, n);
    OrbInput in;
    in.mode = mode;
    in.micLevel = app.MicLevel();
    in.speechLevel = app.SpeechLevel(NowSeconds());
    in.micHistory = hist;
    in.micHistoryN = n;
    in.hovered = hov;
    in.pressed = held;
    in.compact = compact;
    return in;
}

bool Segmented(const char* id, int* v, const char* const* labels, int n) {
    ImGui::PushID(id);
    bool changed = false;
    float wdt = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight();
    float sw = wdt / n;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), anim::Col(ImVec4(1, 1, 1, 0.05f)), h * 0.5f);
    float hx = anim::Spring(ImGui::GetID("hl"), *v * sw, 220.f, 22.f);
    ImVec2 a(p.x + hx + px(3), p.y + px(3)), b(p.x + hx + sw - px(3), p.y + h - px(3));
    dl->AddRectFilledMultiColor(ImVec2(a.x + h * 0.4f, a.y), ImVec2(b.x - h * 0.4f, b.y), anim::Col(theme::Blue, 0.6f),
                                anim::Col(theme::Violet, 0.6f), anim::Col(theme::Violet, 0.6f), anim::Col(theme::Blue, 0.6f));
    dl->AddRectFilled(a, ImVec2(a.x + h, b.y), anim::Col(theme::Blue, 0.6f), (h - px(6)) * 0.5f, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(b.x - h, a.y), b, anim::Col(theme::Violet, 0.6f), (h - px(6)) * 0.5f, ImDrawFlags_RoundCornersRight);
    for (int i = 0; i < n; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + i * sw, p.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(sw, h))) { *v = i; changed = true; }
        float hov = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 12.f);
        ImGui::PopID();
        ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        ImVec4 c = *v == i ? theme::Text : anim::Lerp(theme::TextDim, theme::Text, hov);
        dl->AddText(ImVec2(p.x + i * sw + (sw - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), anim::Col(c), labels[i]);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return changed;
}

void Label(const char* s) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
}

void TrackFocus(int idx) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) g_sel = idx;
}

// ------------------------------------------------------------------ Barra de título
void TitleBar(App& app, ImGuiViewport* vp, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = vp->Pos, b(vp->Pos.x + vp->Size.x, vp->Pos.y + h);
    dl->AddRectFilledMultiColor(a, b, anim::Col(ImVec4(1, 1, 1, 0.035f)), anim::Col(ImVec4(1, 1, 1, 0.035f)),
                                anim::Col(ImVec4(1, 1, 1, 0.0f)), anim::Col(ImVec4(1, 1, 1, 0.0f)));
    float x = a.x + px(18);
    float cy = a.y + h * 0.5f;
    {
        // logo do app com um halo que acompanha o estado da IA
        ImVec2 lc(x + px(14), cy);
        float lr = px(15);
        ImVec4 acc = g_orb.CurrentColor();
        float active = g_mode == OrbMode::Idle ? 0.f : 1.f;
        float glow = anim::To(ImGui::GetID("##logoGlow"), active, 4.f);
        float pulse = 0.5f + 0.5f * std::sin(anim::Time() * 4.f);
        orbfx::RadialGradient(dl, lc, lr * 0.8f, lr * (1.6f + 0.3f * pulse * glow), anim::Col(anim::WithAlpha(acc, 0.25f + 0.35f * glow)), anim::Col(anim::WithAlpha(acc, 0)), 32);
        if (ImTextureID lt = assets::Logo())
            dl->AddImageRounded(lt, ImVec2(lc.x - lr, lc.y - lr), ImVec2(lc.x + lr, lc.y + lr), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, lr);
        else {
            float hist[16];
            OrbInput in = MakeOrbInput(app, g_mode, hist, 16, false, false, true);
            g_logoOrb.Draw(dl, lc, px(10), in);
        }
        dl->AddCircle(lc, lr + px(1), anim::Col(anim::WithAlpha(acc, 0.4f + 0.4f * glow)), 32, px(1.2f));
    }
    x += px(40);
    ImGui::PushFont(theme::fonts.semibold);
    float th = ImGui::GetTextLineHeight();
    dl->AddText(ImVec2(x, cy - th * 0.5f), anim::Col(theme::Text), "KB");
    x += ImGui::CalcTextSize("KB").x + px(16);
    ImGui::PopFont();

    // pílula de status do modelo
    ImGui::PushFont(theme::fonts.caption);
    Config c = app.Cfg();
    std::string model = app.ActiveModel();
    bool online = app.BackendOnline();
    const char* backend = c.backend == LlmBackend::Ollama ? "Ollama" : c.backend == LlmBackend::OpenAICompat ? "OpenAI-compat." : "GGUF local";
    std::string label = std::string(backend) + "  ·  " + (model.empty() ? "sem modelo" : model);
    if (!online) label = std::string(backend) + "  ·  " + (app.ModelsLoading() ? "conectando…" : "offline");
    ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    float pw = ts.x + px(34), ph = ts.y + px(10);
    ImVec2 pa(x, cy - ph * 0.5f), pb(x + pw, cy + ph * 0.5f);
    dl->AddRectFilled(pa, pb, anim::Col(ImVec4(1, 1, 1, 0.05f)), ph * 0.5f);
    ImVec4 dot = online ? theme::Green : app.ModelsLoading() ? theme::Amber : theme::Red;
    float pt = std::fmod(anim::Time(), 1.5f) / 1.5f;
    dl->AddCircleFilled(ImVec2(pa.x + px(13), cy), px(3.5f) * (1 + pt * 1.5f), anim::Col(anim::WithAlpha(dot, 0.35f * (1 - pt))), 16);
    dl->AddCircleFilled(ImVec2(pa.x + px(13), cy), px(3.5f), anim::Col(dot), 16);
    dl->AddText(ImVec2(pa.x + px(24), cy - ts.y * 0.5f), anim::Col(theme::TextDim), label.c_str());
    // atalho
    std::string hk = std::string("Atalho: ") + HotkeyName(c.hotkeyMods, c.hotkeyVk);
    ImVec2 hs = ImGui::CalcTextSize(hk.c_str());
    float hx = pb.x + px(14);
    ImGui::PopFont();

    // botões da direita
    float bs = px(34);
    float bx = b.x - px(12) - bs * 4 - px(12);
    g_title.height = h;

    // desempenho: CPU / GPU / VRAM com mini-gráfico
    float leftLimit = bx - px(10);
    if (c.showPerf) {
        PerfSnapshot ps = app.Perf();
        ImGui::PushFont(theme::fonts.caption);
        struct Pill { std::string label; float value; const float* hist; ImVec4 col; };
        std::vector<Pill> pills;
        pills.push_back({"CPU " + std::to_string((int)(ps.cpu + 0.5f)) + "%", ps.cpu, ps.cpuHist, theme::Cyan});
        if (ps.hasGpu) {
            std::string g = "GPU " + std::to_string((int)(ps.gpu + 0.5f)) + "%";
            if (ps.vramTotalGB > 0) {
                char vb[48];
                snprintf(vb, sizeof(vb), "  %.1f/%.0f GB", ps.vramUsedGB, ps.vramTotalGB);
                g += vb;
            }
            pills.push_back({g, ps.gpu, ps.gpuHist, theme::Green});
        }
        float x2 = leftLimit;
        float pillsStart = leftLimit;
        for (int i = (int)pills.size() - 1; i >= 0; --i) {
            auto& p = pills[i];
            ImVec2 lts = ImGui::CalcTextSize(p.label.c_str());
            float graphW = px(34);
            float w2 = lts.x + graphW + px(22), h2 = lts.y + px(10);
            ImVec2 a2(x2 - w2, cy - h2 * 0.5f), b2(x2, cy + h2 * 0.5f);
            if (a2.x < hx + px(4)) break;
            ImVec4 vc = p.value > 85 ? theme::Red : p.value > 60 ? theme::Amber : p.col;
            dl->AddRectFilled(a2, b2, anim::Col(ImVec4(1, 1, 1, 0.05f)), h2 * 0.5f);
            // barra de uso ao fundo
            float fill = anim::To(ImGui::GetID(p.label.substr(0, 3).c_str()), p.value / 100.f, 4.f);
            dl->AddRectFilled(a2, ImVec2(a2.x + (b2.x - a2.x) * fill, b2.y), anim::Col(anim::WithAlpha(vc, 0.10f)), h2 * 0.5f);
            dl->AddText(ImVec2(a2.x + px(10), cy - lts.y * 0.5f), anim::Col(theme::TextDim), p.label.c_str());
            // sparkline dos últimos 60 s
            ImVec2 g0(b2.x - graphW - px(8), a2.y + px(5)), g1(b2.x - px(8), b2.y - px(5));
            ImVec2 pts[PerfSnapshot::kHist];
            for (int k = 0; k < PerfSnapshot::kHist; ++k) {
                float v = p.hist[(ps.histPos + k) % PerfSnapshot::kHist] / 100.f;
                pts[k] = ImVec2(g0.x + (g1.x - g0.x) * k / (PerfSnapshot::kHist - 1), g1.y - (g1.y - g0.y) * std::clamp(v, 0.f, 1.f));
            }
            dl->AddPolyline(pts, PerfSnapshot::kHist, anim::Col(vc), 0, px(1.4f));
            // dica com detalhes
            ImGui::SetCursorScreenPos(a2);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##perf", ImVec2(w2, h2));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (i == 0) {
                    ImGui::Text("Processador: %.0f%%", ps.cpu);
                    ImGui::Text("Memória RAM: %.1f de %.1f GB (%.0f%%)", ps.ramUsedGB, ps.ramTotalGB, ps.ram);
                } else {
                    ImGui::TextUnformatted(ps.gpuName.c_str());
                    ImGui::Text("Uso: %.0f%%", ps.gpu);
                    if (ps.vramTotalGB > 0) ImGui::Text("Memória de vídeo: %.1f de %.1f GB", ps.vramUsedGB, ps.vramTotalGB);
                    else if (ps.vramUsedGB > 0) ImGui::Text("Memória de vídeo em uso: %.1f GB", ps.vramUsedGB);
                    if (ps.gpuTempC >= 0) ImGui::Text("Temperatura: %d °C", ps.gpuTempC);
                    if (ps.gpuPowerW >= 0) ImGui::Text("Consumo: %.0f W", ps.gpuPowerW);
                }
                ImGui::EndTooltip();
            }
            pillsStart = a2.x;
            x2 = a2.x - px(8);
        }
        ImGui::PopFont();
        leftLimit = pillsStart - px(10);
    }
    g_title.buttonsStartX = leftLimit + px(10) - vp->Pos.x;
    if (hx + hs.x < leftLimit) {
        ImGui::PushFont(theme::fonts.caption);
        dl->AddText(ImVec2(hx, cy - hs.y * 0.5f), anim::Col(theme::TextFaint), hk.c_str());
        ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(ImVec2(bx, cy - bs * 0.5f));
    if (w::IconButton("mini", Icon::Sparkle, bs, c.showMiniOrb ? "Orb flutuante: ligado" : "Orb flutuante: desligado", c.showMiniOrb, theme::Cyan, false)) {
        g_edit.showMiniOrb = !g_edit.showMiniOrb;
        Apply(app);
    }
    ImGui::SameLine(0, px(4));
    if (w::IconButton("min", Icon::Minimize, bs, "Minimizar", false, theme::Cyan, false)) g_req.minimize = true;
    ImGui::SameLine(0, px(4));
    if (w::IconButton("full", g_fullscreen ? Icon::Shrink : Icon::Expand, bs, g_fullscreen ? "Sair da tela cheia (F11 / Esc)" : "Tela cheia (F11)", g_fullscreen, theme::Cyan, false))
        g_req.toggleFullscreen = true;
    ImGui::SameLine(0, px(4));
    if (w::IconButton("close", Icon::Close, bs, c.minimizeToTray ? "Fechar (continua na bandeja)" : "Sair", false, theme::Red, false)) {
        if (c.minimizeToTray) g_req.hideToTray = true;
        else g_req.quit = true;
    }
}

// ------------------------------------------------------------------ Barra lateral
void Sidebar(App& app, ImGuiViewport* vp, float top, float wdt) {
    struct Item { Icon icon; const char* tip; const char* win; bool* flag; };
    Item items[] = {
        {Icon::Chat, "Conversa", kChat, nullptr},
        {Icon::Grid, "Embeds (itens mostrados)", kEmbeds, &g_showEmbeds},
        {Icon::Cpu, "Modelos de IA e voz", kModels, &g_showModels},
        {Icon::Gear, "Configurações", kSettings, &g_showSettings},
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float bs = px(44);
    float step = px(56);
    float y0 = vp->Pos.y + top + px(12);
    float x = vp->Pos.x + (wdt - bs) * 0.5f;
    float iy = anim::Spring(ImGui::GetID("##sideind"), y0 + g_sel * step, 200.f, 20.f);
    dl->AddRectFilled(ImVec2(vp->Pos.x + px(4), iy + px(9)), ImVec2(vp->Pos.x + px(7), iy + bs - px(9)), anim::Col(theme::Cyan), px(2));
    orbfx::RadialGradient(dl, ImVec2(vp->Pos.x + px(6), iy + bs * 0.5f), 0, px(22), anim::Col(anim::WithAlpha(theme::Cyan, 0.25f)), anim::Col(anim::WithAlpha(theme::Cyan, 0)), 24);
    for (int i = 0; i < 4; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(x, y0 + i * step));
        ImGui::PushID(i);
        if (w::IconButton("side", items[i].icon, bs, items[i].tip, g_sel == i, theme::Cyan, false)) {
            if (items[i].flag) *items[i].flag = true;
            g_focusNext = items[i].win;
            g_sel = i;
        }
        ImGui::PopID();
    }
    // botão de microfone rápido no rodapé
    float by = vp->Pos.y + vp->Size.y - bs - px(16);
    ImGui::SetCursorScreenPos(ImVec2(x, by));
    bool listening = app.State() == AssistState::Listening;
    if (w::IconButton("sidemic", Icon::Mic, bs, listening ? "Parar de ouvir" : "Falar", listening, theme::Green)) app.ToggleListening();
}

void BuildLayout(ImGuiID ds, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(ds);
    ImGui::DockBuilderAddNode(ds, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(ds, size);
    ImGuiID left, right;
    ImGui::DockBuilderSplitNode(ds, ImGuiDir_Right, 0.36f, &right, &left);
    ImGui::DockBuilderDockWindow(kChat, left);
    ImGui::DockBuilderDockWindow(kEmbeds, right);
    ImGui::DockBuilderDockWindow(kSettings, right);
    ImGui::DockBuilderDockWindow(kModels, right); // última aba = selecionada
    ImGui::DockBuilderFinish(ds);
}

void Host(App& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##host", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    float titleH = px(48), sideW = px(66);
    TitleBar(app, vp, titleH);
    Sidebar(app, vp, titleH, sideW);
    ImGui::SetCursorScreenPos(ImVec2(vp->Pos.x + sideW, vp->Pos.y + titleH));
    ImVec2 dsSize(vp->Size.x - sideW - px(10), vp->Size.y - titleH - px(10));
    ImGuiID ds = ImGui::GetID("KBDockspace");
    if (!ImGui::DockBuilderGetNode(ds)) BuildLayout(ds, dsSize);
    ImGui::DockSpace(ds, dsSize, ImGuiDockNodeFlags_None);
    ImGui::End();
}

// ------------------------------------------------------------------ Conversa
void OnboardingCard(App& app) {
    bool online = app.BackendOnline();
    std::string model = app.ActiveModel();
    bool stt = app.SttLoaded();
    std::string voice = app.CurrentVoice();
    bool ptVoice = false;
    for (auto& v : app.Voices()) if (v.name == voice && v.portuguese) ptVoice = true;
    if (online && !model.empty() && stt && ptVoice) return;

    w::BeginCard("##onb", theme::Violet);
    ImGui::PushFont(theme::fonts.semibold);
    ImGui::TextUnformatted("Vamos deixar tudo pronto");
    ImGui::PopFont();
    w::Hint("O KB roda 100%% no seu computador. Verifique os itens abaixo:");
    ImGui::Spacing();
    auto row = [&](bool ok, const char* title, const std::string& detail, const char* btn, auto action) {
        ImGui::PushID(title);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float lh = ImGui::GetTextLineHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec4 c = ok ? theme::Green : theme::Amber;
        dl->AddCircleFilled(ImVec2(p.x + lh * 0.5f, p.y + lh * 0.6f), lh * 0.5f, anim::Col(anim::WithAlpha(c, 0.18f)), 20);
        w::DrawIcon(dl, ok ? Icon::Check : Icon::Warning, ImVec2(p.x + lh * 0.5f, p.y + lh * 0.6f), lh * 0.7f, anim::Col(c));
        ImGui::SetCursorScreenPos(ImVec2(p.x + lh + px(10), p.y));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(title);
        w::Hint("%s", detail.c_str());
        ImGui::EndGroup();
        if (!ok && btn) {
            ImGui::SameLine();
            float bw = ImGui::CalcTextSize(btn).x + px(30);
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw));
            if (w::GhostButton(btn)) action();
        }
        ImGui::PopID();
        ImGui::Spacing();
    };
    row(online, "Servidor de IA", online ? "Conectado" : "Nenhum servidor encontrado (Ollama, LM Studio, llama.cpp…)", "Procurar",
        [&] { app.DetectServers(); g_showModels = true; g_focusNext = kModels; });
    row(!model.empty(), "Modelo de linguagem", model.empty() ? std::string("Recomendado: ") + llm::kRecommendedModel + " (~13 GB)" : model,
        online ? "Baixar recomendado" : "Abrir Modelos", [&] {
            if (online && g_edit.backend == LlmBackend::Ollama) app.PullOllamaModel(llm::kRecommendedModel);
            g_showModels = true;
            g_focusNext = kModels;
        });
    row(stt, "Reconhecimento de voz", stt ? "Whisper carregado" : (app.SttLoading() ? "Carregando…" : "Baixe o modelo Whisper small (466 MB)"),
        app.SttLoading() ? nullptr : "Baixar", [&] { app.DownloadWhisper("small"); g_showModels = true; g_focusNext = kModels; });
    row(ptVoice, "Voz em português", ptVoice ? voice : "Instale uma voz pt-BR: Configurações do Windows > Hora e idioma > Fala",
        "Abrir", [&] { ShellExecuteW(nullptr, L"open", L"ms-settings:speech", nullptr, nullptr, SW_SHOWNORMAL); });
    w::EndCard();
}

void Suggestions(App& app) {
    const char* sugg[] = {"Que horas são?", "Me conte uma curiosidade sobre o espaço", "Crie uma lista de tarefas para hoje",
                          "Explique como funciona a fotossíntese", "Mostre um exemplo de código em Python", "Dicas para dormir melhor"};
    float t = anim::Time() - g_emptySince;
    float avail = ImGui::GetContentRegionAvail().x;
    float x = 0;
    // centraliza linha a linha
    std::vector<std::vector<int>> lines(1);
    std::vector<float> widths(1, 0.f);
    for (int i = 0; i < 6; ++i) {
        float wdt = ImGui::CalcTextSize(sugg[i]).x + px(28) + px(10);
        if (widths.back() + wdt > avail && !lines.back().empty()) { lines.emplace_back(); widths.push_back(0); }
        lines.back().push_back(i);
        widths.back() += wdt;
    }
    for (size_t l = 0; l < lines.size(); ++l) {
        x = std::max(0.f, (avail - widths[l]) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        for (size_t k = 0; k < lines[l].size(); ++k) {
            int i = lines[l][k];
            if (k) ImGui::SameLine(0, px(10));
            float ap = anim::Clamp01((t - 0.3f - i * 0.07f) / 0.5f);
            if (w::Chip(sugg[i], ap)) app.SendText(sugg[i]);
        }
    }
}

// Raciocínio do modelo: bloco recolhível acima da resposta (aberto enquanto ele pensa).
void ThinkingSection(Message& m) {
    ImGui::PushID("think");
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID openId = ImGui::GetID("open"), userId = ImGui::GetID("user");
    bool userSet = st->GetBool(userId, false);
    bool open = userSet ? st->GetBool(openId, true) : (m.thinkingLive || m.text.empty());
    float lh = ImGui::GetTextLineHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float wdt = ImGui::GetContentRegionAvail().x;
    bool clicked = ImGui::InvisibleButton("##hdr", ImVec2(wdt, lh + px(10)));
    bool hov = ImGui::IsItemHovered();
    if (clicked) { st->SetBool(userId, true); st->SetBool(openId, !open); open = !open; }
    float cy = p.y + (lh + px(10)) * 0.5f;
    ImVec4 col = m.thinkingLive ? theme::Violet : theme::TextFaint;
    w::DrawIcon(dl, Icon::Bulb, ImVec2(p.x + lh * 0.5f, cy), lh * 0.9f, anim::Col(col));
    char label[96];
    int words = 0;
    bool inWord = false;
    for (char ch : m.thinking) { bool sp = ch == ' ' || ch == '\n'; if (!sp && !inWord) ++words; inWord = !sp; }
    if (m.thinkingLive) snprintf(label, sizeof(label), "Pensando…  %.0fs", m.thinkSecs);
    else snprintf(label, sizeof(label), "Pensamento (%d palavras, %.0fs)", words, m.thinkSecs);
    ImVec2 ls = ImGui::CalcTextSize(label);
    ImVec2 tp(p.x + lh + px(6), cy - ls.y * 0.5f);
    dl->AddText(tp, anim::Col(anim::Lerp(col, theme::Text, hov ? 0.4f : 0.f)), label);
    if (m.thinkingLive) {
        // brilho deslizante sobre o rótulo enquanto pensa
        float sx = tp.x + std::fmod(anim::Time() * 1.2f, 1.6f) / 1.6f * (ls.x + px(60)) - px(30);
        dl->PushClipRect(tp, ImVec2(tp.x + ls.x, tp.y + ls.y), true);
        dl->AddRectFilledMultiColor(ImVec2(sx - px(30), tp.y), ImVec2(sx, tp.y + ls.y), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 90),
                                    IM_COL32(255, 255, 255, 90), IM_COL32(255, 255, 255, 0));
        dl->AddRectFilledMultiColor(ImVec2(sx, tp.y), ImVec2(sx + px(30), tp.y + ls.y), IM_COL32(255, 255, 255, 90), IM_COL32(255, 255, 255, 0),
                                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 90));
        dl->PopClipRect();
    }
    // seta
    float ax = tp.x + ls.x + px(10), ay = cy;
    float rot = anim::To(ImGui::GetID("arrow"), open ? 1.f : 0.f, 12.f);
    float s = px(4);
    ImVec2 a1(ax - s, ay - s * 0.5f), a2(ax + s, ay - s * 0.5f), a3(ax, ay + s * 0.6f);
    if (rot < 0.5f) { a1 = ImVec2(ax - s * 0.5f, ay - s); a2 = ImVec2(ax - s * 0.5f, ay + s); a3 = ImVec2(ax + s * 0.6f, ay); }
    dl->AddTriangleFilled(a1, a2, a3, anim::Col(col));

    float k = anim::To(ImGui::GetID("openAnim"), open ? 1.f : 0.f, 10.f);
    if (k > 0.01f) {
        ImGui::PushFont(theme::fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, anim::WithAlpha(theme::TextDim, 0.9f * k));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.56f, 0.40f, 1.f, 0.06f * k));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10));
        float full = ImGui::CalcTextSize(m.thinking.c_str(), nullptr, false, wdt - px(28)).y + px(18);
        float hgt = std::min(full, px(220)) * anim::EaseOutCubic(k);
        ImGui::BeginChild("##thinkbody", ImVec2(wdt, std::max(1.f, hgt)), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImVec2 cp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(cp, ImVec2(cp.x + px(3), cp.y + ImGui::GetWindowHeight()), anim::Col(anim::WithAlpha(theme::Violet, 0.6f * k)), px(2));
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(m.thinking.c_str());
        ImGui::PopTextWrapPos();
        if (m.thinkingLive) ImGui::SetScrollHereY(1.f); // acompanha o fim enquanto pensa
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopFont();
    }
    ImGui::Spacing();
    ImGui::PopID();
}

void MessageView(App& app, Message& m, float width) {
    (void)app;
    if (m.parsedLen != m.text.size()) {
        m.blocks = ParseBlocks(m.text);
        m.parsedLen = m.text.size();
    }
    m.appear = anim::Approach(m.appear, 1.f, 7.f);
    float e = anim::EaseOutCubic(m.appear);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1 - e) * px(16));
    float pad = px(14);
    char id[32];
    snprintf(id, sizeof(id), "##msg%d", m.id);
    float x0 = ImGui::GetCursorPosX();
    if (m.user) {
        float maxW = width * 0.74f;
        ImVec2 ts = ImGui::CalcTextSize(m.text.c_str(), nullptr, false, maxW - pad * 2);
        float bw = std::min(maxW, ts.x + pad * 2 + px(4));
        float thumbH = px(120), picsW = 0;
        for (auto& pic : m.pictures) if (pic) picsW += thumbH * pic->w / (float)std::max(1, pic->h) + px(6);
        if (!m.pictures.empty()) bw = std::min(maxW, std::max(bw, picsW + pad * 2));
        ImGui::SetCursorPosX(x0 + width - bw);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, px(10)));
        ImGui::BeginChild(id, ImVec2(bw, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize(), b(a.x + sz.x, a.y + sz.y);
        float r = px(16);
        ImDrawFlags corners = ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomLeft;
        dl->AddRectFilled(a, b, anim::Col(ImVec4(0.22f, 0.40f, 0.95f, 0.55f)), r, corners);
        dl->AddRectFilledMultiColor(ImVec2(a.x + r, a.y), ImVec2(b.x - r * 0.3f, b.y), anim::Col(ImVec4(0.22f, 0.40f, 0.95f, 0.0f)),
                                    anim::Col(ImVec4(0.50f, 0.32f, 0.95f, 0.45f)), anim::Col(ImVec4(0.50f, 0.32f, 0.95f, 0.45f)),
                                    anim::Col(ImVec4(0.22f, 0.40f, 0.95f, 0.0f)));
        dl->AddRect(a, b, anim::Col(ImVec4(0.6f, 0.75f, 1.f, 0.25f)), r, corners, px(1));
        if (!m.pictures.empty()) {
            float x0p = ImGui::GetCursorPosX(), availP = ImGui::GetContentRegionAvail().x, used = 0;
            for (size_t i = 0; i < m.pictures.size(); ++i) {
                auto& pic = m.pictures[i];
                if (!pic) continue;
                float tw = thumbH * pic->w / (float)std::max(1, pic->h);
                if (tw > availP) { tw = availP; }
                float th = tw * pic->h / (float)std::max(1, pic->w);
                if (used > 0 && used + tw <= availP) ImGui::SameLine(0, px(6));
                else if (used > 0) { used = 0; ImGui::SetCursorPosX(x0p); }
                RoundedImage(TextureFor(pic), ImVec2(tw, th), px(10));
                w::Tooltip(pic->label.c_str());
                used += tw + px(6);
            }
        }
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(m.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        if (m.fromVoice) {
            ImVec2 mn = ImGui::GetItemRectMin();
            float s = px(16);
            w::DrawIcon(ImGui::GetWindowDrawList(), Icon::Mic, ImVec2(mn.x - s, mn.y + s), s, anim::Col(theme::TextFaint));
        }
    } else {
        float av = px(30);
        ImVec2 ap = ImGui::GetCursorScreenPos();
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        ImVec2 ac(ap.x + av * 0.5f, ap.y + av * 0.5f + px(2));
        orbfx::RadialGradient(pdl, ac, 0, av * 0.5f, anim::Col(theme::Cyan), anim::Col(theme::Violet), 24);
        orbfx::RadialGradient(pdl, ImVec2(ac.x - av * 0.12f, ac.y - av * 0.14f), 0, av * 0.25f, anim::Col(ImVec4(1, 1, 1, 0.5f)), anim::Col(ImVec4(1, 1, 1, 0)), 16);
        if (m.streaming) {
            float t = std::fmod(anim::Time() * 1.2f, 1.f);
            pdl->AddCircle(ac, av * 0.5f + t * px(8), anim::Col(anim::WithAlpha(theme::Cyan, 0.5f * (1 - t))), 24, px(1.5f));
        }
        ImGui::SetCursorPosX(x0 + av + px(10));
        float maxW = width - av - px(10);
        float bw = maxW;
        bool onlyText = !m.blocks.empty();
        float widest = 0;
        for (auto& b : m.blocks) {
            if (IsEmbed(b.type)) { onlyText = false; break; }
            float indent = (b.type == BlockType::Bullet || b.type == BlockType::Numbered) ? px(22) : 0.f;
            widest = std::max(widest, ImGui::CalcTextSize(b.text.c_str(), nullptr, false, maxW - pad * 2 - indent).x + indent);
        }
        if (onlyText) bw = std::min(maxW, widest + pad * 2 + px(24));
        if (m.streaming && m.text.empty()) bw = px(90);
        if (g_edit.showThinking && !m.thinking.empty()) bw = maxW;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, px(11)));
        ImGui::BeginChild(id, ImVec2(bw, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize(), b(a.x + sz.x, a.y + sz.y);
        float r = px(16);
        ImDrawFlags corners = ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersBottomRight;
        ImVec4 bg = m.error ? ImVec4(1.f, 0.35f, 0.4f, 0.10f) : ImVec4(1, 1, 1, 0.05f);
        ImVec4 bd = m.error ? ImVec4(1.f, 0.4f, 0.45f, 0.35f) : ImVec4(1, 1, 1, 0.08f);
        dl->AddRectFilled(a, b, anim::Col(bg), r, corners);
        dl->AddRect(a, b, anim::Col(bd), r, corners, px(1));
        bool showThink = g_edit.showThinking && !m.thinking.empty();
        if (showThink) ThinkingSection(m);
        if (m.streaming && m.text.empty() && showThink) {
            // ainda pensando: o bloco de pensamento já mostra a animação
        } else if (m.streaming && m.text.empty()) {
            // os pontinhos de digitação
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = ImGui::GetTextLineHeight();
            ImGui::Dummy(ImVec2(px(46), h));
            float t = anim::Time();
            for (int i = 0; i < 3; ++i) {
                float bb = std::max(0.f, std::sin(t * 6.f - i * 0.7f));
                dl->AddCircleFilled(ImVec2(p.x + px(8) + i * px(14), p.y + h * 0.55f - bb * px(5)), px(4),
                                    anim::Col(anim::Lerp(theme::TextFaint, theme::Cyan, bb)), 12);
            }
        } else {
            RenderBlocks(m.blocks, m.id, m.streaming);
        }
        // ações ao passar o mouse
        float hv = anim::To(ImGui::GetID("##hv"), ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !m.streaming ? 1.f : 0.f, 12.f);
        if (hv > 0.02f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * hv);
            float bs = px(26);
            ImGui::SetCursorScreenPos(ImVec2(b.x - bs - px(6), a.y + px(5)));
            if (w::IconButton("cp", Icon::Copy, bs, "Copiar resposta")) ImGui::SetClipboardText(m.text.c_str());
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, px(4)));
}

void InputBar(App& app) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float wdt = ImGui::GetContentRegionAvail().x;
    float h = px(54);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float f = anim::To(ImGui::GetID("##inF"), g_inputFocused ? 1.f : 0.f, 10.f);
    ImVec2 b(p.x + wdt, p.y + h);
    if (f > 0.01f)
        for (int i = 1; i <= 5; ++i)
            dl->AddRect(ImVec2(p.x - i * px(1.5f), p.y - i * px(1.5f)), ImVec2(b.x + i * px(1.5f), b.y + i * px(1.5f)),
                        anim::Col(anim::WithAlpha(theme::Cyan, 0.07f * f * (1 - i / 6.f))), h * 0.5f + i * px(1.5f), 0, px(1.5f));
    dl->AddRectFilled(p, b, anim::Col(ImVec4(0.08f + 0.02f * f, 0.10f + 0.02f * f, 0.17f + 0.03f * f, 0.92f)), h * 0.5f);
    dl->AddRect(p, b, anim::Col(anim::Lerp(ImVec4(1, 1, 1, 0.08f), anim::WithAlpha(theme::Cyan, 0.55f), f)), h * 0.5f, 0, px(1));

    float btn = h - px(12);
    AssistState st = app.State();
    bool busy = st == AssistState::Thinking || st == AssistState::Speaking || app.TtsSpeaking();
    // anexar imagem / compartilhar tela
    float sb = px(36);
    ImGui::SetCursorScreenPos(ImVec2(p.x + px(10), p.y + (h - sb) * 0.5f));
    if (w::IconButton("attach", Icon::Image, sb, "Anexar imagem (ou cole com Ctrl+V / arraste para a janela)", !g_attach.empty(), theme::Cyan)) {
        std::string f = platform::PickFile(L"Imagens\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Todos\0*.*\0", "Escolha uma imagem para o KB ver");
        if (!f.empty()) AddAttachment(app, img::FromFile(f));
    }
    ImGui::SameLine(0, px(2));
    bool sharing = app.Screen().Active();
    if (w::IconButton("screen", Icon::Monitor, sb, sharing ? "Parar de compartilhar a tela" : "Compartilhar a tela com o KB (ao vivo)", sharing, theme::Red))
        app.SetScreenShare(!sharing);
    float inX = p.x + px(10) + sb * 2 + px(8);

    ImGui::SetCursorScreenPos(ImVec2(inX, p.y + (h - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(b.x - inX - btn * 2 - px(26));
    const char* hint = sharing ? "Pergunte sobre a sua tela…" : !g_attach.empty() ? "Pergunte sobre a imagem…" : "Digite ou fale com o KB…";
    bool enter = ImGui::InputTextWithHint("##input", hint, g_input, sizeof(g_input), ImGuiInputTextFlags_EnterReturnsTrue);
    g_inputFocused = ImGui::IsItemActive();
    ImGui::PopStyleColor(3);
    // Ctrl+V com imagem na área de transferência
    if ((g_inputFocused || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) &&
        IsClipboardFormatAvailable(CF_DIB))
        AddAttachment(app, img::FromClipboard());
    bool hasText = g_input[0] != 0 || !g_attach.empty();
    if (enter && hasText) {
        app.SendText(g_input, false, g_attach);
        g_attach.clear();
        g_input[0] = 0;
        g_stickBottom = true;
        ImGui::SetKeyboardFocusHere(-1);
    }

    // microfone
    ImVec2 mp(b.x - btn * 2 - px(14), p.y + px(6));
    ImGui::SetCursorScreenPos(mp);
    bool listening = st == AssistState::Listening;
    ImGuiID mid = ImGui::GetID("##mic");
    bool micClick = ImGui::InvisibleButton("##mic", ImVec2(btn, btn));
    bool micHov = ImGui::IsItemHovered();
    w::Tooltip(listening ? "Parar de ouvir" : "Falar (ou use o atalho)");
    float lf = anim::To(mid, listening ? 1.f : 0.f, 10.f);
    float mh = anim::To(mid + 1, micHov ? 1.f : 0.f, 14.f);
    ImVec2 mc(mp.x + btn * 0.5f, mp.y + btn * 0.5f);
    float lvl = app.MicLevel();
    if (lf > 0.01f) {
        for (int i = 0; i < 3; ++i) {
            float ph = std::fmod(anim::Time() * 1.2f + i / 3.f, 1.f);
            dl->AddCircle(mc, btn * 0.5f + ph * px(14) * (0.6f + lvl), anim::Col(anim::WithAlpha(theme::Green, 0.45f * (1 - ph) * lf)), 32, px(1.5f));
        }
        orbfx::RadialGradient(dl, mc, 0, btn * 0.5f * (1 + 0.15f * lvl), anim::Col(anim::WithAlpha(theme::Green, 0.95f * lf)), anim::Col(anim::WithAlpha(theme::Cyan, 0.85f * lf)), 32);
    }
    dl->AddCircleFilled(mc, btn * 0.5f, anim::Col(ImVec4(1, 1, 1, (0.05f + 0.06f * mh) * (1 - lf))), 32);
    w::DrawIcon(dl, Icon::Mic, mc, btn * 0.5f, anim::Col(anim::Lerp(anim::Lerp(theme::TextDim, theme::Text, mh), ImVec4(0.02f, 0.1f, 0.1f, 1), lf)));
    if (micClick) app.ToggleListening();

    // enviar / parar
    ImVec2 sp(b.x - btn - px(6), p.y + px(6));
    ImGui::SetCursorScreenPos(sp);
    bool sClick = ImGui::InvisibleButton("##send", ImVec2(btn, btn));
    bool sHov = ImGui::IsItemHovered();
    w::Tooltip(busy ? "Parar" : "Enviar");
    ImGuiID sid = ImGui::GetItemID();
    float sh = anim::To(sid, sHov ? 1.f : 0.f, 14.f);
    float act = anim::To(sid + 1, (hasText || busy) ? 1.f : 0.f, 10.f);
    ImVec2 sc(sp.x + btn * 0.5f, sp.y + btn * 0.5f);
    ImVec4 c1 = busy ? theme::Pink : theme::Blue, c2 = busy ? theme::Violet : theme::Violet;
    orbfx::RadialGradient(dl, sc, 0, btn * 0.5f * (1 + 0.06f * sh), anim::Col(anim::WithAlpha(c1, 0.25f + 0.7f * act)), anim::Col(anim::WithAlpha(c2, 0.2f + 0.7f * act)), 32);
    w::DrawIcon(dl, busy ? Icon::Stop : Icon::Send, sc, btn * 0.5f, anim::Col(ImVec4(1, 1, 1, 0.5f + 0.5f * act)));
    if (sClick) {
        if (busy) app.Interrupt();
        else if (hasText) {
            app.SendText(g_input, false, g_attach);
            g_attach.clear();
            g_input[0] = 0;
            g_stickBottom = true;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
    ImGui::Dummy(ImVec2(0, 0));
}

std::string LastModelUsed(App& app) {
    {
        std::lock_guard<std::mutex> l(app.MessagesMutex());
        auto& ms = app.MessagesUnsafe();
        if (!ms.empty() && !ms.back().user && !ms.back().modelUsed.empty()) return ms.back().modelUsed;
    }
    return app.ActiveModel();
}

void ChatWindow(App& app) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(360), px(380)), ImVec2(FLT_MAX, FLT_MAX));
    if (!BeginPanel(kChat, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse, g_orb.CurrentColor())) {
        ImGui::End();
        return;
    }
    TrackFocus(0);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    bool empty;
    {
        std::lock_guard<std::mutex> l(app.MessagesMutex());
        empty = app.MessagesUnsafe().empty();
    }
    static bool wasEmpty = false;
    if (empty && !wasEmpty) g_emptySince = anim::Time();
    wasEmpty = empty;

    float target = empty ? avail.y * 0.46f : std::clamp(avail.y * 0.34f, px(190), px(270));
    float orbH = anim::To(ImGui::GetID("##orbH"), target, 5.f);
    // reserva espaço para o rótulo + legenda abaixo do orb
    ImGui::PushFont(theme::fonts.title);
    float titleLH = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    float labelBlock = titleLH + ImGui::GetFontSize() + px(22);
    float orbZone = std::max(px(60), orbH - labelBlock);
    float R = std::min({orbZone * 0.34f, avail.x * 0.18f, px(118)});
    ImVec2 c(origin.x + avail.x * 0.5f, origin.y + orbZone * 0.52f);
    if (!empty) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - px(34), origin.y));
        if (w::IconButton("clr", Icon::Trash, px(32), "Limpar conversa")) app.ClearConversation();
    }
    ImGui::SetCursorScreenPos(ImVec2(c.x - R * 1.15f, c.y - R * 1.15f));
    bool clicked = ImGui::InvisibleButton("##orb", ImVec2(R * 2.3f, R * 2.3f));
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) app.ToggleListening();
    float hist[48];
    OrbInput in = MakeOrbInput(app, g_mode, hist, 48, hov, held, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    g_orb.Draw(dl, c, R, in);

    // rótulo do estado com transição
    static std::string cur, prev;
    static float fade = 1.f;
    std::string lbl = ModeLabel(g_mode);
    if (g_mode == OrbMode::Idle) lbl = text::GreetingForNow() + "! Como posso ajudar?";
    if (lbl != cur) { prev = cur; cur = lbl; fade = 0.f; }
    fade = anim::Approach(fade, 1.f, 7.f);
    ImGui::PushFont(theme::fonts.title);
    float ly = origin.y + orbZone + px(2);
    auto drawCentered = [&](const std::string& s, float y, float alpha, ImVec4 col) {
        ImVec2 ts = ImGui::CalcTextSize(s.c_str());
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, y), anim::Col(col, alpha), s.c_str());
    };
    if (fade < 0.99f && !prev.empty()) drawCentered(prev, ly - fade * px(10), 1 - fade, theme::Text);
    drawCentered(cur, ly + (1 - anim::EaseOutCubic(fade)) * px(10), anim::EaseOutCubic(fade), theme::Text);
    float lh = ImGui::GetTextLineHeight();
    ImGui::PopFont();

    // legenda
    ImGui::PushFont(theme::fonts.caption);
    std::string cap;
    Config cfg = app.Cfg();
    switch (g_mode) {
    case OrbMode::Listening: cap = "Fale agora — eu paro quando você terminar"; break;
    case OrbMode::Transcribing: cap = "Transcrevendo sua fala…"; break;
    case OrbMode::Thinking: cap = app.LiveCaption().empty() ? "Consultando " + LastModelUsed(app) : "“" + app.LiveCaption() + "”"; break;
    case OrbMode::Speaking: case OrbMode::Showing: cap = LastModelUsed(app); break;
    case OrbMode::Error: cap = "Veja a mensagem abaixo"; break;
    default:
        cap = std::string("Clique no orb, pressione ") + HotkeyName(cfg.hotkeyMods, cfg.hotkeyVk) +
              (cfg.wakeWordEnabled ? " ou diga “" + cfg.wakeWord + "”" : "");
    }
    if (cap.size() > 90) cap = cap.substr(0, 87) + "…";
    {
        ImVec2 ts = ImGui::CalcTextSize(cap.c_str());
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, ly + lh + px(4)), anim::Col(theme::TextFaint), cap.c_str());
    }
    ImGui::PopFont();

    // lista de mensagens
    float inputH = px(54);
    float attachH = anim::To(ImGui::GetID("##attH"), g_attach.empty() ? 0.f : px(74), 12.f);
    float listTop = origin.y + orbH + px(6);
    float listH = std::max(px(60), origin.y + avail.y - listTop - inputH - attachH - px(12));
    ImGui::SetCursorScreenPos(ImVec2(origin.x, listTop));
    ImGui::BeginChild("##list", ImVec2(avail.x, listH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    {
        // fade nas bordas da lista
        if (ImGui::GetIO().MouseWheel > 0 && ImGui::IsWindowHovered()) g_stickBottom = false;
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0) && ImGui::GetScrollY() < ImGui::GetScrollMaxY() - px(4)) g_stickBottom = false;
        float width = ImGui::GetContentRegionAvail().x - px(8);
        if (empty) {
            ImGui::Dummy(ImVec2(0, px(6)));
            Suggestions(app);
            ImGui::Dummy(ImVec2(0, px(12)));
            OnboardingCard(app);
        } else {
            std::lock_guard<std::mutex> l(app.MessagesMutex());
            auto& ms = app.MessagesUnsafe();
            int ver = app.MessagesVersion();
            if (ver != g_lastVersion && !ms.empty() && ms.back().user) g_stickBottom = true;
            g_lastVersion = ver;
            for (auto& m : ms) MessageView(app, m, width);
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        float sy = ImGui::GetScrollY(), my = ImGui::GetScrollMaxY();
        if (sy >= my - px(2)) g_stickBottom = true;
        if (g_stickBottom && my > 0 && !empty) ImGui::SetScrollY(anim::Approach(sy, my, 12.f) + 0.5f);
    }
    ImGui::EndChild();
    // anexos aguardando envio
    if (attachH > 1.f) {
        float y = origin.y + avail.y - inputH - attachH;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + px(14), y + px(4)));
        float th = std::max(1.f, attachH - px(12));
        for (size_t i = 0; i < g_attach.size(); ++i) {
            auto& a = g_attach[i];
            ImGui::PushID((int)i);
            float tw = th * a->w / (float)std::max(1, a->h);
            ImVec2 tp = ImGui::GetCursorScreenPos();
            RoundedImage(TextureFor(a), ImVec2(tw, th), px(10));
            ImGui::GetWindowDrawList()->AddRect(tp, ImVec2(tp.x + tw, tp.y + th), anim::Col(anim::WithAlpha(theme::Cyan, 0.5f)), px(10), 0, px(1));
            w::Tooltip(a->label.c_str());
            ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(tp.x + tw - px(22), tp.y + px(2)));
            bool rm = w::IconButton("rm", Icon::Close, px(20), "Remover");
            ImGui::SetCursorScreenPos(ImVec2(tp.x + tw + px(10), tp.y));
            (void)after;
            ImGui::PopID();
            if (rm) { g_attach.erase(g_attach.begin() + i); break; }
        }
    }
    // prévia ao vivo da tela compartilhada
    if (app.Screen().Active()) {
        if (auto fr = app.Screen().Latest(); fr && app.Screen().FrameId() != g_screenFrame) {
            g_screenFrame = app.Screen().FrameId();
            g_screenTex = UploadTexture(g_screenTex, fr->rgba.data(), fr->w, fr->h);
            g_screenW = fr->w;
            g_screenH = fr->h;
        }
        if (g_screenTex && g_screenW > 0) {
            float pw = std::min(px(220), avail.x * 0.3f), ph = pw * g_screenH / (float)g_screenW;
            ImVec2 pp(origin.x + px(4), origin.y + px(4));
            ImDrawList* fdl = ImGui::GetWindowDrawList();
            fdl->AddRectFilled(ImVec2(pp.x - px(4), pp.y - px(4)), ImVec2(pp.x + pw + px(4), pp.y + ph + px(28)), anim::Col(ImVec4(0.05f, 0.06f, 0.11f, 0.92f)), px(12));
            fdl->AddImageRounded((ImTextureID)(intptr_t)g_screenTex, pp, ImVec2(pp.x + pw, pp.y + ph), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(8));
            float pulse = 0.5f + 0.5f * std::sin(anim::Time() * 4.f);
            fdl->AddCircleFilled(ImVec2(pp.x + px(10), pp.y + ph + px(12)), px(4.5f), anim::Col(anim::WithAlpha(theme::Red, 0.6f + 0.4f * pulse)), 16);
            ImGui::PushFont(theme::fonts.caption);
            fdl->AddText(ImVec2(pp.x + px(20), pp.y + ph + px(4)), anim::Col(theme::Text), "AO VIVO · o KB vê sua tela");
            ImGui::PopFont();
            fdl->AddRect(ImVec2(pp.x - px(4), pp.y - px(4)), ImVec2(pp.x + pw + px(4), pp.y + ph + px(28)), anim::Col(anim::WithAlpha(theme::Red, 0.35f + 0.3f * pulse)), px(12), 0, px(1.5f));
            ImGui::SetCursorScreenPos(ImVec2(pp.x + pw - px(26), pp.y + px(2)));
            if (w::IconButton("stopshare", Icon::Close, px(24), "Parar de compartilhar")) app.SetScreenShare(false);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + avail.y - inputH));
    InputBar(app);
    ImGui::End();
}

// ------------------------------------------------------------------ Embeds
void EmbedsWindow(App& app) {
    if (!BeginPanel(kEmbeds, &g_showEmbeds, 0, theme::Amber)) { ImGui::End(); return; }
    TrackFocus(1);
    std::vector<std::pair<int, Block>> items;
    {
        std::lock_guard<std::mutex> l(app.MessagesMutex());
        auto& ms = app.MessagesUnsafe();
        for (auto it = ms.rbegin(); it != ms.rend(); ++it) {
            if (it->user) continue;
            if (it->parsedLen != it->text.size()) { it->blocks = ParseBlocks(it->text); it->parsedLen = it->text.size(); }
            for (size_t i = 0; i < it->blocks.size(); ++i)
                if (IsEmbed(it->blocks[i].type)) items.push_back({it->id * 64 + (int)i, it->blocks[i]});
        }
    }
    w::SectionTitle("Itens mostrados", Icon::Grid);
    if (items.empty()) {
        ImGui::Dummy(ImVec2(0, px(20)));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float aw = ImGui::GetContentRegionAvail().x;
        float t = anim::Time();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c(p.x + aw * 0.5f, p.y + px(40) + std::sin(t * 1.5f) * px(4));
        orbfx::RadialGradient(dl, c, 0, px(46), anim::Col(anim::WithAlpha(theme::Amber, 0.18f)), anim::Col(anim::WithAlpha(theme::Amber, 0)), 32);
        w::DrawIcon(dl, Icon::Sparkle, c, px(40), anim::Col(theme::Amber));
        ImGui::Dummy(ImVec2(0, px(96)));
        w::Hint("Quando o KB mostrar código, tabelas, passos ou cartões, eles aparecem aqui. "
                "Use o botão de janela em qualquer item para abri-lo flutuando na tela — e arraste a aba de "
                "qualquer painel para fora para criar uma janela separada, ou de volta para acoplar.");
    } else {
        w::Hint("%d item(ns). Clique no ícone de janela para destacar.", (int)items.size());
        ImGui::Spacing();
        for (auto& [key, b] : items) {
            ImGui::PushID(key);
            RenderEmbed(b, key + 500000, true);
            ImGui::PopID();
            ImGui::Spacing();
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------------ Modelos
void JobsList(App& app) {
    auto jobs = app.Jobs();
    if (jobs.empty()) return;
    w::SectionTitle("Downloads", Icon::Download);
    for (auto& j : jobs) {
        ImGui::PushID(j.get());
        w::BeginCard("##job", j->failed ? theme::Red : j->finished ? theme::Green : theme::Cyan);
        ImGui::TextUnformatted(j->title.c_str());
        std::string st = j->Status();
        uint64_t d = j->done, t = j->total;
        if (t) st += "  ·  " + text::FormatBytes(d) + " / " + text::FormatBytes(t);
        w::Hint("%s", st.c_str());
        if (!j->finished) {
            w::Progress(j->Progress(), ImVec2(ImGui::GetContentRegionAvail().x - px(90), px(8)), t == 0);
            ImGui::SameLine();
            if (w::GhostButton("Cancelar")) j->cancel = true;
        }
        w::EndCard();
        ImGui::PopID();
        ImGui::Spacing();
    }
}

bool ModelRow(const ModelInfo& m, bool active, int idx) {
    ImGui::PushID(idx);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float wdt = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetTextLineHeight() * 2.4f;
    bool click = ImGui::InvisibleButton("##row", ImVec2(wdt, h));
    ImGuiID id = ImGui::GetItemID();
    float hv = anim::To(id, ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
    float ac = anim::To(id + 1, active ? 1.f : 0.f, 10.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + wdt, p.y + h);
    dl->AddRectFilled(p, b, anim::Col(anim::Lerp(ImVec4(1, 1, 1, 0.02f + 0.03f * hv), anim::WithAlpha(theme::Cyan, 0.12f), ac)), px(12));
    if (ac > 0.01f) dl->AddRect(p, b, anim::Col(anim::WithAlpha(theme::Cyan, 0.5f * ac)), px(12), 0, px(1));
    float cy = p.y + h * 0.5f;
    float rr = px(8);
    ImVec2 rc(p.x + px(18), cy);
    dl->AddCircle(rc, rr, anim::Col(anim::Lerp(theme::TextFaint, theme::Cyan, ac)), 20, px(1.5f));
    dl->AddCircleFilled(rc, rr * 0.55f * anim::EaseOutBack(ac), anim::Col(theme::Cyan), 16);
    float lh = ImGui::GetTextLineHeight();
    dl->AddText(ImVec2(p.x + px(36), cy - lh), anim::Col(theme::Text), m.name.c_str());
    std::string det = (m.size ? text::FormatBytes(m.size) : std::string("tamanho desconhecido")) + (m.details.empty() ? "" : "  ·  " + m.details);
    ImGui::PushFont(theme::fonts.caption);
    dl->AddText(ImVec2(p.x + px(36), cy + px(1)), anim::Col(theme::TextFaint), det.c_str());
    if (m.vision) {
        const char* vl = "visão";
        ImVec2 vs = ImGui::CalcTextSize(vl);
        ImVec2 dsz = ImGui::CalcTextSize(det.c_str());
        ImVec2 va(p.x + px(36) + dsz.x + px(10), cy + px(1));
        dl->AddRectFilled(ImVec2(va.x, va.y), ImVec2(va.x + vs.x + px(12), va.y + vs.y + px(2)), anim::Col(anim::WithAlpha(theme::Green, 0.18f)), px(8));
        dl->AddText(ImVec2(va.x + px(6), va.y + px(1)), anim::Col(theme::Green), vl);
    }
    double gb = m.size / 1073741824.0;
    if (gb >= 11.0 && gb <= 15.5) {
        const char* bl = "≈13 GB";
        ImVec2 ts = ImGui::CalcTextSize(bl);
        ImVec2 ba(b.x - ts.x - px(28), cy - ts.y * 0.5f - px(2));
        dl->AddRectFilled(ba, ImVec2(ba.x + ts.x + px(14), ba.y + ts.y + px(4)), anim::Col(anim::WithAlpha(theme::Violet, 0.25f)), px(10));
        dl->AddText(ImVec2(ba.x + px(7), ba.y + px(2)), anim::Col(theme::Violet), bl);
    }
    ImGui::PopFont();
    ImGui::PopID();
    return click;
}

void ModelsWindow(App& app) {
    if (!BeginPanel(kModels, &g_showModels, 0, theme::Violet)) { ImGui::End(); return; }
    TrackFocus(2);
    bool online = app.BackendOnline();

    w::SectionTitle("Servidor de IA", Icon::Cpu);
    {
        const char* labels[] = {"Ollama", "OpenAI-compat.", "Arquivo GGUF"};
        int b = (int)g_edit.backend;
        if (Segmented("backend", &b, labels, 3)) {
            g_edit.backend = (LlmBackend)b;
            Apply(app);
        }
        w::StatusDot(online ? theme::Green : app.ModelsLoading() ? theme::Amber : theme::Red, true);
        ImGui::SameLine();
        std::string err = app.ModelsError();
        if (online) ImGui::Text("Conectado a %s", llm::BaseUrl(g_edit).c_str());
        else {
            ImGui::TextUnformatted(app.ModelsLoading() ? "Conectando…" : "Desconectado");
            if (!err.empty()) w::Hint("%s", err.c_str());
        }
        if (g_edit.backend != LlmBackend::LocalGguf) {
            std::string& url = g_edit.backend == LlmBackend::Ollama ? g_edit.ollamaUrl : g_edit.openaiUrl;
            if (!ImGui::IsItemActive() && strcmp(g_urlBuf, url.c_str()) != 0 && !ImGui::IsAnyItemActive())
                snprintf(g_urlBuf, sizeof(g_urlBuf), "%s", url.c_str());
            Label("Endereço");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##url", g_urlBuf, sizeof(g_urlBuf));
            if (ImGui::IsItemDeactivatedAfterEdit()) { url = g_urlBuf; Apply(app); }
            if (g_edit.backend == LlmBackend::OpenAICompat) {
                if (!ImGui::IsAnyItemActive()) snprintf(g_keyBuf, sizeof(g_keyBuf), "%s", g_edit.openaiKey.c_str());
                Label("Chave de API (opcional)");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##key", g_keyBuf, sizeof(g_keyBuf), ImGuiInputTextFlags_Password);
                if (ImGui::IsItemDeactivatedAfterEdit()) { g_edit.openaiKey = g_keyBuf; Apply(app); }
            }
        }
        ImGui::Spacing();
        if (app.DetectingServers()) {
            w::Spinner("##det", px(10), px(2.5f), theme::Cyan);
            ImGui::SameLine();
            ImGui::TextUnformatted("Procurando servidores locais…");
        } else if (w::PrimaryButton("Procurar servidores", ImVec2(0, 0), Icon::Search)) {
            app.DetectServers();
        }
        ImGui::SameLine();
        if (w::GhostButton("Atualizar", ImVec2(0, 0), Icon::Refresh)) app.RefreshModels();

        auto servers = app.Servers();
        for (size_t i = 0; i < servers.size(); ++i) {
            auto& s = servers[i];
            ImGui::PushID((int)i);
            w::BeginCard("##srv", theme::Green);
            ImGui::PushFont(theme::fonts.semibold);
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::PopFont();
            w::Hint("%s  ·  %d modelo(s)", s.url.c_str(), (int)s.models.size());
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - px(70));
            if (w::GhostButton("Usar")) {
                g_edit.backend = s.backend;
                if (s.backend == LlmBackend::Ollama) g_edit.ollamaUrl = s.url;
                else g_edit.openaiUrl = s.url;
                g_edit.model = s.models.empty() ? "" : g_edit.model;
                Apply(app);
            }
            w::EndCard();
            ImGui::PopID();
        }
    }

    if (g_edit.backend != LlmBackend::LocalGguf) {
        w::SectionTitle("Modelos instalados", Icon::Sparkle);
        auto models = app.Models();
        std::string active = app.ActiveModel();
        if (models.empty()) {
            w::Hint(online ? "Nenhum modelo instalado ainda." : "Conecte-se a um servidor para ver os modelos.");
        } else {
            w::Hint("Clique para escolher. Sem escolha, o KB usa o maior modelo que cabe na sua RAM.");
            for (size_t i = 0; i < models.size(); ++i)
                if (ModelRow(models[i], models[i].name == active, (int)i)) { g_edit.model = models[i].name; Apply(app); }
            if (!g_edit.model.empty() && w::GhostButton("Escolher automaticamente")) { g_edit.model.clear(); Apply(app); }
        }
    }

    if (g_edit.backend == LlmBackend::Ollama) {
        w::SectionTitle("Baixar modelo", Icon::Download);
        uint64_t ram = platform::TotalRamBytes();
        w::Hint("Seu PC tem %s de RAM. Modelos de ~13 GB precisam de 16 GB+ (ideal: GPU com 16 GB de VRAM).", text::FormatBytes(ram).c_str());
        w::BeginCard("##rec", theme::Violet);
        ImGui::PushFont(theme::fonts.semibold);
        ImGui::TextUnformatted("Recomendados (português + visão)");
        ImGui::PopFont();
        w::Hint("%s · ~8 GB — Gemma 3 12B: enxerga imagens e a tela; cabe em placas de vídeo de 8 GB.", llm::kRecommendedLight);
        if (w::PrimaryButton("Baixar gemma3:12b (~8 GB)", ImVec2(0, 0), Icon::Download)) app.PullOllamaModel(llm::kRecommendedLight);
        w::Hint("%s · ~13 GB — mesma família em qualidade máxima (precisa de ~16 GB de RAM livre).", llm::kRecommendedModel);
        if (w::GhostButton("Baixar versão de 13 GB", ImVec2(0, 0), Icon::Download)) app.PullOllamaModel(llm::kRecommendedModel);
        w::EndCard();
        ImGui::Spacing();
        const char* others[] = {"mistral-nemo", "qwen2.5vl:7b", "qwen2.5:14b", "llama3.1:8b", "gemma3:4b"};
        const char* sizes[] = {"7 GB", "6 GB, visão", "9 GB", "4.9 GB", "3.3 GB, visão"};
        for (int i = 0; i < 5; ++i) {
            char lbl[96];
            snprintf(lbl, sizeof(lbl), "%s · %s", others[i], sizes[i]);
            if (i) {
                float nx = ImGui::GetItemRectMax().x + px(10) + ImGui::CalcTextSize(lbl).x + px(28);
                if (nx < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine(0, px(10));
            }
            if (w::Chip(lbl)) app.PullOllamaModel(others[i]);
        }
        ImGui::Spacing();
        Label("Ou digite o nome de qualquer modelo da biblioteca do Ollama:");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - px(110));
        ImGui::InputTextWithHint("##pull", "ex.: qwen2.5:14b-instruct-q6_K", g_pullName, sizeof(g_pullName));
        ImGui::SameLine();
        if (w::GhostButton("Baixar", ImVec2(px(100), 0)) && g_pullName[0]) app.PullOllamaModel(text::Trim(g_pullName));
    }

    JobsList(app);

    w::SectionTitle("Arquivos GGUF no seu PC", Icon::Folder);
    {
        w::Hint("Usa um .gguf que você já tem (LM Studio, downloads…) com o llama-server do llama.cpp.");
        if (app.ScanningFiles()) {
            w::Spinner("##scan", px(10), px(2.5f), theme::Cyan);
            ImGui::SameLine();
            ImGui::TextUnformatted("Procurando arquivos…");
        } else if (w::GhostButton("Procurar arquivos", ImVec2(0, 0), Icon::Search)) {
            app.ScanLocalFiles();
        }
        ImGui::SameLine();
        if (w::GhostButton("Escolher .gguf…", ImVec2(0, 0), Icon::Folder)) {
            std::string f = platform::PickFile(L"Modelos GGUF\0*.gguf\0Todos\0*.*\0", "Escolha um modelo .gguf");
            if (!f.empty()) { g_edit.ggufPath = f; g_edit.backend = LlmBackend::LocalGguf; Apply(app); }
        }
        auto files = app.GgufFiles();
        for (size_t i = 0; i < files.size() && i < 30; ++i) {
            auto& f = files[i];
            ImGui::PushID((int)i + 100);
            bool active = g_edit.ggufPath == f.path;
            ModelInfo mi{f.name, f.size, ""};
            if (ModelRow(mi, active && g_edit.backend == LlmBackend::LocalGguf, (int)i + 100)) {
                g_edit.ggufPath = f.path;
                g_edit.backend = LlmBackend::LocalGguf;
                Apply(app);
            }
            ImGui::PopID();
        }
        if (g_edit.backend == LlmBackend::LocalGguf) {
            ImGui::Spacing();
            Label("llama-server.exe");
            ImGui::TextWrapped("%s", g_edit.llamaServerPath.empty() ? "(não encontrado)" : g_edit.llamaServerPath.c_str());
            if (w::GhostButton("Escolher llama-server.exe…")) {
                std::string f = platform::PickFile(L"llama-server\0llama-server.exe\0Executáveis\0*.exe\0", "Localize o llama-server.exe");
                if (!f.empty()) { g_edit.llamaServerPath = f; Apply(app); }
            }
            ImGui::SameLine();
            if (w::GhostButton("Baixar llama.cpp")) platform::OpenUrl("https://github.com/ggml-org/llama.cpp/releases");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##ngl", &g_edit.ggufGpuLayers, 0, 99, "Camadas na GPU: %d")) {}
            if (ImGui::IsItemDeactivatedAfterEdit()) Apply(app);
            w::StatusDot(app.LocalServerReady() ? theme::Green : app.LocalServerRunning() ? theme::Amber : theme::Red, app.LocalServerRunning());
            ImGui::SameLine();
            ImGui::TextUnformatted(app.LocalServerReady() ? "Modelo local pronto" : app.LocalServerRunning() ? "Carregando modelo…" : "Parado");
            ImGui::SameLine();
            if (app.LocalServerRunning()) { if (w::GhostButton("Parar")) app.StopLocalServer(); }
            else if (w::GhostButton("Iniciar")) app.StartLocalServer();
        }
    }

    w::SectionTitle("Reconhecimento de voz (Whisper)", Icon::Mic);
    {
        w::StatusDot(app.SttLoaded() ? theme::Green : app.SttLoading() ? theme::Amber : theme::Red, app.SttLoading());
        ImGui::SameLine();
        std::string path = app.SttModelPath();
        ImGui::TextUnformatted(app.SttLoaded() ? path.substr(path.find_last_of("\\/") + 1).c_str() : app.SttLoading() ? "Carregando…" : "Nenhum modelo carregado");
        for (auto& o : stt::Options()) {
            ImGui::PushID(o.id);
            std::string dest = config::ModelsDir() + "\\ggml-" + o.id + ".bin";
            bool have = platform::FileExists(dest);
            bool inUse = app.SttModelPath() == dest;
            ImVec2 p = ImGui::GetCursorScreenPos();
            (void)p;
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", o.label);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextFaint);
            ImGui::Text("%s", o.size);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            uint64_t partial = have ? 0 : platform::FileSize(dest + ".part");
            if (partial) {
                ImGui::SameLine();
                char pct[48];
                snprintf(pct, sizeof(pct), "incompleto: %.0f%%", 100.0 * partial / (double)o.bytes);
                w::Badge(pct, theme::Amber);
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - px(110));
            if (inUse) w::Badge("Em uso", theme::Green);
            else if (have) { if (w::GhostButton("Usar", ImVec2(px(104), 0))) { g_edit.whisperModel = dest; Apply(app); } }
            else if (w::GhostButton(partial ? "Continuar" : "Baixar", ImVec2(px(104), 0))) app.DownloadWhisper(o.id);
            ImGui::PopID();
        }
        for (auto& f : app.WhisperFiles()) {
            if (f.path.find(config::ModelsDir()) == 0) continue;
            ImGui::PushID(f.path.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", f.name.c_str());
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - px(96));
            if (app.SttModelPath() == f.path) w::Badge("Em uso", theme::Green);
            else if (w::GhostButton("Usar", ImVec2(px(90), 0))) { g_edit.whisperModel = f.path; Apply(app); }
            ImGui::PopID();
        }
        if (w::GhostButton("Escolher arquivo .bin…", ImVec2(0, 0), Icon::Folder)) {
            std::string f = platform::PickFile(L"Modelos Whisper\0ggml-*.bin\0Todos\0*.*\0", "Escolha um modelo do Whisper");
            if (!f.empty()) { g_edit.whisperModel = f; Apply(app); }
        }
    }
    ImGui::Dummy(ImVec2(0, px(20)));
    ImGui::End();
}

// ------------------------------------------------------------------ Celular (QR code)
void DrawQr(const std::string& data, float size) {
    using qrcodegen::QrCode;
    QrCode qr = QrCode::encodeText(data.c_str(), QrCode::Ecc::MEDIUM);
    int n = qr.getSize();
    const float border = 2.f;
    float cell = size / (n + border * 2);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(255, 255, 255, 255), px(10));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (qr.getModule(x, y)) {
                ImVec2 a(p.x + (border + x) * cell, p.y + (border + y) * cell);
                dl->AddRectFilled(a, ImVec2(a.x + cell + 0.5f, a.y + cell + 0.5f), IM_COL32(11, 15, 26, 255));
            }
}

void MobileSection(App& app, bool& ch) {
    w::SectionTitle("Celular (Android e iPhone)", Icon::Phone);
    w::Hint("Use o KB pelo celular na mesma rede Wi-Fi: converse, mande fotos da câmera para ele ver e ouça as respostas.");
    ch |= w::Toggle("Permitir acesso pelo celular", &g_edit.mobileEnabled,
                    "Abre um servidor só na sua rede local, protegido por um código secreto no QR code.");
    if (!app.MobileRunning()) return;
    std::string url = app.MobileUrl();
    float qs = std::min(px(220), ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - qs) * 0.5f);
    DrawQr(url, qs);
    ImGui::Spacing();
    ImGui::PushFont(theme::fonts.mono);
    ImGui::TextWrapped("%s", url.c_str());
    ImGui::PopFont();
    if (w::GhostButton("Copiar link", ImVec2(0, 0), Icon::Copy)) ImGui::SetClipboardText(url.c_str());
    w::Hint("1. Conecte o celular no mesmo Wi-Fi do PC.\n2. Aponte a câmera para o QR code.\n"
            "3. iPhone: Compartilhar > Adicionar à Tela de Início.  Android: menu ⋮ > Adicionar à tela inicial.\n"
            "Se o Windows perguntar sobre o Firewall, permita o KB em redes privadas.");
    auto ips = app.LanIps();
    if (ips.size() > 1) {
        std::string others;
        for (size_t i = 1; i < ips.size(); ++i) others += (i > 1 ? ", " : "") + ips[i];
        w::Hint("Se não abrir, troque o endereço por: %s", others.c_str());
    }
}

// ------------------------------------------------------------------ Configurações
void SettingsWindow(App& app) {
    if (!BeginPanel(kSettings, &g_showSettings, 0, theme::Cyan)) { ImGui::End(); return; }
    TrackFocus(3);
    bool ch = false;

    w::SectionTitle("Geral", Icon::Gear);
    ch |= w::Toggle("Iniciar com o Windows", &g_edit.startWithWindows, "Abre o KB automaticamente quando você liga o PC");
    if (g_edit.startWithWindows) ch |= w::Toggle("   Iniciar minimizada na bandeja", &g_edit.startMinimized);
    ch |= w::Toggle("Fechar para a bandeja do sistema", &g_edit.minimizeToTray);
    ch |= w::Toggle("Orb flutuante quando minimizada", &g_edit.showMiniOrb);
    ch |= w::Toggle("Saudação por voz ao iniciar", &g_edit.greetOnStart);
    ch |= w::Toggle("Reduzir animações", &g_edit.reduceMotion);
    ch |= w::Toggle("Mostrar desempenho (CPU e GPU) na barra de título", &g_edit.showPerf);
    ch |= w::Toggle("Mostrar o pensamento da IA", &g_edit.showThinking, "Modelos de raciocínio (ex.: qwen3.5, deepseek-r1) mostram como chegaram à resposta.");
    ch |= w::Toggle("Foto de fundo", &g_edit.bgImage);
    if (g_edit.bgImage) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##bgop", &g_edit.bgOpacity, 0.1f, 1.f, "Intensidade da foto: %.2f", ImGuiSliderFlags_None);
        if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    }
    Label("Atalho global para falar");
    int cur = -1;
    for (int i = 0; i < (int)std::size(kHotkeys); ++i)
        if (kHotkeys[i].mods == g_edit.hotkeyMods && kHotkeys[i].vk == g_edit.hotkeyVk) cur = i;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##hk", cur >= 0 ? kHotkeys[cur].name : "Personalizado")) {
        for (int i = 0; i < (int)std::size(kHotkeys); ++i)
            if (ImGui::Selectable(kHotkeys[i].name, i == cur)) {
                g_edit.hotkeyMods = kHotkeys[i].mods;
                g_edit.hotkeyVk = kHotkeys[i].vk;
                ch = true;
            }
        ImGui::EndCombo();
    }

    w::SectionTitle("Fala", Icon::Wave);
    ch |= w::Toggle("Responder em voz alta", &g_edit.speakReplies);
    auto voices = app.Voices();
    std::string curVoice = app.CurrentVoice();
    Label("Voz");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##voice", curVoice.empty() ? "(padrão)" : curVoice.c_str())) {
        for (int pass = 0; pass < 2; ++pass)
            for (auto& v : voices) {
                if ((pass == 0) != v.portuguese) continue;
                std::string l = v.name + "  (" + v.language + ")";
                if (ImGui::Selectable(l.c_str(), v.name == curVoice)) { g_edit.ttsVoice = v.name; ch = true; }
            }
        ImGui::EndCombo();
    }
    bool anyPt = false;
    for (auto& v : voices) anyPt |= v.portuguese;
    if (!anyPt) {
        w::BeginCard("##nopt", theme::Amber);
        ImGui::TextWrapped("Nenhuma voz em português instalada.");
        w::Hint("Configurações do Windows > Hora e idioma > Idioma e região > Português (Brasil) > Opções > Fala. Depois reinicie o KB.");
        if (w::GhostButton("Abrir configurações de fala")) ShellExecuteW(nullptr, L"open", L"ms-settings:speech", nullptr, nullptr, SW_SHOWNORMAL);
        w::EndCard();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##rate", &g_edit.ttsRate, -5, 6, "Velocidade: %d");
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##vol", &g_edit.ttsVolume, 10, 100, "Volume: %d%%");
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    if (w::GhostButton("Testar voz", ImVec2(0, 0), Icon::Play)) app.TestVoice();

    w::SectionTitle("Microfone", Icon::Mic);
    {
        static std::vector<std::string> devs;
        static double lastEnum = -100;
        if (NowSeconds() - lastEnum > 10) { devs = app.MicDevices(); lastEnum = NowSeconds(); }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##mic", g_edit.micDevice.empty() ? "Padrão do Windows" : g_edit.micDevice.c_str())) {
            if (ImGui::Selectable("Padrão do Windows", g_edit.micDevice.empty())) { g_edit.micDevice.clear(); ch = true; }
            for (auto& d : devs)
                if (ImGui::Selectable(d.c_str(), d == g_edit.micDevice)) { g_edit.micDevice = d; ch = true; }
            ImGui::EndCombo();
        }
        Label("Nível do microfone");
        w::Progress(app.MicLevel(), ImVec2(-1, px(8)));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##sens", &g_edit.micSensitivity, 0.4f, 3.0f, "Sensibilidade: %.1fx");
        if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
        ch |= w::Toggle("Palavra de ativação (sempre ouvindo)", &g_edit.wakeWordEnabled,
                        "Fica ouvindo e responde quando você diz a palavra. Usa mais CPU.");
        if (g_edit.wakeWordEnabled) {
            if (!ImGui::IsAnyItemActive()) snprintf(g_wakeBuf, sizeof(g_wakeBuf), "%s", g_edit.wakeWord.c_str());
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##wake", "kb", g_wakeBuf, sizeof(g_wakeBuf));
            if (ImGui::IsItemDeactivatedAfterEdit()) { g_edit.wakeWord = text::Trim(g_wakeBuf); ch = true; }
            w::Hint("Diga, por exemplo: “%s, que horas são?”", g_edit.wakeWord.c_str());
        }
    }

    w::SectionTitle("Inteligência", Icon::Sparkle);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##temp", &g_edit.temperature, 0.f, 1.2f, "Criatividade (temperatura): %.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##ctx", &g_edit.contextSize, 2048, 32768, "Contexto: %d tokens");
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##hist", &g_edit.historyTurns, 1, 40, "Memória da conversa: %d trocas");
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    ch |= w::Toggle("Trocar para um modelo com visão quando houver imagem", &g_edit.autoVisionModel,
                    "Ex.: o mistral-nemo não enxerga imagens; o KB usa automaticamente um modelo com visão que você tenha.");

    MobileSection(app, ch);

    w::SectionTitle("Sobre", Icon::Info);
    w::Hint("KB 1.0 — assistente de voz local. Seus dados ficam no seu PC.");
    if (w::GhostButton("Abrir pasta de dados", ImVec2(0, 0), Icon::Folder)) platform::OpenFolder(config::AppDataDir());
    ImGui::Dummy(ImVec2(0, px(20)));

    if (ch) Apply(app);
    ImGui::End();
}

// ------------------------------------------------------------------ Orb flutuante
void MiniOrb(App& app) {
    ImGuiWindowClass wc;
    wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_TopMost | ImGuiViewportFlags_NoTaskBarIcon;
    wc.ParentViewportId = 0;
    ImGui::SetNextWindowClass(&wc);
    ImVec2 size(px(300), px(76));
    ImVec2 pos(g_edit.miniOrbX, g_edit.miniOrbY);
    const auto& mons = ImGui::GetPlatformIO().Monitors;
    if (g_edit.miniOrbX < 0 && !mons.empty()) {
        const auto& m = mons[0];
        pos = ImVec2(m.WorkPos.x + m.WorkSize.x - size.x - px(24), m.WorkPos.y + m.WorkSize.y - size.y - px(24));
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##KBMini", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    ImGuiViewport* vp = ImGui::GetWindowViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImVec2 wb(wp.x + ws.x, wp.y + ws.y);
    ImVec4 acc = g_miniOrb.CurrentColor();
    dl->AddRectFilledMultiColor(wp, wb, anim::Col(ImVec4(0.06f, 0.07f, 0.13f, 1)), anim::Col(ImVec4(0.05f, 0.05f, 0.11f, 1)),
                                anim::Col(ImVec4(0.08f, 0.05f, 0.15f, 1)), anim::Col(ImVec4(0.06f, 0.07f, 0.13f, 1)));
    orbfx::RadialGradient(dl, ImVec2(wp.x + px(38), wp.y + ws.y * 0.5f), 0, px(110), anim::Col(anim::WithAlpha(acc, 0.22f)), anim::Col(anim::WithAlpha(acc, 0)), 40);

    ImDrawList* fg = ImGui::GetWindowDrawList();
    float R = px(22);
    ImVec2 c(wp.x + px(38), wp.y + ws.y * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(c.x - R * 1.3f, c.y - R * 1.3f));
    bool clicked = ImGui::InvisibleButton("##mo", ImVec2(R * 2.6f, R * 2.6f));
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    w::Tooltip("Clique para falar");
    if (clicked) app.ToggleListening();
    float hist[32];
    OrbInput in = MakeOrbInput(app, g_mode, hist, 32, hov, held, true);
    g_miniOrb.Draw(fg, c, R, in);

    float tx = wp.x + px(76);
    ImGui::PushFont(theme::fonts.semibold);
    std::string lbl = ModeLabel(g_mode);
    if (g_mode == OrbMode::Idle) lbl = "KB";
    fg->AddText(ImVec2(tx, wp.y + px(14)), anim::Col(theme::Text), lbl.c_str());
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts.caption);
    std::string cap = app.LiveCaption();
    if (g_mode == OrbMode::Idle || cap.empty()) {
        Config cfg = app.Cfg();
        cap = cfg.wakeWordEnabled ? "Diga “" + cfg.wakeWord + "” ou clique no orb" : std::string(HotkeyName(cfg.hotkeyMods, cfg.hotkeyVk));
    }
    float maxW = ws.x - px(76) - px(44);
    fg->PushClipRect(ImVec2(tx, wp.y), ImVec2(tx + maxW, wb.y), true);
    fg->AddText(ImVec2(tx, wp.y + px(40)), anim::Col(theme::TextDim), cap.c_str());
    fg->PopClipRect();
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(wb.x - px(40), wp.y + ws.y * 0.5f - px(16)));
    if (w::IconButton("open", Icon::PopOut, px(32), "Abrir KB")) app.requestShowMain = true;

    if (ImGui::BeginPopupContextWindow("##mctx")) {
        if (ImGui::MenuItem("Abrir KB")) app.requestShowMain = true;
        if (ImGui::MenuItem("Falar agora")) app.StartListening();
        if (ImGui::MenuItem("Ocultar orb flutuante")) { g_edit.showMiniOrb = false; Apply(app); }
        ImGui::Separator();
        if (ImGui::MenuItem("Sair")) app.requestQuit = true;
        ImGui::EndPopup();
    }
    // guarda a posição depois de arrastar
    static ImVec2 lastPos(-1, -1);
    if (!ImGui::IsMouseDown(0) && (std::fabs(wp.x - lastPos.x) > 0.5f || std::fabs(wp.y - lastPos.y) > 0.5f)) {
        if (lastPos.x >= 0 && (std::fabs(wp.x - g_edit.miniOrbX) > 0.5f || std::fabs(wp.y - g_edit.miniOrbY) > 0.5f)) {
            g_edit.miniOrbX = wp.x;
            g_edit.miniOrbY = wp.y;
            Apply(app);
        }
        lastPos = wp;
    }
    ImGui::End();
}

// ------------------------------------------------------------------ Notificações
void Toasts(App& app) {
    auto toasts = app.Toasts();
    if (toasts.empty()) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    double now = NowSeconds();
    float y = vp->Pos.y + vp->Size.y - px(96);
    ImGui::PushFont(theme::fonts.caption);
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        float age = (float)(now - it->time);
        float in = anim::EaseOutBack(anim::Clamp01(age / 0.45f));
        float out = anim::Clamp01((6.f - age) / 0.5f);
        float a = anim::Clamp01(age / 0.2f) * out;
        float maxW = px(360);
        ImVec2 ts = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, maxW);
        float wdt = ts.x + px(52), h = ts.y + px(22);
        float x = vp->Pos.x + vp->Size.x - px(24) - wdt + (1 - in) * (wdt + px(40));
        ImVec2 p(x, y - h);
        ImVec4 col = it->kind == 2 ? theme::Red : it->kind == 1 ? theme::Green : theme::Cyan;
        dl->AddRectFilled(ImVec2(p.x + px(2), p.y + px(4)), ImVec2(p.x + wdt + px(2), p.y + h + px(4)), IM_COL32(0, 0, 0, (int)(70 * a)), px(14));
        dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), ImGui::GetColorU32(ImVec4(0.09f, 0.10f, 0.18f, 0.97f * a)), px(14));
        dl->AddRect(p, ImVec2(p.x + wdt, p.y + h), ImGui::GetColorU32(anim::WithAlpha(col, 0.45f * a)), px(14), 0, px(1));
        w::DrawIcon(dl, it->kind == 2 ? Icon::Warning : it->kind == 1 ? Icon::Check : Icon::Info, ImVec2(p.x + px(22), p.y + h * 0.5f), px(18),
                    ImGui::GetColorU32(anim::WithAlpha(col, a)));
        dl->AddText(theme::fonts.caption, theme::fonts.caption->FontSize, ImVec2(p.x + px(40), p.y + px(11)), ImGui::GetColorU32(anim::WithAlpha(theme::Text, a)),
                    it->text.c_str(), nullptr, maxW);
        y -= (h + px(10)) * std::min(1.f, a * 1.5f + 0.2f);
    }
    ImGui::PopFont();
}
} // namespace

void Init(App& app, float scale) {
    theme::Load(scale);
    assets::Load();
    g_edit = app.Cfg();
}

WindowRequests& Requests() { return g_req; }
void SetFullscreen(bool on) { g_fullscreen = on; }
void OnFilesDropped(App& app, const std::vector<std::string>& paths) {
    int added = 0;
    for (auto& f : paths)
        if (img::IsImageFile(f)) { AddAttachment(app, img::FromFile(f)); ++added; }
    if (!added && !paths.empty()) app.Notify("Arraste imagens (PNG, JPG, BMP, GIF) para o KB ver.", 0);
}
void SetDebugMode(int m) { g_debugMode = m; }
TitleBarInfo GetTitleBar() { return g_title; }
bool WantsContinuousRender() { return true; }

const char* HotkeyName(int mods, int vk) {
    for (auto& h : kHotkeys)
        if (h.mods == mods && h.vk == vk) return h.name;
    return "atalho personalizado";
}

void Frame(App& app, bool mainVisible) {
    CollectTextures();
    Config cfg = app.Cfg();
    anim::SetReduceMotion(cfg.reduceMotion);
    SetBackdropPhoto(cfg.bgImage, ImGui::IsAnyItemActive() ? g_edit.bgOpacity : cfg.bgOpacity);
    if (!ImGui::IsAnyItemActive()) g_edit = cfg;
    g_mode = g_debugMode >= 0 ? (OrbMode)g_debugMode : ComputeMode(app);

    if (g_focusNext) {
        ImGui::SetWindowFocus(g_focusNext);
        g_focusNext = nullptr;
    }

    if (mainVisible) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        DrawBackdrop(ImGui::GetBackgroundDrawList(vp), vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), g_orb.CurrentColor(), true);
        Host(app);
        ChatWindow(app);
        if (g_showEmbeds) EmbedsWindow(app);
        if (g_showModels) ModelsWindow(app);
        if (g_showSettings) SettingsWindow(app);
        Toasts(app);
    }
    static int popTest = GetEnvironmentVariableW(L"KB_POPOUT", nullptr, 0) > 0 ? 30 : -1;
    if (popTest > 0 && --popTest == 0) {
        std::lock_guard<std::mutex> l(app.MessagesMutex());
        for (auto& m : app.MessagesUnsafe())
            for (size_t i = 0; i < m.blocks.size(); ++i)
                if (IsEmbed(m.blocks[i].type) && popTest == 0) { PopOut(m.blocks[i], m.id * 64 + (int)i, true); popTest = -1; }
    }
    if (cfg.showMiniOrb && !mainVisible) MiniOrb(app);
    RenderFloatingEmbeds();
}

} // namespace ui
