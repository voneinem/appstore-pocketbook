#pragma once
// HTTPS download with libcurl. Certificate verification is always on: there is no option, flag or
// environment variable that turns it off. Only https:// addresses are accepted, also for redirects.

#include <cstdint>
#include <functional>
#include <string>

namespace pbstore {

enum class FetchKind { None, Unreachable, HttpStatus, Tls, TooLarge, Protocol, Timeout, Other };

struct FetchOptions {
    uint64_t max_bytes = 64ull * 1024 * 1024;   // the download is aborted at this size
    long timeout_seconds = 120;                 // total time for the whole transfer
    // Trust anchor file (PEM). Empty: the system certificate store. When this or ca_blob is given the
    // system store and the environment (SSL_CERT_FILE, SSL_CERT_DIR) are not consulted at all.
    std::string ca_file;
    // Trust anchors as PEM bytes (for a bundle compiled into the program). Same rule as ca_file.
    std::string ca_blob;
    // User-Agent header. Empty: "pbstore-catalog/<version>".
    std::string user_agent;
    // Send no User-Agent header at all (the store app on the reader: constitution Principle V, spec FR-043). Overrides user_agent.
    bool no_user_agent = false;
    // A transfer that receives no data for this many seconds ends as FetchKind::Timeout ("no data").
    // 0: no limit besides timeout_seconds.
    long stall_seconds = 0;
    // Called with (bytes received, total bytes or 0 if unknown), at least once per second while the
    // transfer runs and once at the end.
    std::function<void(uint64_t done, uint64_t total)> progress;
};

struct FetchResult {
    bool ok = false;
    FetchKind kind = FetchKind::None;
    long http_status = 0;
    std::string body;
    std::string error;   // plain text naming the cause
};

FetchResult https_get(const std::string& url, const FetchOptions& options = FetchOptions());

// What the checks call to download: a function so tests can inject fakes.
using Fetcher = std::function<FetchResult(const std::string& url, uint64_t max_bytes)>;
Fetcher make_https_fetcher(const FetchOptions& base = FetchOptions());

}  // namespace pbstore
