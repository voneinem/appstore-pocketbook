#pragma once
// The early refusals before a download (research R-08), from the signed package size: storage space (the package plus the
// installer's 10 MiB margin) and memory (the engine holds the package in RAM: twice its size plus 32 MiB). Two conditions,
// two rule ids (spec FR-015). The exact space rule of feature 002 still runs in the planner after the download.

#include <cstdint>
#include <optional>

#include "pbstore/rules.h"
#include "pbstore/store/memory_info.h"

namespace pbstore {
namespace store {

constexpr uint64_t kSpaceMargin = 10ull * 1024 * 1024;
constexpr uint64_t kMemoryMargin = 32ull * 1024 * 1024;

enum class MemoryResult { Ok, NoSpace, NoMemory };

struct MemoryDecision {
    MemoryResult result = MemoryResult::Ok;
    uint64_t needed = 0;      // for the refused condition
    uint64_t available = 0;
};

// Storage is checked first. Unknown memory is Ok: the download is bounded by the signed size and fails with a message if the
// allocation fails.
MemoryDecision decide_memory(uint64_t package_size, uint64_t free_bytes, std::optional<uint64_t> mem_available);

struct MemoryFinding {
    Finding finding;   // E-INS-07 (storage) or E-STO-03 (memory); rule_id empty when Ok
};
MemoryFinding memory_finding(const MemoryDecision& decision);

class MemoryGate {
public:
    explicit MemoryGate(MemoryInfo& memory) : memory_(memory) {}
    MemoryDecision check(uint64_t package_size, uint64_t free_bytes) { return decide_memory(package_size, free_bytes, memory_.available_bytes()); }

private:
    MemoryInfo& memory_;
};

}  // namespace store
}  // namespace pbstore
