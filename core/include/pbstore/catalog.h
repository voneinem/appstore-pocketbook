#pragma once
// The catalog (docs/contracts/catalog-format.md): the signed list of apps. This header holds the
// data structures, the strict parser used by every consumer, and the deterministic writer.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/rules.h"

namespace pbstore {

constexpr size_t kMaxCatalogBytes = 1024 * 1024;     // 1 MiB
constexpr int64_t kMaxPackageBytes = 67108864;       // 64 MiB

struct LocalizedText {
    std::string en;
    std::optional<std::string> de;
};

struct CatalogEntry {
    std::string id, name;
    LocalizedText short_description, long_description;
    std::string version;        // semantic version
    std::string release_date;   // YYYY-MM-DD
    std::string package_url;
    int64_t package_size = 0;
    std::string package_sha256;
    std::string license, source_url, author;
    std::string min_firmware, target_device;
    std::string icon_url, changelog_url;
    std::vector<std::string> withdrawn_versions;
    std::vector<std::string> listed_versions;   // ascending, contains `version`
    std::string menu_entry_id;
    std::optional<std::string> data_dir;        // system/<name> or none
};

struct Catalog {
    std::string schema_version;   // "MAJOR.MINOR"
    int64_t serial = 0;
    std::string generated_at;     // YYYY-MM-DDTHH:MM:SSZ
    std::vector<CatalogEntry> apps;
};

struct CatalogParseResult {
    std::optional<Catalog> catalog;      // set only if the document is valid
    std::vector<Finding> findings;       // E-CAT-02 (malformed) or E-CAT-03 (newer major)
};

// Strict parse and validation. A schema with a newer major than supported is reported as
// E-CAT-03 ("needs updating") without judging its fields; unknown fields are ignored.
CatalogParseResult parse_catalog(const std::string& bytes);

// Deterministic output: apps sorted by id, fixed key order, two-space indentation, trailing newline.
std::string write_catalog(const Catalog& catalog);

// An app is offered only if its listed version is not withdrawn. Installed copies are never changed.
bool offered(const CatalogEntry& entry);

// True if `version` (for example the version installed on a reader) is withdrawn. A consumer can
// tell the user about it; it never removes or changes an installed copy because of a withdrawal.
bool is_withdrawn(const CatalogEntry& entry, const std::string& version);

}  // namespace pbstore
