#include "pbstore/install/reader_fs.h"

namespace pbstore {
namespace install {

bool valid_relative_path(const Path& p) {
    if (p.empty()) return true;   // the root
    if (p.front() == '/' || p.back() == '/') return false;
    size_t start = 0;
    while (start <= p.size()) {
        const size_t slash = p.find('/', start);
        const std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        for (char c : part)
            if (c == '\0') return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

Path parent_path(const Path& p) {
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? Path() : p.substr(0, slash);
}

std::string base_name(const Path& p) {
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

Path join_path(const Path& dir, const std::string& name) { return dir.empty() ? name : dir + "/" + name; }

bool ensure_directory(ReaderFs& fs, const Path& dir) {
    if (dir.empty()) return true;
    const Stat s = fs.stat(dir);
    if (s.kind == Kind::Dir) return true;
    if (s.exists()) return false;
    if (!ensure_directory(fs, parent_path(dir))) return false;
    return fs.make_dir(dir) || fs.stat(dir).kind == Kind::Dir;
}

}  // namespace install
}  // namespace pbstore
