#include "ui/embeds.h"
#include "ui/anim.h"
#include "ui/orb.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/assets.h"
#include "core/platform.h"
#include "core/text.h"
#include "core/files.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>

using theme::px;

namespace ui {

namespace {
std::unordered_map<ImGuiID, float> g_firstSeen;

float Age(ImGuiID id) {
    float t = anim::Time();
    auto it = g_firstSeen.find(id);
    if (it == g_firstSeen.end()) it = g_firstSeen.emplace(id, t).first;
    return t - it->second;
}

struct Floating {
    int key;
    Block block;
    bool open = true;
    bool pinned = false;
    bool pinApplied = false;
    void* lastHwnd = nullptr;
    float appear = 0.f;
    ImVec2 pos;
};
std::vector<Floating> g_floats;

ImVec4 EmbedColor(BlockType t) {
    switch (t) {
    case BlockType::Code: return theme::Blue;
    case BlockType::Card: return theme::Cyan;
    case BlockType::Steps: return theme::Violet;
    case BlockType::Warning: return theme::Amber;
    case BlockType::Tip: return theme::Green;
    case BlockType::Table: return theme::Pink;
    case BlockType::File: return theme::Amber;
    default: return theme::Cyan;
    }
}

Icon EmbedIcon(BlockType t) {
    switch (t) {
    case BlockType::Code: return Icon::Cpu;
    case BlockType::Card: return Icon::Sparkle;
    case BlockType::Steps: return Icon::Check;
    case BlockType::Warning: return Icon::Warning;
    case BlockType::Tip: return Icon::Bulb;
    case BlockType::Table: return Icon::Grid;
    case BlockType::File: return Icon::File;
    default: return Icon::Info;
    }
}

std::string EmbedText(const Block& b) {
    switch (b.type) {
    case BlockType::Steps: {
        std::string s;
        for (size_t i = 0; i < b.items.size(); ++i) s += std::to_string(i + 1) + ". " + b.items[i] + "\n";
        return s;
    }
    case BlockType::Table: {
        std::string s;
        for (auto& r : b.rows) {
            for (size_t i = 0; i < r.size(); ++i) s += (i ? "\t" : "") + r[i];
            s += "\n";
        }
        return s;
    }
    case BlockType::Card: return b.title + "\n" + b.text;
    default: return b.text;
    }
}

void TypingDots() {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(px(46), h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = anim::Time();
    for (int i = 0; i < 3; ++i) {
        float ph = t * 6.f - i * 0.7f;
        float b = std::max(0.f, std::sin(ph));
        ImVec2 c(p.x + px(8) + i * px(14), p.y + h * 0.55f - b * px(5));
        dl->AddCircleFilled(c, px(4), anim::Col(anim::Lerp(theme::TextFaint, theme::Cyan, b)), 12);
    }
}

void Paragraph(const std::string& s, bool caret) {
    if (caret) {
        bool on = std::fmod(anim::Time(), 1.0f) < 0.55f;
        std::string t = s + (on ? " \xE2\x96\x8D" : "  ");
        ImGui::TextWrapped("%s", t.c_str());
    } else {
        ImGui::TextWrapped("%s", s.c_str());
    }
}
} // namespace

const char* EmbedTitle(const Block& b) {
    switch (b.type) {
    case BlockType::Code: return "Código";
    case BlockType::Card: return "Cartão";
    case BlockType::Steps: return "Passo a passo";
    case BlockType::Warning: return "Atenção";
    case BlockType::Tip: return "Dica";
    case BlockType::Table: return "Tabela";
    case BlockType::File: return "Pedido para criar arquivo";
    default: return "Item";
    }
}

static void EmbedHeader(const Block& b, int key, bool allowPopOut, ImVec4 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float right = p.x + ImGui::GetContentRegionAvail().x;
    w::DrawIcon(dl, EmbedIcon(b.type), ImVec2(p.x + lh * 0.5f, p.y + lh * 0.5f), lh * 0.95f, anim::Col(col));
    std::string label = EmbedTitle(b);
    if (b.type == BlockType::Code && !b.title.empty()) label = b.title;
    if (b.type == BlockType::Card && !b.title.empty()) label = b.title;
    ImGui::SetCursorScreenPos(ImVec2(p.x + lh + px(8), p.y));
    ImGui::PushFont(theme::fonts.semibold);
    ImGui::PushStyleColor(ImGuiCol_Text, b.type == BlockType::Card ? theme::Text : col);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (!b.complete) {
        ImGui::SameLine();
        w::Spinner("##sp", lh * 0.4f, px(2), col);
    }
    // botões no canto direito
    float bs = lh * 1.35f;
    int nbtn = allowPopOut ? 2 : 1;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(right - nbtn * (bs + px(4)), p.y - (bs - lh) * 0.5f));
    ImGui::PushID(key);
    if (w::IconButton("copy", Icon::Copy, bs, "Copiar")) ImGui::SetClipboardText(EmbedText(b).c_str());
    if (allowPopOut) {
        ImGui::SameLine(0, px(4));
        if (w::IconButton("pop", Icon::PopOut, bs, "Abrir em janela flutuante")) PopOut(b, key);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + std::max(lh, bs) + px(6)));
    ImGui::Dummy(ImVec2(0, 0));
}

