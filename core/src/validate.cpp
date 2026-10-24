#include "pbstore/validate.h"

#include <cctype>
#include <vector>

namespace pbstore {
namespace validate {

namespace {

bool lower(char c) { return c >= 'a' && c <= 'z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool alnum(char c) { return lower(c) || digit(c) || (c >= 'A' && c <= 'Z'); }

bool all_digits(const std::string& s, size_t pos, size_t n) {
    if (pos + n > s.size()) return false;
    for (size_t i = 0; i < n; ++i)
        if (!digit(s[pos + i])) return false;
    return true;
}
int num(const std::string& s, size_t pos, size_t n) {
    int v = 0;
    for (size_t i = 0; i < n; ++i) v = v * 10 + (s[pos + i] - '0');
    return v;
}
bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

bool name_part(const std::string& s) {  // owner, repo, tag, asset stem: letters, digits and . _ -
    if (s.empty()) return false;
    for (char c : s)
        if (!alnum(c) && c != '.' && c != '_' && c != '-') return false;
    return s != "." && s != "..";
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

}  // namespace

bool is_valid_utf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        size_t n;      // number of continuation bytes
        unsigned cp;
        if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
        else return false;
        if (i + n >= s.size()) return false;   // the continuation bytes must exist
        for (size_t k = 1; k <= n; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;   // overlong, surrogate
        if (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;                    // overlong, too large
        i += n + 1;
    }
    return true;
}

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

bool text_length_between(const std::string& s, size_t min_len, size_t max_len) {
    if (!is_valid_utf8(s)) return false;
    const size_t n = utf8_length(s);
    return n >= min_len && n <= max_len;
}

bool is_app_id(const std::string& s) {
    if (s.size() < 2 || s.size() > 40 || !lower(s[0])) return false;
    for (char c : s)
        if (!lower(c) && !digit(c) && c != '-') return false;
    return true;
}

bool is_menu_entry_id(const std::string& s) {
    if (s.size() < 3 || s.size() > 42 || s.compare(0, 2, "U_") != 0) return false;
    for (size_t i = 2; i < s.size(); ++i)
        if (!alnum(s[i]) && s[i] != '_') return false;
    return true;
}

bool is_data_dir(const std::string& s) {
    static const std::string prefix = "system/";
    if (s.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string name = s.substr(prefix.size());
    if (name.size() < 2 || name.size() > 40 || !(lower(name[0]) || digit(name[0]))) return false;
    for (char c : name)
        if (!lower(c) && !digit(c) && c != '_' && c != '-') return false;
    return true;
}

bool is_safe_path(const std::string& s) {
    if (s.empty() || s.size() > 200) return false;
    size_t seg_start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '/') {
            const size_t len = i - seg_start;
            if (len == 0) return false;                         // empty segment, leading or trailing "/"
            if (s[seg_start] == '.') return false;              // ".", ".." and hidden names
            seg_start = i + 1;
            continue;
        }
        const char c = s[i];
        if (!alnum(c) && c != '.' && c != '_' && c != '-' && c != ' ') return false;
    }
    return true;
}

bool is_https_url(const std::string& s) {
    static const std::string scheme = "https://";
    if (s.compare(0, scheme.size(), scheme) != 0 || s.size() > 2048) return false;
    for (unsigned char c : s)
        if (c <= 0x20 || c == 0x7F || c >= 0x80) return false;
    size_t end = s.find_first_of("/?#", scheme.size());
    const std::string host = s.substr(scheme.size(), end == std::string::npos ? std::string::npos : end - scheme.size());
    if (host.empty()) return false;
    for (char c : host)
        if (!alnum(c) && c != '.' && c != '-' && c != ':') return false;
    return host[0] != '.' && host[0] != '-' && host[0] != ':';
}

bool is_iso_date(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    if (!all_digits(s, 0, 4) || !all_digits(s, 5, 2) || !all_digits(s, 8, 2)) return false;
    const int y = num(s, 0, 4), m = num(s, 5, 2), d = num(s, 8, 2);
    if (m < 1 || m > 12 || d < 1) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int max = days[m - 1] + ((m == 2 && leap(y)) ? 1 : 0);
    return d <= max;
}

bool is_iso_timestamp(const std::string& s) {
    if (s.size() != 20 || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
    if (!is_iso_date(s.substr(0, 10))) return false;
    if (!all_digits(s, 11, 2) || !all_digits(s, 14, 2) || !all_digits(s, 17, 2)) return false;
    return num(s, 11, 2) <= 23 && num(s, 14, 2) <= 59 && num(s, 17, 2) <= 59;
}

std::optional<GithubRelease> parse_github_release_url(const std::string& url) {
    static const std::string prefix = "https://github.com/";
    if (url.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    if (url.find_first_of("?#") != std::string::npos) return std::nullopt;
    const std::vector<std::string> p = split(url.substr(prefix.size()), '/');
    // owner / repo / releases / download / tag / asset.zip
    if (p.size() != 6 || p[2] != "releases" || p[3] != "download") return std::nullopt;
    if (!name_part(p[0]) || !name_part(p[1]) || !name_part(p[4])) return std::nullopt;
    const std::string& asset = p[5];
    if (asset.size() <= 4 || asset.compare(asset.size() - 4, 4, ".zip") != 0 || !name_part(asset)) return std::nullopt;
    return GithubRelease{p[0], p[1], p[4], asset};
}

std::optional<GithubRepo> parse_github_repo_url(const std::string& url) {
    static const std::string prefix = "https://github.com/";
    if (url.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    if (url.find_first_of("?#") != std::string::npos) return std::nullopt;
    std::string rest = url.substr(prefix.size());
    if (!rest.empty() && rest.back() == '/') rest.pop_back();
    const std::vector<std::string> p = split(rest, '/');
    if (p.size() != 2 || !name_part(p[0])) return std::nullopt;
    std::string repo = p[1];
    if (repo.size() > 4 && repo.compare(repo.size() - 4, 4, ".git") == 0) repo.resize(repo.size() - 4);
    if (!name_part(repo)) return std::nullopt;
    return GithubRepo{p[0], repo};
}

}  // namespace validate
}  // namespace pbstore
