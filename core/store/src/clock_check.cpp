#include "pbstore/store/clock_check.h"

#include <array>
#include <cstdio>

namespace pbstore {
namespace store {

namespace {

using Stamp = std::array<int, 6>;   // year, month, day, hour, minute, second

Stamp stamp_of(const install::Utc& u) { return {u.year, u.month, u.day, u.hour, u.minute, u.second}; }

// "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM:SSZ"
std::optional<Stamp> parse_stamp(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (s.size() == 10) {
        if (std::sscanf(s.c_str(), "%4d-%2d-%2d", &y, &mo, &d) != 3 || s[4] != '-' || s[7] != '-') return std::nullopt;
    } else if (s.size() == 20 && s[10] == 'T' && s[19] == 'Z') {
        if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &y, &mo, &d, &h, &mi, &se) != 6) return std::nullopt;
    } else {
        return std::nullopt;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return std::nullopt;
    return Stamp{y, mo, d, h, mi, se};
}

}  // namespace

bool clock_looks_wrong(const install::Utc& now, const std::string& release_date, const std::optional<std::string>& cached_generated_at, FetchKind failure) {
    if (failure != FetchKind::Tls) return false;
    if (now.year < 2024 || now.year > 2100) return true;
    const Stamp n = stamp_of(now);
    if (const auto release = parse_stamp(release_date)) if (n < *release) return true;
    if (cached_generated_at) if (const auto generated = parse_stamp(*cached_generated_at)) if (n < *generated) return true;
    return false;
}

std::string clock_date_text(const install::Utc& now) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", now.year, now.month, now.day);
    return buf;
}

}  // namespace store
}  // namespace pbstore
