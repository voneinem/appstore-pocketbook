#include "pbstore/schema.h"

namespace pbstore {

namespace {
bool number(const std::string& s, int* out) {
    if (s.empty() || s.size() > 9) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    int n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    *out = n;
    return true;
}
}  // namespace

std::optional<SchemaVersion> parse_schema_version(const std::string& text) {
    const size_t dot = text.find('.');
    if (dot == std::string::npos) return std::nullopt;
    SchemaVersion v;
    if (!number(text.substr(0, dot), &v.major) || !number(text.substr(dot + 1), &v.minor)) return std::nullopt;
    return v;
}

SchemaSupport schema_support(const SchemaVersion& v) {
    if (v.major == kSupportedSchemaMajor) return SchemaSupport::Supported;
    return v.major > kSupportedSchemaMajor ? SchemaSupport::NeedsUpdate : SchemaSupport::Unsupported;
}

}  // namespace pbstore
