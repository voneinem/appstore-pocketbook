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
    // Additional trust anchor file (PEM). Empty: the system certificate store. This adds trust; it
    // cannot weaken the verification.
    std::string ca_file;
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
