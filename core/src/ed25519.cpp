#include "pbstore/ed25519.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <memory>
#include <stdexcept>
#include <vector>

namespace pbstore {

namespace {
struct PkeyDeleter { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };
struct CtxDeleter { void operator()(EVP_MD_CTX* c) const { EVP_MD_CTX_free(c); } };
using Pkey = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using MdCtx = std::unique_ptr<EVP_MD_CTX, CtxDeleter>;

Pkey private_key(const std::string& seed) {
    if (seed.size() != kEd25519SeedLen) return nullptr;
    return Pkey(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                             reinterpret_cast<const unsigned char*>(seed.data()), seed.size()));
}
}  // namespace

std::string generate_seed() {
    std::vector<unsigned char> buf(kEd25519SeedLen);
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) throw std::runtime_error("no random bytes available");
    std::string seed(reinterpret_cast<const char*>(buf.data()), buf.size());
    OPENSSL_cleanse(buf.data(), buf.size());
    return seed;
}

std::string ed25519_public_from_seed(const std::string& seed) {
    Pkey key = private_key(seed);
    if (!key) return "";
    unsigned char pub[kEd25519PublicKeyLen];
    size_t len = sizeof pub;
    if (EVP_PKEY_get_raw_public_key(key.get(), pub, &len) != 1 || len != sizeof pub) return "";
    return std::string(reinterpret_cast<const char*>(pub), len);
}

std::string ed25519_sign(const std::string& seed, const std::string& message) {
    Pkey key = private_key(seed);
    if (!key) return "";
    MdCtx ctx(EVP_MD_CTX_new());
    if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) return "";
    unsigned char sig[kEd25519SignatureLen];
    size_t len = sizeof sig;
    if (EVP_DigestSign(ctx.get(), sig, &len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) != 1 ||
        len != sizeof sig)
        return "";
    return std::string(reinterpret_cast<const char*>(sig), len);
}

bool ed25519_verify(const std::string& public_key, const std::string& message, const std::string& signature) {
    if (public_key.size() != kEd25519PublicKeyLen || signature.size() != kEd25519SignatureLen) return false;
    Pkey key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                         reinterpret_cast<const unsigned char*>(public_key.data()), public_key.size()));
    if (!key) return false;
    MdCtx ctx(EVP_MD_CTX_new());
    if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) return false;
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
                            reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
}

}  // namespace pbstore
