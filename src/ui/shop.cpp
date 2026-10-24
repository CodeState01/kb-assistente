// Loja do KB: vitrine com os produtos da treinadora e das marcas parceiras.
#include "ui/shared.h"
#include "app.h"
#include "audio/sfx.h"
#include "core/platform.h"
#include "core/text.h"
#include "features/store.h"
#include "ui/anim.h"
#include "ui/assets.h"
#include "ui/embeds.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using theme::px;

namespace ui {
namespace {

store::Data g_data;
double g_loadedAt = -100;
bool g_admin = false;
int g_cat = 0;               // 0 = tudo, 1 = da KB, 2.. = categorias
char g_search[128] = "";
std::string g_openId;        // produto aberto no detalhe
int g_variant = -1;
std::string g_size;
char g_coupon[32] = "";
bool g_openReq = false;

// formulário de produto próprio
struct Form {
    bool editing = false;
    std::string id, image;
    char name[160] = "", category[64] = "Shorts", price[24] = "", sizes[96] = "P, M, G, GG", desc[600] = "";
} g_form;
char g_newCoupon[32] = "";
int g_newPercent = 10;
char g_whats[40] = "", g_storeName[80] = "";

const ImVec4 kRed(0.902f, 0.224f, 0.275f, 1.f);

void Reload() {
    g_data = store::Load();
    g_loadedAt = ImGui::GetTime();
    snprintf(g_whats, sizeof(g_whats), "%s", g_data.whatsapp.c_str());
    snprintf(g_storeName, sizeof(g_storeName), "%s", g_data.storeName.c_str());
}

std::vector<store::Product> All() {
    std::vector<store::Product> all = g_data.own;
    auto p = store::Partner();
    all.insert(all.end(), p.begin(), p.end());
    return all;
}

std::string Ellipsize(std::string t, float maxW) {
    if (ImGui::CalcTextSize(t.c_str()).x <= maxW) return t;
    while (t.size() > 3 && ImGui::CalcTextSize((t + "…").c_str()).x > maxW) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
    }
    return t + "…";
}

// Quebra o nome em até 2 linhas
void TwoLines(ImDrawList* dl, ImVec2 p, float maxW, const std::string& s, ImU32 col) {
    std::vector<std::string> words = text::Split(s, ' ');
    std::string l1, l2;
    size_t i = 0;
    for (; i < words.size(); ++i) {
        std::string t = l1.empty() ? words[i] : l1 + " " + words[i];
        if (ImGui::CalcTextSize(t.c_str()).x > maxW && !l1.empty()) break;
        l1 = t;
    }
    for (; i < words.size(); ++i) l2 += (l2.empty() ? "" : " ") + words[i];
    dl->AddText(p, col, l1.c_str());
    if (!l2.empty()) dl->AddText(ImVec2(p.x, p.y + ImGui::GetTextLineHeight()), col, Ellipsize(l2, maxW).c_str());
}

void DrawProductImage(ImDrawList* dl, const std::string& key, ImVec2 a, ImVec2 b, float r) {
    // fundo claro: as fotos das lojas têm fundo branco
    dl->AddRectFilled(a, b, IM_COL32(242, 243, 246, 255), r, ImDrawFlags_RoundCornersTop);
    PicturePtr pic = store::Image(key);
    float bw = b.x - a.x, bh = b.y - a.y;
    if (pic && pic->w > 0) {
        float sc = std::min((bw - px(12)) / pic->w, (bh - px(12)) / pic->h);
        float iw = pic->w * sc, ih = pic->h * sc;
        ImVec2 ia(a.x + (bw - iw) * 0.5f, a.y + (bh - ih) * 0.5f);
        float ap = anim::To(ImHashStr(key.c_str()), 1.f, 6.f);
        dl->AddImage(PictureTexture(pic), ia, ImVec2(ia.x + iw, ia.y + ih), ImVec2(0, 0), ImVec2(1, 1), anim::Col(ImVec4(1, 1, 1, ap)));
    } else {
        // carregando: brilho passando
        float t = std::fmod((float)ImGui::GetTime() * 0.8f, 1.f);
        float x = a.x + (bw + px(80)) * t - px(40);
        dl->PushClipRect(a, b, true);
        dl->AddRectFilledMultiColor(ImVec2(x - px(40), a.y), ImVec2(x, b.y), IM_COL32(242, 243, 246, 0), IM_COL32(255, 255, 255, 200),
                                    IM_COL32(255, 255, 255, 200), IM_COL32(242, 243, 246, 0));
        dl->PopClipRect();
        w::DrawIcon(dl, Icon::Bag, ImVec2(a.x + bw * 0.5f, a.y + bh * 0.5f), px(34), IM_COL32(190, 194, 204, 255));
    }
}

void Badge(ImDrawList* dl, ImVec2 p, const char* t, ImVec4 bg, ImVec4 fg) {
    ImGui::PushFont(theme::fonts.label);
    ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + px(14), p.y + ts.y + px(6)), anim::Col(bg), px(4));
    dl->AddText(ImVec2(p.x + px(7), p.y + px(3)), anim::Col(fg), t);
    ImGui::PopFont();
}