namespace {
void Label2(const char* s) {
    ImGui::PushFont(theme::fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
} // namespace
// ------------------------------------------------------------------ Pedido de criação de arquivo
namespace {
struct FileState {
    bool resolved = false;
    files::Resolved res;
    int status = 0;  // 0 aguardando, 1 criado, 2 negado, 3 erro
    std::string error;
    std::string resolvedFor;
};
std::unordered_map<int, FileState> g_fileStates;

void FileRequestCard(const Block& b, int key, ImVec4 col) {
    int skey = key % 500000; // o mesmo pedido no chat, no painel Embeds e na janela flutuante
    FileState& st = g_fileStates[skey];
    if (!st.resolved || st.resolvedFor != b.title) {
        st.res = files::Resolve(b.title);
        st.resolved = true;
        st.resolvedFor = b.title;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;

    Label2("Caminho exato do arquivo:");
    ImGui::PushFont(theme::fonts.mono);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string shown = st.res.ok ? st.res.path : (b.title.empty() ? std::string("(o modelo não informou o caminho)") : b.title);
        ImVec2 ts = ImGui::CalcTextSize(shown.c_str(), nullptr, false, avail - px(20));
        dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + ts.y + px(14)), anim::Col(ImVec4(0, 0, 0, 0.35f)), px(8));
        dl->AddRect(p, ImVec2(p.x + avail, p.y + ts.y + px(14)), anim::Col(anim::WithAlpha(col, 0.5f)), px(8), 0, px(1));
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(10), p.y + px(7)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail - px(20));
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ts.y + px(20)));
    }
    ImGui::PopFont();

    if (!st.res.ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Red);
        ImGui::TextWrapped("Não é possível criar: %s", st.res.error.c_str());
        ImGui::PopStyleColor();
        return;
    }
    if (st.res.exists && st.status == 0) w::Badge("Já existe — será SUBSTITUÍDO", theme::Red);
    else if (st.status == 0) w::Badge("Arquivo novo", theme::Green);
    if (st.res.parentMissing && st.status == 0) { ImGui::SameLine(); w::Badge("a pasta também será criada", theme::Amber); }
    if (st.res.risky && st.status == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Amber);
        ImGui::TextWrapped("Atenção: é um tipo de arquivo que pode executar comandos ou fica numa pasta do sistema. Só permita se você confia no conteúdo.");
        ImGui::PopStyleColor();
    }

    // prévia do conteúdo
    int lines = 1;
    for (char ch : b.text) lines += ch == '\n';
    Label2((std::string("Conteúdo (") + std::to_string(lines) + (lines == 1 ? " linha, " : " linhas, ") + text::FormatBytes(b.text.size()) + "):").c_str());
    ImGui::PushFont(theme::fonts.mono);
    float lh = ImGui::GetTextLineHeightWithSpacing();
    float h = std::min(px(220), lh * std::min(lines, 12) + px(16));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.30f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    char cid[32];
    snprintf(cid, sizeof(cid), "##fprev%d", key);
    ImGui::BeginChild(cid, ImVec2(avail, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(b.text.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();

    if (!b.complete) {
        w::Spinner("##fsp", px(9), px(2), col);
        ImGui::SameLine();
        ImGui::TextDisabled("Recebendo o conteúdo…");
        return;
    }
    ImGui::PushID(skey);
    switch (st.status) {
    case 0:
        if (w::PrimaryButton("Permitir criar este arquivo", ImVec2(0, 0), Icon::Check)) {
            std::string err;
            files::Resolved again = files::Resolve(b.title); // revalida no momento do clique
            if (!again.ok || again.path != st.res.path) { st.status = 3; st.error = "o caminho mudou; peça de novo"; }
            else if (files::WriteExact(st.res.path, b.text, &err)) st.status = 1;
            else { st.status = 3; st.error = err; }
        }
        ImGui::SameLine();
        if (w::GhostButton("Negar", ImVec2(0, 0), Icon::Close)) st.status = 2;
        break;
    case 1:
        w::Badge("Arquivo criado", theme::Green);
        ImGui::SameLine();
        if (w::GhostButton("Mostrar na pasta", ImVec2(0, 0), Icon::Folder)) files::RevealInExplorer(st.res.path);
        break;
    case 2: w::Badge("Você negou — nada foi criado", theme::TextFaint); break;
    default:
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Red);
        ImGui::TextWrapped("Falhou: %s", st.error.c_str());
        ImGui::PopStyleColor();
        if (w::GhostButton("Tentar de novo")) st.status = 0;
    }
    ImGui::PopID();
}
} // namespace

