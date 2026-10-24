#include "ui/blocks.h"
#include "core/text.h"
#include <cstring>

static std::vector<std::string> TableRow(const std::string& line) {
    std::string l = text::Trim(line);
    if (!l.empty() && l.front() == '|') l.erase(0, 1);
    if (!l.empty() && l.back() == '|') l.pop_back();
    std::vector<std::string> cells;
    for (auto& c : text::Split(l, '|')) cells.push_back(StripInline(text::Trim(c)));
    return cells;
}

static bool IsSeparatorRow(const std::string& line) {
    bool dash = false;
    for (char c : line) {
        if (c == '-') dash = true;
        else if (c != '|' && c != ':' && c != ' ' && c != '\t') return false;
    }
    return dash;
}

std::string StripInline(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        // escapes do markdown: \_ \* \# ...
        if (s[i] == '\\' && i + 1 < s.size() && strchr("_*#`[]()>-.!|", s[i + 1])) { o += s[++i]; continue; }
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') { ++i; continue; }
        if (s[i] == '_' && i + 1 < s.size() && s[i + 1] == '_') { ++i; continue; }
        if (s[i] == '`') continue;
        o += s[i];
    }
    return o;
}

static BlockType FenceType(const std::string& lang) {
    std::string l = text::Fold(text::Trim(lang));
    if (l == "card" || l == "cartao") return BlockType::Card;
    if (l == "passos" || l == "steps" || l == "etapas") return BlockType::Steps;
    if (l == "aviso" || l == "warning" || l == "alerta") return BlockType::Warning;
    if (l == "dica" || l == "tip" || l == "nota" || l == "info") return BlockType::Tip;
    if (l == "tabela" || l == "table") return BlockType::Table;
    if (text::StartsWith(l, "arquivo") || text::StartsWith(l, "file ") || l == "file") return BlockType::File;
    return BlockType::Code;
}

