#pragma once
// Test doubles (spec FR-040, task T103a): a ReaderFs and a StateDirs wrapper that record the path of every mutating call - create, both
// paths of a rename, remove, make and remove a folder; for the state folder write, append, rename and remove. A ReaderFs wrapper can also
// lose power at mutating call number N (the pulled cable of the fault matrix): what was written before stays, the call itself is not made.
// A shared Record collects the calls of every wrapper that was opened during an operation.

#include <memory>
#include <string>
#include <vector>

#include "crash_fs.h"
#include "forwarding_fs.h"
#include "pbstore/install/state_dirs.h"

namespace pbtest {

struct Record {
    struct Call {
        std::string op;      // create, rename, remove, make_dir, remove_dir
        std::string path;    // the path written; for a rename the source
        std::string target;  // for a rename the target
    };
    std::vector<Call> calls;
    int mutating = 0;        // all mutating calls seen, also those after the power was lost
    int lose_at = 0;         // > 0: PowerLoss is thrown instead of making call number `lose_at`
    bool lost = false;

    void clear() { calls.clear(); mutating = 0; }
};

class RecordingFs : public ForwardingFs {
public:
    RecordingFs(std::unique_ptr<pbstore::install::ReaderFs> base, std::shared_ptr<Record> record)
        : ForwardingFs(std::shared_ptr<pbstore::install::ReaderFs>(std::move(base))), record_(std::move(record)) {}

    bool create_exclusive(const Path& p, const std::string& data, bool flush) override { note("create", p, ""); return ForwardingFs::create_exclusive(p, data, flush); }
    bool rename_free(const Path& a, const Path& b) override { note("rename", a, b); return ForwardingFs::rename_free(a, b); }
    bool remove_file(const Path& p) override { note("remove", p, ""); return ForwardingFs::remove_file(p); }
    bool make_dir(const Path& p) override { note("make_dir", p, ""); return ForwardingFs::make_dir(p); }
    bool remove_dir(const Path& p) override { note("remove_dir", p, ""); return ForwardingFs::remove_dir(p); }

private:
    void note(const char* op, const Path& p, const Path& target) {
        ++record_->mutating;
        if (record_->lose_at > 0 && record_->mutating >= record_->lose_at) {
            record_->lost = true;
            throw PowerLoss{};
        }
        record_->calls.push_back({op, p, target});
    }
    std::shared_ptr<Record> record_;
};

// The state folder (system/pbstore/app and the launcher's run/ folder), recorded.
class RecordingStateDirs : public pbstore::install::StateDirs {
public:
    RecordingStateDirs(std::unique_ptr<pbstore::install::StateDirs> base, std::shared_ptr<Record> record, std::string prefix)
        : base_(std::move(base)), record_(std::move(record)), prefix_(std::move(prefix)) {}

    std::string state_dir() override { return base_->state_dir(); }
    bool read(const std::string& name, std::string* out) override { return base_->read(name, out); }
    bool write_atomic(const std::string& name, const std::string& data) override { note("write", name, ""); return base_->write_atomic(name, data); }
    bool append(const std::string& name, const std::string& data) override { note("append", name, ""); return base_->append(name, data); }
    bool rename(const std::string& from, const std::string& to) override { note("rename", from, to); return base_->rename(from, to); }
    bool remove(const std::string& name) override { note("remove", name, ""); return base_->remove(name); }
    uint64_t size(const std::string& name) override { return base_->size(name); }

private:
    void note(const char* op, const std::string& name, const std::string& target) {
        record_->calls.push_back({op, prefix_ + name, target.empty() ? target : prefix_ + target});
    }
    std::unique_ptr<pbstore::install::StateDirs> base_;
    std::shared_ptr<Record> record_;
    std::string prefix_;   // where the folder lies on the reader, for example "system/pbstore/app/"
};

}  // namespace pbtest
