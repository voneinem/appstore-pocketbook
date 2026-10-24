#include "pbstore/firmware_version.h"

namespace pbstore {

std::optional<FirmwareVersion> parse_firmware(const std::string& text) {
    FirmwareVersion v;
    size_t start = 0;
    while (true) {
        const size_t pos = text.find('.', start);
        const std::string part = text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (part.empty() || part.size() > 9) return std::nullopt;
        uint32_t n = 0;
        for (char c : part) {
            if (c < '0' || c > '9') return std::nullopt;
            n = n * 10 + static_cast<uint32_t>(c - '0');
        }
        v.parts.push_back(n);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (v.parts.size() < 2) return std::nullopt;
    return v;
}

int compare(const FirmwareVersion& a, const FirmwareVersion& b) {
    const size_t n = std::max(a.parts.size(), b.parts.size());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t x = i < a.parts.size() ? a.parts[i] : 0;
        const uint32_t y = i < b.parts.size() ? b.parts[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::string to_string(const FirmwareVersion& v) {
    std::string s;
    for (size_t i = 0; i < v.parts.size(); ++i) s += (i ? "." : "") + std::to_string(v.parts[i]);
    return s;
}

}  // namespace pbstore
