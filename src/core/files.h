#pragma once
#include <string>

// Criação de arquivos pedida pelo modelo — sempre com aprovação explícita do usuário,
// mostrando o caminho exato. Só aquele arquivo é escrito.
namespace files {

struct Resolved {
    bool ok = false;
    std::string path;        // caminho absoluto final (o que o usuário aprova)
    std::string error;
    bool exists = false;     // vai sobrescrever
    bool parentMissing = false;
    bool risky = false;      // executável/script
};

Resolved Resolve(const std::string& requested);
bool WriteExact(const std::string& path, const std::string& content, std::string* err);
void RevealInExplorer(const std::string& path);
std::string DesktopDir();
std::string DocumentsDir();

} // namespace files
