#pragma once
// Available memory. MemoryGate is the only caller.

#include <cstdint>
#include <optional>

namespace pbstore {
namespace store {

class MemoryInfo {
public:
    virtual ~MemoryInfo() = default;
    virtual std::optional<uint64_t> available_bytes() = 0;   // MemAvailable; nullopt if it cannot be read
};

}  // namespace store
}  // namespace pbstore
