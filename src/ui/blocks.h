#pragma once
#include <string>
#include <vector>

// Conteúdo de uma resposta, dividido em blocos. Blocos "embed" viram cartões visuais
// que podem ser destacados em janelas flutuantes.
enum class BlockType {
    Paragraph,
    Heading,
    Bullet,
    Numbered,
    Quote,
    Code,     // ```linguagem
    Card,     // ```card     (1ª linha = título)
    Steps,    // ```passos   (uma etapa por linha)
    Warning,  // ```aviso
    Tip,      // ```dica
    Table,    // ```tabela   (colunas separadas por |)  ou tabela markdown
    File,     // ```arquivo <caminho>  (pedido de criação de arquivo; exige aprovação)
};

struct Block {
    BlockType type = BlockType::Paragraph;
    std::string text;                           // parágrafo / corpo
    std::string title;                          // cartão / linguagem do código
    std::vector<std::string> items;             // passos
    std::vector<std::vector<std::string>> rows; // tabela (linha 0 = cabeçalho)
    int number = 0;                             // lista numerada
    bool complete = true;                       // false enquanto o bloco ainda está chegando
};

inline bool IsEmbed(BlockType t) {
    return t == BlockType::Code || t == BlockType::Card || t == BlockType::Steps || t == BlockType::Warning ||
           t == BlockType::Tip || t == BlockType::Table || t == BlockType::File;
}

std::vector<Block> ParseBlocks(const std::string& markdown);
std::string StripInline(const std::string& s); // remove **, `, etc. para exibição simples
