// Agenda: rotina semanal em grade, pedidos de aula experimental e link público de agendamento.
#include "ui/shared.h"
#include "app.h"
#include "audio/sfx.h"
#include "core/platform.h"
#include "core/text.h"
#include "features/agenda.h"
#include "ui/anim.h"
#include "ui/embeds.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

using theme::px;

namespace ui {
namespace {

agenda::Data g_ag;
double g_agLoaded = -100;
int g_agTab = 0;
// editor de item da rotina
bool g_editOpen = false;
bool g_editNew = false;
agenda::Item g_item;
char g_title[80] = "", g_start[8] = "", g_end[8] = "";
char g_studio[80] = "", g_addr[160] = "";

const ImVec4 kRed(0.902f, 0.224f, 0.275f, 1.f);
const ImVec4 kGray(0.42f, 0.45f, 0.5f, 1.f);

ImVec4 TypeColor(int t) { return t == agenda::Class ? kRed : t == agenda::Busy ? kGray : theme::Cyan; }
const char* TypeLabel(int t) { return t == agenda::Class ? "Aula (aceita experimental)" : t == agenda::Busy ? "Ocupado / compromisso" : "Livre para experimental"; }

int Minutes(const std::string& s) {
    int h = 0, m = 0;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) < 1) return -1;
    return h * 60 + m;
}

void Reload() {
    g_ag = agenda::Load();
    g_agLoaded = ImGui::GetTime();
    snprintf(g_studio, sizeof(g_studio), "%s", g_ag.studio.c_str());
    snprintf(g_addr, sizeof(g_addr), "%s", g_ag.address.c_str());
}

void OpenEditor(const agenda::Item* it) {
    g_editNew = it == nullptr;
    g_item = it ? *it : agenda::Item();
    if (!it) { g_item.id = agenda::NewId("r"); g_item.title = "Muay Thai"; g_item.days = {1, 3, 5}; }
    snprintf(g_title, sizeof(g_title), "%s", g_item.title.c_str());
    snprintf(g_start, sizeof(g_start), "%s", g_item.start.c_str());
    snprintf(g_end, sizeof(g_end), "%s", g_item.end.c_str());
    g_editOpen = true;
}

