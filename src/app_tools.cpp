// Ferramentas que a IA pode usar: pesquisa na web, leitura de páginas, skills, treinos e timer.
#include "app.h"
#include "core/skills.h"
#include "core/text.h"
#include "net/web.h"
#include "features/store.h"
#include "features/agenda.h"
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
        tools.push_back(fn("read_page", "Lê o texto de uma página da web (use depois de web_search para ler um resultado). Não abre nada na tela.",
                           {{"url", {{"type", "string"}, {"description", "endereço completo https://..."}}}}, {"url"}));
    }
    auto S = [](const char* d) { return json{{"type", "string"}, {"description", d}}; };
    auto I = [](const char* d) { return json{{"type", "integer"}, {"description", d}}; };
    // ---- computador
    tools.push_back(fn("open_website", "Abre um site no navegador da pessoa (YouTube, Gmail, Instagram...). Para trocar a conta do Gmail use https://accounts.google.com/AccountChooser?continue=https://mail.google.com/",
                       {{"url", S("endereço, ex.: youtube.com ou https://www.youtube.com/results?search_query=muay+thai")}}, {"url"}));
    tools.push_back(fn("open_app", "Abre um programa instalado no PC (Chrome, Spotify, WhatsApp, Bloco de Notas, Calculadora...).", {{"name", S("nome do programa")}}, {"name"}));
    tools.push_back(fn("read_file", "Lê um arquivo de texto do PC para você ver ou editar (para editar, depois escreva o arquivo inteiro num bloco ```arquivo).",
                       {{"path", S("caminho, ex.: Área de Trabalho\\notas.txt")}}, {"path"}));
    tools.push_back(fn("list_folder", "Lista os arquivos de uma pasta do PC.", {{"path", S("pasta, ex.: Documentos")}}, {"path"}));
    if (c.computerControl) {
        tools.push_back(fn("look_at_screen", "Mostra a janela ativa e a lista numerada dos botões, links e campos que dá para clicar.", json::object(), {}));
        tools.push_back(fn("click", "Clica (com o mouse) num controle da lista do look_at_screen.",
                           {{"element", I("número do controle")}, {"double", json{{"type", "boolean"}}}}, {"element"}));
        tools.push_back(fn("click_xy", "Clica numa posição da imagem da tela (só se o controle não aparecer na lista).", {{"x", I("x na imagem")}, {"y", I("y na imagem")}}, {"x", "y"}));
        tools.push_back(fn("type_text", "Digita um texto (opcionalmente clicando antes num campo da lista e apertando Enter no fim).",
                           {{"text", S("o que digitar")}, {"element", I("campo onde digitar (opcional)")}, {"enter", json{{"type", "boolean"}}}}, {"text"}));
        tools.push_back(fn("press_keys", "Aperta teclas/atalhos: enter, tab, esc, ctrl+l, ctrl+t, alt+tab, ctrl+w, f5, pagedown...", {{"keys", S("atalho")}}, {"keys"}));
        tools.push_back(fn("scroll", "Rola a janela ativa.", {{"direction", S("up ou down")}, {"amount", I("quanto (1-20)")}}, {"direction"}));
        tools.push_back(fn("wait", "Espera a página/programa carregar.", {{"seconds", I("segundos (1-15)")}}, {"seconds"}));
    }
    // ---- loja
    tools.push_back(fn("store_search", "Procura produtos na loja do KB (luvas, bandagens, caneleiras, shorts, camisetas, tops da KB...) com preço e tamanhos.",
                       {{"query", S("o que procurar, ex.: luva 14 oz")}}, {"query"}));
    tools.push_back(fn("show_product", "Abre a loja do KB mostrando um produto (use o id entre colchetes do store_search).", {{"id", S("id do produto")}}, {"id"}));
    tools.push_back(fn("open_store", "Abre a janela da loja do KB.", json::object(), {}));
    // ---- agenda
    tools.push_back(fn("agenda_add", "Adiciona um item à rotina semanal da treinadora (aula, compromisso ou horário livre).",
                       {{"title", S("ex.: Muay Thai adulto")}, {"days", S("dias, ex.: segunda a sexta / seg, qua e sex / sábado")},
                        {"start", S("início HH:MM")}, {"end", S("fim HH:MM")}, {"type", S("aula (aceita experimental), ocupado ou livre")},
                        {"trial_spots", I("vagas de experimental por aula (padrão 2)")}},
                       {"title", "days", "start", "end"}));
    tools.push_back(fn("agenda_list", "Mostra a rotina semanal cadastrada.", json::object(), {}));
    tools.push_back(fn("agenda_remove", "Remove um item da rotina (use o id entre colchetes do agenda_list).", {{"id", S("id")}}, {"id"}));
    tools.push_back(fn("agenda_free_slots", "Lista os próximos horários livres para aula experimental.", json::object(), {}));
    tools.push_back(fn("agenda_book_trial", "Pede uma aula experimental para alguém (a treinadora confirma no notebook).",
                       {{"name", S("nome do aluno")}, {"phone", S("WhatsApp com DDD")}, {"slot_key", S("código do horário, ex.: 2026-10-27 19:00")}},
                       {"name", "slot_key"}));
    tools.push_back(fn("open_agenda", "Abre a janela da agenda.", json::object(), {}));
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
    if (c.webTools) p += " (web_search para pesquisar na internet, read_page para ler uma página)";
    p += ". Depois de usar uma ferramenta, responda normalmente em português.";
    p += "\nCOMPUTADOR: para abrir sites use open_website (ex.: \"abre o YouTube\"), para programas open_app.";
    if (c.computerControl)
        p += " Para tarefas na tela (clicar, preencher, procurar no YouTube, trocar conta do Gmail): look_at_screen, depois click/type_text/press_keys/scroll."
             " Cada ação já devolve a tela atualizada. Vá passo a passo e confirme o resultado. NUNCA digite senhas, dados de cartão ou códigos de"
             " verificação: peça para a pessoa digitar. Antes de enviar, comprar, publicar ou apagar algo, pergunte à pessoa.";
    p += "\nLOJA: store_search acha produtos (luvas, bandagens, caneleiras e os produtos da KB); mostre com show_product. Produtos de marcas"
         " parceiras são comprados na loja original (o KB abre o carrinho); os da KB pelo WhatsApp com cupom.";
    p += "\nAGENDA: a treinadora fala a rotina e você salva com agenda_add (um item por aula/compromisso). Para aula experimental, use"
         " agenda_free_slots e agenda_book_trial; a aula só fica marcada depois que ela confirmar no notebook.";
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

