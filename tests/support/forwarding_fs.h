#pragma once
// Test double base: a ReaderFs that forwards every call to another one (by reference or shared).

#include <memory>

#include "pbstore/install/reader_fs.h"

namespace pbtest {

class ForwardingFs : public pbstore::install::ReaderFs {
public:
    using Path = pbstore::install::Path;
    explicit ForwardingFs(pbstore::install::ReaderFs& base) : base_(&base) {}
    explicit ForwardingFs(std::shared_ptr<pbstore::install::ReaderFs> owned) : owned_(std::move(owned)), base_(owned_.get()) {}

    pbstore::install::Stat stat(const Path& p) override { return base_->stat(p); }
    std::vector<pbstore::install::DirEntry> list(const Path& d) override { return base_->list(d); }
    bool read_file(const Path& p, uint64_t max, std::string* out) override { return base_->read_file(p, max, out); }
    bool create_exclusive(const Path& p, const std::string& data, bool flush) override { return base_->create_exclusive(p, data, flush); }
    bool rename_free(const Path& a, const Path& b) override { return base_->rename_free(a, b); }
    bool remove_file(const Path& p) override { return base_->remove_file(p); }
    bool make_dir(const Path& p) override { return base_->make_dir(p); }
    bool remove_dir(const Path& p) override { return base_->remove_dir(p); }
    bool flush_dir(const Path& d) override { return base_->flush_dir(d); }
    bool flush_volume() override { return base_->flush_volume(); }
    pbstore::install::Space space() override { return base_->space(); }
    std::unique_ptr<pbstore::install::Lock> lock() override { return base_->lock(); }

protected:
    pbstore::install::ReaderFs& base() { return *base_; }

private:
    std::shared_ptr<pbstore::install::ReaderFs> owned_;
    pbstore::install::ReaderFs* base_;
};

}  // namespace pbtest