// ---------------------------------------------------------------- grade semanal
void WeekGrid() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    int lo = 6 * 60, hi = 22 * 60;
    for (auto& r : g_ag.routine) {
        int a = Minutes(r.start), b = Minutes(r.end);
        if (a >= 0) lo = std::min(lo, (a / 60) * 60);
        if (b > 0) hi = std::max(hi, ((b + 59) / 60) * 60);
    }
    float hourH = px(34), labelW = px(46), headH = px(30);
    int hours = (hi - lo) / 60;
    float gridH = headH + hours * hourH;
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(avail, gridH + px(4)));
    float colW = (avail - labelW) / 7.f;
    w::Surface(dl, o, ImVec2(o.x + avail, o.y + gridH), px(10), 0.f);
    static const int order[7] = {1, 2, 3, 4, 5, 6, 0}; // segunda primeiro
    time_t now = time(nullptr);
    std::tm tn = *localtime(&now);
    ImGui::PushFont(theme::fonts.label);
    for (int c = 0; c < 7; ++c) {
        float x = o.x + labelW + c * colW;
        bool today = order[c] == tn.tm_wday;
        if (today) dl->AddRectFilled(ImVec2(x, o.y + px(1)), ImVec2(x + colW, o.y + gridH - px(1)), anim::Col(ImVec4(0.18f, 0.56f, 1.f, 0.06f)));
        std::string dn = agenda::DayName(order[c]);
        for (auto& ch : dn) if ((unsigned char)ch < 0x80) ch = (char)toupper((unsigned char)ch);
        if (auto p = dn.find("\xC3\xA1"); p != std::string::npos) dn.replace(p, 2, "\xC3\x81");  // á → Á
        ImVec2 ts = ImGui::CalcTextSize(dn.c_str());
        dl->AddText(ImVec2(x + (colW - ts.x) * 0.5f, o.y + (headH - ts.y) * 0.5f), anim::Col(today ? theme::Cyan : theme::TextDim), dn.c_str());
        if (c) dl->AddLine(ImVec2(x, o.y + px(6)), ImVec2(x, o.y + gridH - px(6)), anim::Col(ImVec4(1, 1, 1, 0.05f)));
    }
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts.caption);
    for (int h = 0; h <= hours; ++h) {
        float y = o.y + headH + h * hourH;
        dl->AddLine(ImVec2(o.x + labelW - px(4), y), ImVec2(o.x + avail - px(6), y), anim::Col(ImVec4(1, 1, 1, h == 0 ? 0.08f : 0.035f)));
        if (h < hours) {
            char b[8];
            snprintf(b, sizeof(b), "%02d:00", lo / 60 + h);
            dl->AddText(ImVec2(o.x + px(8), y + px(2)), anim::Col(theme::TextFaint), b);
        }
    }
    // linha de "agora"
    int nowMin = tn.tm_hour * 60 + tn.tm_min;
    if (nowMin >= lo && nowMin <= hi) {
        int c = (tn.tm_wday + 6) % 7;
        float y = o.y + headH + (nowMin - lo) / 60.f * hourH;
        float x = o.x + labelW + c * colW;
        dl->AddLine(ImVec2(x, y), ImVec2(x + colW, y), anim::Col(kRed), px(2));
        dl->AddCircleFilled(ImVec2(x, y), px(4), anim::Col(kRed));
    }
    // blocos
    for (auto& r : g_ag.routine) {
        int a = Minutes(r.start), b = Minutes(r.end);
        if (a < 0 || b <= a) continue;
        for (int d : r.days) {
            int c = (d + 6) % 7;
            float x0 = o.x + labelW + c * colW + px(3), x1 = x0 + colW - px(6);
            float y0 = o.y + headH + (a - lo) / 60.f * hourH + px(1), y1 = o.y + headH + (b - lo) / 60.f * hourH - px(1);
            ImGui::SetCursorScreenPos(ImVec2(x0, y0));
            ImGui::PushID((r.id + std::to_string(d)).c_str());
            bool click = ImGui::InvisibleButton("##blk", ImVec2(x1 - x0, std::max(px(8), y1 - y0)));
            float hv = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
            ImGui::PopID();
            ImVec4 col = TypeColor(r.type);
            w::Glow(dl, ImVec2(x0, y0), ImVec2(x1, y1), px(5), col, 0.3f + 0.5f * hv);
            w::GradRect(dl, ImVec2(x0, y0), ImVec2(x1, y1), anim::Lerp(col, ImVec4(1, 1, 1, 1), 0.10f + 0.1f * hv), anim::Lerp(col, ImVec4(0, 0, 0, 1), 0.25f), px(5));
            dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
            dl->AddText(ImVec2(x0 + px(5), y0 + px(2)), IM_COL32_WHITE, r.title.c_str());
            if (y1 - y0 > px(30)) dl->AddText(ImVec2(x0 + px(5), y0 + px(17)), IM_COL32(255, 255, 255, 190), (r.start + "–" + r.end).c_str());
            dl->PopClipRect();
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                w::Tooltip((r.title + " · " + r.start + "–" + r.end + "\n" + TypeLabel(r.type) + "\nClique para editar").c_str());
            }
            if (click) OpenEditor(&r);
        }
    }
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + gridH + px(8)));
    ImGui::Dummy(ImVec2(0, 0));
}

