#pragma once
// The Linux MountSource: /proc/self/mountinfo, the usual removable-media folders, /sys/bus/usb and gvfs.

#include "pbstore/install/mount_source.h"

namespace pbstore {
namespace install {

class PosixMountSource : public MountSource {
public:
    std::vector<Mount> mounts() override;
    std::vector<std::string> extra_roots() override;
    std::vector<std::string> usb_manufacturers() override;
    std::vector<std::string> mtp_mounts() override;
};

}  // namespace install
}  // namespace pbstore
