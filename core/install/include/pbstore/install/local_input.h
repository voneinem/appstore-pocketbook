#pragma once
// A package chosen on this computer (spec FR-024..): only a regular, readable file of at most 64 MiB; read once
// into memory, so later changes of the file cannot change what is written; all package rules apply; the checks
// that need a signed catalog entry are replaced by the marking "not from the catalog, not signed by the
// maintainer".

#include <functional>
#include <string>

#include "pbstore/install/package_pipeline.h"

namespace pbstore {
namespace install {

constexpr const char* kLocalMarking = "not from the catalog, not signed by the maintainer";

// Reads a regular file (no link, no pipe, no device), at most `max_bytes`. False with a reason otherwise.
using LocalFileReader = std::function<bool(const std::string& path, uint64_t max_bytes, std::string* out, std::string* why)>;

struct LocalPackageResult {
    Package package;                 // source "local"
    std::vector<Finding> findings;   // E-INS-27 (the file), E-PKG-01..15 (the package)
};

LocalPackageResult read_local_package(const std::string& path, const LocalFileReader& read);
// The package rules for bytes that were read already.
Package verify_local_package(std::string bytes);

}  // namespace install
}  // namespace pbstore