void ItemEditor() {
    const char* id = "Item da rotina###agitem";
    if (g_editOpen) { ImGui::OpenPopup(id); g_editOpen = false; }
    ImGuiViewport* vp = ImGui::GetWindowViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(520), 0), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal(id, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) return;
    FieldLabel("Nome");
    ImGui::SetNextItemWidth(px(480));
    ImGui::InputText("##t", g_title, sizeof(g_title));
    FieldLabel("Tipo");
    for (int t = 0; t < 3; ++t) {
        if (t) ImGui::SameLine();
        bool sel = g_item.type == t;
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? TypeColor(t) : theme::TextDim);
        if (ImGui::RadioButton(TypeLabel(t), sel)) g_item.type = t;
        ImGui::PopStyleColor();
    }
    FieldLabel("Dias");
    static const int order[7] = {1, 2, 3, 4, 5, 6, 0};
    for (int i = 0; i < 7; ++i) {
        int d = order[i];
        if (i) ImGui::SameLine(0, px(6));
        bool on = std::find(g_item.days.begin(), g_item.days.end(), d) != g_item.days.end();
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 sz(px(58), px(36));
        if (ImGui::InvisibleButton("##d", sz)) {
            if (on) g_item.days.erase(std::remove(g_item.days.begin(), g_item.days.end(), d), g_item.days.end());
            else g_item.days.push_back(d);
            sfx::Play(on ? sfx::ToggleOff : sfx::ToggleOn);
        }
        float hv = anim::To(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (on) { w::Glow(dl, p, ImVec2(p.x + sz.x, p.y + sz.y), px(6), TypeColor(g_item.type), 0.6f); dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), anim::Col(TypeColor(g_item.type)), px(6)); }
        else w::Surface(dl, p, ImVec2(p.x + sz.x, p.y + sz.y), px(6), hv);
        std::string dn = agenda::DayName(d);
        ImVec2 ts = ImGui::CalcTextSize(dn.c_str());
        dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f), anim::Col(on ? ImVec4(1, 1, 1, 1) : theme::TextDim), dn.c_str());
        ImGui::PopID();
    }
    FieldLabel("Horário (início e fim)");
    ImGui::SetNextItemWidth(px(110));
    ImGui::InputTextWithHint("##s", "19:00", g_start, sizeof(g_start));
    ImGui::SameLine();
    ImGui::TextUnformatted("até");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(110));
    ImGui::InputTextWithHint("##e", "20:00", g_end, sizeof(g_end));
    if (g_item.type == agenda::Class) {
        ImGui::SameLine(0, px(20));
        ImGui::SetNextItemWidth(px(150));
        ImGui::SliderInt("##sp", &g_item.trialSpots, 0, 10, "%d vagas de experimental");
    }
    std::string s0 = agenda::NormTime(g_start), s1 = agenda::NormTime(g_end);
    bool ok = g_title[0] && !g_item.days.empty() && !s0.empty() && !s1.empty() && s1 > s0;
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::BeginDisabled(!ok);
    if (w::PrimaryButton(g_editNew ? "Adicionar à rotina" : "Salvar", ImVec2(px(180), 0), Icon::Check)) {
        g_item.title = g_title;
        g_item.start = s0;
        g_item.end = s1;
        std::sort(g_item.days.begin(), g_item.days.end());
        auto it = std::find_if(g_ag.routine.begin(), g_ag.routine.end(), [](auto& x) { return x.id == g_item.id; });
        if (it != g_ag.routine.end()) *it = g_item; else g_ag.routine.push_back(g_item);
        agenda::Save(g_ag);
        sfx::Play(sfx::Success);
        open = false;
    }
    ImGui::EndDisabled();
    if (!g_editNew) {
        ImGui::SameLine();
        if (w::GhostButton("Apagar", ImVec2(px(120), 0), Icon::Trash)) {
            g_ag.routine.erase(std::remove_if(g_ag.routine.begin(), g_ag.routine.end(), [](auto& x) { return x.id == g_item.id; }), g_ag.routine.end());
            agenda::Save(g_ag);
            open = false;
        }
    }
    if (!ok) w::Hint("Escolha pelo menos um dia e horários no formato 19:00.");
    if (!open || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- pedidos
void Bookings(App& app) {
    auto& bs = g_ag.bookings;
    std::vector<agenda::Booking*> list;
    for (auto& b : bs) list.push_back(&b);
    time_t now = time(nullptr);
    std::tm tn = *localtime(&now);
    char today[16];
    snprintf(today, sizeof(today), "%04d-%02d-%02d", tn.tm_year + 1900, tn.tm_mon + 1, tn.tm_mday);
    auto rank = [&](const agenda::Booking* b) { return b->status == agenda::Pending ? 0 : (b->status == agenda::Confirmed && b->date >= today) ? 1 : 2; };
    std::stable_sort(list.begin(), list.end(), [&](auto* a, auto* b) {
        int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        return ra == 2 ? (a->date + a->start) > (b->date + b->start) : (a->date + a->start) < (b->date + b->start);
    });
    if (list.empty()) {
        w::Hint("Nenhum pedido ainda. Quando alguém pedir uma aula experimental (pelo link ou falando com o KB), aparece aqui e num aviso na tela para você confirmar.");
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int lastRank = -1;
    bool changed = false;
    for (auto* b : list) {
        int r = rank(b);
        if (r != lastRank) {
            w::SectionTitle(r == 0 ? "Aguardando você" : r == 1 ? "Confirmadas" : "Anteriores", r == 0 ? Icon::Bell : r == 1 ? Icon::Check : Icon::Clock);
            lastRank = r;
        }
        ImGui::PushID(b->id.c_str());
        ImVec2 a = ImGui::GetCursorScreenPos();
        float rw = ImGui::GetContentRegionAvail().x, rh = px(64);
        ImVec4 acc = b->status == agenda::Pending ? theme::Amber : b->status == agenda::Confirmed ? theme::Green : kGray;
        w::Surface(dl, a, ImVec2(a.x + rw, a.y + rh), px(8), 0.f, acc, b->status == agenda::Pending);
        dl->AddRectFilled(a, ImVec2(a.x + px(3), a.y + rh), anim::Col(acc), px(8), ImDrawFlags_RoundCornersLeft);
        float al = r == 2 ? 0.55f : 1.f;
        ImGui::PushFont(theme::fonts.semibold);
        dl->AddText(ImVec2(a.x + px(16), a.y + px(10)), anim::Col(anim::WithAlpha(theme::Text, al)), b->name.c_str());
        ImGui::PopFont();
        std::string sub = agenda::DateLabel(b->date) + " às " + b->start + " · " + b->title + (b->phone.empty() ? "" : " · " + b->phone) +
                          (b->source == "site" ? " · pelo link" : "");
        if (b->status == agenda::Declined) sub += " · recusado";
        if (b->status == agenda::Canceled) sub += " · cancelado";
        ImGui::PushFont(theme::fonts.caption);
        dl->AddText(ImVec2(a.x + px(16), a.y + px(36)), anim::Col(anim::WithAlpha(theme::TextDim, al)), sub.c_str());
        ImGui::PopFont();
        float bx = a.x + rw - px(12);
        auto btn = [&](const char* label, Icon ic, bool primary, float wdt) {
            bx -= wdt;
            ImGui::SetCursorScreenPos(ImVec2(bx, a.y + (rh - ImGui::GetFrameHeight()) * 0.5f));
            bx -= px(8);
            return primary ? w::PrimaryButton(label, ImVec2(wdt, 0), ic) : w::GhostButton(label, ImVec2(wdt, 0), ic);
        };
        if (b->status == agenda::Pending) {
            if (btn("Confirmar", Icon::Check, true, px(130))) {
                for (auto& p : app.PendingPermissions()) if (p.kind == 1 && p.ref == b->id) app.AnswerPermission(p.id, 1);
                if (agenda::SetStatus(b->id, agenda::Confirmed)) changed = true;
            }
            if (btn("Recusar", Icon::Close, false, px(110))) {
                for (auto& p : app.PendingPermissions()) if (p.kind == 1 && p.ref == b->id) app.AnswerPermission(p.id, 0);
                if (agenda::SetStatus(b->id, agenda::Declined)) changed = true;
            }
        } else if (b->status == agenda::Confirmed && r == 1) {
            std::string link = agenda::WhatsappConfirm(g_ag, *b);
            if (!link.empty() && btn("Avisar no WhatsApp", Icon::Phone, true, px(190))) { platform::OpenUrl(link); sfx::Play(sfx::Send); }
            if (btn("Cancelar", Icon::Close, false, px(110))) { agenda::SetStatus(b->id, agenda::Canceled); changed = true; }
        }
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh + px(6)));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (changed) Reload();
}

// ---------------------------------------------------------------- link público
void PublicLink(App& app) {
    Config c = app.Cfg();
    w::SectionTitle("Agendamento pela internet", Icon::Globe);
    w::Hint("Gera uma página onde qualquer pessoa vê os horários livres, conversa com a assistente do KB e pede uma aula experimental. "
            "Cada pedido aparece aqui no notebook para você confirmar ou recusar.");
    bool on = c.agendaPublic;
    if (w::Toggle("Receber pedidos de aula experimental pela internet", &on)) { c.agendaPublic = on; app.ApplyConfig(c); }
    if (!on) return;
    std::string link = app.AgendaLink();
    Tunnel& tn = app.GetTunnel();
    if (link.empty() || tn.GetState() != Tunnel::State::Ready) {
        ImGui::Dummy(ImVec2(0, px(6)));
        w::Spinner("agsp", px(10), px(2.5f), theme::Cyan);
        ImGui::SameLine();
        ImGui::TextUnformatted(tn.GetState() == Tunnel::State::Downloading ? "Baixando o túnel seguro…" : "Criando o link público…");
        if (tn.GetState() == Tunnel::State::Error) w::Hint("%s", tn.Error().c_str());
        return;
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    float qs = px(170);
    DrawQrCode(link, qs);
    ImGui::SameLine(0, px(18));
    ImGui::BeginGroup();
    FieldLabel("Link para mandar no WhatsApp / Instagram");
    ImGui::PushFont(theme::fonts.semibold);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::Cyan);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(link.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (w::PrimaryButton("Copiar link", ImVec2(0, 0), Icon::Copy)) { ImGui::SetClipboardText(link.c_str()); app.Notify("Link copiado.", 1); }
    ImGui::SameLine();
    if (w::GhostButton("Abrir", ImVec2(0, 0), Icon::External)) platform::OpenUrl(link);
    w::Hint("O link muda quando o KB reinicia. Para um endereço fixo (ex.: agenda.kbmuaythai.com.br) é preciso um domínio próprio.");
    ImGui::EndGroup();
}
} // namespace

void AgendaSetTab(int tab) { g_agTab = tab; }

void AgendaWindow(App& app, bool* open) {
    if (ImGui::GetTime() - g_agLoaded > 2.0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !ImGui::IsAnyItemActive()) Reload();
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(560), px(460)), ImVec2(FLT_MAX, FLT_MAX));
    if (!BeginPanel("Agenda###agenda", open, ImGuiWindowFlags_NoCollapse, theme::Cyan)) { ImGui::End(); return; }
    int pending = 0;
    for (auto& b : g_ag.bookings) if (b.status == agenda::Pending) ++pending;
    std::string t1 = pending ? "Pedidos (" + std::to_string(pending) + ")" : "Pedidos";
    const char* tabs[] = {"Rotina da semana", t1.c_str(), "Link de agendamento"};
    SegmentedTabs("agtabs", &g_agTab, tabs, 3);
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::BeginChild("##agbody", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (g_agTab == 0) {
        if (w::PrimaryButton("Adicionar à rotina", ImVec2(0, 0), Icon::Calendar)) OpenEditor(nullptr);
        ImGui::SameLine();
        w::Hint("Ou fale: \"KB, minha rotina: segunda, quarta e sexta tenho aula de Muay Thai das 19h às 20h\".");
        ImGui::Dummy(ImVec2(0, px(2)));
        WeekGrid();
        // legenda
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int t = 0; t < 3; ++t) {
            if (t) ImGui::SameLine(0, px(18));
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(p.x, p.y + px(4)), ImVec2(p.x + px(12), p.y + px(16)), anim::Col(TypeColor(t)), px(3));
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(18), p.y));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextDim);
            ImGui::TextUnformatted(TypeLabel(t));
            ImGui::PopStyleColor();
        }
        w::SectionTitle("Próximos horários para aula experimental", Icon::Star);
        auto slots = agenda::FreeSlots(g_ag, 12);
        if (slots.empty()) w::Hint("Nenhum horário livre: adicione aulas que aceitam experimental ou horários livres na rotina.");
        float x0 = ImGui::GetCursorScreenPos().x, right = x0 + ImGui::GetContentRegionAvail().x, x = x0;
        float y = ImGui::GetCursorScreenPos().y;
        for (auto& s : slots) {
            std::string lbl = s.label + " · " + s.title;
            ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
            float cw = ts.x + px(24), ch = ts.y + px(14);
            if (x + cw > right && x > x0) { x = x0; y += ch + px(6); }
            ImVec2 a(x, y), b(x + cw, y + ch);
            w::Surface(dl, a, b, ch * 0.5f, 0.f);
            dl->AddText(ImVec2(x + px(12), y + px(7)), anim::Col(theme::Text), lbl.c_str());
            x += cw + px(6);
        }
        if (!slots.empty()) { ImGui::SetCursorScreenPos(ImVec2(x0, y + ImGui::GetTextLineHeight() + px(24))); ImGui::Dummy(ImVec2(0, 0)); }
        w::SectionTitle("Academia", Icon::User);
        FieldLabel("Nome (aparece na página de agendamento)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##studio", g_studio, sizeof(g_studio))) { g_ag.studio = g_studio; agenda::Save(g_ag); }
        FieldLabel("Endereço (vai na confirmação para o aluno)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##addr", g_addr, sizeof(g_addr))) { g_ag.address = g_addr; agenda::Save(g_ag); }
    } else if (g_agTab == 1) {
        Bookings(app);
    } else {
        PublicLink(app);
    }
    ImGui::Dummy(ImVec2(0, px(10)));
    ImGui::EndChild();
    ItemEditor();
    ImGui::End();
}

} // namespace ui
