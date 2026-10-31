#pragma once
// The reader has one storage and no drive to look for: a MountSource of exactly one mount. The engine dereferences
// Deps::mounts (engine gap E3), so the store supplies this instead of a null pointer. It needs no InkView and no
// POSIX header.

#include <string>
#include <vector>

#include "pbstore/install/mount_source.h"

namespace pbstore {
namespace store {

class FixedMountSource : public install::MountSource {
public:
    explicit FixedMountSource(std::string root, std::string fstype = "vfat", uint64_t size_bytes = 0)
        : root_(std::move(root)), fstype_(std::move(fstype)), size_(size_bytes) {}
    std::vector<install::Mount> mounts() override { return {install::Mount{root_, fstype_, size_}}; }
    std::vector<std::string> extra_roots() override { return {}; }
    std::vector<std::string> usb_manufacturers() override { return {}; }
    std::vector<std::string> mtp_mounts() override { return {}; }

private:
    std::string root_, fstype_;
    uint64_t size_;
};

}  // namespace store
}  // namespace pbstore
