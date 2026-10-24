#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace text {
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);
std::string Trim(const std::string& s);
// minúsculas + sem acentos (para comparar palavra de ativação etc.)
std::string Fold(const std::string& utf8);
bool StartsWith(const std::string& s, const std::string& prefix);
std::string FormatBytes(uint64_t bytes);
std::string FormatDuration(double seconds);
// Remove marcações de markdown para a fala.
std::string CleanForSpeech(const std::string& s);
std::vector<std::string> Split(const std::string& s, char sep);
// Data/hora local, por extenso em português.
std::string NowPortuguese();
std::string GreetingForNow(); // "Bom dia" / "Boa tarde" / "Boa noite"
}
