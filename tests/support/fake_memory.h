#pragma once
// Test double for pbstore::store::MemoryInfo. The default is 512 MiB available.

#include <optional>

#include "pbstore/store/memory_info.h"

namespace pbtest {

class FakeMemory : public pbstore::store::MemoryInfo {
public:
    void set_available(uint64_t bytes) { available_ = bytes; }
    void set_unreadable() { available_.reset(); }
    std::optional<uint64_t> available_bytes() override { return available_; }

private:
    std::optional<uint64_t> available_ = 512ull << 20;
};

}  // namespace pbtest