void RenderEmbed(const Block& b, int key, bool allowPopOut) {
    ImVec4 col = EmbedColor(b.type);
    ImGuiID id = ImGui::GetID(key);
    float age = Age(id);
    float e = anim::EaseOutBack(anim::Clamp01(age / 0.45f));
    float alpha = anim::Clamp01(age / 0.3f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
    // leve "pulo" de entrada
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.f - e) * px(10));

    char cid[32];
    snprintf(cid, sizeof(cid), "##emb%d", key);
    w::BeginCard(cid, col, ImGui::GetContentRegionAvail().x);
    EmbedHeader(b, key, allowPopOut, col);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    switch (b.type) {
    case BlockType::Code: {
        ImVec2 a = ImGui::GetCursorScreenPos();
        float wdt = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(theme::fonts.mono);
        // fundo do código (usa a altura do frame anterior guardada por ID)
        ImGuiID hid = ImGui::GetID("codeh");
        float hPrev = ImGui::GetStateStorage()->GetFloat(hid, 0.f);
        dl->AddRectFilled(ImVec2(a.x - px(6), a.y - px(4)), ImVec2(a.x + wdt + px(6), a.y + hPrev + px(4)), anim::Col(ImVec4(0, 0, 0, 0.28f)), px(8));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wdt);
        for (auto& line : text::Split(b.text, '\n')) {
            std::string t = text::Trim(line);
            ImVec4 c = theme::Text;
            if (text::StartsWith(t, "//") || text::StartsWith(t, "#") || text::StartsWith(t, "--")) c = theme::TextFaint;
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextWrapped("%s", line.empty() ? " " : line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::GetStateStorage()->SetFloat(hid, ImGui::GetItemRectSize().y);
        ImGui::PopFont();
        break;
    }
    case BlockType::Card:
    case BlockType::Warning:
    case BlockType::Tip: {
        ImGui::PushStyleColor(ImGuiCol_Text, b.type == BlockType::Card ? theme::TextDim : theme::Text);
        ImGui::TextWrapped("%s", b.text.c_str());
        ImGui::PopStyleColor();
        break;
    }
    case BlockType::Steps: {
        float lh = ImGui::GetTextLineHeight();
        float r = lh * 0.62f;
        for (size_t i = 0; i < b.items.size(); ++i) {
            float sa = anim::Clamp01((age - 0.15f - i * 0.12f) / 0.35f);
            float se = anim::EaseOutCubic(sa);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 c(p.x + r, p.y + lh * 0.5f + px(1));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * sa);
            ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + px(12) + (1 - se) * px(12), p.y));
            ImGui::BeginGroup();
            ImGui::TextWrapped("%s", b.items[i].c_str());
            ImGui::EndGroup();
            float itemH = ImGui::GetItemRectSize().y;
            ImGui::PopStyleVar();
            if (i + 1 < b.items.size())
                dl->AddLine(ImVec2(c.x, c.y + r), ImVec2(c.x, p.y + itemH + ImGui::GetStyle().ItemSpacing.y + lh * 0.5f - r),
                            anim::Col(anim::WithAlpha(col, 0.35f), sa), px(2));
            dl->AddCircleFilled(c, r * (0.6f + 0.4f * anim::EaseOutBack(sa)), anim::Col(anim::WithAlpha(col, 0.22f), sa), 24);
            dl->AddCircle(c, r * (0.6f + 0.4f * anim::EaseOutBack(sa)), anim::Col(col, sa), 24, px(1.5f));
            char num[8];
            snprintf(num, sizeof(num), "%d", (int)i + 1);
            ImGui::PushFont(theme::fonts.caption);
            ImVec2 ns = ImGui::CalcTextSize(num);
            dl->AddText(ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f), anim::Col(theme::Text, sa), num);
            ImGui::PopFont();
        }
        break;
    }
    case BlockType::File: FileRequestCard(b, key, col); break;
    case BlockType::Table: {
        int cols = 0;
        for (auto& r : b.rows) cols = std::max(cols, (int)r.size());
        if (cols > 0 && cols < 64) {
            char tid[32];
            snprintf(tid, sizeof(tid), "##tbl%d", key);
            if (ImGui::BeginTable(tid, cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_PadOuterX)) {
                for (size_t r = 0; r < b.rows.size(); ++r) {
                    ImGui::TableNextRow(r == 0 ? ImGuiTableRowFlags_Headers : 0, ImGui::GetTextLineHeight() + px(8));
                    for (int ci = 0; ci < cols; ++ci) {
                        ImGui::TableSetColumnIndex(ci);
                        const std::string& cell = ci < (int)b.rows[r].size() ? b.rows[r][ci] : std::string();
                        if (r == 0) {
                            ImGui::PushFont(theme::fonts.semibold);
                            ImGui::PushStyleColor(ImGuiCol_Text, col);
                            ImGui::TextWrapped("%s", cell.c_str());
                            ImGui::PopStyleColor();
                            ImGui::PopFont();
                        } else {
                            ImGui::TextWrapped("%s", cell.c_str());
                        }
                    }
                }
                ImGui::EndTable();
            }
        }
        break;
    }
    default: break;
    }
    w::EndCard();
    ImGui::PopStyleVar();
}

