// Comandos locais do KB (respondem na hora, sem passar pela IA):
//  - "criar treino": conversa guiada (nome → rounds → duração → descanso) e salva o treino
//  - "começar treino <nome>", "inicia o timer", "abre o timer"
#include "app.h"
#include "core/text.h"
#include <algorithm>

namespace {
bool Has(const std::string& f, std::initializer_list<const char*> words) {
    for (auto* w : words) if (f.find(w) != std::string::npos) return true;
    return false;
}

struct Workout { std::string name; int rounds = 3, roundSec = 180, restSec = 60; };

Workout Decode(const std::string& s) {
    Workout w;
    auto p = text::Split(s, '|');
    if (p.size() >= 4) { w.name = p[0]; w.rounds = atoi(p[1].c_str()); w.roundSec = atoi(p[2].c_str()); w.restSec = atoi(p[3].c_str()); }
    return w;
}
std::string Encode(const Workout& w) {
    std::string n = w.name;
    std::replace(n.begin(), n.end(), '|', '/');
    return n + "|" + std::to_string(w.rounds) + "|" + std::to_string(w.roundSec) + "|" + std::to_string(w.restSec);
}
} // namespace

void App::Reply(const std::string& msg, bool fromVoice, bool expectAnswer) {
    AddMessage(false, msg, false);
    if (Cfg().speakReplies) Say(msg);
    // pergunta feita por voz? escuta a resposta sozinho quando terminar de falar
    autoListen_ = fromVoice && expectAnswer;
    AssistState s = state_.load();
    if (s == AssistState::Thinking || s == AssistState::Transcribing) SetState(AssistState::Idle);
}

bool App::HandleLocalCommand(const std::string& q, bool fromVoice) {
    std::string f = text::Fold(q);
    std::lock_guard<std::mutex> l(dialogMtx_);

    // ---- conversa de criação de treino em andamento
    if (dlgStep_ > 0) {
        AddMessage(true, q, false, fromVoice);
        if (Has(f, {"cancela", "deixa pra la", "esquece", "para de criar"})) {
            dlgStep_ = 0;
            Reply("Tudo bem, cancelei a criação do treino.", fromVoice, false);
            return true;
        }
        switch (dlgStep_) {
        case 1: {
            std::string name = text::Trim(q);
            while (!name.empty() && (name.back() == '.' || name.back() == '!')) name.pop_back();
            for (const char* pre : {"o nome e ", "o nome é ", "nome ", "chama ", "se chama "})
                if (text::Fold(name).rfind(text::Fold(pre), 0) == 0) name = text::Trim(name.substr(strlen(pre)));
            if (name.empty()) { Reply("Não entendi o nome. Qual é o nome do treino?", fromVoice, true); return true; }
            if (name.size() > 40) name.resize(40);
            dlgName_ = name;
            dlgStep_ = 2;
            Reply("Treino \"" + name + "\". Quantos rounds?", fromVoice, true);
            return true;
        }
        case 2: {
            int n = 0;
            if (!text::ParseIntPt(q, &n) || n < 1 || n > 30) { Reply("Não entendi. Quantos rounds? Pode ser de 1 a 30.", fromVoice, true); return true; }
            dlgRounds_ = n;
            dlgStep_ = 3;
            Reply(std::to_string(n) + (n == 1 ? " round. " : " rounds. ") + "Quanto tempo dura cada round? Pode falar em segundos ou minutos.", fromVoice, true);
            return true;
        }
        case 3: {
            int sec = text::ParseDurationPt(q);
            if (sec < 5 || sec > 1800) { Reply("Não entendi o tempo. Quantos segundos dura cada round? Exemplo: 180 segundos ou 3 minutos.", fromVoice, true); return true; }
            dlgRoundSec_ = sec;
            dlgStep_ = 4;
            Reply("Rounds de " + text::DurationPt(sec) + ". E quanto tempo de descanso entre os rounds?", fromVoice, true);
            return true;
        }
        case 4: {
            int sec = Has(f, {"sem descanso", "nenhum", "zero"}) ? 0 : text::ParseDurationPt(q);
            if (sec < 0 || sec > 900) { Reply("Não entendi. Quanto tempo de descanso? Exemplo: 1 minuto, 30 segundos ou sem descanso.", fromVoice, true); return true; }
            Workout w{dlgName_, dlgRounds_, dlgRoundSec_, sec};
            Config c = Cfg();
            // substitui um treino com o mesmo nome
            c.workouts.erase(std::remove_if(c.workouts.begin(), c.workouts.end(),
                                            [&](const std::string& s) { return text::Fold(Decode(s).name) == text::Fold(w.name); }),
                             c.workouts.end());
            c.workouts.push_back(Encode(w));
            c.timerRounds = w.rounds;
            c.timerRoundSec = w.roundSec;
            c.timerRestSec = w.restSec;
            ApplyConfig(c);
            dlgStep_ = 5;
            requestTimer = 1;
            Reply("Pronto! Treino \"" + w.name + "\" salvo: " + std::to_string(w.rounds) + " rounds de " + text::DurationPt(w.roundSec) +
                  (w.restSec ? " com " + text::DurationPt(w.restSec) + " de descanso." : " sem descanso.") + " Quer começar agora?",
                  fromVoice, true);
            return true;
        }
        case 5:
            dlgStep_ = 0;
            if (Has(f, {"sim", "bora", "vamos", "comeca", "inicia", "pode", "manda", "quero", "claro", "ok"})) {
                requestTimer = 2;
                Reply("Começando. Bom treino!", fromVoice, false);
            } else {
                Reply("Combinado. O treino está salvo no Timer, em Meus treinos.", fromVoice, false);
            }
            return true;
        }
    }

    // ---- iniciar a conversa: "criar treino", "novo treino", "monta um treino"
    if (Has(f, {"treino"}) && Has(f, {"cria", "criar", "novo", "nova", "monta", "montar", "adiciona", "fazer um", "faz um"})) {
        AddMessage(true, q, false, fromVoice);
        dlgStep_ = 1;
        dlgName_.clear();
        Reply("Vamos criar um treino. Qual é o nome dele?", fromVoice, true);
        return true;
    }

    // ---- começar um treino salvo pelo nome: "começar treino de saco", "inicia o treino sparring"
    if (Has(f, {"treino"}) && Has(f, {"comeca", "comecar", "inicia", "iniciar", "bora", "roda", "rodar", "faz o", "vamos"})) {
        Config c = Cfg();
        for (auto& s : c.workouts) {
            Workout w = Decode(s);
            if (!w.name.empty() && f.find(text::Fold(w.name)) != std::string::npos) {
                AddMessage(true, q, false, fromVoice);
                c.timerRounds = w.rounds;
                c.timerRoundSec = w.roundSec;
                c.timerRestSec = w.restSec;
                ApplyConfig(c);
                requestTimer = 2;
                Reply("Começando o treino \"" + w.name + "\": " + std::to_string(w.rounds) + " rounds de " + text::DurationPt(w.roundSec) + ".", fromVoice, false);
                return true;
            }
        }
    }

    // ---- abrir / iniciar o timer
    bool timerWord = f.find("timer") != std::string::npos || f.find("cronometro") != std::string::npos;
    if (timerWord && f.size() < 80) {
        AddMessage(true, q, false, fromVoice);
        bool start = Has(f, {"inici", "comec", "liga", "start", "bora"});
        requestTimer = start ? 2 : 1;
        Reply(start ? "Timer iniciado. Bom treino!" : "Abri o timer de rounds. É só apertar Iniciar quando quiser.", fromVoice, false);
        return true;
    }
    return false;
}