// Cartão da vitrine; devolve true se clicado
bool ProductCard(const store::Product& p, float cw, float ch) {
    ImGui::PushID(p.id.c_str());
    ImVec2 a = ImGui::GetCursorScreenPos();
    bool click = ImGui::InvisibleButton("##card", ImVec2(cw, ch));
    float hov = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 12.f);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lift = px(3) * hov;
    a.y -= lift;
    ImVec2 b(a.x + cw, a.y + ch);
    float r = px(10);
    if (hov > 0.01f) w::Glow(dl, a, b, r, p.own ? kRed : theme::Cyan, 0.6f * hov);
    w::Surface(dl, a, b, r, hov);
    float imgH = cw * 0.86f;
    DrawProductImage(dl, p.image, a, ImVec2(b.x, a.y + imgH), r);
    // selo
    if (p.own) Badge(dl, ImVec2(a.x + px(8), a.y + px(8)), "KB", kRed, ImVec4(1, 1, 1, 1));
    else Badge(dl, ImVec2(a.x + px(8), a.y + px(8)), p.brand.c_str(), ImVec4(0.07f, 0.08f, 0.1f, 0.85f), ImVec4(1, 1, 1, 0.9f));
    bool anyOut = !p.variants.empty() && std::none_of(p.variants.begin(), p.variants.end(), [](auto& v) { return v.available; });
    if (anyOut) Badge(dl, ImVec2(b.x - px(78), a.y + px(8)), "ESGOTADO", ImVec4(0.3f, 0.3f, 0.33f, 0.9f), ImVec4(1, 1, 1, 0.9f));
    float tx = a.x + px(12), ty = a.y + imgH + px(9), tw = cw - px(24);
    ImGui::PushFont(theme::fonts.caption);
    dl->AddText(ImVec2(tx, ty), anim::Col(theme::TextFaint), p.category.c_str());
    float lh = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts.body);
    TwoLines(dl, ImVec2(tx, ty + lh + px(2)), tw, p.name, anim::Col(theme::Text));
    float nl = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts.title);
    std::string price = store::Money(p.price);
    dl->AddText(ImVec2(tx, b.y - ImGui::GetTextLineHeight() - px(10)), anim::Col(p.own ? kRed : theme::Text), price.c_str());
    ImGui::PopFont();
    (void)nl;
    ImGui::PopID();
    return click;
}

