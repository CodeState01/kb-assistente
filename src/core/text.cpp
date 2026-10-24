#include "core/text.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace text {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

std::string Fold(const std::string& utf8) {
    std::wstring w = Widen(utf8);
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        wchar_t l = (wchar_t)(uintptr_t)CharLowerW((LPWSTR)(uintptr_t)c);
        char r;
        switch (l) {
        case L'á': case L'à': case L'â': case L'ã': case L'ä': r = 'a'; break;
        case L'é': case L'è': case L'ê': case L'ë': r = 'e'; break;
        case L'í': case L'ì': case L'î': case L'ï': r = 'i'; break;
        case L'ó': case L'ò': case L'ô': case L'õ': case L'ö': r = 'o'; break;
        case L'ú': case L'ù': case L'û': case L'ü': r = 'u'; break;
        case L'ç': r = 'c'; break;
        case L'ñ': r = 'n'; break;
        default:
            if (l < 128) r = (char)l;
            else r = ' ';
        }
        out += r;
    }
    return out;
}

bool StartsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string FormatBytes(uint64_t b) {
    char buf[64];
    double v = (double)b;
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    snprintf(buf, sizeof(buf), i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
    return buf;
}

std::string FormatDuration(double s) {
    char buf[64];
    if (s < 60) snprintf(buf, sizeof(buf), "%.0fs", s);
    else snprintf(buf, sizeof(buf), "%dm%02ds", (int)(s / 60), (int)s % 60);
    return buf;
}

std::string CleanForSpeech(const std::string& in) {
    std::string s;
    s.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        // remove URLs
        if ((c == 'h' && in.compare(i, 7, "http://") == 0) || (c == 'h' && in.compare(i, 8, "https://") == 0)) {
            while (i < in.size() && in[i] != ' ' && in[i] != '\n' && in[i] != ')') ++i;
            s += " o link ";
            continue;
        }
        if (c == '*' || c == '#' || c == '`' || c == '|' || c == '>' || c == '~') continue;
        if (c == '_' && (i == 0 || in[i - 1] == ' ' || i + 1 == in.size() || in[i + 1] == ' ')) continue;
        if (c == '[' || c == ']') continue;
        s += c;
    }
    // "- item" no início -> "item"
    std::string t = Trim(s);
    if (StartsWith(t, "- ")) t = t.substr(2);
    return t;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(a, i - a));
            a = i + 1;
        }
    }
    return out;
}

std::string NowPortuguese() {
    static const char* dias[] = {"domingo", "segunda-feira", "terça-feira", "quarta-feira",
                                 "quinta-feira", "sexta-feira", "sábado"};
    static const char* meses[] = {"janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho",
                                  "agosto", "setembro", "outubro", "novembro", "dezembro"};
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s, %d de %s de %d, %02d:%02d", dias[lt.tm_wday], lt.tm_mday, meses[lt.tm_mon],
             1900 + lt.tm_year, lt.tm_hour, lt.tm_min);
    return buf;
}

std::string GreetingForNow() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    if (lt.tm_hour >= 5 && lt.tm_hour < 12) return "Bom dia";
    if (lt.tm_hour >= 12 && lt.tm_hour < 18) return "Boa tarde";
    return "Boa noite";
}

} // namespace text

namespace text {

static std::vector<std::string> Words(const std::string& folded) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : folded) {
        if (isalnum((unsigned char)c)) cur += c;
        else { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool ParseIntPt(const std::string& s, int* out) {
    std::string f = Fold(s);
    for (size_t i = 0; i < f.size(); ++i)
        if (isdigit((unsigned char)f[i])) { *out = atoi(f.c_str() + i); return true; }
    static const struct { const char* w; int v; } kNum[] = {
        {"zero", 0}, {"um", 1}, {"uma", 1}, {"dois", 2}, {"duas", 2}, {"tres", 3}, {"quatro", 4}, {"cinco", 5},
        {"seis", 6}, {"sete", 7}, {"oito", 8}, {"nove", 9}, {"dez", 10}, {"onze", 11}, {"doze", 12}, {"treze", 13},
        {"quatorze", 14}, {"catorze", 14}, {"quinze", 15}, {"dezesseis", 16}, {"dezessete", 17}, {"dezoito", 18},
        {"dezenove", 19}, {"vinte", 20}, {"trinta", 30}, {"quarenta", 40}, {"cinquenta", 50}, {"sessenta", 60},
        {"setenta", 70}, {"oitenta", 80}, {"noventa", 90}, {"cem", 100}, {"cento", 100}, {"duzentos", 200}, {"trezentos", 300}};
    int total = 0;
    bool any = false;
    for (auto& w : Words(f))
        for (auto& n : kNum)
            if (w == n.w) { total += n.v; any = true; break; }
    if (any) *out = total;
    return any;
}

int ParseDurationPt(const std::string& s) {
    std::string f = Fold(s);
    // "2:30"
    size_t colon = f.find(':');
    if (colon != std::string::npos && colon > 0 && isdigit((unsigned char)f[colon - 1])) {
        size_t a = colon;
        while (a > 0 && isdigit((unsigned char)f[a - 1])) --a;
        return atoi(f.c_str() + a) * 60 + atoi(f.c_str() + colon + 1);
    }
    bool meio = f.find("meio") != std::string::npos || f.find("meia") != std::string::npos;
    size_t m = f.find("min");
    if (m != std::string::npos) {
        int mins = 0, secs = 0;
        bool hasMin = ParseIntPt(f.substr(0, m), &mins);
        std::string rest = f.substr(m);
        size_t sp = rest.find(' ');
        if (sp != std::string::npos && rest.find("seg") != std::string::npos) ParseIntPt(rest.substr(sp), &secs);
        if (!hasMin && meio) return 30;          // "meio minuto"
        return mins * 60 + secs + (meio ? 30 : 0);
    }
    int v = 0;
    if (!ParseIntPt(f, &v)) return meio ? 30 : -1;
    if (f.find("seg") != std::string::npos) return v;
    // sem unidade: números pequenos são minutos ("3" = 3 minutos), grandes são segundos ("90")
    return v <= 15 ? v * 60 + (meio ? 30 : 0) : v;
}

std::string DurationPt(int sec) {
    char b[64];
    int m = sec / 60, s = sec % 60;
    if (m && s) snprintf(b, sizeof(b), "%d min e %d s", m, s);
    else if (m) snprintf(b, sizeof(b), m == 1 ? "%d minuto" : "%d minutos", m);
    else snprintf(b, sizeof(b), "%d segundos", s);
    return b;
}

} // namespace text
