// Ferramentas que a IA pode usar: pesquisa na web, leitura de páginas, skills, treinos e timer.
#include "app.h"
#include "core/skills.h"
#include "core/text.h"
#include "net/web.h"
#include <nlohmann/json.hpp>
#include <algorithm>

using nlohmann::json;

std::string App::ToolsJson() {
    auto fn = [](const char* name, const char* desc, json props, std::vector<std::string> req) {
        return json{{"type", "function"},
                    {"function", {{"name", name}, {"description", desc},
                                  {"parameters", {{"type", "object"}, {"properties", props}, {"required", req}}}}}};
    };
    json tools = json::array();
    Config c = Cfg();
    if (c.webTools) {
        tools.push_back(fn("web_search", "Pesquisa na internet. Use para notícias, fatos recentes, preços, clima, resultados de jogos ou qualquer coisa que você não saiba com certeza.",
                           {{"query", {{"type", "string"}, {"description", "o que pesquisar"}}}}, {"query"}));
        tools.push_back(fn("open_url", "Abre uma página da web e devolve o texto dela (use depois de web_search para ler um resultado).",
                           {{"url", {{"type", "string"}, {"description", "endereço completo https://..."}}}}, {"url"}));
    }
    tools.push_back(fn("use_skill", "Carrega as instruções de uma skill (habilidade) instalada. Use quando o pedido combinar com a descrição de uma skill.",
                       {{"name", {{"type", "string"}, {"description", "nome da skill"}}}}, {"name"}));
    tools.push_back(fn("create_workout", "Cria e salva um treino no timer de rounds do KB.",
                       {{"name", {{"type", "string"}}}, {"rounds", {{"type", "integer"}}}, {"round_seconds", {{"type", "integer"}}},
                        {"rest_seconds", {{"type", "integer"}}}},
                       {"name", "rounds", "round_seconds", "rest_seconds"}));
    tools.push_back(fn("start_timer", "Abre e inicia o timer de rounds. Se informar o nome de um treino salvo, usa esse treino.",
                       {{"workout_name", {{"type", "string"}}}}, {}));
    return tools.dump();
}

std::string App::ToolsPrompt() {
    Config c = Cfg();
    std::string p = "\n\nFERRAMENTAS: você pode chamar ferramentas quando ajudar";
    if (c.webTools) p += " (web_search para pesquisar na internet, open_url para ler uma página)";
    p += ". Depois de usar uma ferramenta, responda normalmente em português.";
    auto list = skills::LoadAll(c.disabledSkills);
    bool any = false;
    for (auto& s : list) {
        if (!s.enabled) continue;
        if (!any) { p += "\nSKILLS instaladas (carregue com use_skill quando o pedido combinar):"; any = true; }
        p += "\n- " + s.name + ": " + s.description;
    }
    return p;
}

