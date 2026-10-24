#pragma once
#include <string>
#include <vector>

// Skills: arquivos .md com instruções que a IA carrega quando precisa (ferramenta use_skill).
// Formato:
//   ---
//   name: treino-muay-thai
//   description: Monta treinos de Muay Thai e usa o timer de rounds.
//   ---
//   (instruções em texto livre)
namespace skills {

struct Skill {
    std::string name, description, body, path;
    bool enabled = true;
};

std::string Dir();                       // %APPDATA%\KB\skills
void EnsureDefaults();                   // cria as skills que vêm com o KB (só se não existirem)
std::vector<Skill> LoadAll(const std::vector<std::string>& disabled);
std::string NewSkillTemplate();          // cria um arquivo novo e devolve o caminho

} // namespace skills
