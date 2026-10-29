#include "pbstore/store/identity_parse.h"

#include <cctype>

namespace pbstore {
namespace store {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (!std::isdigit(c)) return false;
    return true;
}

}  // namespace

std::optional<std::string> parse_model(const std::string& text) {
    const std::string t = trim(text);
    if (t.empty() || t.size() > 32) return std::nullopt;
    for (unsigned char c : t)
        if (!(std::isalnum(c) && c < 128) && c != '.' && c != '_' && c != '-') return std::nullopt;
    return t;
}

std::optional<std::string> parse_firmware(const std::string& text) {
    std::string t = trim(text);
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
    const size_t a = t.find('.');
    const size_t b = a == std::string::npos ? std::string::npos : t.find('.', a + 1);
    if (b == std::string::npos) return std::nullopt;
    if (t.find('.', b + 1) != std::string::npos) return std::nullopt;
    if (!all_digits(t.substr(0, a)) || !all_digits(t.substr(a + 1, b - a - 1)) || !all_digits(t.substr(b + 1))) return std::nullopt;
    if (t.size() > 32) return std::nullopt;
    return t;
}

namespace {

std::string shown(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw)
        if (c >= 0x20 && c < 0x7f && out.size() < 40) out += static_cast<char>(c);
    return out;
}

std::string describe_one(const std::function<std::string()>& get, std::optional<std::string> (*parse)(const std::string&)) {
    const std::string raw = get ? get() : std::string();
    if (trim(raw).empty()) return "unknown";
    if (const auto v = parse(raw)) return *v;
    return "\"" + shown(trim(raw)) + "\" (not recognised)";
}

}  // namespace

std::string StoreIdentitySource::describe() const {
    return "model " + describe_one(model_, parse_model) + ", firmware " + describe_one(firmware_, parse_firmware);
}

}  // namespace store
}  // namespace pbstore
