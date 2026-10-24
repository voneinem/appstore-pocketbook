#include "pbstore/semver.h"

namespace pbstore {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ident_char(char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'; }
bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!is_digit(c)) return false;
    return true;
}

// A number without leading zeros and at most 9 digits.
bool parse_number(const std::string& s, uint32_t* out) {
    if (!all_digits(s) || s.size() > 9) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    *out = static_cast<uint32_t>(std::stoul(s));
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

int compare_identifier(const std::string& a, const std::string& b) {
    const bool na = all_digits(a), nb = all_digits(b);
    if (na && nb) {  // numeric identifiers compare as numbers (no leading zeros, so by length first)
        if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (na) return -1;  // numeric identifiers sort below alphanumeric ones
    if (nb) return 1;
    return a < b ? -1 : (a > b ? 1 : 0);
}

}  // namespace

std::optional<SemVer> parse_semver(const std::string& text) {
    if (text.empty()) return std::nullopt;
    std::string core = text, pre;
    const size_t dash = text.find('-');
    if (dash != std::string::npos) {
        core = text.substr(0, dash);
        pre = text.substr(dash + 1);
        if (pre.empty()) return std::nullopt;
    }
    for (char c : text)
        if (!is_ident_char(c) && c != '.') return std::nullopt;  // also rejects '+', spaces, non-ASCII
    const std::vector<std::string> nums = split(core, '.');
    if (nums.size() != 3) return std::nullopt;
    SemVer v;
    if (!parse_number(nums[0], &v.major) || !parse_number(nums[1], &v.minor) || !parse_number(nums[2], &v.patch))
        return std::nullopt;
    if (dash != std::string::npos) {
        for (const std::string& id : split(pre, '.')) {
            if (id.empty()) return std::nullopt;
            for (char c : id)
                if (!is_ident_char(c)) return std::nullopt;
            if (all_digits(id) && id.size() > 1 && id[0] == '0') return std::nullopt;
            v.pre.push_back(id);
        }
    }
    return v;
}

int compare(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() && b.pre.empty()) return 0;
    if (a.pre.empty()) return 1;   // a release is newer than its pre-releases
    if (b.pre.empty()) return -1;
    const size_t n = std::min(a.pre.size(), b.pre.size());
    for (size_t i = 0; i < n; ++i) {
        const int c = compare_identifier(a.pre[i], b.pre[i]);
        if (c != 0) return c;
    }
    if (a.pre.size() == b.pre.size()) return 0;
    return a.pre.size() < b.pre.size() ? -1 : 1;
}

std::string to_string(const SemVer& v) {
    std::string s = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    for (size_t i = 0; i < v.pre.size(); ++i) s += (i == 0 ? "-" : ".") + v.pre[i];
    return s;
}

}  // namespace pbstore
