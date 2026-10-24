#pragma once
// Semantic versions for apps: MAJOR.MINOR.PATCH with an optional pre-release (-rc1, -alpha.1).
// No leading "v", no build metadata (+...), no leading zeros, at most 9 digits per number.
// Ordering follows the semantic-versioning precedence rules (a pre-release is older than the
// release it precedes).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pbstore {

struct SemVer {
    uint32_t major = 0, minor = 0, patch = 0;
    std::vector<std::string> pre;  // pre-release identifiers, empty for a release
};

std::optional<SemVer> parse_semver(const std::string& text);
// <0, 0, >0 like strcmp.
int compare(const SemVer& a, const SemVer& b);
std::string to_string(const SemVer& v);

}  // namespace pbstore
