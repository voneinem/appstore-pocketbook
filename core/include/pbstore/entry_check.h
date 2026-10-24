#pragma once
// check_entry: what the pull-request automation and developers run for one entry file: the entry
// rules, the package download, the package rules, and the cross-checks between the two
// (docs/contracts/entry-file.md, package-manifest.md).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/entry.h"
#include "pbstore/fetch.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {

struct PackageSource {
    std::optional<std::string> local_zip;   // a package file given on the command line: no download
    Fetcher fetcher;                         // otherwise the package is fetched from package_url
};

struct EntryCheckOptions {
    std::string today;                       // current UTC date YYYY-MM-DD
    std::optional<Catalog> live;             // the verified live catalog, if one is available
};

struct EntryCheckResult {
    std::vector<Finding> findings;           // errors and notices
    std::optional<Entry> entry;
    std::optional<Manifest> manifest;
    int64_t package_size = 0;
    std::string package_sha256;

    bool ok() const;                         // no error findings, entry and manifest present
};

// W-ENT-01 (notices): the owner or repository of package_url or source_url changed compared with the
// entry of the same app in the live catalog. Nothing for an app that is not in the live catalog.
std::vector<Finding> compare_with_live(const Entry& entry, const Catalog& live, const std::string& subject);

EntryCheckResult check_entry(const std::string& entry_bytes, const std::string& file_name, const PackageSource& source,
                             const EntryCheckOptions& options);

}  // namespace pbstore
