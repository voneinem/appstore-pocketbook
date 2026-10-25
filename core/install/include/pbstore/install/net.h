#pragma once
// Downloads for the installer: HTTPS only, certificate verification always on, trust anchors chosen once
// (the system bundle when one is found, else the bundle compiled into the program), no environment variable
// can add or remove trust. Built on pbstore::https_get.

#include <functional>
#include <string>
#include <vector>

#include "pbstore/fetch.h"

namespace pbstore {
namespace install {

using DownloadProgress = std::function<void(uint64_t done, uint64_t total)>;

// One download: the address, the largest body accepted, the longest total time, progress reports.
using Downloader = std::function<FetchResult(const std::string& url, uint64_t max_bytes, long total_seconds, const DownloadProgress& progress)>;

// Total times (seconds) for the steps of an operation; a transfer that receives no data for 30 seconds ends
// whatever these say.
constexpr long kCatalogSeconds = 30;
constexpr long kPackageSeconds = 600;
constexpr long kStallSeconds = 30;

struct NetOptions {
    std::string ca_file;      // trust anchors file; empty: use ca_blob
    std::string ca_blob;      // trust anchors compiled into the program
    std::string user_agent;   // "pbstore-install/<version>"; no other header is sent
    long stall_seconds = kStallSeconds;
};

Downloader make_https_downloader(const NetOptions& options);

// The system bundles probed in this order; the first readable, non-empty file is used.
const std::vector<std::string>& system_ca_bundle_candidates();
// Chooses the trust anchors: the first readable non-empty file among `candidates`, else the compiled-in
// bundle. Never returns options with neither (libcurl's own defaults and the environment are never used).
// `read` reads a file (false if it cannot be read); the production one reads from the disk.
NetOptions choose_trust_anchors(const std::vector<std::string>& candidates, const std::function<bool(const std::string&, std::string*)>& read,
                                const std::string& bundled_pem);

}  // namespace install
}  // namespace pbstore
