// T023: the mount-source contract, run for the test double and for the Linux implementation.
#include "fake_mount_source.h"
#include "mount_source_contract.h"
#include "pbstore/install/posix_mount_source.h"
#include "test_support.h"

using namespace pbstore::install;

PB_TEST(the_fake_mount_source_meets_the_contract) {
    pbtest::FakeMountSource fake;
    pbtest::run_mount_source_contract(fake);                  // empty: callable
    fake.mount("/run/media/u/PB1030", "vfat", 8ull << 30);
    fake.mount("/", "ext4", 100ull << 30);
    fake.add_extra_root("/media");
    fake.add_usb_manufacturer("PocketBook");
    fake.add_mtp_mount("/run/user/1000/gvfs/mtp:host=x");
    pbtest::run_mount_source_contract(fake);
    PB_CHECK_EQ(fake.mounts().size(), size_t(2));
}

PB_TEST(the_linux_mount_source_meets_the_contract_on_this_computer) {
    PosixMountSource posix;
    pbtest::run_mount_source_contract(posix);
    PB_CHECK(!posix.mounts().empty());                         // at least the root file system
}

PB_TEST_MAIN()
