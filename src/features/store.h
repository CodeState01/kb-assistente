#pragma once
#include "media/image.h"
#include <string>
#include <vector>

// Loja do KB: produtos da treinadora (vendidos pelo WhatsApp, com cupom) e de marcas parceiras
// (a compra é feita na loja original — o KB abre o carrinho já com o tamanho escolhido).
namespace store {

struct Variant {
    std::string name;   // "12", "M", "3 metros"
    std::string cart;   // link que coloca o item no carrinho da loja original
    bool available = true;
};

struct Product {
    std::string id, name, brand, category, description;
    std::string url;     // página do produto na loja original
    std::string image;   // URL (loja parceira) ou caminho local (produtos próprios)
    std::string cart;    // carrinho direto (produto sem tamanhos)
    double price = 0;
    bool own = false;    // produto da KB (vende pelo WhatsApp)
    std::vector<Variant> variants;
    std::vector<std::string> sizes; // tamanhos dos produtos próprios ("P,M,G")
};

struct Coupon {
    std::string code;
    int percent = 10;
    bool active = true;
    std::string note;
};

struct Data {
    std::string storeName = "KB Muay Thai";
    std::string whatsapp;              // número da treinadora (com DDD)
    std::vector<Product> own;          // produtos da KB
    std::vector<Coupon> coupons;
};

Data Load();
void Save(const Data& d);
std::vector<Product> Partner();                       // catálogo das marcas parceiras (embutido no app)
std::vector<std::string> Categories(const std::vector<Product>& all);
const Coupon* FindCoupon(const Data& d, const std::string& code);
double ApplyCoupon(double price, const Coupon* c);
std::string Money(double v);                         // "R$ 1.234,90"

// Imagem do produto: devolve a miniatura quando já está pronta (baixa/decodifica em segundo plano).
PicturePtr Image(const std::string& urlOrPath);
std::string ImportImage(const std::string& file);    // copia uma foto para a pasta da loja

// Comprar: produto próprio → WhatsApp com o pedido; parceiro → carrinho da loja original.
std::string BuyLink(const Data& d, const Product& p, int variant, const std::string& size, const Coupon* c);

// Texto para a IA (ferramenta store_search)
std::string SearchText(const std::string& query, int max = 8);
std::string NewId();

} // namespace store
