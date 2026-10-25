#pragma once
// Test double: the state folder in memory.

#include <map>
#include <string>
#include <vector>

#include "pbstore/install/state_dirs.h"

namespace pbtest {

class MemStateDirs : public pbstore::install::StateDirs {
public:
    std::string state_dir() override { return "/state"; }
    bool read(const std::string& name, std::string* out) override {
        auto it = files_.find(name);
        if (it == files_.end()) return false;
        if (out) *out = it->second;
        return true;
    }
    bool write_atomic(const std::string& name, const std::string& data) override {
        if (fail_writes) return false;
        files_[name] = data;
        return true;
    }
    bool append(const std::string& name, const std::string& data) override {
        if (fail_writes) return false;
        files_[name] += data;
        return true;
    }
    bool rename(const std::string& from, const std::string& to) override {
        auto it = files_.find(from);
        if (it == files_.end()) return false;
        files_[to] = it->second;
        files_.erase(from);
        return true;
    }
    bool remove(const std::string& name) override { return files_.erase(name) > 0; }
    uint64_t size(const std::string& name) override {
        auto it = files_.find(name);
        return it == files_.end() ? UINT64_MAX : it->second.size();
    }
    bool exists(const std::string& name) const { return files_.count(name) > 0; }
    std::vector<std::string> names() const {
        std::vector<std::string> v;
        for (const auto& kv : files_) v.push_back(kv.first);
        return v;
    }
    std::string text(const std::string& name) const {
        auto it = files_.find(name);
        return it == files_.end() ? std::string() : it->second;
    }
    bool fail_writes = false;

private:
    std::map<std::string, std::string> files_;
};

}  // namespace pbtest
