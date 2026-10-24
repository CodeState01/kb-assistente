#include "media/image.h"
#include "core/text.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_WINDOWS_UTF8
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"

namespace img {

namespace {
// Reduz por média de área (boa qualidade para reduções grandes).
std::vector<uint8_t> Downscale(const uint8_t* src, int sw, int sh, int stride, bool bgra, int dw, int dh) {
    std::vector<uint8_t> out((size_t)dw * dh * 4);
    double fx = (double)sw / dw, fy = (double)sh / dh;
    for (int y = 0; y < dh; ++y) {
        int y0 = (int)(y * fy), y1 = std::max(y0 + 1, std::min(sh, (int)((y + 1) * fy)));
        for (int x = 0; x < dw; ++x) {
            int x0 = (int)(x * fx), x1 = std::max(x0 + 1, std::min(sw, (int)((x + 1) * fx)));
            unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* p = src + (size_t)yy * stride + (size_t)x0 * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4) {
                    r += p[0]; g += p[1]; b += p[2]; a += p[3]; ++n;
                }
            }
            uint8_t* d = &out[((size_t)y * dw + x) * 4];
            uint8_t c0 = (uint8_t)(r / n), c2 = (uint8_t)(b / n);
            d[0] = bgra ? c2 : c0;
            d[1] = (uint8_t)(g / n);
            d[2] = bgra ? c0 : c2;
            d[3] = 255;
            (void)a;
        }
    }
    return out;
}

void FitSize(int w, int h, int maxSide, int& ow, int& oh) {
    if (w <= maxSide && h <= maxSide) { ow = w; oh = h; return; }
    double s = (double)maxSide / std::max(w, h);
    ow = std::max(1, (int)(w * s));
    oh = std::max(1, (int)(h * s));
}

void JpegSink(void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx;
    v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
}
} // namespace

PicturePtr FromPixels(const uint8_t* px, int w, int h, int stride, bool bgra, const std::string& label, int maxSide) {
    if (!px || w <= 0 || h <= 0) return nullptr;
    auto pic = std::make_shared<Picture>();
    pic->label = label;
    int mw, mh;
    FitSize(w, h, maxSide, mw, mh);
    std::vector<uint8_t> model = Downscale(px, w, h, stride, bgra, mw, mh);
    std::vector<uint8_t> jpg;
    // JPEG sem alpha: converte para RGB
    std::vector<uint8_t> rgb((size_t)mw * mh * 3);
    for (size_t i = 0, n = (size_t)mw * mh; i < n; ++i) {
        rgb[i * 3] = model[i * 4];
        rgb[i * 3 + 1] = model[i * 4 + 1];
        rgb[i * 3 + 2] = model[i * 4 + 2];
    }
    stbi_write_jpg_to_func(JpegSink, &jpg, mw, mh, 3, rgb.data(), 85);
    pic->jpegB64 = Base64(jpg.data(), jpg.size());
    int tw, th;
    FitSize(mw, mh, 480, tw, th);
    pic->rgba = Downscale(model.data(), mw, mh, mw * 4, false, tw, th);
    pic->w = tw;
    pic->h = th;
    return pic;
}

PicturePtr FromMemory(const uint8_t* data, size_t len, const std::string& label, int maxSide) {
    int w, h, n;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
    if (!px) return nullptr;
    PicturePtr p = FromPixels(px, w, h, w * 4, false, label, maxSide);
    stbi_image_free(px);
    return p;
}

PicturePtr FromFile(const std::string& path, int maxSide) {
    int w, h, n;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4); // STBI_WINDOWS_UTF8: caminho UTF-8
    if (!px) return nullptr;
    std::string name = path.substr(path.find_last_of("\\/") + 1);
    PicturePtr p = FromPixels(px, w, h, w * 4, false, name, maxSide);
    stbi_image_free(px);
    return p;
}

PicturePtr FromClipboard(int maxSide) {
    if (!IsClipboardFormatAvailable(CF_DIB) || !OpenClipboard(nullptr)) return nullptr;
    PicturePtr result;
    HANDLE hData = GetClipboardData(CF_DIB);
    if (hData) {
        auto* bi = (BITMAPINFOHEADER*)GlobalLock(hData);
        if (bi && (bi->biBitCount == 32 || bi->biBitCount == 24) && bi->biCompression != BI_JPEG && bi->biCompression != BI_PNG) {
            int w = bi->biWidth, h = std::abs(bi->biHeight);
            bool bottomUp = bi->biHeight > 0;
            int bpp = bi->biBitCount / 8;
            int srcStride = ((w * bi->biBitCount + 31) / 32) * 4;
            const uint8_t* bits = (const uint8_t*)bi + bi->biSize + (bi->biCompression == BI_BITFIELDS ? 12 : 0) + bi->biClrUsed * 4;
            std::vector<uint8_t> bgra((size_t)w * h * 4);
            for (int y = 0; y < h; ++y) {
                const uint8_t* s = bits + (size_t)(bottomUp ? h - 1 - y : y) * srcStride;
                uint8_t* d = &bgra[(size_t)y * w * 4];
                for (int x = 0; x < w; ++x) {
                    d[x * 4] = s[x * bpp];
                    d[x * 4 + 1] = s[x * bpp + 1];
                    d[x * 4 + 2] = s[x * bpp + 2];
                    d[x * 4 + 3] = 255;
                }
            }
            result = FromPixels(bgra.data(), w, h, w * 4, true, "Área de transferência", maxSide);
        }
        if (bi) GlobalUnlock(hData);
    }
    CloseClipboard();
    return result;
}

std::string Base64(const uint8_t* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = data[i] << 16;
        if (i + 1 < len) v |= data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += i + 1 < len ? tbl[(v >> 6) & 63] : '=';
        out += i + 2 < len ? tbl[v & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> Base64Decode(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+' || c == '-') d = 62;
        else if (c == '/' || c == '_') d = 63;
        else continue;
        v = (v << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((v >> bits) & 0xFF));
        }
    }
    return out;
}

bool IsImageFile(const std::string& path) {
    std::string f = text::Fold(path);
    for (const char* e : {".png", ".jpg", ".jpeg", ".bmp", ".gif"})
        if (f.size() > strlen(e) && f.compare(f.size() - strlen(e), strlen(e), e) == 0) return true;
    return false;
}

} // namespace img
