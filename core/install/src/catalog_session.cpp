#include "pbstore/install/catalog_session.h"

#include "pbstore/catalog_verify.h"

namespace pbstore {
namespace install {

namespace {

std::string host_of(const std::string& url) {
    const size_t start = url.find("://");
    if (start == std::string::npos) return url;
    const size_t end = url.find_first_of("/?#", start + 3);
    std::string host = url.substr(start + 3, end == std::string::npos ? std::string::npos : end - start - 3);
    const size_t at = host.rfind('@');
    return at == std::string::npos ? host : host.substr(at + 1);
}

}  // namespace

Finding download_finding(const FetchResult& r, const std::string& what, const std::string& url) {
    const std::string host = host_of(url);
    switch (r.kind) {
        case FetchKind::Unreachable:
            return make_error("E-INS-26", what, "cannot reach " + host + " to download " + what + "; check the internet connection (" + r.error + ")");
        case FetchKind::Tls:
            return make_error("E-INS-26", what,
                              "the certificate of " + host + " could not be verified, so " + what + " was not downloaded. A wrong date or time on this computer, a network that "
                              "intercepts connections or a wrong address can cause this; check the computer's clock and the network (" + r.error + ")");
        case FetchKind::Protocol:
            return make_error("E-INS-26", what, "the download of " + what + " from " + host + " was stopped because the server redirected to an address that is not HTTPS; the installer only uses HTTPS");
        case FetchKind::TooLarge:
            return make_error("E-INS-26", what, what + " is larger than the signed size allows and the download was stopped (" + r.error + ")");
        case FetchKind::Timeout:
            if (r.error.find("no data") != std::string::npos)
                return make_error("E-INS-36", what, "the download of " + what + " from " + host + " made no progress for " + std::to_string(kStallSeconds) + " seconds and was stopped");
            return make_error("E-INS-26", what, "the download of " + what + " from " + host + " took too long and was stopped (" + r.error + ")");
        case FetchKind::HttpStatus:
            return make_error("E-INS-26", what, host + " answered with status " + std::to_string(r.http_status) + " when " + what + " was requested");
        default:
            return make_error("E-INS-26", what, "the download of " + what + " from " + host + " failed (" + r.error + ")");
    }
}

CatalogSession refresh_catalog(const Downloader& download, StateStore& state, const std::string& url, const std::vector<std::string>& trusted_keys,
                               const DownloadProgress& progress) {
    CatalogSession s;
    // one byte more than the limit, so that a catalog that is too large is reported by the consumer rule E-CAT-05
    FetchResult cat = download(url, kMaxCatalogBytes + 1, kCatalogSeconds, progress);
    if (!cat.ok) {
        if (cat.kind == FetchKind::TooLarge) s.findings.push_back(make_error("E-CAT-05", "catalog.json", "the catalog is larger than 1 MiB"));
        else s.findings.push_back(download_finding(cat, "the catalog", url));
        return s;
    }
    FetchResult sig = download(url + ".sig", 4096, kCatalogSeconds, nullptr);
    if (!sig.ok) {
        s.findings.push_back(download_finding(sig, "the signature of the catalog", url + ".sig"));
        return s;
    }
    const std::optional<int64_t> last = state.last_serial();
    VerifyResult v = verify_catalog(cat.body, sig.body, trusted_keys, last.value_or(0));
    if (!v.ok()) {
        s.findings = v.findings;
        return s;
    }
    s.catalog = v.catalog;
    s.catalog_bytes = cat.body;
    s.signature_bytes = sig.body;
    state.record(v.serial);
    if (!last)
        s.notices.push_back(make_notice("W-INS-03", "catalog", "no earlier catalog serial was on record on this computer (first run, or the record was missing); this catalog is accepted because its signature is genuine"));
    return s;
}

}  // namespace install
}  // namespace pbstore
