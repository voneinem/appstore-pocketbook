#include "pbstore/store/state_names.h"

namespace pbstore {
namespace store {

namespace {
constexpr uint64_t KiB = 1024, MiB = 1024 * 1024;
}

const std::vector<StateName>& state_names() {
    static const std::vector<StateName> names = {
        {"catalog.json", 1 * MiB, true},
        {"catalog.json.sig", 1 * KiB, true},
        {"catalog.json.tmp", 1 * MiB, true},
        {"catalog.json.sig.tmp", 1 * KiB, true},
        {"catalog.meta", 128, true},
        {"state.json", 256, true},
        {"pbstore.log", 512 * KiB, true},
        {"pending-note.txt", 256, true},
        {"last-run-version", 32, true},
        {"run/trying", 64, true},
        {"run/last-exit", 8, false},
        {"run/message", 128, true},
        {"run/pbstore.bin*", 0, false},
    };
    return names;
}

uint64_t limit_of(const std::string& name) {
    for (const StateName& n : state_names())
        if (name == n.name) return n.limit;
    return UINT64_MAX;
}

bool is_store_name(const std::string& name) {
    if (name == "pbstore.log.1" || name == "pbstore.log.2") return true;
    for (const StateName& n : state_names())
        if (name == n.name) return true;
    return false;
}

}  // namespace store
}  // namespace pbstore
