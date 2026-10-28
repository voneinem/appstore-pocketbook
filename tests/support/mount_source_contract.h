#pragma once
// The contract every MountSource must meet: run_mount_source_contract(source). Every mount has an absolute
// mount point and a file system type; the other three lists may be empty but must be callable and hold
// non-empty strings.

#include "pbstore/install/mount_source.h"
#include "test_support.h"

namespace pbtest {

inline void run_mount_source_contract(pbstore::install::MountSource& source) {
    for (const auto& m : source.mounts()) {
        PB_CHECK(!m.mount_point.empty());
        PB_CHECK_EQ(m.mount_point[0], '/');
        PB_CHECK(!m.fstype.empty());
    }
    for (const auto& r : source.extra_roots()) PB_CHECK(!r.empty());
    for (const auto& u : source.usb_manufacturers()) PB_CHECK(!u.empty());
    for (const auto& t : source.mtp_mounts()) PB_CHECK(!t.empty());
}

}  // namespace pbtest
