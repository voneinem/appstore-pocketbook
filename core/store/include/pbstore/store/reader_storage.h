#pragma once
// The reader's configuration of the engine's operating-system interfaces: the same classes the computer uses, over the
// reader's storage (research R-03). Nothing here is a second implementation of the file layer; the contract tests run
// against it exactly as they run against the computer's.

#include <memory>
#include <string>

#include "pbstore/install/mount_source.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/install/state_dirs.h"

namespace pbstore {
namespace store {

struct ReaderStorage {
    install::ReaderFsFactory open_fs;                 // PosixReaderFs over a root
    std::unique_ptr<install::StateDirs> state;        // PosixStateDirs on <root>/system/pbstore/app
    std::unique_ptr<install::MountSource> mounts;     // FixedMountSource: one mount, <root>, vfat
    std::string root;
};

// The reader's storage is /mnt/ext1; tests pass a temporary folder.
ReaderStorage make_reader_storage(const std::string& root = "/mnt/ext1");

}  // namespace store
}  // namespace pbstore
