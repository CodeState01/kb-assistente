#include "features/store.h"
#include "core/config.h"
#include "core/platform.h"
#include "core/text.h"
#include "net/http.h"
#include "net/web.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

using nlohmann::json;

namespace store {
namespace {

std::string Dir() {
    std::string d = config::UserDataDir() + "\\loja";
    CreateDirectoryW(text::Widen(d).c_str(), nullptr);
    CreateDirectoryW(text::Widen(d + "\\img").c_str(), nullptr);
    return d;
}

std::string ReadAll(const std::string& path) {
    std::ifstream f(text::Widen(path), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string Resource(const wchar_t* name) {
    HRSRC r = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!r) return {};
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return {};
    return std::string((const char*)LockResource(g), SizeofResource(nullptr, r));
}

json ToJson(const Product& p) {
    json j = {{"id", p.id}, {"name", p.name}, {"brand", p.brand}, {"category", p.category}, {"description", p.description},
              {"url", p.url}, {"image", p.image}, {"cart", p.cart}, {"price", p.price}, {"sizes", p.sizes}};
    return j;
}

Product FromJson(const json& j, bool own) {
    Product p;
    p.own = own;
    p.id = j.value("id", "");
    p.name = j.value("name", "");
    p.brand = j.value("brand", own ? "KB" : "");
    p.category = j.value("category", "Outros");
    p.description = j.value("description", "");
    p.url = j.value("url", "");
    p.image = j.value("image", "");
    p.cart = j.value("cart", "");
    p.price = j.value("price", 0.0);
    if (j.contains("sizes") && j["sizes"].is_array())
        for (auto& s : j["sizes"]) if (s.is_string()) p.sizes.push_back(s.get<std::string>());
    if (j.contains("variants") && j["variants"].is_array())
        for (auto& v : j["variants"]) p.variants.push_back({v.value("name", ""), v.value("cart", ""), v.value("available", true)});
    return p;
}

// ---- imagens: fila de downloads + cache em memória
struct ImgCache {
    std::mutex m;
    std::condition_variable cv;
    std::map<std::string, PicturePtr> ready;
    std::map<std::string, int> state; // 1 na fila, 2 pronto, 3 falhou
    std::deque<std::string> queue;
    bool started = false;
} g_img;

std::string CacheFile(const std::string& url) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : url) h = (h ^ c) * 1099511628211ull;
    char b[32];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    std::string ext = url.find(".png") != std::string::npos ? ".png" : ".jpg";
    return Dir() + "\\img\\" + b + ext;
}

void Worker() {
    for (;;) {
        std::string key;
        {
            std::unique_lock<std::mutex> l(g_img.m);
            g_img.cv.wait(l, [] { return !g_img.queue.empty(); });
            key = g_img.queue.front();
            g_img.queue.pop_front();
        }
        std::string file = key;
        bool remote = text::StartsWith(key, "http");
        if (remote) {
            file = CacheFile(key);
            if (!platform::FileExists(file) || platform::FileSize(file) < 200) {
                auto r = http::Download(key, file, nullptr, nullptr);
                if (!r.ok()) DeleteFileW(text::Widen(file).c_str());
            }
        }
        PicturePtr pic = platform::FileExists(file) ? img::FromFile(file, 640) : nullptr;
        std::lock_guard<std::mutex> l(g_img.m);
        g_img.state[key] = pic ? 2 : 3;
        if (pic) g_img.ready[key] = pic;
    }
}
} // namespace

std::string NewId() {
    static int n = 0;
    char b[32];
    snprintf(b, sizeof(b), "kb%llx%d", (unsigned long long)GetTickCount64(), ++n);
    return b;
}

Data Load() {
    Data d;
    json j = json::parse(ReadAll(Dir() + "\\loja.json"), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        // primeira vez: um cupom de exemplo para a treinadora editar
        d.coupons.push_back({"KB10", 10, true, "10% nos produtos da KB"});
        return d;
    }
    d.storeName = j.value("storeName", d.storeName);
    d.whatsapp = j.value("whatsapp", "");
    if (j.contains("products"))
        for (auto& p : j["products"]) d.own.push_back(FromJson(p, true));
    if (j.contains("coupons"))
        for (auto& c : j["coupons"]) d.coupons.push_back({c.value("code", ""), c.value("percent", 10), c.value("active", true), c.value("note", "")});
    return d;
}

