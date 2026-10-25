#pragma once
// Test double: what the operating system tells us about drives.

#include "pbstore/install/mount_source.h"

namespace pbtest {

class FakeMountSource : public pbstore::install::MountSource {
public:
    void mount(const std::string& point, const std::string& fstype, uint64_t size) { mounts_.push_back({point, fstype, size}); }
    void add_extra_root(const std::string& p) { extra_.push_back(p); }
    void add_usb_manufacturer(const std::string& m) { usb_.push_back(m); }
    void add_mtp_mount(const std::string& p) { mtp_.push_back(p); }

    std::vector<pbstore::install::Mount> mounts() override { return mounts_; }
    std::vector<std::string> extra_roots() override { return extra_; }
    std::vector<std::string> usb_manufacturers() override { return usb_; }
    std::vector<std::string> mtp_mounts() override { return mtp_; }

private:
    std::vector<pbstore::install::Mount> mounts_;
    std::vector<std::string> extra_, usb_, mtp_;
};

}  // namespace pbtest
