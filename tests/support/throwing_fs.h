#pragma once
// Test double: a ReaderFs over another one that throws PowerLoss at mutating call number N (create, rename, remove, make or remove a
// folder). What was written before stays on the volume, as after a pulled cable. Used to leave an interrupted operation behind for the
// recovery tests of the store.

#include "crash_fs.h"
#include "forwarding_fs.h"

namespace pbtest {

class ThrowingFs : public ForwardingFs {
public:
    ThrowingFs(std::unique_ptr<pbstore::install::ReaderFs> base, int lose_at) : ForwardingFs(std::shared_ptr<pbstore::install::ReaderFs>(std::move(base))), lose_at_(lose_at) {}

    bool create_exclusive(const Path& p, const std::string& data, bool flush) override { tick(); return ForwardingFs::create_exclusive(p, data, flush); }
    bool rename_free(const Path& a, const Path& b) override { tick(); return ForwardingFs::rename_free(a, b); }
    bool remove_file(const Path& p) override { tick(); return ForwardingFs::remove_file(p); }
    bool make_dir(const Path& p) override { tick(); return ForwardingFs::make_dir(p); }
    bool remove_dir(const Path& p) override { tick(); return ForwardingFs::remove_dir(p); }

private:
    void tick() {
        if (lose_at_ > 0 && ++calls_ >= lose_at_) throw PowerLoss{};
    }
    int lose_at_;
    int calls_ = 0;
};

}  // namespace pbtest
