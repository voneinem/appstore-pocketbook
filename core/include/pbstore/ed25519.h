#pragma once
// Ed25519 (RFC 8032) on OpenSSL libcrypto. Keys, seeds and signatures are raw byte strings:
// 32-byte seed (private key), 32-byte public key, 64-byte signature. Functions never throw on
// hostile input: wrong lengths and bad signatures simply fail.

#include <string>

namespace pbstore {

constexpr size_t kEd25519SeedLen = 32;
constexpr size_t kEd25519PublicKeyLen = 32;
constexpr size_t kEd25519SignatureLen = 64;

// 32 random bytes from the OpenSSL generator. Throws std::runtime_error if none are available.
std::string generate_seed();
// The public key of a seed, or "" if the seed is not 32 bytes.
std::string ed25519_public_from_seed(const std::string& seed);
// The signature of `message`, or "" if the seed is not 32 bytes.
std::string ed25519_sign(const std::string& seed, const std::string& message);
// True only for a valid signature of `message` by `public_key`.
bool ed25519_verify(const std::string& public_key, const std::string& message, const std::string& signature);

}  // namespace pbstore
