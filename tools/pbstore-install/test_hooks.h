#pragma once
// Hooks that exist only in pbstore-install-test (the test build, never shipped): a fake list of drives, the
// host of the package addresses, and a cut-off after N changes to the reader, as by a pulled cable.

#ifdef PBSTORE_TEST_BUILD

#include <memory>

#include "pbstore/install/engine.h"
#include "pbstore/install/open_url.h"

namespace pbstore {
namespace install {
namespace cli {

// Replaces the mount source if PBSTORE_TEST_MOUNTS is set ("path|fstype|size;path|fstype|size").
MountSource* test_mount_source_or(MountSource* real);
// Maps https://github.com/ to a local server if PBSTORE_TEST_HOST_MAP is set ("github.com=localhost:PORT").
Downloader apply_test_host_map(Downloader inner);
// Faults on the reader, for the tests of the messages: PBSTORE_TEST_STOP_AFTER=N ends the program abruptly after
// the Nth mutating call (a pulled cable); PBSTORE_TEST_VANISH_AFTER=N makes every call fail from then on (the
// reader is gone); PBSTORE_TEST_FREE_BYTES=N limits the free space; PBSTORE_TEST_READ_ONLY=1 makes it read-only.
ReaderFsFactory apply_test_stop_after(ReaderFsFactory inner);
// Prints the address of the assistant instead of starting a browser (PBSTORE_TEST_OPEN_URL set); null if not set.
OpenUrl* test_open_url();
// A clock that stands still (PBSTORE_TEST_NOW set): 2026-10-25T12:00:00Z, local 14:00:00 +0200; null if not set.
Clock* test_clock();
// The model and firmware of the reader (PBSTORE_TEST_MODEL, PBSTORE_TEST_FIRMWARE); null if neither is set.
IdentitySource* test_identity_source();

}  // namespace cli
}  // namespace install
}  // namespace pbstore

#endif  // PBSTORE_TEST_BUILD
