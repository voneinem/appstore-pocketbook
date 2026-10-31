#include "pbstore/store/memory_gate.h"

#include <cstdio>
#include <string>

#include "pbstore/store/store_rules.h"

namespace pbstore {
namespace store {

namespace {
std::string mib(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}
}  // namespace

MemoryDecision decide_memory(uint64_t package_size, uint64_t free_bytes, std::optional<uint64_t> mem_available) {
    MemoryDecision d;
    const uint64_t space_needed = package_size + kSpaceMargin;
    if (free_bytes < space_needed) {
        d.result = MemoryResult::NoSpace;
        d.needed = space_needed;
        d.available = free_bytes;
        return d;
    }
    const uint64_t memory_needed = 2 * package_size + kMemoryMargin;
    if (mem_available && *mem_available < memory_needed) {
        d.result = MemoryResult::NoMemory;
        d.needed = memory_needed;
        d.available = *mem_available;
    }
    return d;
}

MemoryFinding memory_finding(const MemoryDecision& d) {
    MemoryFinding f;
    if (d.result == MemoryResult::NoSpace)
        f.finding = make_error("E-INS-07", "reader",
                               "there is not enough free space on the reader for this app: " + mib(d.needed) + " are needed and " + mib(d.available) + " are free");
    else if (d.result == MemoryResult::NoMemory)
        f.finding = make_store_finding("E-STO-03", "memory", "There is not enough free memory to download this app: " + mib(d.needed) + " are needed and " + mib(d.available) + " are available.");
    return f;
}

}  // namespace store
}  // namespace pbstore
