#pragma once
// "Byte-identical" in the installer tests means: the same relative paths (with their letter case), kinds,
// sizes and SHA-256. Timestamps and permissions are ignored (spec Assumptions). Implemented once, here.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "pbstore/install/reader_fs.h"
#include "pbstore/sha256.h"

namespace pbtest {

struct TreeItem {
    std::string path;
    char kind = 'f';        // 'f' file, 'd' directory, 'l' link, 'o' other
    uint64_t size = 0;
    std::string sha256;     // files only
    bool operator==(const TreeItem& o) const { return path == o.path && kind == o.kind && size == o.size && sha256 == o.sha256; }
};
using Snapshot = std::vector<TreeItem>;

struct Diff {
    bool identical = true;
    std::vector<std::string> added, removed, changed;
    std::string describe() const {
        std::string s;
        for (const auto& p : added) s += "added: " + p + "\n";
        for (const auto& p : removed) s += "removed: " + p + "\n";
        for (const auto& p : changed) s += "changed: " + p + "\n";
        return s.empty() ? "identical" : s;
    }
};

namespace detail {
inline void walk_dir(const std::string& root, const std::string& rel, Snapshot* out) {
    DIR* d = ::opendir((rel.empty() ? root : root + "/" + rel).c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    ::closedir(d);
    for (const std::string& n : names) {
        const std::string r = rel.empty() ? n : rel + "/" + n;
        struct stat st;
        if (::lstat((root + "/" + r).c_str(), &st) != 0) continue;
        TreeItem item;
        item.path = r;
        if (S_ISDIR(st.st_mode)) {
            item.kind = 'd';
            out->push_back(item);
            walk_dir(root, r, out);
        } else if (S_ISREG(st.st_mode)) {
            std::ifstream f(root + "/" + r, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            item.size = data.size();
            item.sha256 = pbstore::sha256_hex(data);
            out->push_back(item);
        } else {
            item.kind = S_ISLNK(st.st_mode) ? 'l' : 'o';
            out->push_back(item);
        }
    }
}
inline void walk_fs(pbstore::install::ReaderFs& fs, const std::string& rel, Snapshot* out) {
    for (const auto& e : fs.list(rel)) {
        const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
        TreeItem item;
        item.path = r;
        switch (e.kind) {
            case pbstore::install::Kind::Dir:
                item.kind = 'd';
                out->push_back(item);
                walk_fs(fs, r, out);
                break;
            case pbstore::install::Kind::File: {
                std::string data;
                fs.read_file(r, UINT64_MAX, &data);
                item.size = data.size();
                item.sha256 = pbstore::sha256_hex(data);
                out->push_back(item);
                break;
            }
            default:
                item.kind = e.kind == pbstore::install::Kind::Link ? 'l' : 'o';
                out->push_back(item);
        }
    }
}
inline void sort_items(Snapshot* s) {
    std::sort(s->begin(), s->end(), [](const TreeItem& a, const TreeItem& b) { return a.path < b.path; });
}
}  // namespace detail

inline Snapshot snapshot(const std::string& root) {
    Snapshot s;
    detail::walk_dir(root, "", &s);
    detail::sort_items(&s);
    return s;
}
inline Snapshot snapshot(pbstore::install::ReaderFs& fs) {
    Snapshot s;
    detail::walk_fs(fs, "", &s);
    detail::sort_items(&s);
    return s;
}

inline Diff compare(const Snapshot& a, const Snapshot& b) {
    Diff d;
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (j >= b.size() || (i < a.size() && a[i].path < b[j].path)) {
            d.removed.push_back(a[i++].path);
        } else if (i >= a.size() || b[j].path < a[i].path) {
            d.added.push_back(b[j++].path);
        } else {
            if (!(a[i] == b[j])) d.changed.push_back(a[i].path);
            ++i;
            ++j;
        }
    }
    d.identical = d.added.empty() && d.removed.empty() && d.changed.empty();
    return d;
}

}  // namespace pbtest
