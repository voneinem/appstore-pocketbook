#pragma once
// The store's catalog cache over the state folder (research R-09): the exact bytes of the last accepted catalog and its
// signature, the date of the last refresh, and the serial memory, which is the larger of the serial in state.json and the
// serial of the cached catalog when it verifies. Implements the engine's CatalogCache hook (engine addition E1).
//
// Order of the writes at an accepted refresh: the engine records the serial in state.json first (never lowers), then store()
// writes both temporary files, renames the signature, renames the catalog and writes the date. A cut after any call leaves a
// cache that verifies (the old or the new one) or one that fails its check on load and is discarded; the remembered serial is
// never below the cache's.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/catalog_cache.h"
#include "pbstore/install/clock.h"
#include "pbstore/install/state_dirs.h"

namespace pbstore {
namespace store {

class StoreCatalogCache : public install::CatalogCache {
public:
    StoreCatalogCache(install::StateDirs& dirs, install::Clock& clock) : dirs_(dirs), clock_(clock) {}

    bool load(std::string* catalog, std::string* signature) override;
    bool store(const std::string& catalog, const std::string& signature) override;
    void discard() override;

    // "YYYY-MM-DDTHH:MM:SSZ" of the last successful refresh by the reader's clock; nothing when unknown or malformed.
    std::optional<std::string> refreshed_at();

    // max(state.json, serial of the cached catalog if it verifies); nothing when both are missing (the first-run rule).
    std::optional<int64_t> effective_last_serial(const std::vector<std::string>& trusted_keys);
    // Raises state.json to the cached serial when that is higher or state.json is missing or damaged. Never lowers it.
    void restore_serial_memory(const std::vector<std::string>& trusted_keys);

private:
    install::StateDirs& dirs_;
    install::Clock& clock_;
};

}  // namespace store
}  // namespace pbstore
