#pragma once
// Loading the live (currently published) catalog for check-entry and build. It is verified exactly
// like a consumer would verify it; a catalog that cannot be loaded or verified is E-BLD-03.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/fetch.h"

namespace pbstore {

struct LiveCatalogResult {
    std::optional<Catalog> catalog;
    std::vector<Finding> findings;   // E-BLD-03 with the cause named
};

// `dir` holds catalog.json and catalog.json.sig.
LiveCatalogResult load_live_catalog_dir(const std::string& dir, const std::vector<std::string>& trusted_keys);
// `catalog_url` is the https:// address of catalog.json; the signature is catalog_url + ".sig".
LiveCatalogResult load_live_catalog_url(const std::string& catalog_url, const std::vector<std::string>& trusted_keys,
                                        const Fetcher& fetcher);

}  // namespace pbstore