std::string App::RunTool(int msgId, const ToolCall& call, std::string* imageB64) {
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
    static const char* kComputer[] = {"open_website", "open_app", "read_file", "list_folder", "look_at_screen", "click", "double_click",
                                      "right_click", "click_xy", "type_text", "press_keys", "scroll", "wait"};
    for (auto k : kComputer)
        if (call.name == k) return RunComputerTool(msgId, call, imageB64);
    if (call.name == "store_search") {
        std::string q = str("query");
        AddToolEvent(msgId, {"store", "Procurando na loja: " + q, 1, {}});
        return store::SearchText(q);
    }
    if (call.name == "show_product" || call.name == "open_store") {
        std::string id = str("id");
        PostUi(id.empty() ? "shop" : "shop:" + id);
        AddToolEvent(msgId, {"store", "Abrindo a loja", 1, {}});
        return "A loja foi aberta na tela" + std::string(id.empty() ? "." : " no produto.");
    }
    if (call.name == "open_agenda") {
        PostUi("agenda");
        AddToolEvent(msgId, {"agenda", "Abrindo a agenda", 1, {}});
        return "Agenda aberta na tela.";
    }
    if (call.name == "agenda_list") {
        AddToolEvent(msgId, {"agenda", "Lendo a rotina", 1, {}});
        return agenda::RoutineText(agenda::Load());
    }
    if (call.name == "agenda_free_slots") {
        AddToolEvent(msgId, {"agenda", "Vendo horários livres", 1, {}});
        return agenda::SlotsText(agenda::Load());
    }
    if (call.name == "agenda_add") {
        agenda::Item it;
        it.id = agenda::NewId("r");
        it.title = str("title").empty() ? "Aula" : str("title");
        int mask = agenda::ParseDays(str("days"));
        for (int d = 0; d < 7; ++d) if (mask & (1 << d)) it.days.push_back(d);
        it.start = agenda::NormTime(str("start"));
        it.end = agenda::NormTime(str("end"));
        if (it.days.empty() || it.start.empty()) return "Não entendi os dias ou o horário. Pergunte de novo à pessoa.";
        if (it.end.empty() || it.end <= it.start) {
            int h = atoi(it.start.c_str()) + 1;
            char b[8];
            snprintf(b, sizeof(b), "%02d:%s", std::min(h, 23), it.start.substr(3).c_str());
            it.end = b;
        }
        std::string ty = text::Fold(str("type"));
        it.type = ty.find("ocup") != std::string::npos || ty.find("compromisso") != std::string::npos ? agenda::Busy
                : ty.find("livre") != std::string::npos ? agenda::Free : agenda::Class;
        it.trialSpots = std::clamp(num("trial_spots", 2), 0, 20);
        agenda::Data d = agenda::Load();
        d.routine.push_back(it);
        agenda::Save(d);
        AddToolEvent(msgId, {"agenda", "Rotina: " + it.title + " · " + agenda::DaysText(it.days) + " " + it.start + "–" + it.end, 1, {}});
        return "Salvo na rotina: " + it.title + ", " + agenda::DaysText(it.days) + ", " + it.start + " às " + it.end + ".";
    }
    if (call.name == "agenda_remove") {
        agenda::Data d = agenda::Load();
        std::string id = str("id");
        size_t before = d.routine.size();
        d.routine.erase(std::remove_if(d.routine.begin(), d.routine.end(), [&](const agenda::Item& x) { return x.id == id; }), d.routine.end());
        agenda::Save(d);
        AddToolEvent(msgId, {"agenda", "Removendo da rotina", d.routine.size() < before ? 1 : 2, {}});
        return d.routine.size() < before ? "Removido." : "Não achei esse item.";
    }
    if (call.name == "agenda_book_trial") {
        std::string err;
        std::string id = RequestTrial(str("name"), str("phone"), str("slot_key"), "kb", &err);
        AddToolEvent(msgId, {"agenda", id.empty() ? "Horário indisponível" : "Pedido de aula enviado para confirmação", id.empty() ? 2 : 1, {}});
        return id.empty() ? "Não deu: " + err : "Pedido criado; aparece um cartão no notebook para a treinadora confirmar.";
    }
    if (call.name == "read_page" || call.name == "open_url") {
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
