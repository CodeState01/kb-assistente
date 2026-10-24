#include "features/agenda.h"
#include "core/config.h"
#include "core/text.h"
#include "net/web.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

using nlohmann::json;

namespace agenda {
namespace {

std::mutex g_mtx; // pedidos chegam pelo site (outra thread) e pela interface

std::string Path() { return config::UserDataDir() + "\\agenda.json"; }

int Minutes(const std::string& hhmm) {
    int h = 0, m = 0;
    if (sscanf(hhmm.c_str(), "%d:%d", &h, &m) < 1) return -1;
    return h * 60 + m;
}

std::string DateStr(const std::tm& t) {
    char b[16];
    snprintf(b, sizeof(b), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return b;
}

Data LoadUnlocked() {
    Data d;
    std::ifstream f(text::Widen(Path()), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return d;
    d.studio = j.value("studio", d.studio);
    d.address = j.value("address", "");
    d.daysAhead = std::clamp(j.value("daysAhead", 14), 1, 60);
    if (j.contains("routine"))
        for (auto& r : j["routine"]) {
            Item it;
            it.id = r.value("id", NewId("r"));
            it.title = r.value("title", "Aula");
            it.start = r.value("start", "19:00");
            it.end = r.value("end", "20:00");
            it.type = r.value("type", 0);
            it.trialSpots = r.value("trialSpots", 2);
            if (r.contains("days")) for (auto& x : r["days"]) if (x.is_number()) it.days.push_back(x.get<int>());
            d.routine.push_back(it);
        }
    if (j.contains("bookings"))
        for (auto& b : j["bookings"]) {
            Booking k;
            k.id = b.value("id", "");
            k.name = b.value("name", "");
            k.phone = b.value("phone", "");
            k.date = b.value("date", "");
            k.start = b.value("start", "");
            k.end = b.value("end", "");
            k.title = b.value("title", "");
            k.source = b.value("source", "");
            k.note = b.value("note", "");
            k.status = b.value("status", 0);
            k.created = b.value("created", 0LL);
            d.bookings.push_back(k);
        }
    return d;
}

void SaveUnlocked(const Data& d) {
    json j = {{"studio", d.studio}, {"address", d.address}, {"daysAhead", d.daysAhead}, {"routine", json::array()}, {"bookings", json::array()}};
    for (auto& r : d.routine)
        j["routine"].push_back({{"id", r.id}, {"title", r.title}, {"start", r.start}, {"end", r.end}, {"type", r.type}, {"trialSpots", r.trialSpots}, {"days", r.days}});
    for (auto& b : d.bookings)
        j["bookings"].push_back({{"id", b.id}, {"name", b.name}, {"phone", b.phone}, {"date", b.date}, {"start", b.start}, {"end", b.end},
                                 {"title", b.title}, {"source", b.source}, {"note", b.note}, {"status", b.status}, {"created", b.created}});
    std::string p = Path();
    std::ofstream f(text::Widen(p + ".tmp"), std::ios::binary);
    f << j.dump(1);
    f.close();
    MoveFileExW(text::Widen(p + ".tmp").c_str(), text::Widen(p).c_str(), MOVEFILE_REPLACE_EXISTING);
}
} // namespace

std::string NewId(const char* prefix) {
    static int n = 0;
    char b[40];
    snprintf(b, sizeof(b), "%s%llx%d", prefix, (unsigned long long)GetTickCount64(), ++n);
    return b;
}

Data Load() { std::lock_guard<std::mutex> l(g_mtx); return LoadUnlocked(); }
void Save(const Data& d) { std::lock_guard<std::mutex> l(g_mtx); SaveUnlocked(d); }

std::string DayName(int w, bool s) {
    static const char* sh[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sáb"};
    static const char* lg[] = {"domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado"};
    w = ((w % 7) + 7) % 7;
    return s ? sh[w] : lg[w];
}

std::string DaysText(const std::vector<int>& days) {
    std::vector<int> d = days;
    std::sort(d.begin(), d.end());
    d.erase(std::unique(d.begin(), d.end()), d.end());
    if (d.size() == 7) return "todos os dias";
    if (d == std::vector<int>{1, 2, 3, 4, 5}) return "segunda a sexta";
    if (d == std::vector<int>{1, 2, 3, 4, 5, 6}) return "segunda a sábado";
    std::string out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i) out += i + 1 == d.size() ? " e " : ", ";
        out += DayName(d[i]);
    }
    return out;
}

std::string DateLabel(const std::string& date) {
    std::tm t{};
    if (sscanf(date.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return date;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_hour = 12;
    mktime(&t);
    char b[32];
    snprintf(b, sizeof(b), "%s, %02d/%02d", DayName(t.tm_wday).c_str(), t.tm_mday, t.tm_mon + 1);
    return b;
}

int ParseDays(const std::string& s) {
    std::string f = text::Fold(s);
    static const char* keys[7][3] = {{"dom", "", ""}, {"seg", "2a", ""}, {"ter", "3a", ""}, {"qua", "4a", ""}, {"qui", "5a", ""}, {"sex", "6a", ""}, {"sab", "", ""}};
    if (f.find("todos os dias") != std::string::npos || f.find("todo dia") != std::string::npos) return 0x7F;
    if (f.find("fim de semana") != std::string::npos || f.find("final de semana") != std::string::npos) return 1 | 64;
    int mask = 0;
    std::vector<int> found;
    for (int d = 0; d < 7; ++d)
        for (auto k : keys[d])
            if (*k && f.find(k) != std::string::npos) { mask |= 1 << d; found.push_back(d); break; }
    // "segunda a sexta" → intervalo
    if (f.find(" a ") != std::string::npos && found.size() == 2) {
        int a = found[0], b = found[1];
        size_t pa = std::string::npos, pb = std::string::npos;
        for (auto k : keys[a]) if (*k) pa = std::min(pa, f.find(k));
        for (auto k : keys[b]) if (*k) pb = std::min(pb, f.find(k));
        if (pb < pa) std::swap(a, b);
        mask = 0;
        for (int d = a;; d = (d + 1) % 7) { mask |= 1 << d; if (d == b) break; }
    }
    if (!mask && (f.find("dia de semana") != std::string::npos || f.find("dias uteis") != std::string::npos)) mask = 0x3E;
    return mask;
}

std::string NormTime(const std::string& s) {
    std::string f = text::Fold(text::Trim(s));
    int h = -1, m = 0;
    if (f.find(':') != std::string::npos) sscanf(f.c_str(), "%d:%d", &h, &m);
    else sscanf(f.c_str(), "%dh%d", &h, &m);
    if (h < 0 || h > 23 || m < 0 || m > 59) return {};
    if (h < 12 && (f.find("pm") != std::string::npos || f.find("noite") != std::string::npos || f.find("tarde") != std::string::npos)) h += 12;
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", h, m);
    return b;
}

std::vector<Slot> FreeSlots(const Data& d, int maxSlots) {
    std::vector<Slot> out;
    time_t now = time(nullptr);
    std::tm today = *localtime(&now);
    int nowMin = today.tm_hour * 60 + today.tm_min;
    for (int off = 0; off <= d.daysAhead && (int)out.size() < maxSlots; ++off) {
        std::tm t = today;
        t.tm_mday += off;
        t.tm_hour = 12;
        mktime(&t);
        std::string date = DateStr(t);
        std::vector<const Item*> items;
        for (auto& it : d.routine)
            if (std::find(it.days.begin(), it.days.end(), t.tm_wday) != it.days.end()) items.push_back(&it);
        std::sort(items.begin(), items.end(), [](auto* a, auto* b) { return Minutes(a->start) < Minutes(b->start); });
        for (auto* it : items) {
            if (it->type == Busy) continue;
            int s0 = Minutes(it->start), s1 = Minutes(it->end);
            if (s0 < 0) continue;
            if (off == 0 && s0 <= nowMin + 60) continue; // com pelo menos 1 hora de antecedência
            bool clash = false;
            for (auto* o : items)
                if (o->type == Busy && Minutes(o->start) < s1 && Minutes(o->end) > s0) clash = true;
            if (clash) continue;
            int cap = it->type == Free ? 1 : std::max(1, it->trialSpots);
            int used = 0;
            for (auto& b : d.bookings)
                if (b.date == date && b.start == it->start && (b.status == Pending || b.status == Confirmed)) ++used;
            if (used >= cap) continue;
            Slot s;
            s.date = date;
            s.start = it->start;
            s.end = it->end;
            s.title = it->title;
            s.key = date + " " + it->start;
            s.spotsLeft = cap - used;
            s.label = DateLabel(date) + " às " + it->start;
            out.push_back(s);
            if ((int)out.size() >= maxSlots) break;
        }
    }
    return out;
}

std::string Request(const std::string& name, const std::string& phone, const std::string& slotKey, const std::string& source, std::string* err) {
    std::lock_guard<std::mutex> l(g_mtx);
    Data d = LoadUnlocked();
    auto slots = FreeSlots(d, 400);
    auto it = std::find_if(slots.begin(), slots.end(), [&](const Slot& s) { return s.key == slotKey; });
    if (it == slots.end()) { if (err) *err = "Esse horário não está mais disponível."; return {}; }
    int pending = 0;
    for (auto& b : d.bookings) if (b.status == Pending) ++pending;
    if (pending >= 30) { if (err) *err = "Muitos pedidos aguardando. Tente mais tarde."; return {}; }
    std::string digits;
    for (char c : phone) if (c >= '0' && c <= '9') digits += c;
    for (auto& b : d.bookings) {
        std::string bd;
        for (char c : b.phone) if (c >= '0' && c <= '9') bd += c;
        if (!digits.empty() && bd == digits && b.date == it->date && b.start == it->start && b.status != Declined && b.status != Canceled) return b.id;
    }
    Booking b;
    b.id = NewId("b");
    b.name = name.substr(0, 80);
    b.phone = phone.substr(0, 30);
    b.date = it->date;
    b.start = it->start;
    b.end = it->end;
    b.title = it->title;
    b.source = source;
    b.status = Pending;
    b.created = (long long)time(nullptr);
    d.bookings.push_back(b);
    SaveUnlocked(d);
    return b.id;
}

bool SetStatus(const std::string& id, int status, Booking* out) {
    std::lock_guard<std::mutex> l(g_mtx);
    Data d = LoadUnlocked();
    for (auto& b : d.bookings)
        if (b.id == id) {
            b.status = status;
            if (out) *out = b;
            SaveUnlocked(d);
            return true;
        }
    return false;
}

bool Find(const std::string& id, Booking* out) {
    Data d = Load();
    for (auto& b : d.bookings) if (b.id == id) { if (out) *out = b; return true; }
    return false;
}

std::string RoutineText(const Data& d) {
    if (d.routine.empty()) return "A rotina ainda está vazia.";
    std::string out;
    for (auto& r : d.routine) {
        const char* kind = r.type == Class ? "aula (aceita experimental)" : r.type == Busy ? "ocupado" : "horário livre para experimental";
        out += "- [" + r.id + "] " + r.title + ": " + DaysText(r.days) + ", " + r.start + "–" + r.end + " · " + kind;
        if (r.type == Class) out += ", " + std::to_string(r.trialSpots) + " vaga(s) de experimental por aula";
        out += "\n";
    }
    return out;
}

std::string SlotsText(const Data& d, int max) {
    auto s = FreeSlots(d, max);
    if (s.empty()) return "Nenhum horário livre para aula experimental nos próximos " + std::to_string(d.daysAhead) + " dias.";
    std::string out;
    for (auto& x : s) out += "- {" + x.key + "} " + x.label + " — " + x.title + "\n";
    return out;
}

std::string WhatsappConfirm(const Data& d, const Booking& b) {
    std::string num;
    for (char c : b.phone) if (c >= '0' && c <= '9') num += c;
    if (num.empty()) return {};
    if (num.size() <= 11) num = "55" + num;
    std::string first = b.name.substr(0, b.name.find(' '));
    std::string msg = "Oi, " + first + "! Sua aula experimental de " + b.title + " na " + d.studio + " está confirmada para " + DateLabel(b.date) +
                      " às " + b.start + ".";
    if (!d.address.empty()) msg += " Endereço: " + d.address + ".";
    msg += " Venha com roupa confortável e chegue 10 minutos antes. Até lá! 🥊";
    return "https://wa.me/" + num + "?text=" + web::UrlEncode(msg);
}

} // namespace agenda
