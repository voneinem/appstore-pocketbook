#pragma once
// The Linux reader of a package file chosen by the user.

#include <cstdint>
#include <string>

namespace pbstore {
namespace install {

// Regular files only: a missing file, a folder, a pipe, a device and every symbolic link are refused.
bool read_regular_file(const std::string& path, uint64_t max_bytes, std::string* out, std::string* why);

}  // namespace install
}  // namespace pbstore