void Save(const Data& d) {
    json j = {{"storeName", d.storeName}, {"whatsapp", d.whatsapp}, {"products", json::array()}, {"coupons", json::array()}};
    for (auto& p : d.own) j["products"].push_back(ToJson(p));
    for (auto& c : d.coupons) j["coupons"].push_back({{"code", c.code}, {"percent", c.percent}, {"active", c.active}, {"note", c.note}});
    std::string path = Dir() + "\\loja.json";
    std::ofstream f(text::Widen(path + ".tmp"), std::ios::binary);
    f << j.dump(1);
    f.close();
    MoveFileExW(text::Widen(path + ".tmp").c_str(), text::Widen(path).c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::vector<Product> Partner() {
    static std::vector<Product> cache;
    static bool loaded = false;
    if (loaded) return cache;
    loaded = true;
    json j = json::parse(Resource(L"STORE_JSON"), nullptr, false);
    if (j.is_discarded() || !j.contains("products")) return cache;
    for (auto& p : j["products"]) cache.push_back(FromJson(p, false));
    return cache;
}

std::vector<std::string> Categories(const std::vector<Product>& all) {
    std::vector<std::string> out;
    for (auto& p : all)
        if (std::find(out.begin(), out.end(), p.category) == out.end()) out.push_back(p.category);
    return out;
}

const Coupon* FindCoupon(const Data& d, const std::string& code) {
    std::string c = text::Fold(text::Trim(code));
    if (c.empty()) return nullptr;
    for (auto& k : d.coupons)
        if (k.active && text::Fold(k.code) == c) return &k;
    return nullptr;
}

double ApplyCoupon(double price, const Coupon* c) {
    if (!c) return price;
    return std::max(0.0, price * (100 - std::clamp(c->percent, 0, 100)) / 100.0);
}

std::string Money(double v) {
    long long cents = (long long)(v * 100 + 0.5);
    long long reais = cents / 100;
    std::string r = std::to_string(reais), g;
    for (size_t i = 0; i < r.size(); ++i) {
        if (i && (r.size() - i) % 3 == 0) g += '.';
        g += r[i];
    }
    char b[8];
    snprintf(b, sizeof(b), ",%02lld", cents % 100);
    return "R$ " + g + b;
}

PicturePtr Image(const std::string& key) {
    if (key.empty()) return nullptr;
    std::lock_guard<std::mutex> l(g_img.m);
    auto it = g_img.ready.find(key);
    if (it != g_img.ready.end()) return it->second;
    if (g_img.state[key] == 0) {
        g_img.state[key] = 1;
        g_img.queue.push_back(key);
        if (!g_img.started) {
            g_img.started = true;
            for (int i = 0; i < 4; ++i) std::thread(Worker).detach();
        }
        g_img.cv.notify_one();
    }
    return nullptr;
}

std::string ImportImage(const std::string& file) {
    std::string ext = file.substr(file.find_last_of('.') == std::string::npos ? file.size() : file.find_last_of('.'));
    std::string dest = Dir() + "\\img\\" + NewId() + ext;
    if (!CopyFileW(text::Widen(file).c_str(), text::Widen(dest).c_str(), FALSE)) return {};
    return dest;
}

std::string BuyLink(const Data& d, const Product& p, int variant, const std::string& size, const Coupon* c) {
    if (!p.own) {
        // parceiro: carrinho da loja original com o tamanho escolhido (ou a página do produto)
        if (variant >= 0 && variant < (int)p.variants.size() && !p.variants[variant].cart.empty()) return p.variants[variant].cart;
        if (!p.cart.empty()) return p.cart;
        return p.url;
    }
    std::string num;
    for (char ch : d.whatsapp) if (ch >= '0' && ch <= '9') num += ch;
    if (num.empty()) return {};
    if (num.size() <= 11) num = "55" + num; // Brasil
    std::string msg = "Olá! Quero comprar na " + d.storeName + ":\n\n• " + p.name;
    if (!size.empty()) msg += " — tamanho " + size;
    msg += "\n• Preço: " + Money(p.price);
    if (c) msg += "\n• Cupom " + c->code + " (" + std::to_string(c->percent) + "% off): " + Money(ApplyCoupon(p.price, c));
    msg += "\n\nPedido feito pelo app KB.";
    return "https://wa.me/" + num + "?text=" + web::UrlEncode(msg);
}

std::string SearchText(const std::string& query, int max) {
    Data d = Load();
    std::vector<Product> all = d.own;
    auto partner = Partner();
    all.insert(all.end(), partner.begin(), partner.end());
    std::vector<std::string> words;
    for (auto& w : text::Split(text::Fold(query), ' '))
        if (w.size() > 2) words.push_back(w);
    std::vector<std::pair<int, const Product*>> hits;
    for (auto& p : all) {
        std::string hay = text::Fold(p.name + " " + p.category + " " + p.brand + " " + p.description);
        int score = 0;
        for (auto& w : words) {
            std::string stem = w.size() > 4 ? w.substr(0, w.size() - 1) : w; // luvas → luva
            if (hay.find(stem) != std::string::npos) ++score;
        }
        if (words.empty() || score > 0) hits.push_back({score + (p.own ? 1 : 0), &p});
    }
    std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::string out;
    int n = 0;
    for (auto& h : hits) {
        if (n++ >= max) break;
        const Product& p = *h.second;
        out += "- [" + p.id + "] " + p.name + " (" + (p.own ? "produto da KB" : p.brand) + ", " + p.category + ") " + Money(p.price);
        std::string sz;
        for (auto& v : p.variants) if (v.available) sz += (sz.empty() ? "" : "/") + v.name;
        for (auto& s : p.sizes) sz += (sz.empty() ? "" : "/") + s;
        if (!sz.empty()) out += " tamanhos " + sz;
        out += "\n";
    }
    if (out.empty()) out = "Nenhum produto encontrado.\n";
    if (!d.coupons.empty()) {
        std::string cs;
        for (auto& c : d.coupons) if (c.active) cs += (cs.empty() ? "" : ", ") + c.code + " (" + std::to_string(c.percent) + "%)";
        if (!cs.empty()) out += "Cupons ativos (só para produtos da KB): " + cs + "\n";
    }
    return out;
}

} // namespace store