// ---------------------------------------------------------------- detalhe do produto
void ProductDetail(App& app) {
    const char* id = "Produto###shopdetail";
    if (g_openReq) { ImGui::OpenPopup(id); g_openReq = false; }
    ImGuiViewport* vp = ImGui::GetWindowViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(px(860), vp->Size.x - px(40)), std::min(px(560), vp->Size.y - px(40))), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal(id, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar)) return;
    store::Product p;
    bool found = false;
    for (auto& q : All()) if (q.id == g_openId) { p = q; found = true; break; }
    if (!found) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 o = ImGui::GetCursorScreenPos();
    float imgW = std::min(avail.x * 0.46f, avail.y);
    // imagem
    dl->AddRectFilled(o, ImVec2(o.x + imgW, o.y + avail.y), IM_COL32(242, 243, 246, 255), px(10));
    DrawProductImage(dl, p.image, o, ImVec2(o.x + imgW, o.y + avail.y), px(10));
    // fechar
    ImGui::SetCursorScreenPos(ImVec2(o.x + avail.x - px(32), o.y));
    if (w::IconButton("close", Icon::Close, px(32), "Fechar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
    // informações
    float x = o.x + imgW + px(24), wdt = avail.x - imgW - px(24);
    ImGui::SetCursorScreenPos(ImVec2(x, o.y + px(4)));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(x - ImGui::GetWindowPos().x + wdt - px(40));   // coordenada local da janela
    if (p.own) Badge(dl, ImGui::GetCursorScreenPos(), "PRODUTO KB", kRed, ImVec4(1, 1, 1, 1));
    else Badge(dl, ImGui::GetCursorScreenPos(), (p.brand + " · loja oficial").c_str(), ImVec4(1, 1, 1, 0.08f), theme::TextDim);
    ImGui::Dummy(ImVec2(0, px(26)));
    ImGui::PushFont(theme::fonts.title);
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(4)));
    const store::Coupon* cp = p.own ? store::FindCoupon(g_data, g_coupon) : nullptr;
    ImGui::PushFont(theme::fonts.huge);
    ImGui::PushStyleColor(ImGuiCol_Text, p.own ? kRed : theme::Text);
    ImGui::TextUnformatted(store::Money(store::ApplyCoupon(p.price, cp)).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (cp) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextFaint);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("de %s · cupom %s -%d%%", store::Money(p.price).c_str(), cp->code.c_str(), cp->percent);
        ImGui::PopStyleColor();
    } else if (!p.own && p.price > 100) {
        ImGui::PushFont(theme::fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
        ImGui::Text("ou 3x de %s na loja", store::Money(p.price / 3).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    if (!p.description.empty()) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
        ImGui::TextUnformatted(p.description.c_str());
        ImGui::PopStyleColor();
    }
    // tamanhos
    std::vector<std::pair<std::string, bool>> opts;
    for (auto& v : p.variants) opts.push_back({v.name, v.available});
    for (auto& s : p.sizes) opts.push_back({s, true});
    if (!opts.empty()) {
        ImGui::Dummy(ImVec2(0, px(8)));
        bool gloves = text::Fold(p.category).find("luva") != std::string::npos && !p.variants.empty() && isdigit((unsigned char)p.variants[0].name[0]);
        FieldLabel(gloves ? "Tamanho (oz)" : "Tamanho");
        for (size_t i = 0; i < opts.size(); ++i) {
            if (i) ImGui::SameLine(0, px(6));
            ImGui::PushID((int)i);
            bool sel = p.own ? g_size == opts[i].first : g_variant == (int)i;
            ImVec2 sp = ImGui::GetCursorScreenPos();
            ImVec2 ts = ImGui::CalcTextSize(opts[i].first.c_str());
            ImVec2 sz(std::max(ts.x + px(22), px(46)), px(38));
            if (ImGui::InvisibleButton("##sz", sz) && opts[i].second) {
                if (p.own) g_size = opts[i].first; else g_variant = (int)i;
                sfx::Play(sfx::Tab);
            }
            float hv = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
            ImVec2 sb(sp.x + sz.x, sp.y + sz.y);
            if (sel) { w::Glow(dl, sp, sb, px(6), p.own ? kRed : theme::Cyan, 0.7f); dl->AddRectFilled(sp, sb, anim::Col(p.own ? kRed : theme::Cyan), px(6)); }
            else w::Surface(dl, sp, sb, px(6), opts[i].second ? hv : 0.f);
            ImVec4 tc = !opts[i].second ? theme::TextFaint : sel ? ImVec4(1, 1, 1, 1) : theme::Text;
            dl->AddText(ImVec2(sp.x + (sz.x - ts.x) * 0.5f, sp.y + (sz.y - ts.y) * 0.5f), anim::Col(tc), opts[i].first.c_str());
            if (!opts[i].second) {
                dl->AddLine(ImVec2(sp.x + px(6), sb.y - px(6)), ImVec2(sb.x - px(6), sp.y + px(6)), anim::Col(theme::TextFaint), px(1));
                w::Tooltip("Esgotado neste tamanho");
            }
            ImGui::PopID();
        }
    }
    // cupom (só produtos da KB)
    if (p.own) {
        ImGui::Dummy(ImVec2(0, px(8)));
        FieldLabel("Cupom de desconto");
        ImGui::SetNextItemWidth(px(200));
        ImGui::InputTextWithHint("##coupon", "ex.: KB10", g_coupon, sizeof(g_coupon), ImGuiInputTextFlags_CharsUppercase);
        if (g_coupon[0]) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, cp ? theme::Green : theme::Red);
            ImGui::TextUnformatted(cp ? "Cupom aplicado" : "Cupom inválido");
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    // botão comprar (fixo embaixo)
    float bh = px(50);
    ImGui::SetCursorScreenPos(ImVec2(x, o.y + avail.y - bh - px(26)));
    bool needSize = p.own ? (!p.sizes.empty() && g_size.empty()) : (!p.variants.empty() && g_variant < 0);
    std::string label = p.own ? "Comprar pelo WhatsApp" : "Comprar na loja " + p.brand;
    ImGui::BeginDisabled(needSize);
    if (w::PrimaryButton(label.c_str(), ImVec2(wdt - px(8), bh), p.own ? Icon::Phone : Icon::Cart)) {
        std::string link = store::BuyLink(g_data, p, g_variant, g_size, cp);
        if (link.empty()) app.Notify("Cadastre o WhatsApp da loja em Loja → Gerenciar.", 2);
        else {
            sfx::Play(sfx::Coin);
            platform::OpenUrl(link);
            app.Notify(p.own ? "Abrindo o WhatsApp com o seu pedido…" : "Abrindo o carrinho da loja " + p.brand + " com o tamanho escolhido…", 1);
        }
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(ImVec2(x, o.y + avail.y - px(20)));
    ImGui::PushFont(theme::fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextFaint);
    if (needSize) ImGui::TextUnformatted("Escolha o tamanho para continuar.");
    else if (p.own) ImGui::TextUnformatted("O pedido vai pronto no WhatsApp da loja; é só enviar.");
    else {
        ImGui::TextUnformatted("Você vai direto para o carrinho da loja oficial.");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Cyan);
        if (ImGui::Selectable("Ver na loja##see", false, 0, ImGui::CalcTextSize("Ver na loja"))) platform::OpenUrl(p.url);
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (!open) { ImGui::CloseCurrentPopup(); sfx::Play(sfx::Close, 0.6f); }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- gerenciar (treinadora)
void AdminPanel(App& app) {
    bool save = false;
    w::SectionTitle("Dados da loja", Icon::Gear);
    FieldLabel("Nome da loja");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##sn", g_storeName, sizeof(g_storeName))) { g_data.storeName = g_storeName; save = true; }
    FieldLabel("WhatsApp para receber os pedidos (com DDD)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##wa", "(11) 91234-5678", g_whats, sizeof(g_whats), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CallbackCharFilter,
                                 [](ImGuiInputTextCallbackData* d) { return (d->EventChar >= '0' && d->EventChar <= '9') || strchr(" ()-+", (int)d->EventChar) ? 0 : 1; })) {
        g_data.whatsapp = g_whats;
        save = true;
    }

    w::SectionTitle("Cupons", Icon::Tag);
    int del = -1;
    for (size_t i = 0; i < g_data.coupons.size(); ++i) {
        auto& c = g_data.coupons[i];
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rw = ImGui::GetContentRegionAvail().x, rh = px(46);
        w::Surface(ImGui::GetWindowDrawList(), p, ImVec2(p.x + rw, p.y + rh), px(8), 0.f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(12), p.y + px(8)));
        w::DrawIcon(ImGui::GetWindowDrawList(), Icon::Tag, ImVec2(p.x + px(22), p.y + rh * 0.5f), px(20), anim::Col(c.active ? kRed : theme::TextFaint));
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(40), p.y + (rh - ImGui::GetFrameHeight()) * 0.5f));
        char code[32];
        snprintf(code, sizeof(code), "%s", c.code.c_str());
        ImGui::SetNextItemWidth(px(140));
        if (ImGui::InputText("##code", code, sizeof(code), ImGuiInputTextFlags_CharsUppercase)) { c.code = code; save = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(110));
        if (ImGui::SliderInt("##pc", &c.percent, 1, 90, "%d%% off")) save = true;
        ImGui::SameLine();
        bool act = c.active;
        if (w::IconButton("act", act ? Icon::Check : Icon::Close, ImGui::GetFrameHeight(), act ? "Ativo (clique para pausar)" : "Pausado (clique para ativar)", act, theme::Green)) { c.active = !c.active; save = true; }
        ImGui::SameLine();
        if (w::IconButton("del", Icon::Trash, ImGui::GetFrameHeight(), "Apagar cupom")) del = (int)i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rh + px(6)));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (del >= 0) { g_data.coupons.erase(g_data.coupons.begin() + del); save = true; }
    ImGui::SetNextItemWidth(px(160));
    ImGui::InputTextWithHint("##nc", "Novo cupom", g_newCoupon, sizeof(g_newCoupon), ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(110));
    ImGui::SliderInt("##np", &g_newPercent, 1, 90, "%d%% off");
    ImGui::SameLine();
    if (w::PrimaryButton("Criar cupom", ImVec2(0, 0), Icon::Tag) && g_newCoupon[0]) {
        g_data.coupons.push_back({g_newCoupon, g_newPercent, true, ""});
        g_newCoupon[0] = 0;
        save = true;
        sfx::Play(sfx::Success);
    }
    w::Hint("Cupons valem só para os produtos da KB (as lojas parceiras têm os próprios preços).");

    w::SectionTitle(g_form.editing ? "Editar produto" : "Adicionar produto da KB", Icon::Bag);
    {
        float iw = px(150);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##photo", ImVec2(iw, iw));
        bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            std::string f = platform::PickFile(L"Imagens\0*.png;*.jpg;*.jpeg;*.webp;*.bmp\0", "Foto do produto");
            if (!f.empty()) g_form.image = store::ImportImage(f);
        }
        if (!g_form.image.empty()) DrawProductImage(dl, g_form.image, p, ImVec2(p.x + iw, p.y + iw), px(8));
        else {
            w::Surface(dl, p, ImVec2(p.x + iw, p.y + iw), px(8), hov ? 1.f : 0.f);
            w::DrawIcon(dl, Icon::Image, ImVec2(p.x + iw * 0.5f, p.y + iw * 0.42f), px(30), anim::Col(theme::TextDim));
            ImGui::PushFont(theme::fonts.caption);
            const char* t = "Escolher foto";
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(p.x + (iw - ts.x) * 0.5f, p.y + iw * 0.62f), anim::Col(theme::TextDim), t);
            ImGui::PopFont();
        }
        ImGui::SameLine(0, px(14));
        ImGui::BeginGroup();
        float fw = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(fw);
        ImGui::InputTextWithHint("##fn", "Nome (ex.: Short Muay Thai KB Preto)", g_form.name, sizeof(g_form.name));
        static const char* cats[] = {"Shorts", "Camisetas", "Tops", "Luvas", "Bandagens", "Caneleiras", "Acessórios", "Outros"};
        ImGui::SetNextItemWidth(fw * 0.5f - px(5));
        if (ImGui::BeginCombo("##fc", g_form.category)) {
            for (auto c : cats) if (ImGui::Selectable(c, !strcmp(c, g_form.category))) snprintf(g_form.category, sizeof(g_form.category), "%s", c);
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, px(10));
        ImGui::SetNextItemWidth(fw * 0.5f - px(5));
        ImGui::InputTextWithHint("##fp", "Preço (ex.: 129,90)", g_form.price, sizeof(g_form.price), ImGuiInputTextFlags_CharsDecimal);
        ImGui::SetNextItemWidth(fw);
        ImGui::InputTextWithHint("##fs", "Tamanhos separados por vírgula (ex.: P, M, G, GG)", g_form.sizes, sizeof(g_form.sizes));
        ImGui::SetNextItemWidth(fw);
        ImVec2 dp = ImGui::GetCursorScreenPos();
        ImGui::InputTextMultiline("##fd", g_form.desc, sizeof(g_form.desc), ImVec2(fw, px(52)));
        if (!g_form.desc[0] && !ImGui::IsItemActive())
            ImGui::GetWindowDrawList()->AddText(ImVec2(dp.x + ImGui::GetStyle().FramePadding.x, dp.y + ImGui::GetStyle().FramePadding.y),
                                                anim::Col(theme::TextFaint), "Descrição (tecido, modelagem, cores...)");
        ImGui::EndGroup();
    }
    std::string priceStr = g_form.price;
    std::replace(priceStr.begin(), priceStr.end(), ',', '.');
    double price = atof(priceStr.c_str());
    bool ok = g_form.name[0] && price > 0;
    ImGui::BeginDisabled(!ok);
    if (w::PrimaryButton(g_form.editing ? "Salvar alterações" : "Adicionar à loja", ImVec2(0, 0), Icon::Check)) {
        store::Product p;
        p.own = true;
        p.id = g_form.editing ? g_form.id : store::NewId();
        p.name = text::Trim(g_form.name);
        p.brand = "KB";
        p.category = g_form.category;
        p.price = price;
        p.image = g_form.image;
        p.description = text::Trim(g_form.desc);
        for (auto& s : text::Split(g_form.sizes, ',')) if (!text::Trim(s).empty()) p.sizes.push_back(text::Trim(s));
        auto it = std::find_if(g_data.own.begin(), g_data.own.end(), [&](auto& q) { return q.id == p.id; });
        if (it != g_data.own.end()) *it = p; else g_data.own.insert(g_data.own.begin(), p);
        g_form = Form();
        save = true;
        sfx::Play(sfx::Success);
        app.Notify("Produto salvo na loja.", 1);
    }
    ImGui::EndDisabled();
    if (g_form.editing) {
        ImGui::SameLine();
        if (w::GhostButton("Cancelar")) g_form = Form();
    }

    if (!g_data.own.empty()) {
        w::SectionTitle("Produtos da KB", Icon::Grid);
        int rm = -1;
        for (size_t i = 0; i < g_data.own.size(); ++i) {
            auto& p = g_data.own[i];
            ImGui::PushID((int)i + 500);
            ImVec2 a = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x, rh = px(60);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            w::Surface(dl, a, ImVec2(a.x + rw, a.y + rh), px(8), 0.f);
            DrawProductImage(dl, p.image, ImVec2(a.x + px(6), a.y + px(6)), ImVec2(a.x + px(54), a.y + px(54)), px(6));
            dl->AddText(ImVec2(a.x + px(66), a.y + px(10)), anim::Col(theme::Text), p.name.c_str());
            std::string sub = p.category + " · " + store::Money(p.price);
            ImGui::PushFont(theme::fonts.caption);
            dl->AddText(ImVec2(a.x + px(66), a.y + px(33)), anim::Col(theme::TextDim), sub.c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(a.x + rw - px(80), a.y + (rh - px(32)) * 0.5f));
            if (w::IconButton("ed", Icon::Edit, px(32), "Editar")) {
                g_form = Form();
                g_form.editing = true;
                g_form.id = p.id;
                g_form.image = p.image;
                snprintf(g_form.name, sizeof(g_form.name), "%s", p.name.c_str());
                snprintf(g_form.category, sizeof(g_form.category), "%s", p.category.c_str());
                snprintf(g_form.price, sizeof(g_form.price), "%.2f", p.price);
                std::string sz;
                for (auto& s : p.sizes) sz += (sz.empty() ? "" : ", ") + s;
                snprintf(g_form.sizes, sizeof(g_form.sizes), "%s", sz.c_str());
                snprintf(g_form.desc, sizeof(g_form.desc), "%s", p.description.c_str());
            }
            ImGui::SameLine(0, px(6));
            if (w::IconButton("rm", Icon::Trash, px(32), "Remover da loja")) rm = (int)i;
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh + px(6)));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        if (rm >= 0) { g_data.own.erase(g_data.own.begin() + rm); save = true; }
    }
    if (save) store::Save(g_data);
}

} // namespace

