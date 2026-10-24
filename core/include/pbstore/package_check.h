#pragma once
// Validation of a whole app package: ZIP safety, strict root, manifest, and the manifest's file
// list against the real content (docs/contracts/package-manifest.md, E-PKG-01 … E-PKG-15).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/manifest.h"
#include "pbstore/rules.h"
#include "pbstore/zip_package.h"

namespace pbstore {

struct PackageCheckResult {
    std::vector<Finding> findings;
    std::optional<Manifest> manifest;   // set if the manifest could be parsed and validated
    int64_t package_size = 0;           // size of the ZIP file
    std::string package_sha256;         // SHA-256 of the ZIP file

    bool ok() const;                    // no error findings and a manifest
};

PackageCheckResult check_package(const std::string& zip_bytes, const ZipLimits& limits = ZipLimits());

}  // namespace pbstore