void RenderBlocks(const std::vector<Block>& blocks, int msgId, bool streaming) {
    float lh = ImGui::GetTextLineHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < blocks.size(); ++i) {
        const Block& b = blocks[i];
        bool last = i + 1 == blocks.size();
        bool caret = streaming && last;
        ImGui::PushID((int)i);
        switch (b.type) {
        case BlockType::Paragraph: Paragraph(b.text, caret); break;
        case BlockType::Heading:
            ImGui::PushFont(theme::fonts.semibold);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::Cyan);
            ImGui::TextWrapped("%s", b.text.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            break;
        case BlockType::Bullet: {
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(p.x + px(6), p.y + lh * 0.55f), px(3), anim::Col(theme::Cyan), 12);
            ImGui::Indent(px(18));
            Paragraph(b.text, caret);
            ImGui::Unindent(px(18));
            break;
        }
        case BlockType::Numbered: {
            ImVec2 p = ImGui::GetCursorScreenPos();
            char num[12];
            snprintf(num, sizeof(num), "%d.", b.number);
            dl->AddText(p, anim::Col(theme::Cyan), num);
            ImGui::Indent(px(22));
            Paragraph(b.text, caret);
            ImGui::Unindent(px(22));
            break;
        }
        case BlockType::Quote: {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Indent(px(14));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
            Paragraph(b.text, caret);
            ImGui::PopStyleColor();
            ImGui::Unindent(px(14));
            dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + px(3), ImGui::GetItemRectMax().y), anim::Col(theme::Violet), px(2));
            break;
        }
        default:
            ImGui::Dummy(ImVec2(0, px(2)));
            RenderEmbed(b, msgId * 64 + (int)i, true);
            ImGui::Dummy(ImVec2(0, px(2)));
            break;
        }
        ImGui::PopID();
    }
}