std::vector<Block> ParseBlocks(const std::string& md) {
    std::vector<Block> out;
    auto lines = text::Split(md, '\n');
    Block para;
    auto flushPara = [&] {
        std::string t = text::Trim(para.text);
        if (!t.empty()) {
            para.text = StripInline(t);
            out.push_back(para);
        }
        para = Block{};
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string raw = lines[i];
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string t = text::Trim(raw);

        if (text::StartsWith(t, "```")) {
            flushPara();
            Block b;
            std::string lang = t.substr(3);
            b.type = FenceType(lang);
            // Modelos menores às vezes esquecem a palavra "arquivo": um bloco sem linguagem (ou txt)
            // cuja 1ª linha é um caminho completo com extensão também vira pedido de arquivo (sempre com aprovação).
            {
                std::string fl = text::Fold(text::Trim(lang));
                if (fl.empty() || fl == "txt" || fl == "text" || fl == "plaintext") {
                    std::string first = i + 1 < lines.size() ? text::Trim(lines[i + 1]) : "";
                    bool pathLike = first.size() > 4 && isalpha((unsigned char)first[0]) && first[1] == ':' &&
                                    (first[2] == '\\' || first[2] == '/');
                    if (pathLike) {
                        size_t slash = first.find_last_of("\\/"), dot = first.find_last_of('.');
                        pathLike = dot != std::string::npos && dot > slash && first.size() - dot <= 6 &&
                                   first.find(' ', dot) == std::string::npos;
                    }
                    if (pathLike) { b.type = BlockType::File; lang = "arquivo"; }
                }
            }
            b.title = text::Trim(lang);
            std::vector<std::string> body;
            b.complete = false;
            for (++i; i < lines.size(); ++i) {
                std::string l = lines[i];
                if (!l.empty() && l.back() == '\r') l.pop_back();
                if (text::StartsWith(text::Trim(l), "```")) { b.complete = true; break; }
                // fechamento colado no fim da linha ("Maçã```")
                std::string tl = text::Trim(l);
                if (tl.size() > 3 && tl.compare(tl.size() - 3, 3, "```") == 0) {
                    body.push_back(tl.substr(0, tl.size() - 3));
                    b.complete = true;
                    break;
                }
                body.push_back(l);
            }
            switch (b.type) {
            case BlockType::File: {
                // título = caminho (tudo depois da palavra "arquivo"/"file"), corpo = conteúdo exato
                std::string rest = text::Trim(lang);
                size_t sp = rest.find_first_of(" \t:");
                b.title = sp == std::string::npos ? "" : text::Trim(rest.substr(sp + 1));
                if (!b.title.empty() && b.title[0] == ':') b.title = text::Trim(b.title.substr(1));
                // alguns modelos colocam o caminho na primeira linha do bloco
                if (b.title.empty() && !body.empty()) {
                    std::string f0 = text::Trim(body[0]);
                    std::string low = text::Fold(f0);
                    if (text::StartsWith(low, "caminho:") || text::StartsWith(low, "path:")) f0 = text::Trim(f0.substr(f0.find(':') + 1));
                    if (f0.find(":\\") != std::string::npos || f0.find(":/") == 1 || text::StartsWith(f0, "~") || text::StartsWith(f0, "%")) {
                        b.title = f0;
                        body.erase(body.begin());
                    }
                }
                for (size_t k = 0;k < body.size(); ++k) b.text += body[k] + (k + 1 < body.size() ? "\n" : "");
                break;
            }
            case BlockType::Code: {
                for (size_t k = 0; k < body.size(); ++k) b.text += body[k] + (k + 1 < body.size() ? "\n" : "");
                break;
            }
            case BlockType::Card: {
                size_t k = 0;
                while (k < body.size() && text::Trim(body[k]).empty()) ++k;
                b.title = k < body.size() ? StripInline(text::Trim(body[k])) : "";
                std::string rest;
                for (++k; k < body.size(); ++k) rest += body[k] + "\n";
                b.text = StripInline(text::Trim(rest));
                break;
            }
            case BlockType::Steps: {
                b.title.clear();
                for (auto& l : body) {
                    std::string s = text::Trim(l);
                    // remove "1." / "-" do começo
                    size_t p = 0;
                    while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.' || s[p] == ')' || s[p] == '-')) ++p;
                    s = text::Trim(s.substr(p));
                    if (!s.empty()) b.items.push_back(StripInline(s));
                }
                break;
            }
            case BlockType::Table: {
                b.title.clear();
                for (auto& l : body) {
                    if (text::Trim(l).empty() || IsSeparatorRow(l)) continue;
                    b.rows.push_back(TableRow(l));
                }
                break;
            }
            default: {
                std::string rest;
                for (auto& l : body) rest += l + "\n";
                b.text = StripInline(text::Trim(rest));
                b.title.clear();
                break;
            }
            }
            out.push_back(std::move(b));
            continue;
        }

        if (t.empty()) { flushPara(); continue; }

        // Tabela markdown
        if (t.front() == '|' && i + 1 < lines.size() && IsSeparatorRow(text::Trim(lines[i + 1]))) {
            flushPara();
            Block b;
            b.type = BlockType::Table;
            for (; i < lines.size(); ++i) {
                std::string l = text::Trim(lines[i]);
                if (l.empty() || l.front() != '|') { --i; break; }
                if (IsSeparatorRow(l)) continue;
                b.rows.push_back(TableRow(l));
            }
            out.push_back(std::move(b));
            continue;
        }

        if (t[0] == '#') {
            flushPara();
            size_t p = t.find_first_not_of('#');
            Block b;
            b.type = BlockType::Heading;
            b.text = StripInline(text::Trim(p == std::string::npos ? "" : t.substr(p)));
            out.push_back(b);
            continue;
        }
        if (text::StartsWith(t, "- ") || text::StartsWith(t, "* ") || text::StartsWith(t, "• ")) {
            flushPara();
            Block b;
            b.type = BlockType::Bullet;
            b.text = StripInline(text::Trim(t.substr(t[0] == '-' || t[0] == '*' ? 2 : 4)));
            out.push_back(b);
            continue;
        }
        if (text::StartsWith(t, "> ")) {
            flushPara();
            Block b;
            b.type = BlockType::Quote;
            b.text = StripInline(t.substr(2));
            out.push_back(b);
            continue;
        }
        {
            size_t p = 0;
            while (p < t.size() && isdigit((unsigned char)t[p])) ++p;
            if (p > 0 && p < 4 && p + 1 < t.size() && (t[p] == '.' || t[p] == ')') && t[p + 1] == ' ') {
                flushPara();
                Block b;
                b.type = BlockType::Numbered;
                b.number = atoi(t.c_str());
                b.text = StripInline(text::Trim(t.substr(p + 2)));
                out.push_back(b);
                continue;
            }
        }
        if (!para.text.empty()) para.text += ' ';
        para.text += t;
    }
    flushPara();
    return out;
}