void App::AddToolEvent(int msgId, const ToolEvent& ev) {
    std::lock_guard<std::mutex> l(msgMtx_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
        if (it->id == msgId) { it->toolEvents.push_back(ev); break; }
    ++msgVersion_;
}

void App::UpdateLastToolEvent(int msgId, int status, const std::vector<std::pair<std::string, std::string>>& sources) {
    std::lock_guard<std::mutex> l(msgMtx_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
        if (it->id == msgId && !it->toolEvents.empty()) {
            it->toolEvents.back().status = status;
            it->toolEvents.back().sources = sources;
            break;
        }
    ++msgVersion_;
}

std::string App::RunTool(int msgId, const ToolCall& call) {
    json a = json::parse(call.argsJson.empty() ? "{}" : call.argsJson, nullptr, false);
    if (a.is_discarded() || !a.is_object()) a = json::object();
    auto str = [&](const char* k) { return a.contains(k) && a[k].is_string() ? a[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k, int d) {
        if (!a.contains(k)) return d;
        if (a[k].is_number()) return a[k].get<int>();
        if (a[k].is_string()) { int v = d; text::ParseIntPt(a[k].get<std::string>(), &v); return v; }
        return d;
    };

    if (call.name == "web_search") {
        std::string q = str("query");
        if (q.empty()) return "Erro: informe a busca.";
        AddToolEvent(msgId, {"web_search", "Pesquisando na web: " + q, 0, {}});
        std::string err;
        auto res = web::Search(q, 6, &err);
        std::vector<std::pair<std::string, std::string>> src;
        std::string out = "Resultados da pesquisa por \"" + q + "\":\n";
        for (size_t i = 0; i < res.size(); ++i) {
            out += std::to_string(i + 1) + ". " + res[i].title + "\n   " + res[i].url + "\n   " + res[i].snippet + "\n";
            src.push_back({res[i].title, res[i].url});
        }
        // lê o começo das 2 primeiras páginas: os resumos da busca sozinhos costumam ser vagos
        int read = 0;
        for (size_t i = 0; i < res.size() && read < 2; ++i) {
            std::string ferr;
            std::string page = web::FetchText(res[i].url, 2200, nullptr, &ferr);
            if (page.size() < 200) continue;
            out += "\nTrecho da página " + std::to_string(i + 1) + " (" + res[i].title + "):\n" + page + "\n";
            ++read;
        }
        out += "\nHoje é " + text::NowPortuguese() + ". Prefira a informação mais recente e diga de qual site ela veio.";
        UpdateLastToolEvent(msgId, res.empty() ? 2 : 1, src);
        return res.empty() ? "Não encontrei resultados (" + err + ")." : out;
    }
    if (call.name == "open_url") {
        std::string url = str("url");
        std::string host = url;
        size_t s = host.find("://");
        if (s != std::string::npos) host = host.substr(s + 3);
        host = host.substr(0, host.find('/'));
        AddToolEvent(msgId, {"open_url", "Lendo " + host, 0, {}});
        std::string title, err;
        std::string t = web::FetchText(url, 6000, &title, &err);
        UpdateLastToolEvent(msgId, t.empty() ? 2 : 1, {{title.empty() ? host : title, url}});
        return t.empty() ? "Não consegui abrir a página (" + err + ")." : "Página: " + title + "\n\n" + t;
    }
    if (call.name == "use_skill") {
        std::string n = text::Fold(str("name"));
        for (auto& sk : skills::LoadAll(Cfg().disabledSkills)) {
            if (!sk.enabled || text::Fold(sk.name) != n) continue;
            AddToolEvent(msgId, {"use_skill", "Usando a skill " + sk.name, 1, {}});
            return "Instruções da skill " + sk.name + ":\n" + sk.body;
        }
        return "Skill não encontrada.";
    }
    if (call.name == "create_workout") {
        std::string name = str("name");
        int rounds = std::clamp(num("rounds", 3), 1, 30), rs = std::clamp(num("round_seconds", 180), 5, 1800), rest = std::clamp(num("rest_seconds", 60), 0, 900);
        if (name.empty()) name = "Treino";
        std::replace(name.begin(), name.end(), '|', '/');
        Config c = Cfg();
        c.workouts.erase(std::remove_if(c.workouts.begin(), c.workouts.end(),
                                        [&](const std::string& w) { return text::Fold(text::Split(w, '|')[0]) == text::Fold(name); }),
                         c.workouts.end());
        c.workouts.push_back(name + "|" + std::to_string(rounds) + "|" + std::to_string(rs) + "|" + std::to_string(rest));
        c.timerRounds = rounds;
        c.timerRoundSec = rs;
        c.timerRestSec = rest;
        ApplyConfig(c);
        requestTimer = 1;
        AddToolEvent(msgId, {"create_workout", "Treino salvo no timer: " + name, 1, {}});
        return "Treino \"" + name + "\" salvo: " + std::to_string(rounds) + " rounds de " + text::DurationPt(rs) + ", descanso " + text::DurationPt(rest) + ".";
    }
    if (call.name == "start_timer") {
        std::string n = text::Fold(str("workout_name"));
        Config c = Cfg();
        if (!n.empty())
            for (auto& w : c.workouts) {
                auto p = text::Split(w, '|');
                if (p.size() >= 4 && text::Fold(p[0]).find(n) != std::string::npos) {
                    c.timerRounds = atoi(p[1].c_str());
                    c.timerRoundSec = atoi(p[2].c_str());
                    c.timerRestSec = atoi(p[3].c_str());
                    ApplyConfig(c);
                    break;
                }
            }
        requestTimer = 2;
        AddToolEvent(msgId, {"start_timer", "Timer iniciado", 1, {}});
        return "Timer iniciado.";
    }
    return "Ferramenta desconhecida.";
}

bool App::ModelHasTools(const std::string& name) {
    {
        std::lock_guard<std::mutex> l(dataMtx_);
        for (auto& m : models_)
            if (m.name == name && m.capsKnown) return m.tools;
    }
    // ainda não sabemos: pergunta ao Ollama agora (rápido)
    std::vector<ModelInfo> one(1);
    one[0].name = name;
    llm::FillCapabilities(Cfg(), one);
    return one[0].tools;
}

// Para modelos sem suporte a ferramentas: pesquisa antes, quando o pedido claramente precisa da internet.
bool App::NeedsWebSearch(const std::string& q) {
    std::string f = text::Fold(q);
    for (const char* k : {"pesquis", "procura na internet", "procure na internet", "busca na internet", "na web", "no google",
                          "noticia", "cotacao", "previsao do tempo", "placar", "resultado do jogo", "quem ganhou", "hoje"})
        if (f.find(k) != std::string::npos) return true;
    return false;
}
