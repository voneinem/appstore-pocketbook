#include "pbstore/store/catalog_cache.h"

#include <algorithm>

#include "pbstore/catalog_verify.h"
#include "pbstore/install/state_store.h"
#include "pbstore/store/state_names.h"

namespace pbstore {
namespace store {

bool StoreCatalogCache::load(std::string* catalog, std::string* signature) {
    const uint64_t c = dirs_.size(kCatalogName), s = dirs_.size(kCatalogSignatureName);
    if (c == UINT64_MAX || s == UINT64_MAX || c > limit_of(kCatalogName) || s > limit_of(kCatalogSignatureName)) return false;
    std::string cat, sig;
    if (!dirs_.read(kCatalogName, &cat) || !dirs_.read(kCatalogSignatureName, &sig)) return false;
    *catalog = std::move(cat);
    *signature = std::move(sig);
    return true;
}

bool StoreCatalogCache::store(const std::string& catalog, const std::string& signature) {
    if (catalog.size() > limit_of(kCatalogName) || signature.size() > limit_of(kCatalogSignatureName)) return false;
    // both temporary files first; the signature is renamed before the catalog: a catalog with an old or missing signature fails
    // its check on load and is discarded, which is safe
    if (!dirs_.write_atomic(kCatalogTmpName, catalog)) return false;
    if (!dirs_.write_atomic(kCatalogSignatureTmpName, signature)) return false;
    if (!dirs_.rename(kCatalogSignatureTmpName, kCatalogSignatureName)) return false;
    if (!dirs_.rename(kCatalogTmpName, kCatalogName)) return false;
    dirs_.write_atomic(kCatalogMetaName, "refreshed_at=" + clock_.now().iso() + "\n");   // informational: a failure here loses only the date
    return true;
}

void StoreCatalogCache::discard() {
    // only the names the store made; the serial memory (state.json), the log and unknown files stay
    for (const char* name : {kCatalogName, kCatalogSignatureName, kCatalogMetaName, kCatalogTmpName, kCatalogSignatureTmpName,
                             "catalog.json.tmp.tmp", "catalog.json.sig.tmp.tmp"})
        dirs_.remove(name);
}

std::optional<std::string> StoreCatalogCache::refreshed_at() {
    const uint64_t size = dirs_.size(kCatalogMetaName);
    if (size == UINT64_MAX || size > limit_of(kCatalogMetaName)) return std::nullopt;
    std::string text;
    if (!dirs_.read(kCatalogMetaName, &text)) return std::nullopt;
    static const std::string kPrefix = "refreshed_at=";
    if (text.compare(0, kPrefix.size(), kPrefix) != 0) return std::nullopt;
    std::string value = text.substr(kPrefix.size());
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    // YYYY-MM-DDTHH:MM:SSZ
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[19] != 'Z') return std::nullopt;
    for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18})
        if (value[i] < '0' || value[i] > '9') return std::nullopt;
    return value;
}

namespace {
std::optional<int64_t> cached_serial(StoreCatalogCache& cache, const std::vector<std::string>& keys) {
    std::string catalog, signature;
    if (!cache.load(&catalog, &signature)) return std::nullopt;
    VerifyResult v = verify_catalog(catalog, signature, keys, 0);
    if (!v.ok()) return std::nullopt;
    return v.serial;
}
}  // namespace

std::optional<int64_t> StoreCatalogCache::effective_last_serial(const std::vector<std::string>& trusted_keys) {
    const std::optional<int64_t> state = install::StateStore(dirs_).last_serial();
    const std::optional<int64_t> cached = cached_serial(*this, trusted_keys);
    if (!state) return cached;
    if (!cached) return state;
    return std::max(*state, *cached);
}

void StoreCatalogCache::restore_serial_memory(const std::vector<std::string>& trusted_keys) {
    const std::optional<int64_t> cached = cached_serial(*this, trusted_keys);
    if (!cached) return;
    install::StateStore state(dirs_);
    const std::optional<int64_t> current = state.last_serial();
    if (!current || *cached > *current) state.record(*cached);   // record never lowers
}

}  // namespace store
}  // namespace pbstore
