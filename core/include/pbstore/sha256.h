#pragma once
// SHA-256 on top of OpenSSL libcrypto, plus the strict lowercase hex form used in the contracts.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace pbstore {

using Sha256Digest = std::array<uint8_t, 32>;

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(const void* data, size_t len);
    Sha256Digest finish();          // the object must not be used afterwards
    std::string finish_hex();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string hex_encode(const uint8_t* data, size_t len);
std::string sha256_hex(const std::string& data);
std::string sha256_hex(const void* data, size_t len);

// Exactly 64 lowercase hex characters.
bool is_sha256_hex(const std::string& s);
std::optional<Sha256Digest> parse_sha256_hex(const std::string& s);

}  // namespace pbstore
