#include "pbstore/live_catalog.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "pbstore/catalog_verify.h"

namespace pbstore {

namespace {

LiveCatalogResult failure(const std::string& where, const std::string& cause) {
    LiveCatalogResult r;
    r.findings.push_back(make_error("E-BLD-03", "live catalog", "the live catalog (" + where + ") cannot be used: " + cause));
    return r;
}

LiveCatalogResult verify(const std::string& where, const std::string& bytes, const std::string& sig, const std::vector<std::string>& keys) {
    VerifyResult v = verify_catalog(bytes, sig, keys, 0);
    if (!v.ok()) {
        std::string cause;
        for (const Finding& f : v.findings) cause += (cause.empty() ? "" : "; ") + std::string("[") + f.rule_id + "] " + f.message;
        return failure(where, cause);
    }
    LiveCatalogResult r;
    r.catalog = std::move(v.catalog);
    return r;
}

bool slurp(const std::filesystem::path& p, std::string* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

}  // namespace

LiveCatalogResult load_live_catalog_dir(const std::string& dir, const std::vector<std::string>& trusted_keys) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return failure(dir, "directory not found");
    std::string bytes, sig;
    if (!slurp(fs::path(dir) / "catalog.json", &bytes)) return failure(dir, "catalog.json is missing");
    if (!slurp(fs::path(dir) / "catalog.json.sig", &sig)) return failure(dir, "catalog.json.sig is missing");
    return verify(dir, bytes, sig, trusted_keys);
}

LiveCatalogResult load_live_catalog_url(const std::string& catalog_url, const std::vector<std::string>& trusted_keys, const Fetcher& fetcher) {
    if (catalog_url.compare(0, 8, "https://") != 0) return failure(catalog_url, "the address must start with https://");
    FetchResult cat = fetcher(catalog_url, kMaxCatalogBytes + 1);
    if (!cat.ok) return failure(catalog_url, cat.error);
    FetchResult sig = fetcher(catalog_url + ".sig", 4096);
    if (!sig.ok) return failure(catalog_url + ".sig", sig.error);
    return verify(catalog_url, cat.body, sig.body, trusted_keys);
}

}  // namespace pbstore
