#pragma once
// The Linux implementation of ReaderFs over a directory (a mounted reader or a folder chosen by hand).
// Paths are opened component by component without following links.

#include "pbstore/install/reader_fs.h"

namespace pbstore {
namespace install {

class PosixReaderFs : public ReaderFs {
public:
    explicit PosixReaderFs(std::string root);
    ~PosixReaderFs() override;
    PosixReaderFs(const PosixReaderFs&) = delete;
    PosixReaderFs& operator=(const PosixReaderFs&) = delete;

    Stat stat(const Path& p) override;
    std::vector<DirEntry> list(const Path& dir) override;
    bool read_file(const Path& p, uint64_t max_bytes, std::string* out) override;
    bool create_exclusive(const Path& p, const std::string& data, bool flush) override;
    bool rename_free(const Path& from, const Path& to) override;
    bool remove_file(const Path& p) override;
    bool make_dir(const Path& p) override;
    bool remove_dir(const Path& p) override;
    bool flush_dir(const Path& dir) override;
    bool flush_volume() override;
    Space space() override;
    std::unique_ptr<Lock> lock() override;

private:
    std::string root_;
    int root_fd_ = -1;
    // Opens a directory below the root; -1 if it is missing, not a directory or a link lies on the way
    // (*link is then set when it was a link).
    int open_dir(const Path& dir, bool* link) const;
};

}  // namespace install
}  // namespace pbstore
