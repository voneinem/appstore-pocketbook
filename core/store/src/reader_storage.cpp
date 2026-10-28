#include "pbstore/store/reader_storage.h"

#include "pbstore/install/posix_reader_fs.h"
#include "pbstore/install/posix_state_dirs.h"
#include "pbstore/store/fixed_mount_source.h"

namespace pbstore {
namespace store {

ReaderStorage make_reader_storage(const std::string& root) {
    ReaderStorage s;
    s.root = root;
    s.open_fs = [](const std::string& r) -> std::unique_ptr<install::ReaderFs> { return std::make_unique<install::PosixReaderFs>(r); };
    s.state = std::make_unique<install::PosixStateDirs>(root + "/system/pbstore/app");
    s.mounts = std::make_unique<FixedMountSource>(root);
    return s;
}

}  // namespace store
}  // namespace pbstore
