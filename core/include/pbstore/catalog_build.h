#pragma once
// Rebuilding the catalog from all entry files (docs/contracts/catalog-format.md, "Producer rules").
// The previously published, signature-verified catalog is the only state: unchanged entries keep
// the values recorded there and are not downloaded again; new or changed entries are downloaded
// once. Any failing entry fails the whole build: there is no partial catalog.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/entry_check.h"
#include "pbstore/fetch.h"
#include "pbstore/rules.h"

namespace pbstore {

struct BuildOptions {
    std::string entries_dir;              // catalog/entries: one <app-id>.json per app
    std::optional<Catalog> live;          // the verified live catalog
    bool first_publish = false;           // the only way to build without a live catalog
    bool dry_run = false;                 // run every check, produce no catalog bytes
    int64_t now = 0;                      // UTC epoch seconds: generated_at and the serial's lower bound
    std::string today;                    // UTC date YYYY-MM-DD for release_date checks
    Fetcher fetcher;                      // downloads of new or changed packages
};

struct BuildResult {
    std::vector<Finding> findings;        // errors (E-ENT, E-PKG, E-BLD) and notices
    std::optional<Catalog> catalog;       // set if the build succeeded (also for a dry run)
    std::string catalog_bytes;            // the unsigned catalog.json; empty for a dry run or a failure
    size_t carried_over = 0;              // entries that kept their recorded values
    size_t downloaded = 0;                // entries whose package was downloaded

    bool ok() const;
};

// E-BLD-01 duplicate menu entry id, E-BLD-02 duplicate data folder, E-BLD-03 no live catalog and not
// a first publish, E-BLD-06 catalog too large, E-BLD-07 an entry failed (all failures are reported).
// The serial is max(now, live serial + 1).
BuildResult build_catalog(const BuildOptions& options);

}  // namespace pbstore
