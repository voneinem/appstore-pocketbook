#pragma once
// Test double: a StateDirs over another one that counts the mutating calls (write_atomic, append, rename, remove)
// and loses power after call N: that call and every later one does nothing and returns false. Reads still work, as
// they do after a restart. Used to prove that the order of the writes keeps the state consistent.

#include "pbstore/install/state_dirs.h"

namespace pbtest {

class CuttingStateDirs : public pbstore::install::StateDirs {
public:
    explicit CuttingStateDirs(pbstore::install::StateDirs& base, int cut_after = -1) : base_(base), cut_after_(cut_after) {}
    // Power is lost at mutating call number `n` (1 is the first): it and every later one is lost. -1: never.
    void cut_at(int n) { cut_after_ = n; calls_ = 0; }
    int mutating_calls() const { return calls_; }
    bool lost() const { return cut_after_ >= 0 && calls_ >= cut_after_; }

    std::string state_dir() override { return base_.state_dir(); }
    bool read(const std::string& name, std::string* out) override { return base_.read(name, out); }
    bool write_atomic(const std::string& name, const std::string& data) override { return allow() && base_.write_atomic(name, data); }
    bool append(const std::string& name, const std::string& data) override { return allow() && base_.append(name, data); }
    bool rename(const std::string& from, const std::string& to) override { return allow() && base_.rename(from, to); }
    bool remove(const std::string& name) override { return allow() && base_.remove(name); }
    uint64_t size(const std::string& name) override { return base_.size(name); }

private:
    bool allow() {
        ++calls_;
        return !(cut_after_ >= 0 && calls_ >= cut_after_);
    }
    pbstore::install::StateDirs& base_;
    int cut_after_;
    int calls_ = 0;
};

}  // namespace pbtest
