#pragma once
// Firmware versions follow the device maker's scheme: dotted decimal numbers with at least two
// parts (6.8.4143). Compared part by part as numbers; missing trailing parts count as 0.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pbstore {

struct FirmwareVersion {
    std::vector<uint32_t> parts;
};

// Parts are 1 to 9 ASCII digits each, at least two parts.
std::optional<FirmwareVersion> parse_firmware(const std::string& text);
int compare(const FirmwareVersion& a, const FirmwareVersion& b);
std::string to_string(const FirmwareVersion& v);

}  // namespace pbstore
