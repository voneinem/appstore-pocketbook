#pragma once
// MemAvailable from /proc/meminfo, on the reader and on the computer (the file is standard Linux).

#include <cstdint>
#include <optional>
#include <string>

#include "pbstore/store/memory_info.h"

namespace pbstore {
namespace store {

// The value of the line "MemAvailable: <n> kB" in bytes; nothing for a text without that line or with a
// number that is missing, negative, garbled or too large.
std::optional<uint64_t> parse_mem_available(const std::string& text);

class ProcMeminfo : public MemoryInfo {
public:
    explicit ProcMeminfo(std::string path = "/proc/meminfo") : path_(std::move(path)) {}
    std::optional<uint64_t> available_bytes() override;

private:
    std::string path_;
};

}  // namespace store
}  // namespace pbstore
