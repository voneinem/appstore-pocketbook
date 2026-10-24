#include "pbstore/base64.h"

#include <cstdint>

namespace pbstore {

namespace {
const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int value_of(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + c - 'a';
    if (c >= '0' && c <= '9') return 52 + c - '0';
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

std::string base64_encode(const std::string& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) |
                           static_cast<uint8_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back(kAlphabet[n & 63]);
    }
    const size_t rest = bytes.size() - i;
    if (rest == 1) {
        const uint32_t n = static_cast<uint8_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out += "==";
    } else if (rest == 2) {
        const uint32_t n = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::optional<std::string> base64_decode(const std::string& input) {
    std::string text = input;
    if (!text.empty() && text.back() == '\n') text.pop_back();  // one trailing newline is allowed
    if (text.size() % 4 != 0) return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int v[4];
        int pad = 0;
        for (int j = 0; j < 4; ++j) {
            const char c = text[i + j];
            if (c == '=') {
                if (!last || j < 2) return std::nullopt;  // padding only at the end of the last quartet
                ++pad;
                v[j] = 0;
            } else {
                if (pad) return std::nullopt;             // data after padding
                v[j] = value_of(c);
                if (v[j] < 0) return std::nullopt;
            }
        }
        const uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        out.push_back(static_cast<char>((n >> 16) & 0xFF));
        if (pad < 2) out.push_back(static_cast<char>((n >> 8) & 0xFF));
        if (pad < 1) out.push_back(static_cast<char>(n & 0xFF));
        // the bits that do not belong to a decoded byte must be zero (canonical encoding)
        if (pad == 2 && (n & 0xFFFF) != 0) return std::nullopt;
        if (pad == 1 && (n & 0xFF) != 0) return std::nullopt;
    }
    return out;
}

}  // namespace pbstore
