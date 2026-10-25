#pragma once
// Test double: wraps another ReaderFs and makes it behave like the reader's FAT volume: names are looked up
// ignoring ASCII letter case and two names that differ only in case collide; nothing is ever replaced; no
// links can be made (existing ones are reported); a file is at most 4 GiB minus one byte; names use only
// A-Za-z0-9 . _ - + and space.

#include <algorithm>
#include <cctype>

#include "forwarding_fs.h"

namespace pbtest {

class FatRulesFs : public ForwardingFs {
public:
    static constexpr uint64_t kMaxFileBytes = 4294967295ull;

    explicit FatRulesFs(pbstore::install::ReaderFs& base, uint64_t max_file_bytes = kMaxFileBytes)
        : ForwardingFs(base), max_file_bytes_(max_file_bytes) {}

    pbstore::install::Stat stat(const Path& p) override { return ForwardingFs::stat(resolve(p)); }
    std::vector<pbstore::install::DirEntry> list(const Path& d) override { return ForwardingFs::list(resolve(d)); }
    bool read_file(const Path& p, uint64_t max, std::string* out) override { return ForwardingFs::read_file(resolve(p), max, out); }
    bool create_exclusive(const Path& p, const std::string& data, bool flush) override {
        if (data.size() > max_file_bytes_) return false;
        Path target;
        if (!new_name(p, &target)) return false;
        return ForwardingFs::create_exclusive(target, data, flush);
    }
    bool rename_free(const Path& from, const Path& to) override {
        Path target;
        if (!new_name(to, &target)) return false;
        return ForwardingFs::rename_free(resolve(from), target);
    }
    bool remove_file(const Path& p) override { return ForwardingFs::remove_file(resolve(p)); }
    bool make_dir(const Path& p) override {
        Path target;
        if (!new_name(p, &target)) return false;
        return ForwardingFs::make_dir(target);
    }
    bool remove_dir(const Path& p) override { return ForwardingFs::remove_dir(resolve(p)); }
    bool flush_dir(const Path& d) override { return ForwardingFs::flush_dir(resolve(d)); }

    static bool allowed_name(const std::string& n) {
        if (n.empty() || n.size() > 255) return false;
        for (unsigned char c : n)
            if (!(std::isalnum(c) && c < 128) && c != '.' && c != '_' && c != '-' && c != '+' && c != ' ') return false;
        return true;
    }

private:
    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    // The path with every component spelled as it is stored (components that do not exist stay as given).
    Path resolve(const Path& p) {
        if (!pbstore::install::valid_relative_path(p)) return p;
        Path actual;
        size_t start = 0;
        bool exists = true;
        while (start < p.size()) {
            const size_t slash = p.find('/', start);
            const std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            std::string use = part;
            if (exists) {
                bool found = false;
                for (const auto& e : ForwardingFs::list(actual))
                    if (lower(e.name) == lower(part)) { use = e.name; found = true; break; }
                if (!found) {
                    // a link on the way hides its content; keep the given name so the base reports it
                    exists = false;
                }
            }
            actual = pbstore::install::join_path(actual, use);
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return actual;
    }
    // The path to create: parent as stored, last name as given; false if the name collides or is not allowed.
    bool new_name(const Path& p, Path* out) {
        if (p.empty() || !pbstore::install::valid_relative_path(p)) return false;
        const std::string name = pbstore::install::base_name(p);
        if (!allowed_name(name)) return false;
        const Path parent = resolve(pbstore::install::parent_path(p));
        for (const auto& e : ForwardingFs::list(parent))
            if (lower(e.name) == lower(name)) return false;
        *out = pbstore::install::join_path(parent, name);
        return true;
    }
    uint64_t max_file_bytes_;
};

}  // namespace pbtest
