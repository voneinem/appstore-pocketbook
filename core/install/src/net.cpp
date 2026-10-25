#include "pbstore/install/net.h"

#include <fstream>
#include <sstream>

#include "pbstore_cacert.h"

namespace pbstore {
namespace install {

Downloader make_https_downloader(const NetOptions& options) {
    NetOptions o = options;
    // never fall back to libcurl's own defaults or to the environment: without anchors the bundled ones are used
    if (o.ca_file.empty() && o.ca_blob.empty()) o.ca_blob = pbstore_cacert_bundle();
    return [o](const std::string& url, uint64_t max_bytes, long total_seconds, const DownloadProgress& progress) {
        FetchOptions f;
        f.max_bytes = max_bytes;
        f.timeout_seconds = total_seconds;
        f.stall_seconds = o.stall_seconds;
        f.user_agent = o.user_agent;
        f.progress = progress;
        if (!o.ca_file.empty()) f.ca_file = o.ca_file;
        else f.ca_blob = o.ca_blob;
        return https_get(url, f);
    };
}

const std::vector<std::string>& system_ca_bundle_candidates() {
    static const std::vector<std::string> paths = {
        "/etc/ssl/certs/ca-certificates.crt",   // Debian, Ubuntu
        "/etc/pki/tls/certs/ca-bundle.crt",     // Fedora, RHEL
        "/etc/ssl/ca-bundle.pem",               // openSUSE
        "/etc/pki/tls/cacert.pem",              // older RHEL
        "/etc/ssl/cert.pem",                    // Alpine, macOS
    };
    return paths;
}

NetOptions choose_trust_anchors(const std::vector<std::string>& candidates, const std::function<bool(const std::string&, std::string*)>& read,
                                const std::string& bundled_pem) {
    NetOptions o;
    for (const std::string& path : candidates) {
        std::string content;
        if (read(path, &content) && !content.empty() && content.find("BEGIN CERTIFICATE") != std::string::npos) {
            o.ca_file = path;
            return o;
        }
    }
    o.ca_blob = bundled_pem;
    return o;
}

}  // namespace install
}  // namespace pbstore
