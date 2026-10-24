#pragma once
#include <string>
#include <vector>

// Pesquisa na internet (DuckDuckGo, sem chave de API) e leitura de páginas como texto.
namespace web {

struct Result {
    std::string title, url, snippet;
};

std::vector<Result> Search(const std::string& query, int maxResults = 6, std::string* err = nullptr);
// Baixa a página e devolve só o texto legível (sem scripts/menus), limitado a maxChars.
std::string FetchText(const std::string& url, size_t maxChars, std::string* title = nullptr, std::string* err = nullptr);
std::string UrlEncode(const std::string& s);
std::string HtmlToText(const std::string& html);

} // namespace web
