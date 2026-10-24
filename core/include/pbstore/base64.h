#pragma once
// Standard-alphabet base64 (RFC 4648 section 4) with strict decoding: canonical padding, no
// whitespace except one trailing "\n", no URL alphabet, unused bits must be zero.

#include <optional>
#include <string>

namespace pbstore {

std::string base64_encode(const std::string& bytes);
std::optional<std::string> base64_decode(const std::string& text);

}  // namespace pbstore