void PopOut(const Block& b, int key, bool pinned) {
    for (auto& f : g_floats)
        if (f.key == key) {
            f.open = true;
            ImGui::SetWindowFocus(("###float" + std::to_string(key)).c_str());
            return;
        }
    Floating f;
    f.key = key;
    f.block = b;
    f.pinned = pinned;
    ImVec2 m = ImGui::GetIO().MousePos;
    f.pos = ImVec2(m.x + px(30) + g_floats.size() * px(24), m.y - px(60) + g_floats.size() * px(24));
    g_floats.push_back(std::move(f));
}

int FloatingCount() { return (int)g_floats.size(); }

void RenderFloatingEmbeds() {
    for (size_t i = 0; i < g_floats.size();) {
        Floating& f = g_floats[i];
        f.appear = anim::Approach(f.appear, f.open ? 1.f : 0.f, f.open ? 9.f : 14.f);
        if (!f.open && f.appear < 0.02f) {
            g_floats.erase(g_floats.begin() + i);
            continue;
        }
        ImGuiWindowClass wc;
        wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&wc);
        ImGui::SetNextWindowPos(f.pos, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(px(460), px(340)), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(px(260), px(160)), ImVec2(FLT_MAX, FLT_MAX));
        float e = anim::EaseOutCubic(f.appear);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
        std::string title = std::string(EmbedTitle(f.block)) + (f.block.title.empty() ? "" : " — " + f.block.title) +
                            "###float" + std::to_string(f.key);
        bool open = f.open;
        if (BeginPanel(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings, EmbedColor(f.block.type))) {
            ImGuiViewport* vp = ImGui::GetWindowViewport();
            // fixar no topo
            if (w::IconButton("pin", Icon::Pin, ImGui::GetFrameHeight(), f.pinned ? "Desafixar" : "Manter sempre visível", f.pinned)) {
                f.pinned = !f.pinned;
                f.pinApplied = false;
            }
            ImGui::SameLine();
            w::Hint(f.pinned ? "Sempre no topo" : "Arraste a aba para acoplar no KB");
            if (vp->PlatformHandleRaw != f.lastHwnd) { f.lastHwnd = vp->PlatformHandleRaw; f.pinApplied = false; }
            if (!f.pinApplied && vp != ImGui::GetMainViewport()) {
                platform::SetTopMost(vp->PlatformHandleRaw, f.pinned);
                f.pinApplied = true;
            }
            ImGui::Spacing();
            RenderEmbed(f.block, f.key + 1000000, false);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        if (!open) f.open = false;
        ++i;
    }
}

bool BeginPanel(const char* name, bool* open, ImGuiWindowFlags flags, ImVec4 accent) {
    static std::unordered_map<ImGuiID, bool> transparent;
    ImGuiID id = ImHashStr(name);
    auto it = transparent.find(id);
    bool tr = it == transparent.end() ? true : it->second;
    if (tr) flags |= ImGuiWindowFlags_NoBackground;
    bool vis = ImGui::Begin(name, open, flags);
    ImGuiViewport* vp = ImGui::GetWindowViewport();
    bool own = vp != ImGui::GetMainViewport();
    // só a conversa deixa a foto de fundo aparecer; os outros painéis são sólidos (legibilidade)
    bool isChat = strstr(name, "###chat") != nullptr;
    transparent[id] = own || (ImGui::IsWindowDocked() && isChat);
    if (own && vis) DrawBackdrop(ImGui::GetBackgroundDrawList(vp), vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), accent, false);
    return vis;
}

