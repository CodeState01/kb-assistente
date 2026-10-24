#include "ui/assets.h"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "stb_image.h"

namespace assets {
namespace {
GLuint g_bg = 0, g_logo = 0;
int g_bgW = 0, g_bgH = 0;
std::vector<ImVec2> g_shape;
float g_shapeRatio = 0.5f; // meia-largura das letras / meia-largura da logo

bool LoadRes(const wchar_t* name, const uint8_t** data, DWORD* size) {
    HRSRC r = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10));
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    *size = SizeofResource(nullptr, r);
    *data = g ? (const uint8_t*)LockResource(g) : nullptr;
    return *data != nullptr;
}

GLuint Upload(const unsigned char* px, int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return t;
}

// Amostra os pixels azuis das letras da logo para formar o "KB" com partículas.
void BuildShape(const unsigned char* px, int w, int h) {
    float cx = w * 0.5f, cy = h * 0.5f, rmax = std::min(w, h) * 0.36f;
    std::vector<unsigned char> mask((size_t)w * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = px + ((size_t)y * w + x) * 4;
            int r = p[0], g = p[1], b = p[2], a = p[3];
            float dx = x - cx, dy = y - cy;
            if (a >= 200 && b >= 150 && b >= r + 70 && b >= g + 15 && dx * dx + dy * dy <= rmax * rmax) mask[(size_t)y * w + x] = 1;
        }
    auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && mask[(size_t)y * w + x]; };
    // contorno das letras (fica legível) + um pouco do preenchimento
    std::vector<ImVec2> edge, fill;
    const int d = std::max(2, w / 128);
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2) {
            if (!at(x, y)) continue;
            bool border = !at(x - d, y) || !at(x + d, y) || !at(x, y - d) || !at(x, y + d);
            (border ? edge : fill).push_back(ImVec2((float)x, (float)y));
        }
    uint32_t sseed = 7654321;
    auto shuffle = [&](std::vector<ImVec2>& v) {
        for (size_t i = v.size() > 0 ? v.size() - 1 : 0; i > 0; --i) {
            sseed = sseed * 1664525u + 1013904223u;
            std::swap(v[i], v[sseed % (i + 1)]);
        }
    };
    shuffle(edge);
    shuffle(fill);
    std::vector<ImVec2> cand(edge.begin(), edge.begin() + std::min<size_t>(edge.size(), 900));
    cand.insert(cand.end(), fill.begin(), fill.begin() + std::min<size_t>(fill.size(), 300));
    if (cand.size() < 50) return;
    ImVec2 mn(1e9f, 1e9f), mx(-1e9f, -1e9f);
    for (auto& c : cand) { mn.x = std::min(mn.x, c.x); mn.y = std::min(mn.y, c.y); mx.x = std::max(mx.x, c.x); mx.y = std::max(mx.y, c.y); }
    float sx = (mx.x - mn.x) * 0.5f, sy = (mx.y - mn.y) * 0.5f, s = std::max(sx, sy);
    g_shapeRatio = s / (std::min(w, h) * 0.5f);
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    // embaralha de forma determinística e pega até 900 pontos
    uint32_t seed = 1234567;
    for (size_t i = cand.size() - 1; i > 0; --i) {
        seed = seed * 1664525u + 1013904223u;
        std::swap(cand[i], cand[seed % (i + 1)]);
    }
    size_t n = std::min<size_t>(cand.size(), 1200);
    g_shape.clear();
    for (size_t i = 0; i < n; ++i) g_shape.push_back(ImVec2((cand[i].x - c.x) / s, (cand[i].y - c.y) / s));
}
} // namespace

void Load() {
    const uint8_t* d;
    DWORD sz;
    int w, h, n;
    if (LoadRes(L"BG_JPG", &d, &sz)) {
        if (unsigned char* px = stbi_load_from_memory(d, (int)sz, &w, &h, &n, 4)) {
            g_bg = Upload(px, w, h);
            g_bgW = w;
            g_bgH = h;
            stbi_image_free(px);
        }
    }
    if (LoadRes(L"LOGO_PNG", &d, &sz)) {
        if (unsigned char* px = stbi_load_from_memory(d, (int)sz, &w, &h, &n, 4)) {
            g_logo = Upload(px, w, h);
            BuildShape(px, w, h);
            stbi_image_free(px);
        }
    }
}

ImTextureID Background(int* w, int* h) {
    if (w) *w = g_bgW;
    if (h) *h = g_bgH;
    return (ImTextureID)(intptr_t)g_bg;
}
ImTextureID Logo() { return (ImTextureID)(intptr_t)g_logo; }
const std::vector<ImVec2>& KbShape() { return g_shape; }
float KbShapeLogoRatio() { return g_shapeRatio; }

} // namespace assets