void ShopTestView(bool admin, int cat) { Reload(); g_loadedAt = ImGui::GetTime(); g_admin = admin; g_cat = cat; }

void ShopShowProduct(const std::string& id) {
    g_openId = id;
    g_variant = -1;
    g_size.clear();
    g_openReq = true;
}

void ShopWindow(App& app, bool* open) {
    if (ImGui::GetTime() - g_loadedAt > 5.0 && !g_admin) Reload();
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(520), px(480)), ImVec2(FLT_MAX, FLT_MAX));
    if (!BeginPanel("Loja KB###shop", open, ImGuiWindowFlags_NoCollapse, kRed)) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;

    // ---- cabeçalho com a marca
    {
        ImVec2 a = ImGui::GetCursorScreenPos();
        float h = px(92);
        ImVec2 b(a.x + avail, a.y + h);
        w::GradRect(dl, a, b, ImVec4(0.16f, 0.07f, 0.09f, 1.f), ImVec4(0.07f, 0.075f, 0.09f, 1.f), px(12));
        dl->AddRectFilledMultiColor(ImVec2(a.x + avail * 0.45f, a.y), b, anim::Col(ImVec4(0.18f, 0.56f, 1.f, 0.f)), anim::Col(ImVec4(0.18f, 0.56f, 1.f, 0.16f)),
                                    anim::Col(ImVec4(0.18f, 0.56f, 1.f, 0.10f)), anim::Col(ImVec4(0.18f, 0.56f, 1.f, 0.f)));
        dl->AddRect(a, b, anim::Col(ImVec4(1, 1, 1, 0.06f)), px(12));
        if (ImTextureID logo = assets::Logo()) dl->AddImageRounded(logo, ImVec2(a.x + px(14), a.y + px(14)), ImVec2(a.x + px(78), a.y + px(78)), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(32));
        ImGui::PushFont(theme::fonts.title);
        dl->AddText(ImVec2(a.x + px(92), a.y + px(18)), anim::Col(theme::Text), g_data.storeName.c_str());
        ImGui::PopFont();
        ImGui::PushFont(theme::fonts.caption);
        dl->AddText(ImVec2(a.x + px(92), a.y + px(52)), anim::Col(theme::TextDim), "Equipamentos de Muay Thai · produtos da KB e da Maximum");
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(b.x - px(150), a.y + (h - ImGui::GetFrameHeight()) * 0.5f));
        if (g_admin ? w::PrimaryButton("Ver vitrine", ImVec2(px(136), 0), Icon::Bag) : w::GhostButton("Gerenciar", ImVec2(px(136), 0), Icon::Edit)) {
            g_admin = !g_admin;
            if (!g_admin) Reload();
        }
        ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + px(10)));
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (g_admin) {
        ImGui::BeginChild("##admin", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        AdminPanel(app);
        ImGui::Dummy(ImVec2(0, px(10)));
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    // ---- busca + categorias
    auto all = All();
    std::vector<std::string> cats = store::Categories(all);
    std::vector<std::string> chips = {"Tudo", "Da KB"};
    for (auto& c : cats) chips.push_back(c);
    ImGui::SetNextItemWidth(px(260));
    ImGui::InputTextWithHint("##search", "Buscar (ex.: luva 14 oz, bandagem)", g_search, sizeof(g_search));
    ImGui::SameLine(0, px(10));
    {
        float x0 = ImGui::GetCursorScreenPos().x, y0 = ImGui::GetCursorScreenPos().y;
        float x = x0, right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        for (size_t i = 0; i < chips.size(); ++i) {
            ImVec2 ts = ImGui::CalcTextSize(chips[i].c_str());
            float cw = ts.x + px(24), ch = ImGui::GetFrameHeight();
            if (x + cw > right) { x = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x; y0 += ch + px(6); }
            ImGui::SetCursorScreenPos(ImVec2(x, y0));
            ImGui::PushID((int)i);
            if (ImGui::InvisibleButton("##cat", ImVec2(cw, ch)) && g_cat != (int)i) { g_cat = (int)i; sfx::Play(sfx::Tab); }
            float hv = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
            ImGui::PopID();
            ImVec2 a(x, y0), b(x + cw, y0 + ch);
            bool sel = g_cat == (int)i;
            ImVec4 acc = i == 1 ? kRed : theme::Cyan;
            if (sel) { w::Glow(dl, a, b, ch * 0.5f, acc, 0.5f); dl->AddRectFilled(a, b, anim::Col(acc), ch * 0.5f); }
            else { dl->AddRectFilled(a, b, anim::Col(ImVec4(1, 1, 1, 0.04f + 0.05f * hv)), ch * 0.5f); dl->AddRect(a, b, anim::Col(ImVec4(1, 1, 1, 0.08f)), ch * 0.5f); }
            dl->AddText(ImVec2(x + px(12), y0 + (ch - ts.y) * 0.5f), anim::Col(sel ? ImVec4(1, 1, 1, 1) : theme::TextDim), chips[i].c_str());
            x += cw + px(6);
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, y0 + ImGui::GetFrameHeight() + px(12)));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // ---- vitrine
    std::vector<const store::Product*> shown;
    std::string q = text::Fold(g_search);
    for (auto& p : all) {
        if (g_cat == 1 && !p.own) continue;
        if (g_cat >= 2 && p.category != chips[g_cat]) continue;
        if (!q.empty()) {
            std::string hay = text::Fold(p.name + " " + p.category + " " + p.brand);
            bool okAll = true;
            for (auto& wd : text::Split(q, ' ')) {
                if (wd.empty()) continue;
                std::string stem = wd.size() > 4 ? wd.substr(0, wd.size() - 1) : wd;
                bool sizeHit = false;
                for (auto& v : p.variants) if (text::Fold(v.name).rfind(wd, 0) == 0) sizeHit = true;
                if (hay.find(stem) == std::string::npos && !sizeHit && wd != "oz") okAll = false;
            }
            if (!okAll) continue;
        }
        shown.push_back(&p);
    }
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    float gw = ImGui::GetContentRegionAvail().x;
    float gap = px(14);
    int cols = std::max(2, (int)((gw + gap) / (px(210) + gap)));
    float cw = (gw - gap * (cols - 1)) / cols;
    float ch = cw * 0.86f + px(118);
    if (g_cat == 1 && g_data.own.empty()) {
        ImVec2 a = ImGui::GetCursorScreenPos();
        float h = px(170);
        w::Surface(dl, a, ImVec2(a.x + gw, a.y + h), px(12), 0.f);
        w::DrawIcon(ImGui::GetWindowDrawList(), Icon::Bag, ImVec2(a.x + gw * 0.5f, a.y + px(48)), px(40), anim::Col(kRed));
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + px(82)));
        ImGui::PushFont(theme::fonts.semibold);
        const char* t1 = "Em breve: shorts, camisetas e tops da KB";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (gw - ImGui::CalcTextSize(t1).x) * 0.5f);
        ImGui::TextUnformatted(t1);
        ImGui::PopFont();
        const char* t2 = "Clique em Gerenciar para cadastrar os produtos com foto, preço e tamanhos.";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (gw - ImGui::CalcTextSize(t2).x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
        ImGui::TextUnformatted(t2);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + px(10)));
    }
    for (size_t i = 0; i < shown.size(); ++i) {
        if (i % cols) ImGui::SameLine(0, gap);
        else if (i) ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        // aparece em cascata
        float ap = anim::To(ImHashStr(shown[i]->id.c_str()) + 77, 1.f, 5.f + (i % 12) * 0.3f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ap);
        if (ProductCard(*shown[i], cw, ch)) { ShopShowProduct(shown[i]->id); sfx::Play(sfx::Open, 0.6f); }
        ImGui::PopStyleVar();
    }
    if (shown.empty() && !(g_cat == 1 && g_data.own.empty())) w::Hint("Nada encontrado com esse filtro.");
    ImGui::Dummy(ImVec2(0, px(10)));
    ImGui::EndChild();
    ProductDetail(app);
    ImGui::End();
}

} // namespace ui
