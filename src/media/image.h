#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Imagem anexada a uma mensagem: miniatura RGBA para a interface + JPEG em base64 para o modelo.
struct Picture {
    int w = 0, h = 0;                 // miniatura
    std::vector<uint8_t> rgba;
    std::string jpegB64;              // enviado ao modelo de visão
    std::string label;                // "foto.png", "Tela", "Área de transferência"...
    unsigned int tex = 0;             // textura OpenGL (criada pela UI)
};
using PicturePtr = std::shared_ptr<Picture>;

namespace img {
// maxSide: maior lado da imagem enviada ao modelo.
PicturePtr FromFile(const std::string& path, int maxSide = 1344);
PicturePtr FromMemory(const uint8_t* data, size_t len, const std::string& label, int maxSide = 1344);
PicturePtr FromPixels(const uint8_t* px, int w, int h, int stride, bool bgra, const std::string& label, int maxSide = 1344);
PicturePtr FromClipboard(int maxSide = 1344);
std::string Base64(const uint8_t* data, size_t len);
std::vector<uint8_t> Base64Decode(const std::string& s);
bool IsImageFile(const std::string& path);
}
