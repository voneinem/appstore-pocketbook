#pragma once
// schema_version "MAJOR.MINOR" of the catalog, the package manifest and the entry file.
// Readers ignore unknown fields. A newer major than supported means "this program needs updating";
// a minor change is additive (Principle VII).

#include <optional>
#include <string>

namespace pbstore {

constexpr int kSupportedSchemaMajor = 1;
constexpr const char* kSupportedSchema = "1.0";  // the version this program writes

struct SchemaVersion {
    int major = 0;
    int minor = 0;
};

enum class SchemaSupport { Supported, NeedsUpdate, Unsupported };

// "MAJOR.MINOR": two numbers without leading zeros, at most 9 digits each.
std::optional<SchemaVersion> parse_schema_version(const std::string& text);
SchemaSupport schema_support(const SchemaVersion& v);

}  // namespace pbstore