static bool g_photoOn = true;
static float g_photoOpacity = 0.45f;
void SetBackdropPhoto(bool on, float opacity) { g_photoOn = on; g_photoOpacity = opacity; }

void DrawBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 accent, bool particles) {
    float t = anim::Time() * (anim::ReduceMotion() ? 0.2f : 1.f);
    ImU32 top = anim::Col(theme::Bg), bot = anim::Col(theme::Bg2);
    dl->AddRectFilledMultiColor(a, b, top, top, bot, bot);
    int iw = 0, ih = 0;
    ImTextureID photo = assets::Background(&iw, &ih);
    if (particles && g_photoOn && photo && iw > 0 && ih > 0) {
        // foto atrás da conversa, com bordas que se dissolvem no grafite
        float H = (b.y - a.y) * 1.02f, W = H * iw / (float)ih;
        float breathe = 1.f + 0.008f * std::sin(t * 0.25f);
        H *= breathe; W *= breathe;
        float cx = a.x + (b.x - a.x) * 0.56f, top0 = a.y + ((b.y - a.y) - H) * 0.5f;
        ImVec2 p0(cx - W * 0.5f, top0), p1(cx + W * 0.5f, top0 + H);
        float op = 0.15f + 0.55f * std::clamp(g_photoOpacity, 0.f, 1.f);
        dl->AddImage(photo, p0, p1, ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(ImVec4(1, 1, 1, op)));
        ImU32 solid = anim::Col(theme::Bg), clear = anim::Col(theme::Bg, 0.f);
        float fw = W * 0.30f;
        dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + fw, p1.y), solid, clear, clear, solid);
        dl->AddRectFilledMultiColor(ImVec2(p1.x - fw, p0.y), p1, clear, solid, solid, clear);
        dl->AddRectFilled(a, ImVec2(p0.x + 1, b.y), solid);
        dl->AddRectFilled(ImVec2(p1.x - 1, a.y), b, solid);
        // camada escura + vinheta inferior: o texto sempre legível
        dl->AddRectFilled(a, b, anim::Col(theme::Bg, 0.30f));
        dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - (b.y - a.y) * 0.45f), b, clear, clear, anim::Col(theme::Bg, 0.92f), anim::Col(theme::Bg, 0.92f));
    }
    // ---- iluminação de estúdio: luz principal azul (cima/esquerda), contraluz vermelha (baixo/direita),
    // luz da cor do KB no centro e vinheta nas bordas
    float W = b.x - a.x, H = b.y - a.y, R = std::max(W, H);
    float drift = std::sin(t * 0.07f);
    orbfx::RadialGradient(dl, ImVec2(a.x + W * (0.10f + 0.04f * drift), a.y - H * 0.08f), 0, R * 0.8f,
                          anim::Col(anim::WithAlpha(theme::Cyan, 0.11f)), anim::Col(anim::WithAlpha(theme::Cyan, 0.f)), 72);
    orbfx::RadialGradient(dl, ImVec2(b.x - W * (0.04f - 0.03f * drift), b.y + H * 0.06f), 0, R * 0.62f,
                          anim::Col(anim::WithAlpha(theme::Red, 0.075f)), anim::Col(anim::WithAlpha(theme::Red, 0.f)), 72);
    if (particles && accent.w > 0)
        orbfx::RadialGradient(dl, ImVec2(a.x + W * 0.52f, a.y + H * 0.36f), 0, R * 0.42f,
                              anim::Col(anim::WithAlpha(accent, 0.06f)), anim::Col(anim::WithAlpha(accent, 0.f)), 64);
    ImU32 vin = anim::Col(ImVec4(0, 0, 0, 0.38f)), none = anim::Col(ImVec4(0, 0, 0, 0));
    float vw = std::min(W, H) * 0.16f;
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + vw, b.y), vin, none, none, vin);
    dl->AddRectFilledMultiColor(ImVec2(b.x - vw, a.y), b, none, vin, vin, none);
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - vw), b, none, none, vin, vin);
}

} // namespace ui
