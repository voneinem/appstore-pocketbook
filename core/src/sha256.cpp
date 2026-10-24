#include "pbstore/sha256.h"

#include <openssl/evp.h>

#include <stdexcept>

namespace pbstore {

struct Sha256::Impl {
    EVP_MD_CTX* ctx = nullptr;
};

Sha256::Sha256() : impl_(new Impl) {
    impl_->ctx = EVP_MD_CTX_new();
    if (!impl_->ctx || EVP_DigestInit_ex(impl_->ctx, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 initialisation failed");
}

Sha256::~Sha256() {
    if (impl_ && impl_->ctx) EVP_MD_CTX_free(impl_->ctx);
}

void Sha256::update(const void* data, size_t len) {
    if (len && EVP_DigestUpdate(impl_->ctx, data, len) != 1) throw std::runtime_error("SHA-256 update failed");
}

Sha256Digest Sha256::finish() {
    Sha256Digest out{};
    unsigned int n = 0;
    if (EVP_DigestFinal_ex(impl_->ctx, out.data(), &n) != 1 || n != out.size())
        throw std::runtime_error("SHA-256 finish failed");
    return out;
}

std::string Sha256::finish_hex() {
    const Sha256Digest d = finish();
    return hex_encode(d.data(), d.size());
}

std::string hex_encode(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s.push_back(digits[data[i] >> 4]);
        s.push_back(digits[data[i] & 0xF]);
    }
    return s;
}

std::string sha256_hex(const void* data, size_t len) {
    Sha256 h;
    h.update(data, len);
    return h.finish_hex();
}

std::string sha256_hex(const std::string& data) { return sha256_hex(data.data(), data.size()); }

std::optional<Sha256Digest> parse_sha256_hex(const std::string& s) {
    if (s.size() != 64) return std::nullopt;
    Sha256Digest out{};
    for (size_t i = 0; i < 32; ++i) {
        int v[2];
        for (int j = 0; j < 2; ++j) {
            const char c = s[i * 2 + j];
            if (c >= '0' && c <= '9') v[j] = c - '0';
            else if (c >= 'a' && c <= 'f') v[j] = 10 + c - 'a';
            else return std::nullopt;
        }
        out[i] = static_cast<uint8_t>((v[0] << 4) | v[1]);
    }
    return out;
}

bool is_sha256_hex(const std::string& s) { return parse_sha256_hex(s).has_value(); }

}  // namespace pbstore
