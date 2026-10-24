#include "net/web.h"
#include "net/http.h"
#include "core/text.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace web {

namespace {
const char* kUA = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36";

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

std::string DecodeEntities(const std::string& s) {
    static const struct { const char* n; unsigned cp; } kEnt[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}, {"aacute", 0xE1}, {"eacute", 0xE9},
        {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"atilde", 0xE3}, {"otilde", 0xF5}, {"ccedil", 0xE7}, {"acirc", 0xE2},
        {"ecirc", 0xEA}, {"ocirc", 0xF4}, {"agrave", 0xE0}, {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Ccedil", 0xC7}, {"hellip", 0x2026},
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"ldquo", 0x201C}, {"rdquo", 0x201D}};
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        std::string e = s.substr(i + 1, semi - i - 1);
        unsigned cp = 0;
        bool ok = false;
        if (!e.empty() && e[0] == '#') {
            cp = (e.size() > 1 && (e[1] == 'x' || e[1] == 'X')) ? (unsigned)strtoul(e.c_str() + 2, nullptr, 16) : (unsigned)strtoul(e.c_str() + 1, nullptr, 10);
            ok = cp > 0;
        } else {
            for (auto& k : kEnt) if (e == k.n) { cp = k.cp; ok = true; break; }
        }
        if (ok) { AppendUtf8(out, cp); i = semi; }
        else out += s[i];
    }
    return out;
}

std::string UrlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

std::string Collapse(const std::string& s) {
    std::string o;
    bool space = false;
    int newlines = 0;
    for (char c : s) {
        if (c == '\n') { if (newlines < 2) o += '\n'; ++newlines; space = false; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { space = true; continue; }
        if (space && !o.empty() && o.back() != '\n') o += ' ';
        space = false;
        newlines = 0;
        o += c;
    }
    return text::Trim(o);
}
} // namespace

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else if (c == ' ') o += '+';
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

std::string HtmlToText(const std::string& html) {
    std::string lower = html;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    std::string out;
    out.reserve(html.size() / 3);
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] == '<') {
            size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            std::string tag = lower.substr(i + 1, std::min<size_t>(end - i - 1, 12));
            // pula blocos inteiros que não são conteúdo
            for (const char* skip : {"script", "style", "noscript", "svg", "nav", "footer", "header", "form"}) {
                if (tag.rfind(skip, 0) == 0) {
                    std::string close = std::string("</") + skip;
                    size_t c = lower.find(close, end);
                    if (c != std::string::npos) end = lower.find('>', c);
                    break;
                }
            }
            if (end == std::string::npos) break;
            if (tag.rfind("br", 0) == 0 || tag.rfind("p", 0) == 0 || tag.rfind("/p", 0) == 0 || tag.rfind("li", 0) == 0 ||
                tag.rfind("h", 0) == 0 || tag.rfind("/h", 0) == 0 || tag.rfind("div", 0) == 0 || tag.rfind("tr", 0) == 0)
                out += '\n';
            i = end + 1;
            continue;
        }
        out += html[i++];
    }
    return Collapse(DecodeEntities(out));
}

std::vector<Result> Search(const std::string& query, int maxResults, std::string* err) {
    std::vector<Result> res;
    http::Options o;
    o.connectTimeoutMs = 8000;
    o.receiveTimeoutMs = 15000;
    o.headers.push_back(kUA);
    o.headers.push_back("Accept-Language: pt-BR,pt;q=0.9");
    auto r = http::Request("GET", "https://html.duckduckgo.com/html/?q=" + UrlEncode(query) + "&kl=br-pt", "", o);
    if (!r.ok()) { if (err) *err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error; return res; }
    const std::string& h = r.body;
    size_t pos = 0;
    while ((int)res.size() < maxResults && (pos = h.find("class=\"result__a\"", pos)) != std::string::npos) {
        Result it;
        size_t hrefStart = h.rfind("href=\"", pos);
        size_t tagEnd = h.find('>', pos);
        size_t hp = h.find("href=\"", pos);
        if (hp != std::string::npos && hp < tagEnd) hrefStart = hp;
        if (hrefStart == std::string::npos || tagEnd == std::string::npos) break;
        size_t hrefEnd = h.find('"', hrefStart + 6);
        std::string href = DecodeEntities(h.substr(hrefStart + 6, hrefEnd - hrefStart - 6));
        size_t u = href.find("uddg=");
        if (u != std::string::npos) {
            size_t amp = href.find('&', u);
            href = UrlDecode(href.substr(u + 5, amp == std::string::npos ? std::string::npos : amp - u - 5));
        }
        if (href.rfind("//", 0) == 0) href = "https:" + href;
        size_t titleEnd = h.find("</a>", tagEnd);
        it.title = HtmlToText(h.substr(tagEnd + 1, titleEnd - tagEnd - 1));
        it.url = href;
        size_t sn = h.find("result__snippet", titleEnd);
        size_t nextRes = h.find("class=\"result__a\"", titleEnd);
        if (sn != std::string::npos && (nextRes == std::string::npos || sn < nextRes)) {
            size_t s0 = h.find('>', sn), s1 = h.find("</a>", s0);
            if (s0 != std::string::npos && s1 != std::string::npos) it.snippet = HtmlToText(h.substr(s0 + 1, s1 - s0 - 1));
        }
        pos = titleEnd;
        // ignora anúncios
        if (it.url.find("duckduckgo.com/y.js") != std::string::npos || it.url.empty()) continue;
        res.push_back(it);
    }
    if (res.empty() && err) *err = "nenhum resultado";
    return res;
}

std::string FetchText(const std::string& url, size_t maxChars, std::string* title, std::string* err) {
    if (!(text::StartsWith(url, "http://") || text::StartsWith(url, "https://"))) { if (err) *err = "endereço inválido"; return {}; }
    http::Options o;
    o.connectTimeoutMs = 8000;
    o.receiveTimeoutMs = 15000;
    o.headers.push_back(kUA);
    o.headers.push_back("Accept-Language: pt-BR,pt;q=0.9");
    std::string body;
    auto r = http::Request("GET", url, "", o, [&](const char* d, size_t n, uint64_t) {
        body.append(d, n);
        return body.size() < 3 * 1024 * 1024; // no máximo 3 MB
    });
    if (!r.ok()) { if (err) *err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error; return {}; }
    if (title) {
        std::string lower = body.substr(0, 20000);
        for (auto& c : lower) c = (char)tolower((unsigned char)c);
        size_t t0 = lower.find("<title"), t1 = lower.find("</title>");
        if (t0 != std::string::npos && t1 != std::string::npos && t1 > t0) {
            size_t gt = lower.find('>', t0);
            *title = HtmlToText(body.substr(gt + 1, t1 - gt - 1));
        }
    }
    std::string t = HtmlToText(body);
    if (t.size() > maxChars) {
        t.resize(maxChars);
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back(); // não corta no meio de um caractere
        if (!t.empty() && (unsigned char)t.back() >= 0xC0) t.pop_back();
        t += "…";
    }
    return t;
}

} // namespace web
