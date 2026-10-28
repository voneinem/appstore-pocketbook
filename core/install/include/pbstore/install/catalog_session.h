#pragma once
// Fetching the catalog and checking it before anything of it is used: signature against the built-in keys,
// schema, rollback against the serial remembered on this computer. The serial is remembered only for a
// catalog that was accepted.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/install/net.h"
#include "pbstore/install/state_store.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct CatalogSession {
    std::optional<Catalog> catalog;
    std::vector<Finding> findings;   // E-CAT-01..05, E-INS-26, E-INS-36
    std::vector<Finding> notices;    // W-INS-03 on a first run
    std::string catalog_bytes;       // the accepted catalog exactly as downloaded (set only when accepted)
    std::string signature_bytes;     // its signature exactly as downloaded
};

// `url` is the address of catalog.json; the signature is `url` + ".sig".
CatalogSession refresh_catalog(const Downloader& download, StateStore& state, const std::string& url, const std::vector<std::string>& trusted_keys,
                               const DownloadProgress& progress = nullptr);

// The finding for a failed download: distinct plain sentences for an unreachable host, a certificate problem
// (which mentions the computer's clock), a redirect to HTTP, a download that is too large, a transfer that
// made no progress, a timeout and an error status. None suggests turning the verification off.
Finding download_finding(const FetchResult& result, const std::string& what, const std::string& url);

}  // namespace install
}  // namespace pbstore
