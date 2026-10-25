#pragma once
// Where the operating system tells us about drives. One implementation per platform; tests use a fake.

#include <cstdint>
#include <string>
#include <vector>

namespace pbstore {
namespace install {

struct Mount {
    std::string mount_point;
    std::string fstype;          // vfat, exfat, ext4 ...
    uint64_t size_bytes = 0;
};

class MountSource {
public:
    virtual ~MountSource() = default;
    // The mounted file systems.
    virtual std::vector<Mount> mounts() = 0;
    // Directories under the usual removable-media folders (/run/media/<user>, /media/<user>, /media, /mnt).
    virtual std::vector<std::string> extra_roots() = 0;
    // Manufacturer strings of the attached USB devices (a hint that a PocketBook is connected).
    virtual std::vector<std::string> usb_manufacturers() = 0;
    // Mount points of MTP devices (which this program cannot use).
    virtual std::vector<std::string> mtp_mounts() = 0;
};

}  // namespace install
}  // namespace pbstore
