#include "pbstore/fetch.h"

#include <curl/curl.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>

namespace pbstore {

namespace {

struct Body {
    std::string data;
    uint64_t max = 0;
    bool too_large = false;
    const std::function<void(uint64_t, uint64_t)>* progress = nullptr;
    long stall_seconds = 0;          // 0: no limit on silence
    bool stalled = false;
    curl_off_t last_bytes = -1;
    std::chrono::steady_clock::time_point last_change = std::chrono::steady_clock::now();
};

// libcurl calls this at least about once a second, also while nothing arrives. It reports progress
// and ends a transfer that has been silent for the stall time (libcurl's own low-speed check averages
// over several seconds and would let a stalled transfer run on for too long).
int on_progress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    Body* b = static_cast<Body*>(userdata);
    if (dlnow != b->last_bytes) {
        b->last_bytes = dlnow;
        b->last_change = std::chrono::steady_clock::now();
    } else if (b->stall_seconds > 0 &&
               std::chrono::steady_clock::now() - b->last_change >= std::chrono::seconds(b->stall_seconds)) {
        b->stalled = true;
        return 1;   // aborts the transfer
    }
    if (b->progress && *b->progress)
        (*b->progress)(static_cast<uint64_t>(dlnow < 0 ? 0 : dlnow), static_cast<uint64_t>(dltotal < 0 ? 0 : dltotal));
    return 0;
}

size_t on_data(char* ptr, size_t size, size_t nmemb, void* userdata) {
    Body* b = static_cast<Body*>(userdata);
    const size_t n = size * nmemb;
    if (b->data.size() + n > b->max) {
        b->too_large = true;
        return 0;   // aborts the transfer
    }
    b->data.append(ptr, n);
    return n;
}

void init_curl() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

FetchResult fail(FetchKind kind, const std::string& message, long status = 0) {
    FetchResult r;
    r.kind = kind;
    r.http_status = status;
    r.error = message;
    return r;
}

std::string host_of(const std::string& url) {
    const size_t start = url.find("://");
    if (start == std::string::npos) return url;
    const size_t end = url.find_first_of("/?#", start + 3);
    return url.substr(start + 3, end == std::string::npos ? std::string::npos : end - start - 3);
}

}  // namespace

FetchResult https_get(const std::string& url, const FetchOptions& options) {
    static const std::string scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return fail(FetchKind::Protocol, "only https:// addresses are allowed (got \"" + url.substr(0, 40) + "\")");
    init_curl();
    CURL* h = curl_easy_init();
    if (!h) return fail(FetchKind::Other, "the HTTP library could not be started");
    Body body;
    body.max = options.max_bytes;
    char errbuf[CURL_ERROR_SIZE] = {0};
    body.progress = &options.progress;
    const std::string agent = options.user_agent.empty() ? std::string("pbstore-catalog/") + PBSTORE_VERSION : options.user_agent;
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, options.timeout_seconds);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(options.max_bytes));
    if (!options.no_user_agent) curl_easy_setopt(h, CURLOPT_USERAGENT, agent.c_str());   // libcurl sends none unless it is set
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
    if (!options.ca_file.empty()) curl_easy_setopt(h, CURLOPT_CAINFO, options.ca_file.c_str());
    curl_blob ca_blob{};
    if (!options.ca_blob.empty()) {
        ca_blob.data = const_cast<char*>(options.ca_blob.data());
        ca_blob.len = options.ca_blob.size();
        ca_blob.flags = CURL_BLOB_NOCOPY;
        curl_easy_setopt(h, CURLOPT_CAINFO_BLOB, &ca_blob);
    }
    body.stall_seconds = options.stall_seconds;
    if (options.progress || options.stall_seconds > 0) {
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, &body);
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    }

    const CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);

    const std::string detail = errbuf[0] ? std::string(" (") + errbuf + ")" : "";
    if (rc != CURLE_OK) {
        if (body.too_large || rc == CURLE_FILESIZE_EXCEEDED)
            return fail(FetchKind::TooLarge, "the download is larger than " + std::to_string(options.max_bytes) + " bytes");
        switch (rc) {
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CACERT_BADFILE:
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_ISSUER_ERROR:
            case CURLE_SSL_INVALIDCERTSTATUS:
                return fail(FetchKind::Tls, "the server's certificate could not be verified" + detail);
            case CURLE_UNSUPPORTED_PROTOCOL:
                return fail(FetchKind::Protocol, "a redirect to a non-HTTPS address was refused; only https:// is allowed" + detail);
            case CURLE_TOO_MANY_REDIRECTS:
                return fail(FetchKind::Other, "too many redirects (more than 5)");
            case CURLE_ABORTED_BY_CALLBACK:
                if (body.stalled)
                    return fail(FetchKind::Timeout, "no data arrived for " + std::to_string(options.stall_seconds) + " seconds");
                return fail(FetchKind::Other, std::string("the download was interrupted") + detail);
            case CURLE_OPERATION_TIMEDOUT: {
                return fail(FetchKind::Timeout, "the download timed out after " + std::to_string(options.timeout_seconds) + " seconds");
            }
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
                return fail(FetchKind::Unreachable, "cannot reach " + host_of(url) + detail);
            default:
                return fail(FetchKind::Other, std::string("the download failed: ") + curl_easy_strerror(rc) + detail);
        }
    }
    if (status >= 400 || status < 200)
        return fail(FetchKind::HttpStatus, "the server answered with status " + std::to_string(status), status);
    FetchResult r;
    r.ok = true;
    r.http_status = status;
    r.body = std::move(body.data);
    if (options.progress) options.progress(r.body.size(), r.body.size());
    return r;
}

Fetcher make_https_fetcher(const FetchOptions& base) {
    return [base](const std::string& url, uint64_t max_bytes) {
        FetchOptions o = base;
        o.max_bytes = max_bytes;
        return https_get(url, o);
    };
}

}  // namespace pbstore
