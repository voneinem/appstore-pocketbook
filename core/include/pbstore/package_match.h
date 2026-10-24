#pragma once
// The comparison of a downloaded package with the (signed) catalog entry that announced it. The
// entry check of the pull-request automation and the store before it installs anything both call
// this one function (constitution Principle III, items 2 and 3).

#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {

struct PackageMatchResult {
    std::vector<Finding> findings;
    std::optional<Manifest> manifest;   // set if the package is valid and matches the entry

    bool ok() const;
};

// 1. package size and SHA-256 equal the catalog's (E-ENT-15); a mismatch stops here,
// 2. the package follows the contract (E-PKG-xx),
// 3. the manifest agrees with the catalog entry: id (E-ENT-13); version, min_firmware,
//    target_device, menu_entry.id and data_dir (E-ENT-17).
PackageMatchResult package_match(const CatalogEntry& entry, const std::string& package_bytes);

}  // namespace pbstore
