#include "core/skills.h"
#include "core/config.h"
#include "core/platform.h"
#include "core/text.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace skills {

std::string Dir() {
    std::string d = config::AppDataDir() + "\\skills";
    CreateDirectoryW(text::Widen(d).c_str(), nullptr);
    return d;
}

static void WriteIfMissing(const std::string& file, const std::string& content) {
    std::string path = Dir() + "\\" + file;
    if (platform::FileExists(path)) return;
    std::ofstream f(text::Widen(path), std::ios::binary);
    f << content;
}

void EnsureDefaults() {
    WriteIfMissing("pesquisa-web.md",
        "---\nname: pesquisa-web\ndescription: Como pesquisar na internet com qualidade e citar as fontes.\n---\n"
        "1. Use web_search com uma busca curta e objetiva (em português, ou em inglês para assuntos técnicos).\n"
        "2. Se os resumos não bastarem, use open_url em 1 ou 2 resultados confiáveis para ler o conteúdo.\n"
        "3. Responda de forma direta e cite as fontes pelo nome do site (ex.: \"segundo o g1\").\n"
        "4. Se as fontes discordarem, diga isso. Para datas, preços e placares, confira a data da página.\n");
    WriteIfMissing("treino-muay-thai.md",
        "---\nname: treino-muay-thai\ndescription: Monta treinos de Muay Thai/boxe e cria rounds no timer.\n---\n"
        "Você é um treinador de Muay Thai. Ao montar um treino:\n"
        "- Pergunte o nível (iniciante, intermediário, avançado) e o tempo disponível, se não souber.\n"
        "- Estrutura sugerida: aquecimento (pular corda), sombra, saco/manopla, clinch, condicionamento, alongamento.\n"
        "- Rounds típicos: amador 3x2min com 1min de descanso; profissional 5x3min com 2min; saco 6x3min com 1min.\n"
        "- Mostre o plano num bloco ```passos e, se a pessoa quiser, crie o treino no timer com a ferramenta create_workout.\n"
        "- Para começar na hora, use start_timer.\n");
    WriteIfMissing("resumo.md",
        "---\nname: resumo\ndescription: Resume textos, páginas ou conversas em tópicos curtos.\n---\n"
        "Faça um resumo em até 5 tópicos curtos dentro de um bloco ```card (primeira linha = título).\n"
        "Depois diga em uma frase falada qual é a ideia principal. Não invente o que não está no texto.\n");
}

std::vector<Skill> LoadAll(const std::vector<std::string>& disabled) {
    std::vector<Skill> out;
    WIN32_FIND_DATAW fd;
    std::wstring dir = text::Widen(Dir());
    HANDLE h = FindFirstFileW((dir + L"\\*.md").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        Skill s;
        s.path = text::Narrow(dir + L"\\" + fd.cFileName);
        std::ifstream f(text::Widen(s.path), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string all = ss.str();
        if (all.size() >= 3 && (unsigned char)all[0] == 0xEF) all.erase(0, 3); // BOM
        std::string body = all;
        if (text::StartsWith(text::Trim(all), "---")) {
            size_t a = all.find("---");
            size_t b = all.find("\n---", a + 3);
            if (b != std::string::npos) {
                for (auto& line : text::Split(all.substr(a + 3, b - a - 3), '\n')) {
                    std::string l = text::Trim(line);
                    size_t c = l.find(':');
                    if (c == std::string::npos) continue;
                    std::string k = text::Trim(l.substr(0, c)), v = text::Trim(l.substr(c + 1));
                    if (k == "name") s.name = v;
                    else if (k == "description") s.description = v;
                }
                size_t nl = all.find('\n', b + 4);
                body = nl == std::string::npos ? "" : all.substr(nl + 1);
            }
        }
        if (s.name.empty()) {
            std::wstring fn = fd.cFileName;
            s.name = text::Narrow(fn.substr(0, fn.size() - 3));
        }
        s.body = text::Trim(body);
        s.enabled = std::find(disabled.begin(), disabled.end(), s.name) == disabled.end();
        out.push_back(std::move(s));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const Skill& a, const Skill& b) { return a.name < b.name; });
    return out;
}

std::string NewSkillTemplate() {
    std::string path;
    for (int i = 1; i < 100; ++i) {
        path = Dir() + "\\minha-skill-" + std::to_string(i) + ".md";
        if (!platform::FileExists(path)) break;
    }
    std::ofstream f(text::Widen(path), std::ios::binary);
    std::string name = path.substr(path.find_last_of('\\') + 1);
    name = name.substr(0, name.size() - 3);
    f << "---\nname: " << name << "\ndescription: Diga aqui, numa frase, quando a IA deve usar esta skill.\n---\n"
      << "Escreva aqui as instruções para a IA (passo a passo, tom, formato da resposta...).\n";
    return path;
}

} // namespace skills
