#pragma once
// The names, limits and writers of the files in system/pbstore/app/ (contracts/store-state-folder.md, Principle VII). This
// header is the single place that names a file of the state folder: catalog_cache, run_state and pending_note use these
// constants and nothing else names a file. tests/store/test_state_folder_contract parses the contract and compares.

#include <cstdint>
#include <string>
#include <vector>

namespace pbstore {
namespace store {

struct StateName {
    const char* name;      // relative to system/pbstore/app/
    uint64_t limit;        // bytes; 0: no fixed limit (a program)
    bool store_writes;     // the store program writes or removes it (the launcher writes the rest of run/)
};

const std::vector<StateName>& state_names();

// The limit of a name; UINT64_MAX for a name that is not in the table.
uint64_t limit_of(const std::string& name);

// True for a name in the table, and for the rotated copies of the log (pbstore.log.1 and pbstore.log.2).
bool is_store_name(const std::string& name);

constexpr const char* kCatalogName = "catalog.json";
constexpr const char* kCatalogSignatureName = "catalog.json.sig";
constexpr const char* kCatalogMetaName = "catalog.meta";
constexpr const char* kCatalogTmpName = "catalog.json.tmp";
constexpr const char* kCatalogSignatureTmpName = "catalog.json.sig.tmp";
constexpr const char* kPendingNoteName = "pending-note.txt";
constexpr const char* kLastRunVersionName = "last-run-version";

}  // namespace store
}  // namespace pbstore
